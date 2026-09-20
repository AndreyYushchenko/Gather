#include "ItemEditDialog.h"
#include "core/Database.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTextEdit>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

namespace {
constexpr int PageSong = 0;
constexpr int PageVerse = 1;
constexpr int PageAnnouncement = 2;
constexpr int PagePhoto = 3;

int pageForType(ContentType type)
{
    switch (type) {
    case ContentType::Song: return PageSong;
    case ContentType::BibleVerse: return PageVerse;
    case ContentType::Announcement: return PageAnnouncement;
    case ContentType::Photo: return PagePhoto;
    }
    return PageSong;
}
}

ItemEditDialog::ItemEditDialog(QWidget *parent)
    : QDialog(parent)
    , m_isNew(true)
{
    buildUi();
    setAcceptDrops(true);
    setWindowTitle(tr("Добавить запись"));
}

ItemEditDialog::ItemEditDialog(const ContentItem &existing, QWidget *parent)
    : QDialog(parent)
    , m_isNew(false)
    , m_original(existing)
{
    buildUi();
    setAcceptDrops(true);
    setWindowTitle(tr("Редактировать запись"));
    applyExisting();
    m_typeCombo->setEnabled(false);
}

void ItemEditDialog::buildUi()
{
    auto *layout = new QVBoxLayout(this);

    auto *typeRow = new QFormLayout;
    m_typeCombo = new QComboBox(this);
    m_typeCombo->addItem(contentTypeDisplayName(ContentType::Song), static_cast<int>(ContentType::Song));
    m_typeCombo->addItem(contentTypeDisplayName(ContentType::BibleVerse), static_cast<int>(ContentType::BibleVerse));
    m_typeCombo->addItem(contentTypeDisplayName(ContentType::Announcement), static_cast<int>(ContentType::Announcement));
    m_typeCombo->addItem(contentTypeDisplayName(ContentType::Photo), static_cast<int>(ContentType::Photo));
    typeRow->addRow(tr("Категория:"), m_typeCombo);
    layout->addLayout(typeRow);

    m_stack = new QStackedWidget(this);

    // Song page
    {
        auto *page = new QWidget;
        auto *form = new QVBoxLayout(page);
        form->addWidget(new QLabel(tr("Название песни:")));
        m_songTitle = new QLineEdit;
        form->addWidget(m_songTitle);
        form->addWidget(new QLabel(tr("№ в сборнике (необязательно, для быстрого поиска по номеру):")));
        m_songNumber = new QLineEdit;
        m_songNumber->setPlaceholderText(tr("например, 120"));
        form->addWidget(m_songNumber);
        form->addWidget(new QLabel(tr("Текст (куплеты разделяйте пустой строкой):")));
        m_songText = new QTextEdit;
        form->addWidget(m_songText);
        m_stack->insertWidget(PageSong, page);
    }

    // Bible verse page
    {
        auto *page = new QWidget;
        auto *form = new QVBoxLayout(page);
        auto *refRow = new QFormLayout;
        m_verseBook = new QLineEdit;
        m_verseBook->setPlaceholderText(tr("напр. Иоанна"));
        m_verseLocation = new QLineEdit;
        m_verseLocation->setPlaceholderText(tr("напр. 3:16"));
        refRow->addRow(tr("Книга:"), m_verseBook);
        refRow->addRow(tr("Глава:стих:"), m_verseLocation);
        form->addLayout(refRow);
        form->addWidget(new QLabel(tr("Текст:")));
        m_verseText = new QTextEdit;
        form->addWidget(m_verseText);
        m_stack->insertWidget(PageVerse, page);
    }

    // Announcement page
    {
        auto *page = new QWidget;
        auto *form = new QVBoxLayout(page);
        form->addWidget(new QLabel(tr("Заголовок:")));
        m_announcementTitle = new QLineEdit;
        form->addWidget(m_announcementTitle);
        form->addWidget(new QLabel(tr("Текст:")));
        m_announcementText = new QTextEdit;
        form->addWidget(m_announcementText);

        auto *expiryRow = new QHBoxLayout;
        m_announcementHasExpiry = new QCheckBox(tr("Актуально до:"));
        m_announcementExpiry = new QDateEdit(QDate::currentDate().addDays(7));
        m_announcementExpiry->setCalendarPopup(true);
        m_announcementExpiry->setEnabled(false);
        connect(m_announcementHasExpiry, &QCheckBox::toggled, this, &ItemEditDialog::onExpiryToggled);
        expiryRow->addWidget(m_announcementHasExpiry);
        expiryRow->addWidget(m_announcementExpiry);
        expiryRow->addStretch();
        form->addLayout(expiryRow);

        m_stack->insertWidget(PageAnnouncement, page);
    }

    // Photo page
    {
        auto *page = new QWidget;
        auto *form = new QVBoxLayout(page);

        m_photoPreview = new QLabel(tr("Перетащите изображение сюда или нажмите «Выбрать файл»"));
        m_photoPreview->setAlignment(Qt::AlignCenter);
        m_photoPreview->setMinimumHeight(220);
        m_photoPreview->setStyleSheet(QStringLiteral("QLabel { border: 1px dashed gray; }"));
        form->addWidget(m_photoPreview);

        auto *browseButton = new QPushButton(tr("Выбрать файл..."));
        connect(browseButton, &QPushButton::clicked, this, &ItemEditDialog::browseForPhoto);
        form->addWidget(browseButton);

        form->addWidget(new QLabel(tr("Подпись (необязательно):")));
        m_photoCaption = new QLineEdit;
        form->addWidget(m_photoCaption);

        m_stack->insertWidget(PagePhoto, page);
    }

    layout->addWidget(m_stack);

    // Slide background — shared by Song/Verse/Announcement, hidden for Photo
    // (a photo item's own image already is the whole slide).
    {
        m_backgroundGroup = new QWidget;
        auto *bgLayout = new QVBoxLayout(m_backgroundGroup);
        bgLayout->setContentsMargins(0, 8, 0, 0);
        bgLayout->addWidget(new QLabel(tr("Фон слайда (необязательно):")));

        auto *bgTypeRow = new QHBoxLayout;
        m_backgroundTypeCombo = new QComboBox;
        m_backgroundTypeCombo->addItem(tr("Без фона"), static_cast<int>(BackgroundType::None));
        m_backgroundTypeCombo->addItem(tr("Фото"), static_cast<int>(BackgroundType::Photo));
        m_backgroundTypeCombo->addItem(tr("Видео"), static_cast<int>(BackgroundType::Video));
        bgTypeRow->addWidget(m_backgroundTypeCombo);
        bgTypeRow->addStretch();
        bgLayout->addLayout(bgTypeRow);

        m_backgroundPathLabel = new QLabel(tr("Файл не выбран"));
        m_backgroundPathLabel->setStyleSheet(QStringLiteral("color: gray; font-size: 12px;"));
        bgLayout->addWidget(m_backgroundPathLabel);

        auto *bgFileRow = new QHBoxLayout;
        m_backgroundBrowseButton = new QPushButton(tr("Выбрать файл..."));
        connect(m_backgroundBrowseButton, &QPushButton::clicked, this, &ItemEditDialog::browseForBackground);
        bgFileRow->addWidget(m_backgroundBrowseButton);
        bgFileRow->addStretch();
        bgLayout->addLayout(bgFileRow);

        auto *bgYoutubeRow = new QHBoxLayout;
        m_backgroundYoutubeUrl = new QLineEdit;
        m_backgroundYoutubeUrl->setPlaceholderText(tr("...или вставьте ссылку на видео YouTube"));
        bgYoutubeRow->addWidget(m_backgroundYoutubeUrl, 1);
        m_backgroundYoutubeDownloadButton = new QPushButton(tr("Скачать"));
        connect(m_backgroundYoutubeDownloadButton, &QPushButton::clicked, this, &ItemEditDialog::downloadYoutubeBackground);
        bgYoutubeRow->addWidget(m_backgroundYoutubeDownloadButton);
        bgLayout->addLayout(bgYoutubeRow);

        connect(m_backgroundTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &ItemEditDialog::onBackgroundTypeChanged);

        layout->addWidget(m_backgroundGroup);
        onBackgroundTypeChanged(m_backgroundTypeCombo->currentIndex());
    }

    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ItemEditDialog::onTypeChanged);
    m_stack->setCurrentIndex(0);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &ItemEditDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &ItemEditDialog::reject);
    layout->addWidget(buttons);

    resize(520, 600);
}

void ItemEditDialog::onTypeChanged(int index)
{
    Q_UNUSED(index);
    const ContentType type = static_cast<ContentType>(m_typeCombo->currentData().toInt());
    m_stack->setCurrentIndex(pageForType(type));
    m_backgroundGroup->setVisible(type != ContentType::Photo);
}

void ItemEditDialog::onExpiryToggled(bool enabled)
{
    m_announcementExpiry->setEnabled(enabled);
}

void ItemEditDialog::onBackgroundTypeChanged(int index)
{
    Q_UNUSED(index);
    const auto bgType = static_cast<BackgroundType>(m_backgroundTypeCombo->currentData().toInt());
    const bool enabled = bgType != BackgroundType::None;
    const bool videoMode = bgType == BackgroundType::Video;

    m_backgroundBrowseButton->setEnabled(enabled);
    m_backgroundYoutubeUrl->setVisible(videoMode);
    m_backgroundYoutubeDownloadButton->setVisible(videoMode);

    if (!enabled)
        m_backgroundPathLabel->setText(tr("Файл не выбран"));
}

void ItemEditDialog::browseForBackground()
{
    const auto bgType = static_cast<BackgroundType>(m_backgroundTypeCombo->currentData().toInt());
    const QString filter = bgType == BackgroundType::Video
        ? tr("Видео (*.mp4 *.mov *.avi *.mkv *.webm)")
        : tr("Изображения (*.png *.jpg *.jpeg *.bmp *.gif)");

    const QString path = QFileDialog::getOpenFileName(this, tr("Выбрать файл фона"), QString(), filter);
    if (path.isEmpty())
        return;

    m_pendingBackgroundSource = path;
    m_pendingBackgroundIsDownload = false;
    m_backgroundPathLabel->setText(QFileInfo(path).fileName());
}

void ItemEditDialog::downloadYoutubeBackground()
{
    const QString url = m_backgroundYoutubeUrl->text().trimmed();
    if (url.isEmpty()) {
        QMessageBox::information(this, tr("Видео по ссылке"), tr("Вставьте ссылку на видео YouTube."));
        return;
    }

    const QString ytDlp = QStandardPaths::findExecutable(QStringLiteral("yt-dlp"));
    if (ytDlp.isEmpty()) {
        QMessageBox::information(this, tr("Нужна программа yt-dlp"),
            tr("Чтобы скачивать видео с YouTube прямо здесь, нужна бесплатная утилита yt-dlp "
               "(она не входит в Gather):\n\n"
               "1. Скачайте yt-dlp.exe:\n"
               "   https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe\n"
               "2. Положите файл в папку C:\\Windows или в папку с Gather.exe.\n"
               "3. Повторите скачивание здесь.\n\n"
               "Либо проще: скачайте видео вручную любым способом и выберите готовый файл "
               "кнопкой «Выбрать файл...» выше — результат тот же."));
        return;
    }

    const QString destDir = Database::backgroundsDir();
    const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString destTemplate = destDir + QStringLiteral("/") + uuid + QStringLiteral(".%(ext)s");

    m_backgroundYoutubeDownloadButton->setEnabled(false);
    m_backgroundYoutubeDownloadButton->setText(tr("Скачивание..."));
    m_backgroundPathLabel->setText(tr("Скачивание видео с YouTube..."));

    auto *process = new QProcess(this);
    const QStringList args{
        QStringLiteral("-f"), QStringLiteral("bv*[ext=mp4]+ba[ext=m4a]/best[ext=mp4]/best"),
        QStringLiteral("-o"), destTemplate,
        url
    };

    connect(process, &QProcess::finished, this, [this, process, destDir, uuid](int exitCode, QProcess::ExitStatus) {
        process->deleteLater();
        m_backgroundYoutubeDownloadButton->setEnabled(true);
        m_backgroundYoutubeDownloadButton->setText(tr("Скачать"));

        const QDir dir(destDir);
        const QStringList matches = dir.entryList(QStringList{uuid + QStringLiteral("*")}, QDir::Files);

        if (exitCode != 0 || matches.isEmpty()) {
            m_backgroundPathLabel->setText(tr("Файл не выбран"));
            QMessageBox::warning(this, tr("Ошибка"),
                tr("Не удалось скачать видео. Проверьте ссылку и подключение к интернету."));
            return;
        }

        const QString downloaded = dir.absoluteFilePath(matches.first());
        m_pendingBackgroundSource = downloaded;
        m_pendingBackgroundIsDownload = true;
        m_backgroundPathLabel->setText(tr("Видео с YouTube загружено: %1").arg(matches.first()));
    });

    process->start(ytDlp, args);
}

void ItemEditDialog::applyExisting()
{
    const int comboIndex = m_typeCombo->findData(static_cast<int>(m_original.type));
    if (comboIndex >= 0)
        m_typeCombo->setCurrentIndex(comboIndex);
    m_stack->setCurrentIndex(pageForType(m_original.type));

    switch (m_original.type) {
    case ContentType::Song:
        m_songTitle->setText(m_original.title);
        m_songNumber->setText(m_original.refLocation);
        m_songText->setPlainText(m_original.text);
        break;
    case ContentType::BibleVerse:
        m_verseBook->setText(m_original.refBook);
        m_verseLocation->setText(m_original.refLocation);
        m_verseText->setPlainText(m_original.text);
        break;
    case ContentType::Announcement:
        m_announcementTitle->setText(m_original.title);
        m_announcementText->setPlainText(m_original.text);
        if (m_original.expiryDate.isValid()) {
            m_announcementHasExpiry->setChecked(true);
            m_announcementExpiry->setDate(m_original.expiryDate);
        }
        break;
    case ContentType::Photo:
        m_photoCaption->setText(m_original.caption);
        if (!m_original.imagePath.isEmpty() && QFileInfo::exists(m_original.imagePath)) {
            QPixmap pix(m_original.imagePath);
            if (!pix.isNull())
                m_photoPreview->setPixmap(pix.scaled(m_photoPreview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
        break;
    }

    if (m_original.type != ContentType::Photo) {
        const int bgIndex = m_backgroundTypeCombo->findData(static_cast<int>(m_original.backgroundType));
        if (bgIndex >= 0)
            m_backgroundTypeCombo->setCurrentIndex(bgIndex);
        if (!m_original.backgroundPath.isEmpty())
            m_backgroundPathLabel->setText(QFileInfo(m_original.backgroundPath).fileName());
        onBackgroundTypeChanged(m_backgroundTypeCombo->currentIndex());
    }
    m_backgroundGroup->setVisible(m_original.type != ContentType::Photo);
}

void ItemEditDialog::browseForPhoto()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Выбрать изображение"), QString(),
                                                        tr("Изображения (*.png *.jpg *.jpeg *.bmp *.gif)"));
    if (!path.isEmpty())
        setPhotoPath(path);
}

void ItemEditDialog::setPhotoPath(const QString &sourcePath)
{
    QPixmap pix(sourcePath);
    if (pix.isNull()) {
        QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось открыть изображение."));
        return;
    }
    m_pendingPhotoSource = sourcePath;
    m_photoPreview->setPixmap(pix.scaled(m_photoPreview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void ItemEditDialog::dragEnterEvent(QDragEnterEvent *event)
{
    if (m_typeCombo->currentData().toInt() == static_cast<int>(ContentType::Photo) && event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void ItemEditDialog::dropEvent(QDropEvent *event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    if (!urls.isEmpty())
        setPhotoPath(urls.first().toLocalFile());
}

void ItemEditDialog::accept()
{
    const ContentType type = static_cast<ContentType>(m_typeCombo->currentData().toInt());

    if (type == ContentType::Photo && m_pendingPhotoSource.isEmpty() && m_original.imagePath.isEmpty()) {
        QMessageBox::warning(this, tr("Проверка"), tr("Выберите изображение."));
        return;
    }
    if (type == ContentType::Song && m_songTitle->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Проверка"), tr("Укажите название песни."));
        return;
    }
    if (type == ContentType::Announcement && m_announcementTitle->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Проверка"), tr("Укажите заголовок объявления."));
        return;
    }
    if (type == ContentType::BibleVerse && m_verseBook->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Проверка"), tr("Укажите книгу."));
        return;
    }

    if (!m_pendingPhotoSource.isEmpty()) {
        const QString ext = QFileInfo(m_pendingPhotoSource).suffix();
        const QString destName = QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".") + ext;
        const QString destPath = Database::photosDir() + QStringLiteral("/") + destName;
        if (!QFile::copy(m_pendingPhotoSource, destPath)) {
            QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось сохранить изображение."));
            return;
        }
        m_original.imagePath = destPath;
    }

    if (type == ContentType::Photo) {
        m_original.backgroundType = BackgroundType::None;
        m_original.backgroundPath.clear();
    } else {
        const auto bgType = static_cast<BackgroundType>(m_backgroundTypeCombo->currentData().toInt());
        if (bgType == BackgroundType::None) {
            m_original.backgroundType = BackgroundType::None;
            m_original.backgroundPath.clear();
        } else if (!m_pendingBackgroundSource.isEmpty()) {
            if (m_pendingBackgroundIsDownload) {
                m_original.backgroundPath = m_pendingBackgroundSource;
            } else {
                const QString ext = QFileInfo(m_pendingBackgroundSource).suffix();
                const QString destName = QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".") + ext;
                const QString destPath = Database::backgroundsDir() + QStringLiteral("/") + destName;
                if (!QFile::copy(m_pendingBackgroundSource, destPath)) {
                    QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось сохранить фон."));
                    return;
                }
                m_original.backgroundPath = destPath;
            }
            m_original.backgroundType = bgType;
        } else if (m_original.backgroundPath.isEmpty()) {
            QMessageBox::warning(this, tr("Проверка"),
                tr("Выберите файл фона или скачайте видео с YouTube, либо переключите на «Без фона»."));
            return;
        } else {
            // Editing an existing item: background type toggled but no new
            // file was picked — keep the previously saved file.
            m_original.backgroundType = bgType;
        }
    }

    QDialog::accept();
}

void ItemEditDialog::setDefaultType(ContentType type)
{
    if (!m_isNew)
        return;
    const int index = m_typeCombo->findData(static_cast<int>(type));
    if (index >= 0)
        m_typeCombo->setCurrentIndex(index);
}

ContentItem ItemEditDialog::item() const
{
    ContentItem result = m_original;
    result.type = static_cast<ContentType>(m_typeCombo->currentData().toInt());

    switch (result.type) {
    case ContentType::Song:
        result.title = m_songTitle->text().trimmed();
        result.refLocation = m_songNumber->text().trimmed();
        result.text = m_songText->toPlainText();
        break;
    case ContentType::BibleVerse:
        result.refBook = m_verseBook->text().trimmed();
        result.refLocation = m_verseLocation->text().trimmed();
        result.text = m_verseText->toPlainText();
        break;
    case ContentType::Announcement:
        result.title = m_announcementTitle->text().trimmed();
        result.text = m_announcementText->toPlainText();
        result.expiryDate = m_announcementHasExpiry->isChecked() ? m_announcementExpiry->date() : QDate();
        break;
    case ContentType::Photo:
        result.caption = m_photoCaption->text().trimmed();
        break;
    }

    return result;
}
