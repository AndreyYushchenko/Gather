#include "SlideStripPanel.h"
#include "ChevronButton.h"
#include "IconProvider.h"
#include "Theme.h"
#include "core/AppSettings.h"

#include "core/Database.h"

#include <QEasingCurve>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QVariantAnimation>
#include <QUuid>
#include <QVBoxLayout>

// A clickable slide thumbnail. Plain QFrame rather than QPushButton: nesting
// QLabel text inside a QSS-styled QPushButton garbles ClearType text
// rendering on Windows (see the same note on Sidebar's SidebarNavRow).
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
        // border/border-radius down onto the child text QLabel too. Fill is
        // the same navy gradient regardless of selection — only the border
        // changes — matching design.pen (both selected/unselected Slide
        // Cards share one gradient fill).
        setStyleSheet(selected
            ? QStringLiteral("SlideCard { background: qlineargradient(x1:0,y1:0,x2:0.6,y2:1, stop:0 #1c2a4a, stop:1 #0f1524); "
                              "border: 2px solid %1; border-radius: 10px; }").arg(Theme::AccentBlue)
            : QStringLiteral("SlideCard { background: qlineargradient(x1:0,y1:0,x2:0.6,y2:1, stop:0 #1c2a4a, stop:1 #0f1524); "
                              "border: 2px solid transparent; border-radius: 10px; }"));
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
// Slide card width (180, see rebuildSlides()) + the row's own gap (14).
constexpr int kSlideStep = 180 + 14;

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
} // namespace

SlideStripPanel::SlideStripPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("SlideStripPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    buildUi();
    showItem(std::nullopt);
}

void SlideStripPanel::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(28, 18, 28, 18);
    root->setSpacing(16);

    auto *divider = new QWidget;
    divider->setFixedHeight(1);
    divider->setStyleSheet(QStringLiteral("background: %1;").arg(Theme::BorderLight));
    root->addWidget(divider);

    m_body = new QWidget;
    auto *bodyLayout = new QVBoxLayout(m_body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(16);
    root->addWidget(m_body);

    auto *headingRow = new QHBoxLayout;
    auto *heading = new QLabel(tr("Слайды"));
    heading->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 700; color: %1;").arg(Theme::TextDarkPrimary));
    headingRow->addWidget(heading);
    headingRow->addStretch();
    m_collapseButton = new QPushButton;
    m_collapseButton->setFlat(true);
    m_collapseButton->setCursor(Qt::PointingHandCursor);
    m_collapseButton->setFixedSize(24, 24);
    m_collapseButton->setIconSize(QSize(16, 16));
    m_collapseButton->setStyleSheet(QStringLiteral("border: none; background: transparent;"));
    connect(m_collapseButton, &QPushButton::clicked, this, [this]() { setThumbnailsCollapsed(!m_thumbnailsCollapsed); });
    headingRow->addWidget(m_collapseButton);
    bodyLayout->addLayout(headingRow);

    m_slidesScroll = new QScrollArea;
    m_slidesScroll->setWidgetResizable(true);
    m_slidesScroll->setFrameShape(QFrame::NoFrame);
    m_slidesScroll->setMinimumHeight(0);
    m_slidesScroll->setMaximumHeight(130);
    // Free scrolling always rests at some arbitrary offset, so it can leave
    // a card half cut off at the viewport edge. Paging by a whole card
    // (m_pageLeftButton/m_pageRightButton below) instead guarantees every
    // card is either fully shown or fully off-screen — never cut.
    m_slidesScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_slidesScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // QScrollArea's viewport paints its own palette background (light gray
    // on Windows) unless told otherwise.
    m_slidesScroll->viewport()->setStyleSheet(QStringLiteral("QWidget#qt_scrollarea_viewport { background: #ffffff; }"));
    auto *slidesContainer = new QWidget;
    m_slidesRow = new QHBoxLayout(slidesContainer);
    m_slidesRow->setContentsMargins(2, 2, 2, 2);
    m_slidesRow->setSpacing(14);
    m_slidesScroll->setWidget(slidesContainer);
    // setWidget() turns on the content's own (gray) palette fill; let the white viewport show.
    slidesContainer->setAutoFillBackground(false);
    // rebuildSlides() calls updatePageButtons() synchronously right after
    // changing the card count, but the scroll area's own layout pass (which
    // recomputes horizontalScrollBar()'s range from the new content width)
    // only runs later on the event loop — so that call sees the stale range
    // and can hide both arrows even when there's more to page through.
    // rangeChanged fires once the real range is known and corrects it.
    connect(m_slidesScroll->horizontalScrollBar(), &QScrollBar::rangeChanged, this, &SlideStripPanel::updatePageButtons);

    // Collapsing hides this whole row — arrows included, not just the
    // scroll area — so it's one real container (its height is what gets
    // animated), not a bare layout with the arrows left floating outside it.
    // setFixedHeight (not just a maximum) below: a minimum of 0 would let
    // the surrounding layout treat this as compressible and silently shrink
    // it — clipping the (fixed-size) cards inside from the top — whenever
    // the panel as a whole is short on vertical space.
    m_slidesRowContainer = new QWidget;
    m_slidesRowContainer->setFixedHeight(130);
    auto *slidesScrollRow = new QHBoxLayout(m_slidesRowContainer);
    slidesScrollRow->setContentsMargins(0, 0, 0, 0);
    slidesScrollRow->setSpacing(8);
    m_pageLeftButton = new QPushButton;
    m_pageLeftButton->setFlat(true);
    m_pageLeftButton->setCursor(Qt::PointingHandCursor);
    m_pageLeftButton->setFixedSize(24, 130);
    m_pageLeftButton->setIconSize(QSize(16, 16));
    m_pageLeftButton->setIcon(IconProvider::icon(QStringLiteral("arrow-left"), QColor(Theme::TextDarkSecondary), 16));
    m_pageLeftButton->setStyleSheet(QStringLiteral("border: none; background: transparent;"));
    connect(m_pageLeftButton, &QPushButton::clicked, this, [this]() { pageSlides(-1); });
    slidesScrollRow->addWidget(m_pageLeftButton);

    slidesScrollRow->addWidget(m_slidesScroll, 1);

    m_pageRightButton = new QPushButton;
    m_pageRightButton->setFlat(true);
    m_pageRightButton->setCursor(Qt::PointingHandCursor);
    m_pageRightButton->setFixedSize(24, 130);
    m_pageRightButton->setIconSize(QSize(16, 16));
    m_pageRightButton->setIcon(IconProvider::icon(QStringLiteral("arrow-right"), QColor(Theme::TextDarkSecondary), 16));
    m_pageRightButton->setStyleSheet(QStringLiteral("border: none; background: transparent;"));
    connect(m_pageRightButton, &QPushButton::clicked, this, [this]() { pageSlides(1); });
    slidesScrollRow->addWidget(m_pageRightButton);

    bodyLayout->addWidget(m_slidesRowContainer);

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

    auto *onScreenButton = new ChevronButton(QStringLiteral("monitor"), tr("На экран"), QColor(Theme::TextLightPrimary));
    onScreenButton->setObjectName(QStringLiteral("PrimaryButton"));
    connect(onScreenButton, &ChevronButton::clicked, this, &SlideStripPanel::triggerGoLive);
    actionsRow->addWidget(onScreenButton);

    bodyLayout->addLayout(actionsRow);

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

    setStyleSheet(QStringLiteral(R"(
        QWidget#SlideStripPanel { background: #ffffff; }
        QPushButton#OutlineButton, QPushButton#OutlineIconButton {
            background: #ffffff; border: 1px solid %2; border-radius: 9px;
            padding: 9px 14px; font-weight: 600; font-size: 13.5px; color: %1;
        }
        QPushButton#OutlineIconButton { padding: 9px; }
        QPushButton#OutlineButton:hover, QPushButton#OutlineIconButton:hover { background: #f3f4f6; }
        QPushButton#PrimaryButton {
            background: %3; border: none; border-radius: 9px; padding: 9px 14px;
            font-weight: 600; font-size: 13.5px; color: #ffffff;
        }
        QFrame#PrimaryButton {
            background: %3; border: none; border-radius: 9px;
        }
        QPushButton#PrimaryButton:hover, QFrame#PrimaryButton:hover { background: #255ed1; }
    )").arg(Theme::TextDarkPrimary, Theme::BorderLight, Theme::AccentBlue));

    setThumbnailsCollapsed(false);
}

void SlideStripPanel::setThumbnailsCollapsed(bool collapsed)
{
    m_thumbnailsCollapsed = collapsed;
    m_collapseButton->setIcon(IconProvider::icon(collapsed ? QStringLiteral("chevron-down") : QStringLiteral("chevron-up"),
                                                   QColor(Theme::TextDarkSecondary), 16));
    m_collapseButton->setToolTip(collapsed ? tr("Показать слайды") : tr("Скрыть слайды"));

    if (!m_collapseAnimation) {
        // A QVariantAnimation driving setFixedHeight() by hand, rather than
        // a QPropertyAnimation on "maximumHeight": the latter only bounds
        // the height from above, leaving minimumHeight at 0 and the
        // container compressible by the surrounding layout at every step —
        // including once the animation ends and it should be rock solid.
        m_collapseAnimation = new QVariantAnimation(this);
        m_collapseAnimation->setDuration(180);
        m_collapseAnimation->setEasingCurve(QEasingCurve::InOutQuad);
        connect(m_collapseAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            m_slidesRowContainer->setFixedHeight(value.toInt());
        });
    }
    m_collapseAnimation->stop();
    m_collapseAnimation->setStartValue(m_slidesRowContainer->height());
    m_collapseAnimation->setEndValue(collapsed ? 0 : 130);
    m_collapseAnimation->start();
}

void SlideStripPanel::showItem(const std::optional<ContentItem> &item)
{
    m_item = item;

    if (!item) {
        m_body->hide();
        return;
    }

    const bool isTextType = item->type == ContentType::Song || item->type == ContentType::BibleVerse;
    const bool isPhoto = item->type == ContentType::Photo;
    m_body->setVisible(isTextType || isPhoto);
    m_addSlideButton->setVisible(isTextType);
    m_copySlideButton->setVisible(isTextType);
    m_deleteSlideButton->setVisible(isTextType);
    m_replacePhotoButton->setVisible(isPhoto);

    switch (item->type) {
    case ContentType::Song:
    case ContentType::BibleVerse:
        m_slides = item->presentationSlides();
        if (m_slides.isEmpty())
            m_slides << QString();
        break;
    case ContentType::Announcement:
        m_slides = QStringList{item->text};
        break;
    case ContentType::Photo:
    case ContentType::Video:
        m_slides = QStringList{QString()};
        break;
    }

    m_selectedSlide = qBound(0, m_selectedSlide, m_slides.size() - 1);
    rebuildSlides();
}

void SlideStripPanel::triggerGoLive()
{
    if (m_item)
        emit goLiveRequested(*m_item, m_selectedSlide);
}

QString SlideStripPanel::joinSlides(const QStringList &slides) const
{
    return slides.join(QStringLiteral("\n\n"));
}

void SlideStripPanel::applySlideTextChange(const QStringList &newSlides)
{
    if (!m_item)
        return;
    m_slides = newSlides;
    m_item->text = joinSlides(newSlides);
    emit itemTextUpdated(*m_item);
}

void SlideStripPanel::rebuildSlides()
{
    QLayoutItem *child;
    while ((child = m_slidesRow->takeAt(0)) != nullptr) {
        if (child->widget())
            child->widget()->deleteLater();
        delete child;
    }

    // "Нумеровать куплеты": which verse / the chorus under each card.
    const QStringList labels = m_item && m_item->type == ContentType::Song && AppSettings::value(AppSettings::NumberVerses).toBool()
        ? songSlideLabels(m_slides) : QStringList();
    for (int i = 0; i < m_slides.size(); ++i) {
        auto *column = new QWidget;
        auto *columnLayout = new QVBoxLayout(column);
        columnLayout->setContentsMargins(0, 0, 0, 0);
        columnLayout->setSpacing(8);

        auto *card = new SlideCard(m_slides.at(i).isEmpty() ? tr("(пусто)") : m_slides.at(i));
        card->setFixedSize(180, 100);
        card->setSelected(i == m_selectedSlide);
        connect(card, &SlideCard::clicked, this, [this, i]() { selectSlide(i); });

        auto *number = new QLabel(labels.value(i).isEmpty() ? QString::number(i + 1)
                                                             : QStringLiteral("%1 · %2").arg(i + 1).arg(labels.at(i)));
        number->setAlignment(Qt::AlignCenter);
        number->setStyleSheet(QStringLiteral("color: %1; font-size: 12.5px; font-weight: 500;").arg(Theme::TextDarkSecondary));

        columnLayout->addWidget(card);
        columnLayout->addWidget(number);
        m_slidesRow->addWidget(column);
    }
    m_slidesRow->addStretch();
    updatePageButtons();
}

void SlideStripPanel::selectSlide(int index)
{
    m_selectedSlide = qBound(0, index, qMax(0, m_slides.size() - 1));
    rebuildSlides();
}

void SlideStripPanel::pageSlides(int direction)
{
    QScrollBar *bar = m_slidesScroll->horizontalScrollBar();
    const int visibleCards = qMax(1, m_slidesScroll->viewport()->width() / kSlideStep);
    const int newValue = bar->value() + direction * visibleCards * kSlideStep;
    bar->setValue(qBound(bar->minimum(), newValue, bar->maximum()));
    updatePageButtons();
}

void SlideStripPanel::updatePageButtons()
{
    QScrollBar *bar = m_slidesScroll->horizontalScrollBar();
    const bool scrollable = bar->maximum() > bar->minimum();
    m_pageLeftButton->setVisible(scrollable && bar->value() > bar->minimum());
    m_pageRightButton->setVisible(scrollable && bar->value() < bar->maximum());
}

#include "SlideStripPanel.moc"
