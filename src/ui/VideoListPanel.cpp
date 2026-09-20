#include "VideoListPanel.h"
#include "IconProvider.h"
#include "Theme.h"

#include "core/Database.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

namespace {
QString humanSize(qint64 bytes)
{
    if (bytes <= 0)
        return QObject::tr("YouTube");
    if (bytes >= 1024 * 1024)
        return QStringLiteral("%1 МБ").arg(QString::number(bytes / (1024.0 * 1024.0), 'f', 1));
    return QStringLiteral("%1 КБ").arg(QString::number(bytes / 1024.0, 'f', 0));
}

bool isRemoteSource(const QString &path)
{
    return path.startsWith(QStringLiteral("http://")) || path.startsWith(QStringLiteral("https://"));
}
}

// Not in an anonymous namespace: moc cannot generate metaobject code for
// Q_OBJECT classes declared inside one.
class VideoRowWidget : public QFrame {
    Q_OBJECT
public:
    explicit VideoRowWidget(const ContentItem &item, QWidget *parent = nullptr)
        : QFrame(parent)
        , m_id(item.id)
        , m_favorite(item.favorite)
    {
        setFrameShape(QFrame::NoFrame);
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(12, 10, 12, 10);
        layout->setSpacing(10);

        auto *icon = new QLabel;
        icon->setPixmap(IconProvider::pixmap(QStringLiteral("video"), QColor(Theme::TextDarkSecondary), 16));
        layout->addWidget(icon);

        auto *textCol = new QVBoxLayout;
        textCol->setSpacing(2);
        m_titleLabel = new QLabel(item.displayTitle());
        m_titleLabel->setStyleSheet(QStringLiteral("font-weight: 600; font-size: 13.5px;"));
        const QString subtitle = isRemoteSource(item.imagePath)
            ? QObject::tr("YouTube")
            : humanSize(QFileInfo(item.imagePath).size());
        auto *sub = new QLabel(subtitle);
        sub->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(Theme::TextDarkSecondary));
        textCol->addWidget(m_titleLabel);
        textCol->addWidget(sub);
        layout->addLayout(textCol, 1);

        m_starButton = new QPushButton;
        m_starButton->setFlat(true);
        m_starButton->setCursor(Qt::PointingHandCursor);
        m_starButton->setFixedSize(24, 24);
        m_starButton->setIconSize(QSize(17, 17));
        m_starButton->setStyleSheet(QStringLiteral("border: none; background: transparent;"));
        connect(m_starButton, &QPushButton::clicked, this, [this]() { emit starClicked(m_id); });
        layout->addWidget(m_starButton);

        setSelected(false);
    }

    void setSelected(bool selected)
    {
        if (m_selectionApplied && m_selected == selected)
            return;
        m_selected = selected;
        m_selectionApplied = true;
        setStyleSheet(selected
                          ? QStringLiteral("VideoRowWidget { background: %1; border-radius: 9px; }").arg(Theme::AccentBlueBg)
                          : QStringLiteral("VideoRowWidget { background: transparent; border-radius: 9px; }"));
        m_titleLabel->setStyleSheet(QStringLiteral("font-weight: 600; font-size: 13.5px; color: %1;")
                                         .arg(selected ? Theme::AccentBlue : Theme::TextDarkPrimary));
        const QColor starColor(selected ? Theme::AccentBlue : Theme::TextDarkSecondary);
        m_starButton->setIcon(IconProvider::icon(QStringLiteral("star"), starColor, 17, m_favorite));
    }

signals:
    void clicked(int id);
    void starClicked(int id);

protected:
    void mousePressEvent(QMouseEvent *) override { emit clicked(m_id); }

private:
    int m_id;
    bool m_favorite;
    bool m_selected = false;
    bool m_selectionApplied = false;
    QLabel *m_titleLabel = nullptr;
    QPushButton *m_starButton = nullptr;
};

VideoListPanel::VideoListPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("VideoListPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(360);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 18, 16, 18);
    layout->setSpacing(14);

    m_uploadButton = new QPushButton;
    m_uploadButton->setObjectName(QStringLiteral("Dropzone"));
    m_uploadButton->setCursor(Qt::PointingHandCursor);
    m_uploadButton->setIcon(IconProvider::icon(QStringLiteral("upload"), QColor(Theme::AccentBlue), 16));
    m_uploadButton->setIconSize(QSize(16, 16));
    m_uploadButton->setText(tr("Загрузить видео с компьютера"));
    connect(m_uploadButton, &QPushButton::clicked, this, &VideoListPanel::browseForVideo);
    layout->addWidget(m_uploadButton);

    auto *youtubeRow = new QHBoxLayout;
    youtubeRow->setSpacing(8);
    m_youtubeUrl = new QLineEdit;
    m_youtubeUrl->setObjectName(QStringLiteral("SearchBox"));
    m_youtubeUrl->setPlaceholderText(tr("Ссылка на YouTube-видео"));
    m_youtubeUrl->addAction(IconProvider::icon(QStringLiteral("youtube"), QColor(QStringLiteral("#FF0000")), 16),
                             QLineEdit::LeadingPosition);
    youtubeRow->addWidget(m_youtubeUrl, 1);
    m_youtubeButton = new QPushButton(tr("Скачать"));
    m_youtubeButton->setObjectName(QStringLiteral("PrimaryButton"));
    m_youtubeButton->setCursor(Qt::PointingHandCursor);
    m_youtubeButton->setIcon(IconProvider::icon(QStringLiteral("download"), QColor(Theme::TextLightPrimary), 15));
    m_youtubeButton->setIconSize(QSize(15, 15));
    connect(m_youtubeButton, &QPushButton::clicked, this, &VideoListPanel::importFromYoutube);
    youtubeRow->addWidget(m_youtubeButton);
    layout->addLayout(youtubeRow);

    auto *searchRow = new QHBoxLayout;
    searchRow->setSpacing(10);
    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Поиск по названию файла..."));
    m_search->setObjectName(QStringLiteral("SearchBox"));
    m_search->addAction(IconProvider::icon(QStringLiteral("search"), QColor(Theme::TextDarkSecondary), 16),
                         QLineEdit::LeadingPosition);
    searchRow->addWidget(m_search, 1);

    m_filterButton = new QPushButton;
    m_filterButton->setObjectName(QStringLiteral("FilterButton"));
    m_filterButton->setCheckable(true);
    m_filterButton->setFixedSize(38, 38);
    m_filterButton->setIconSize(QSize(16, 16));
    m_filterButton->setIcon(IconProvider::icon(QStringLiteral("list-filter"), QColor(Theme::TextDarkSecondary), 16));
    m_filterButton->setToolTip(tr("Показывать только избранное"));
    m_filterButton->setCursor(Qt::PointingHandCursor);
    searchRow->addWidget(m_filterButton);
    layout->addLayout(searchRow);

    auto *sortRow = new QHBoxLayout;
    sortRow->setSpacing(4);
    m_sortButton = new QPushButton;
    m_sortButton->setObjectName(QStringLiteral("SortButton"));
    m_sortButton->setCursor(Qt::PointingHandCursor);
    m_sortButton->setIcon(IconProvider::icon(QStringLiteral("chevron-down"), QColor(Theme::TextDarkSecondary), 14));
    m_sortButton->setIconSize(QSize(14, 14));
    m_sortButton->setLayoutDirection(Qt::RightToLeft);
    sortRow->addWidget(m_sortButton);
    sortRow->addStretch();
    m_countLabel = new QLabel;
    m_countLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 12.5px; font-weight: 500;").arg(Theme::TextDarkSecondary));
    sortRow->addWidget(m_countLabel);
    layout->addLayout(sortRow);

    auto *sortMenu = new QMenu(m_sortButton);
    QAction *newestAction = sortMenu->addAction(tr("Сначала новые"));
    QAction *alphaAction = sortMenu->addAction(tr("По алфавиту (А-Я)"));
    connect(newestAction, &QAction::triggered, this, [this]() {
        m_sortOrder = SortOrder::DateAddedDesc;
        updateSortLabel();
        emit filtersChanged();
    });
    connect(alphaAction, &QAction::triggered, this, [this]() {
        m_sortOrder = SortOrder::Alphabetical;
        updateSortLabel();
        emit filtersChanged();
    });
    m_sortButton->setMenu(sortMenu);
    updateSortLabel();

    m_list = new QListWidget;
    m_list->setObjectName(QStringLiteral("VideoList"));
    m_list->setSpacing(2);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setSelectionMode(QAbstractItemView::NoSelection);
    m_list->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(m_list, 1);

    m_searchDebounce = new QTimer(this);
    m_searchDebounce->setSingleShot(true);
    m_searchDebounce->setInterval(250);
    connect(m_searchDebounce, &QTimer::timeout, this, &VideoListPanel::filtersChanged);
    connect(m_search, &QLineEdit::textChanged, this, [this]() { m_searchDebounce->start(); });
    connect(m_filterButton, &QPushButton::toggled, this, [this](bool on) {
        m_favoritesOnly = on;
        emit filtersChanged();
    });

    setStyleSheet(QStringLiteral(R"(
        QWidget#VideoListPanel { background: %1; border-right: 1px solid %2; }
        QPushButton#Dropzone {
            background: %5; border: 1.5px dashed %6; border-radius: 9px;
            padding: 9px 14px; font-weight: 600; font-size: 13px; color: %6;
        }
        QPushButton#Dropzone:hover { background: #dbe6fb; }
        QLineEdit#SearchBox {
            background: #ffffff; border: 1px solid %2; border-radius: 9px;
            padding: 9px 12px; font-size: 13px; color: %3;
        }
        QPushButton#PrimaryButton {
            background: %6; border: none; border-radius: 9px; padding: 9px 14px;
            font-weight: 600; font-size: 13px; color: #ffffff;
        }
        QPushButton#PrimaryButton:hover { background: #255ed1; }
        QPushButton#FilterButton {
            background: #ffffff; border: 1px solid %2; border-radius: 9px;
        }
        QPushButton#FilterButton:checked { border-color: #f5a623; }
        QPushButton#SortButton {
            background: transparent; border: none; padding: 0;
            font-size: 12.5px; font-weight: 500; color: %4;
        }
        QPushButton#SortButton::menu-indicator { image: none; width: 0; }
        QListWidget#VideoList { background: transparent; border: none; }
        QListWidget#VideoList::item { border: none; }
    )").arg(Theme::BgPanel, Theme::BorderLight, Theme::TextDarkPrimary, Theme::TextDarkSecondary,
            Theme::AccentBlueBg, Theme::AccentBlue));
}

void VideoListPanel::updateSortLabel()
{
    m_sortButton->setText(m_sortOrder == SortOrder::Alphabetical
        ? tr("Сортировка: по алфавиту (А-Я)")
        : tr("Сортировка: по дате добавления"));
}

void VideoListPanel::browseForVideo()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Выбрать видео"), QString(),
        tr("Видео (*.mp4 *.mov *.avi *.mkv *.webm)"));
    if (path.isEmpty())
        return;

    const QString ext = QFileInfo(path).suffix();
    const QString destName = QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".") + ext;
    const QString destPath = Database::videosDir() + QStringLiteral("/") + destName;
    if (!QFile::copy(path, destPath)) {
        QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось сохранить видео."));
        return;
    }

    ContentItem item;
    item.type = ContentType::Video;
    item.title = QFileInfo(path).completeBaseName();
    item.imagePath = destPath;
    emit videoAdded(item);
}

void VideoListPanel::importFromYoutube()
{
    const QString url = m_youtubeUrl->text().trimmed();
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
               "Либо проще: скачайте видео вручную любым способом и добавьте готовый файл "
               "кнопкой «Загрузить видео с компьютера» выше — результат тот же."));
        return;
    }

    const QString destDir = Database::videosDir();
    const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString destTemplate = destDir + QStringLiteral("/") + uuid + QStringLiteral(".%(ext)s");

    m_youtubeButton->setEnabled(false);
    m_youtubeButton->setText(tr("Скачивание..."));

    auto *process = new QProcess(this);
    const QStringList args{
        QStringLiteral("-f"), QStringLiteral("bv*[ext=mp4]+ba[ext=m4a]/best[ext=mp4]/best"),
        QStringLiteral("-o"), destTemplate,
        QStringLiteral("--print"), QStringLiteral("%(title)s"),
        url
    };

    connect(process, &QProcess::finished, this, [this, process, destDir, uuid](int exitCode, QProcess::ExitStatus) {
        const QString title = QString::fromUtf8(process->readAllStandardOutput()).trimmed();
        process->deleteLater();
        m_youtubeButton->setEnabled(true);
        m_youtubeButton->setText(tr("Скачать"));

        const QDir dir(destDir);
        const QStringList matches = dir.entryList(QStringList{uuid + QStringLiteral("*")}, QDir::Files);

        if (exitCode != 0 || matches.isEmpty()) {
            QMessageBox::warning(this, tr("Ошибка"),
                tr("Не удалось скачать видео. Проверьте ссылку и подключение к интернету."));
            return;
        }

        ContentItem item;
        item.type = ContentType::Video;
        item.title = title.isEmpty() ? QFileInfo(matches.first()).completeBaseName() : title;
        item.imagePath = dir.absoluteFilePath(matches.first());
        m_youtubeUrl->clear();
        emit videoAdded(item);
    });

    process->start(ytDlp, args);
}

void VideoListPanel::setItems(const QList<ContentItem> &items)
{
    m_items = items;
    const std::optional<int> previousSelection = selectedItemId();

    // See LibraryListPanel::setItems for why: without this the list
    // repaints/relayouts after every single addItem() below.
    m_list->setUpdatesEnabled(false);
    m_list->clear();
    for (const ContentItem &item : m_items) {
        auto *row = new VideoRowWidget(item);
        connect(row, &VideoRowWidget::clicked, this, [this](int id) { selectItemById(id); });
        connect(row, &VideoRowWidget::starClicked, this, &VideoListPanel::favoriteToggled);

        auto *listItem = new QListWidgetItem;
        listItem->setSizeHint(row->sizeHint());
        listItem->setData(Qt::UserRole, item.id);
        m_list->addItem(listItem);
        m_list->setItemWidget(listItem, row);
    }
    m_list->setUpdatesEnabled(true);

    // "видео" doesn't decline by count in Russian, unlike the other lists'
    // nouns (запись/плейлист/...), so there's no plural form to pick here.
    m_countLabel->setText(tr("%1 видео").arg(m_items.size()));

    if (previousSelection.has_value())
        selectItemById(*previousSelection);
    else
        emit itemSelected(std::nullopt);
}

std::optional<int> VideoListPanel::selectedItemId() const
{
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->data(Qt::UserRole + 1).toBool())
            return m_list->item(i)->data(Qt::UserRole).toInt();
    return std::nullopt;
}

void VideoListPanel::selectItemById(int id)
{
    bool found = false;
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem *listItem = m_list->item(i);
        auto *row = qobject_cast<VideoRowWidget *>(m_list->itemWidget(listItem));
        const bool matches = listItem->data(Qt::UserRole).toInt() == id;
        if (row)
            row->setSelected(matches);
        listItem->setData(Qt::UserRole + 1, matches);
        if (matches) {
            found = true;
            m_list->scrollToItem(listItem);
        }
    }
    emit itemSelected(found ? std::optional<int>(id) : std::nullopt);
}

QString VideoListPanel::searchText() const
{
    return m_search->text();
}

SortOrder VideoListPanel::sortOrder() const
{
    return m_sortOrder;
}

#include "VideoListPanel.moc"
