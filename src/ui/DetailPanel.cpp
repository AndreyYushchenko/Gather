#include "DetailPanel.h"
#include "IconProvider.h"
#include "Theme.h"

#include "core/Database.h"

#include <QButtonGroup>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTextEdit>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

namespace {
constexpr int TabContent = 0;
constexpr int TabInfo = 1;
constexpr int TabNotes = 2;
}

// A clickable slide thumbnail. Plain QFrame rather than QPushButton: see
// the comment in rebuildSlides() for why.
class SlideCard : public QFrame {
    Q_OBJECT
public:
    explicit SlideCard(const QString &text, QWidget *parent = nullptr)
        : QFrame(parent)
    {
        setFrameShape(QFrame::NoFrame);
        setCursor(Qt::PointingHandCursor);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 12, 12, 12);
        m_text = new QLabel(text, this);
        m_text->setWordWrap(true);
        m_text->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        m_text->setStyleSheet(QStringLiteral("color: white; font-size: 8.5px; background: transparent;"));
        layout->addWidget(m_text);
        setSelected(false);
    }

    void setSelected(bool selected)
    {
        if (m_selectionApplied && m_selected == selected)
            return;
        m_selected = selected;
        m_selectionApplied = true;
        // Scoped to SlideCard: an unscoped rule would cascade its
        // border/border-radius down onto the child text QLabel too.
        setStyleSheet(selected
            ? QStringLiteral("SlideCard { background: #1c2a4a; border: 2px solid %1; border-radius: 10px; }").arg(Theme::AccentBlue)
            : QStringLiteral("SlideCard { background: #212b3f; border: 2px solid transparent; border-radius: 10px; }"));
    }

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *) override { emit clicked(); }

private:
    bool m_selected = false;
    bool m_selectionApplied = false;
    QLabel *m_text = nullptr;
};

namespace {
QPushButton *makeOutlineButton(const QString &iconName, const QString &text)
{
    auto *button = new QPushButton;
    button->setObjectName(QStringLiteral("OutlineButton"));
    button->setCursor(Qt::PointingHandCursor);
    if (!iconName.isEmpty()) {
        button->setIcon(IconProvider::icon(iconName, QColor(Theme::TextDarkPrimary), 15));
        button->setIconSize(QSize(15, 15));
    }
    button->setText(text);
    return button;
}

QPushButton *makeOutlineIconButton(const QString &iconName, const QString &tooltip)
{
    auto *button = new QPushButton;
    button->setObjectName(QStringLiteral("OutlineIconButton"));
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedSize(38, 38);
    button->setIcon(IconProvider::icon(iconName, QColor(Theme::TextDarkPrimary), 15));
    button->setIconSize(QSize(15, 15));
    button->setToolTip(tooltip);
    return button;
}
}

DetailPanel::DetailPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("DetailPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    buildUi();
    showItem(std::nullopt);
}

void DetailPanel::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(28, 22, 28, 22);
    root->setSpacing(16);

    m_emptyState = new QLabel(tr("Выберите запись слева, чтобы увидеть детали"));
    m_emptyState->setAlignment(Qt::AlignCenter);
    m_emptyState->setStyleSheet(QStringLiteral("color: %1; font-size: 14px;").arg(Theme::TextDarkSecondary));
    root->addWidget(m_emptyState);

    m_content = new QWidget;
    root->addWidget(m_content);

    auto *layout = new QVBoxLayout(m_content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    // ---- Header ----
    auto *headerRow = new QHBoxLayout;
    headerRow->setSpacing(10);
    m_titleLabel = new QLabel;
    m_titleLabel->setStyleSheet(QStringLiteral("font-size: 24px; font-weight: 700; color: %1;").arg(Theme::TextDarkPrimary));
    m_starButton = new QPushButton;
    m_starButton->setFlat(true);
    m_starButton->setCursor(Qt::PointingHandCursor);
    m_starButton->setFixedSize(24, 24);
    m_starButton->setIconSize(QSize(20, 20));
    m_starButton->setStyleSheet(QStringLiteral("border: none; background: transparent;"));
    connect(m_starButton, &QPushButton::clicked, this, [this]() {
        if (m_item)
            emit favoriteToggleRequested(m_item->id);
    });
    headerRow->addWidget(m_titleLabel);
    headerRow->addWidget(m_starButton);
    headerRow->addStretch();

    auto *editButton = makeOutlineButton(QStringLiteral("pencil"), tr("Редактировать"));
    connect(editButton, &QPushButton::clicked, this, [this]() {
        if (m_item)
            emit editRequested(m_item->id);
    });
    headerRow->addWidget(editButton);

    auto *moreButton = makeOutlineIconButton(QStringLiteral("ellipsis-vertical"), tr("Ещё"));
    connect(moreButton, &QPushButton::clicked, this, [this, moreButton]() {
        if (!m_item)
            return;
        QMenu menu(this);
        QAction *deleteAction = menu.addAction(tr("Удалить"));
        connect(deleteAction, &QAction::triggered, this, [this]() { emit deleteRequested(m_item->id); });
        menu.exec(moreButton->mapToGlobal(QPoint(0, moreButton->height())));
    });
    headerRow->addWidget(moreButton);
    layout->addLayout(headerRow);

    m_metaLabel = new QLabel;
    m_metaLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 13.5px;").arg(Theme::TextDarkSecondary));
    layout->addWidget(m_metaLabel);

    // ---- Tabs ----
    auto *tabsRow = new QHBoxLayout;
    tabsRow->setSpacing(28);
    m_tabContent = new QPushButton(tr("Текст и слайды"));
    m_tabInfo = new QPushButton(tr("Информация"));
    m_tabNotes = new QPushButton(tr("Заметки"));
    for (QPushButton *tab : {m_tabContent, m_tabInfo, m_tabNotes}) {
        tab->setCheckable(true);
        tab->setObjectName(QStringLiteral("TabButton"));
        tab->setCursor(Qt::PointingHandCursor);
        tabsRow->addWidget(tab);
    }
    tabsRow->addStretch();
    layout->addLayout(tabsRow);

    auto *tabsDivider = new QWidget;
    tabsDivider->setFixedHeight(1);
    tabsDivider->setStyleSheet(QStringLiteral("background: %1;").arg(Theme::BorderLight));
    layout->addWidget(tabsDivider);

    auto *tabGroup = new QButtonGroup(this);
    tabGroup->addButton(m_tabContent, TabContent);
    tabGroup->addButton(m_tabInfo, TabInfo);
    tabGroup->addButton(m_tabNotes, TabNotes);
    m_tabContent->setChecked(true);

    m_tabStack = new QStackedWidget;
    layout->addWidget(m_tabStack, 1);
    connect(tabGroup, &QButtonGroup::idClicked, m_tabStack, &QStackedWidget::setCurrentIndex);

    // ---- Tab: content ----
    auto *page0 = new QWidget;
    auto *page0Layout = new QVBoxLayout(page0);
    page0Layout->setContentsMargins(0, 0, 0, 0);
    page0Layout->setSpacing(14);

    m_lyricsBox = new QTextEdit;
    m_lyricsBox->setReadOnly(true);
    m_lyricsBox->setObjectName(QStringLiteral("LyricsBox"));
    page0Layout->addWidget(m_lyricsBox);

    m_photoBox = new QLabel;
    m_photoBox->setAlignment(Qt::AlignCenter);
    m_photoBox->setMinimumHeight(260);
    m_photoBox->setMaximumHeight(420);
    m_photoBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    m_photoBox->setObjectName(QStringLiteral("LyricsBox"));
    page0Layout->addWidget(m_photoBox);

    m_slidesSection = new QWidget;
    auto *slidesLayout = new QVBoxLayout(m_slidesSection);
    slidesLayout->setContentsMargins(0, 0, 0, 0);
    slidesLayout->setSpacing(10);

    auto *slidesHeading = new QLabel(tr("Слайды"));
    slidesHeading->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 700; color: %1;").arg(Theme::TextDarkPrimary));
    slidesLayout->addWidget(slidesHeading);

    auto *slidesScroll = new QScrollArea;
    slidesScroll->setWidgetResizable(true);
    slidesScroll->setFrameShape(QFrame::NoFrame);
    slidesScroll->setFixedHeight(150);
    slidesScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    slidesScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *slidesContainer = new QWidget;
    m_slidesRow = new QHBoxLayout(slidesContainer);
    m_slidesRow->setContentsMargins(2, 2, 2, 2);
    m_slidesRow->setSpacing(14);
    slidesScroll->setWidget(slidesContainer);
    slidesLayout->addWidget(slidesScroll);

    page0Layout->addWidget(m_slidesSection);

    auto *actionsRow = new QHBoxLayout;
    actionsRow->setSpacing(10);
    m_addSlideButton = makeOutlineButton(QStringLiteral("plus"), tr("Добавить слайд"));
    m_copySlideButton = makeOutlineIconButton(QStringLiteral("copy"), tr("Дублировать слайд"));
    m_deleteSlideButton = makeOutlineIconButton(QStringLiteral("trash-2"), tr("Удалить слайд"));
    m_replacePhotoButton = makeOutlineButton(QStringLiteral("image"), tr("Заменить фото"));
    actionsRow->addWidget(m_addSlideButton);
    actionsRow->addWidget(m_copySlideButton);
    actionsRow->addWidget(m_deleteSlideButton);
    actionsRow->addWidget(m_replacePhotoButton);
    actionsRow->addStretch();

    auto *previewButton = makeOutlineButton(QStringLiteral("eye"), tr("Предпросмотр"));
    connect(previewButton, &QPushButton::clicked, this, [this]() {
        if (m_item)
            emit previewRequested(*m_item, m_selectedSlide);
    });
    actionsRow->addWidget(previewButton);

    auto *onScreenButton = new QPushButton(tr("На экран"));
    onScreenButton->setObjectName(QStringLiteral("PrimaryButton"));
    onScreenButton->setCursor(Qt::PointingHandCursor);
    onScreenButton->setIcon(IconProvider::icon(QStringLiteral("monitor"), QColor(Theme::TextLightPrimary), 15));
    onScreenButton->setIconSize(QSize(15, 15));
    connect(onScreenButton, &QPushButton::clicked, this, &DetailPanel::triggerGoLive);
    actionsRow->addWidget(onScreenButton);

    page0Layout->addLayout(actionsRow);
    m_tabStack->insertWidget(TabContent, page0);

    connect(m_addSlideButton, &QPushButton::clicked, this, [this]() {
        QStringList slides = m_slides;
        slides.insert(qMin(m_selectedSlide + 1, slides.size()), QString());
        applySlideTextChange(slides);
        selectSlide(m_selectedSlide + 1);
    });
    connect(m_copySlideButton, &QPushButton::clicked, this, [this]() {
        if (m_slides.isEmpty())
            return;
        QStringList slides = m_slides;
        slides.insert(m_selectedSlide + 1, slides.at(m_selectedSlide));
        applySlideTextChange(slides);
        selectSlide(m_selectedSlide + 1);
    });
    connect(m_deleteSlideButton, &QPushButton::clicked, this, [this]() {
        if (m_slides.size() <= 1)
            return;
        QStringList slides = m_slides;
        slides.removeAt(m_selectedSlide);
        applySlideTextChange(slides);
        selectSlide(qMin(m_selectedSlide, slides.size() - 1));
    });
    connect(m_replacePhotoButton, &QPushButton::clicked, this, [this]() {
        if (!m_item)
            return;
        const QString path = QFileDialog::getOpenFileName(this, tr("Выбрать изображение"), QString(),
                                                            tr("Изображения (*.png *.jpg *.jpeg *.bmp *.gif)"));
        if (path.isEmpty())
            return;
        const QString ext = QFileInfo(path).suffix();
        const QString destName = QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".") + ext;
        const QString destPath = Database::photosDir() + QStringLiteral("/") + destName;
        if (!QFile::copy(path, destPath)) {
            QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось сохранить изображение."));
            return;
        }
        ContentItem updated = *m_item;
        updated.imagePath = destPath;
        emit itemTextUpdated(updated);
    });

    // ---- Tab: info ----
    auto *page1 = new QWidget;
    auto *page1Layout = new QVBoxLayout(page1);
    m_infoText = new QLabel;
    m_infoText->setWordWrap(true);
    m_infoText->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_infoText->setStyleSheet(QStringLiteral("color: %1; font-size: 13.5px;").arg(Theme::TextDarkPrimary));
    page1Layout->addWidget(m_infoText);
    page1Layout->addStretch();
    m_tabStack->insertWidget(TabInfo, page1);

    // ---- Tab: notes ----
    auto *page2 = new QWidget;
    auto *page2Layout = new QVBoxLayout(page2);
    m_notesEdit = new QTextEdit;
    m_notesEdit->setPlaceholderText(tr("Заметки для оператора (не выводятся на экран)..."));
    page2Layout->addWidget(m_notesEdit);
    m_tabStack->insertWidget(TabNotes, page2);

    m_notesSaveTimer = new QTimer(this);
    m_notesSaveTimer->setSingleShot(true);
    m_notesSaveTimer->setInterval(600);
    connect(m_notesEdit, &QTextEdit::textChanged, this, [this]() { m_notesSaveTimer->start(); });
    connect(m_notesSaveTimer, &QTimer::timeout, this, [this]() {
        if (m_item)
            emit notesChanged(m_item->id, m_notesEdit->toPlainText());
    });

    setStyleSheet(QStringLiteral(R"(
        QWidget#DetailPanel { background: #ffffff; }
        QPushButton#OutlineButton, QPushButton#OutlineIconButton {
            background: #ffffff; border: 1px solid %2; border-radius: 9px;
            padding: 9px 16px; font-weight: 600; font-size: 13.5px; color: %1;
        }
        QPushButton#OutlineIconButton { padding: 9px; }
        QPushButton#OutlineButton:hover, QPushButton#OutlineIconButton:hover { background: #f3f4f6; }
        QPushButton#PrimaryButton {
            background: %3; border: none; border-radius: 9px; padding: 9px 16px;
            font-weight: 600; font-size: 13.5px; color: #ffffff;
        }
        QPushButton#PrimaryButton:hover { background: #255ed1; }
        QPushButton#TabButton {
            background: transparent; border: none; border-bottom: 2px solid transparent;
            padding: 0 0 10px 0; font-size: 14px; font-weight: 500; color: %4;
        }
        QPushButton#TabButton:checked { color: %3; font-weight: 600; border-bottom: 2px solid %3; }
        QTextEdit#LyricsBox, QLabel#LyricsBox {
            border: 1px solid %2; border-radius: 12px; padding: 20px;
            font-size: 14.5px; color: %1;
        }
    )").arg(Theme::TextDarkPrimary, Theme::BorderLight, Theme::AccentBlue, Theme::TextDarkSecondary));
}

void DetailPanel::showItem(const std::optional<ContentItem> &item)
{
    if (m_notesSaveTimer->isActive()) {
        m_notesSaveTimer->stop();
        if (m_item)
            emit notesChanged(m_item->id, m_notesEdit->toPlainText());
    }

    m_item = item;

    if (!item) {
        m_emptyState->show();
        m_content->hide();
        return;
    }

    m_emptyState->hide();
    m_content->show();

    m_titleLabel->setText(item->displayTitle());
    m_starButton->setIcon(IconProvider::icon(QStringLiteral("star"),
                                              QColor(item->favorite ? QStringLiteral("#f5a623") : Theme::TextDarkSecondary),
                                              20, item->favorite));

    const QString category = contentTypeDisplayName(item->type);
    m_metaLabel->setText(tr("%1 · добавлено %2").arg(category, item->createdAt.toString(QStringLiteral("dd.MM.yyyy"))));

    const bool isTextType = item->type == ContentType::Song || item->type == ContentType::BibleVerse;
    const bool isPhoto = item->type == ContentType::Photo;
    m_tabContent->setText(isPhoto ? tr("Просмотр") : tr("Текст и слайды"));
    m_slidesSection->setVisible(isTextType);
    m_addSlideButton->setVisible(isTextType);
    m_copySlideButton->setVisible(isTextType);
    m_deleteSlideButton->setVisible(isTextType);
    m_replacePhotoButton->setVisible(isPhoto);

    QStringList infoLines;
    infoLines << tr("Категория: %1").arg(category);
    infoLines << tr("Добавлено: %1").arg(item->createdAt.toString(QStringLiteral("dd.MM.yyyy HH:mm")));

    switch (item->type) {
    case ContentType::Song:
    case ContentType::BibleVerse:
        m_slides = item->slides();
        if (m_slides.isEmpty())
            m_slides << QString();
        m_lyricsBox->setPlainText(joinSlides(m_slides));
        m_lyricsBox->show();
        m_photoBox->hide();
        if (item->type == ContentType::BibleVerse)
            infoLines << tr("Ссылка: %1 %2").arg(item->refBook, item->refLocation);
        else if (!item->refLocation.isEmpty())
            infoLines << tr("Номер в сборнике: %1").arg(item->refLocation);
        break;

    case ContentType::Announcement:
        m_slides = QStringList{item->text};
        m_lyricsBox->setPlainText(item->text);
        m_lyricsBox->show();
        m_photoBox->hide();
        infoLines << (item->expiryDate.isValid()
                          ? tr("Актуально до: %1").arg(item->expiryDate.toString(QStringLiteral("dd.MM.yyyy")))
                          : tr("Без срока действия"));
        break;

    case ContentType::Photo:
        m_slides = QStringList{QString()};
        m_lyricsBox->hide();
        m_photoBox->show();
        if (!item->imagePath.isEmpty() && QFileInfo::exists(item->imagePath)) {
            QPixmap pix(item->imagePath);
            m_photoBox->setPixmap(pix.scaled(m_photoBox->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
            const QImageReader reader(item->imagePath);
            const QSize dims = reader.size();
            const qint64 bytes = QFileInfo(item->imagePath).size();
            m_metaLabel->setText(tr("%1 · %2×%3 · %4 МБ")
                .arg(category)
                .arg(dims.width())
                .arg(dims.height())
                .arg(QString::number(bytes / (1024.0 * 1024.0), 'f', 1)));
        } else {
            m_photoBox->setText(tr("Изображение не найдено"));
        }
        if (!item->caption.isEmpty())
            infoLines << tr("Подпись: %1").arg(item->caption);
        break;
    }

    m_infoText->setText(infoLines.join(QStringLiteral("\n")));

    m_notesEdit->blockSignals(true);
    m_notesEdit->setPlainText(item->notes);
    m_notesEdit->blockSignals(false);

    m_selectedSlide = qBound(0, m_selectedSlide, m_slides.size() - 1);
    rebuildSlides();
}

void DetailPanel::triggerGoLive()
{
    if (m_item)
        emit goLiveRequested(*m_item, m_selectedSlide);
}

QString DetailPanel::joinSlides(const QStringList &slides) const
{
    return slides.join(QStringLiteral("\n\n"));
}

void DetailPanel::applySlideTextChange(const QStringList &newSlides)
{
    if (!m_item)
        return;
    m_slides = newSlides;
    m_item->text = joinSlides(newSlides);
    emit itemTextUpdated(*m_item);
}

void DetailPanel::rebuildSlides()
{
    QLayoutItem *child;
    while ((child = m_slidesRow->takeAt(0)) != nullptr) {
        if (child->widget())
            child->widget()->deleteLater();
        delete child;
    }

    for (int i = 0; i < m_slides.size(); ++i) {
        auto *column = new QWidget;
        auto *columnLayout = new QVBoxLayout(column);
        columnLayout->setContentsMargins(0, 0, 0, 0);
        columnLayout->setSpacing(8);

        // A plain QWidget, not a QPushButton: nesting QLabel text inside a
        // QSS-styled QPushButton garbles ClearType text rendering on Windows.
        auto *card = new SlideCard(m_slides.at(i).isEmpty() ? tr("(пусто)") : m_slides.at(i));
        card->setFixedSize(150, 108);
        card->setSelected(i == m_selectedSlide);
        connect(card, &SlideCard::clicked, this, [this, i]() { selectSlide(i); });

        auto *number = new QLabel(QString::number(i + 1));
        number->setAlignment(Qt::AlignCenter);
        number->setStyleSheet(QStringLiteral("color: %1; font-size: 12.5px; font-weight: 500;").arg(Theme::TextDarkSecondary));

        columnLayout->addWidget(card);
        columnLayout->addWidget(number);
        m_slidesRow->addWidget(column);
    }
    m_slidesRow->addStretch();
}

void DetailPanel::selectSlide(int index)
{
    m_selectedSlide = qBound(0, index, qMax(0, m_slides.size() - 1));
    rebuildSlides();
}

#include "DetailPanel.moc"
