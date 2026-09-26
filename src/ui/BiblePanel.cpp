#include "BiblePanel.h"
#include "DisplaySettings.h"
#include "ChevronButton.h"
#include "IconProvider.h"
#include "Theme.h"
#include "core/AppSettings.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QCompleter>
#include <QMenu>
#include <QSettings>
#include <QStringListModel>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVariantAnimation>
#include <QVBoxLayout>

namespace {

// QComboBox#FieldBox reserves 12px left / 30px right padding for the
// dropdown arrow (see the stylesheet below). Long item text (a long
// translation or book name) needs to be elided to fit *inside* that
// safe width ourselves: the native Windows style's own combobox label
// painting doesn't respect a custom QSS drop-down's reserved width at
// all here, so an unelided long string paints straight across the full
// box, over the arrow, with no "…" at all.
QString elideForFieldBox(QComboBox *combo, const QString &text, int boxWidth)
{
    // Measuring with a hand-built QFont (even matching family/size/weight
    // on paper) does not reliably match what the widget actually paints —
    // confirmed by testing: it silently produced a string ~40px too wide.
    // ensurePolished() forces the pending QSS to resolve immediately, so
    // combo->fontMetrics() afterward reflects the exact font that will
    // paint this text, not an approximation of it.
    combo->ensurePolished();
    const QFontMetrics metrics = combo->fontMetrics();
    return metrics.elidedText(text, Qt::ElideRight, boxWidth - 46);
}

constexpr int TabReference = 0;
constexpr int TabSearch = 1;

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

QPushButton *makePrimaryButton(const QString &iconName, const QString &text)
{
    auto *button = new QPushButton;
    button->setObjectName(QStringLiteral("PrimaryButton"));
    button->setCursor(Qt::PointingHandCursor);
    button->setIcon(IconProvider::icon(iconName, QColor(Theme::TextLightPrimary), 15));
    button->setIconSize(QSize(15, 15));
    button->setText(text);
    return button;
}

QPushButton *makeOutlineIconButton(const QString &iconName, const QString &tooltip,
                                    const QColor &iconColor = QColor(Theme::TextDarkPrimary), int iconSize = 15)
{
    auto *button = new QPushButton;
    button->setObjectName(QStringLiteral("OutlineIconButton"));
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedSize(38, 38);
    button->setIcon(IconProvider::icon(iconName, iconColor, iconSize));
    button->setIconSize(QSize(iconSize, iconSize));
    button->setToolTip(tooltip);
    return button;
}

// A clickable slide thumbnail, matching design.pen's "Slide Card" (Ez08L):
// a dark navy gradient card with a 2px accent border when selected.
class SlideCard : public QFrame {
    Q_OBJECT
public:
    explicit SlideCard(const QString &text, QWidget *parent = nullptr)
        : QFrame(parent)
    {
        setFrameShape(QFrame::NoFrame);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::ClickFocus);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(14, 14, 14, 14);
        m_text = new QLabel(text, this);
        m_text->setWordWrap(true);
        m_text->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        m_text->setStyleSheet(QStringLiteral("color: white; font-size: 10px; background: transparent;"));
        layout->addWidget(m_text);
        setSelected(false);
    }

    void setSelected(bool selected)
    {
        setStyleSheet(QStringLiteral(
            "SlideCard { border-radius: 10px; border: 2px solid %1;"
            " background: qlineargradient(x1:0, y1:0, x2:0.6, y2:1, stop:0 #1c2a4a, stop:1 #0f1524); }")
                .arg(selected ? Theme::AccentBlue : QStringLiteral("transparent")));
    }

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *) override { emit clicked(); }

private:
    QLabel *m_text = nullptr;
};

// A left-aligned clickable row used for search hits.
QPushButton *makePlaceRow(const QString &text)
{
    auto *button = new QPushButton(text);
    button->setObjectName(QStringLiteral("PlaceRow"));
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

} // namespace

// A single reference-picker field (design.pen node under D7hQPT): a label
// above a bordered value box with a trailing chevron. design.pen gives each
// field an exact pixel width (270/270/130/110/110, summing to 946px with
// gaps) — kept here as the field's *maximum* width, so it renders
// pixel-exact whenever the window has room for that. But design.pen has no
// responsive behavior of its own to fall back on below that, and the app's
// window can legitimately be narrower than 946+margins (smaller screens) —
// so each field can also shrink down to a readable minimum and re-elides
// its own text live as it resizes, rather than ever running into its
// neighbor (Qt's layout never overlaps siblings on its own; the previous
// bug was fixed *widths* leaving Qt nowhere to take the missing space from
// except the gaps between fields, collapsing them to zero). Declared here
// (not in the anonymous namespace above) so BiblePanel.h can forward-declare
// it and BiblePanel can keep a couple of these around as members.
class ReferenceField : public QWidget {
public:
    ReferenceField(const QString &label, int designWidth, int minWidth, QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumWidth(minWidth);
        setMaximumWidth(designWidth);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);
        auto *labelWidget = new QLabel(label);
        labelWidget->setStyleSheet(QStringLiteral("color: %1; font-size: 12.5px; font-weight: 500;").arg(Theme::TextDarkSecondary));
        layout->addWidget(labelWidget);

        m_combo = new QComboBox;
        m_combo->setObjectName(QStringLiteral("FieldBox"));
        m_combo->setFixedHeight(36);
        m_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        layout->addWidget(m_combo);

        // A sibling of the combo, not its child: the native Windows style
        // silently fails to paint custom child content in the last ~30px
        // of a themed QComboBox, so the chevron is drawn as its own
        // top-level paint target instead (see elideForFieldBox's own
        // comment for the matching text-painting issue this sidesteps).
        m_chevron = new QLabel(this);
        m_chevron->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_chevron->setPixmap(IconProvider::pixmap(QStringLiteral("chevron-down"), QColor(Theme::TextDarkSecondary), 14));
        m_chevron->setFixedSize(14, 14);
        m_comboTop = labelWidget->sizeHint().height() + 6;
        positionChevron();
        m_chevron->raise();
    }

    QComboBox *combo() const { return m_combo; }

    // Replaces the combo's items with these (full, unelided) texts,
    // re-eliding each to the field's *current* width — call again whenever
    // the underlying list changes (translation reload, book list refresh).
    void setItems(const QStringList &fullTexts, int currentIndex = 0)
    {
        m_fullTexts = fullTexts;
        rebuildItems();
        if (currentIndex >= 0 && currentIndex < m_combo->count())
            m_combo->setCurrentIndex(currentIndex);
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        positionChevron();
        rebuildItems();
    }

private:
    void positionChevron()
    {
        m_chevron->move(width() - 12 - 14, m_comboTop + (36 - 14) / 2);
    }

    void rebuildItems()
    {
        if (m_fullTexts.isEmpty() || width() <= 0)
            return;
        const int keepIndex = m_combo->currentIndex();
        m_combo->blockSignals(true);
        m_combo->clear();
        for (const QString &text : std::as_const(m_fullTexts)) {
            m_combo->addItem(elideForFieldBox(m_combo, text, width()));
            m_combo->setItemData(m_combo->count() - 1, text, Qt::ToolTipRole);
        }
        if (keepIndex >= 0 && keepIndex < m_combo->count())
            m_combo->setCurrentIndex(keepIndex);
        m_combo->blockSignals(false);
    }

    QComboBox *m_combo;
    QLabel *m_chevron;
    QStringList m_fullTexts;
    int m_comboTop = 0;
};

BiblePanel::BiblePanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("BiblePanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    buildUi();
}

void BiblePanel::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(28, 22, 28, 22);
    root->setSpacing(16);

    // ---- Header (design.pen node IHhN4) ----
    auto *title = new QLabel(tr("Библия"));
    title->setStyleSheet(QStringLiteral("font-size: 24px; font-weight: 700; color: %1;").arg(Theme::TextDarkPrimary));
    root->addWidget(title);

    // ---- Tabs (design.pen node olKsH) ----
    auto *tabsRow = new QHBoxLayout;
    tabsRow->setSpacing(28);
    m_tabReference = new QPushButton(tr("По тексту"));
    m_tabSearch = new QPushButton(tr("По поиску"));
    for (QPushButton *tab : {m_tabReference, m_tabSearch}) {
        tab->setCheckable(true);
        tab->setObjectName(QStringLiteral("TabButton"));
        tab->setCursor(Qt::PointingHandCursor);
        tab->setIconSize(QSize(16, 16));
        tabsRow->addWidget(tab);
    }
    tabsRow->addStretch();

    // Icons recolor with the tab's selected state (design.pen: the active
    // tab's icon/label are $accent-blue, the inactive one is
    // $text-dark-secondary) instead of staying fixed to whichever tab was
    // active at construction time.
    auto restyleTabIcons = [this]() {
        m_tabReference->setIcon(IconProvider::icon(QStringLiteral("refresh-ccw"),
            QColor(m_tabReference->isChecked() ? Theme::AccentBlue : Theme::TextDarkSecondary), 16));
        m_tabSearch->setIcon(IconProvider::icon(QStringLiteral("search"),
            QColor(m_tabSearch->isChecked() ? Theme::AccentBlue : Theme::TextDarkSecondary), 16));
    };
    restyleTabIcons();
    connect(m_tabReference, &QPushButton::toggled, this, restyleTabIcons);
    connect(m_tabSearch, &QPushButton::toggled, this, restyleTabIcons);

    // design.pen's "Tabs Outer" (olKsH) groups the tab row and its 1px
    // underline with NO gap between them, as one item in Tp127's own
    // uniform 16px vertical rhythm. Adding tabsRow and the divider as two
    // separate items straight into `root` would slip an extra, wrong 16px
    // gap in between them (root's own spacing applies between every item
    // it holds) — so they're wrapped in a zero-spacing group first.
    auto *tabsGroup = new QWidget;
    auto *tabsGroupLayout = new QVBoxLayout(tabsGroup);
    tabsGroupLayout->setContentsMargins(0, 0, 0, 0);
    tabsGroupLayout->setSpacing(0);
    tabsGroupLayout->addLayout(tabsRow);
    auto *tabsDivider = new QWidget;
    tabsDivider->setFixedHeight(1);
    tabsDivider->setStyleSheet(QStringLiteral("background: %1;").arg(Theme::BorderLight));
    tabsGroupLayout->addWidget(tabsDivider);
    root->addWidget(tabsGroup);

    auto *tabGroup = new QButtonGroup(this);
    tabGroup->addButton(m_tabReference, TabReference);
    tabGroup->addButton(m_tabSearch, TabSearch);
    m_tabReference->setChecked(true);

    m_tabStack = new QStackedWidget;
    root->addWidget(m_tabStack, 1);
    connect(tabGroup, &QButtonGroup::idClicked, m_tabStack, &QStackedWidget::setCurrentIndex);

    // ---- Page: По тексту (reference browser) ----
    auto *refPage = new QWidget;
    auto *refLayout = new QVBoxLayout(refPage);
    refLayout->setContentsMargins(0, 0, 0, 0);
    refLayout->setSpacing(16);

    // Matches design.pen node D7hQPT ("Reference Picker Row"): five fields
    // at exact pixel widths (270/270/130/110/110) with 14px gaps whenever
    // the window is wide enough — see ReferenceField above for how each one
    // shrinks below that instead of ever overlapping its neighbor.
    auto *pickerRow = new QHBoxLayout;
    pickerRow->setSpacing(14);

    auto makeField = [&](const QString &label, int designWidth, int minWidth) -> ReferenceField * {
        auto *field = new ReferenceField(label, designWidth, minWidth);
        pickerRow->addWidget(field, designWidth);
        return field;
    };

    m_translationField = makeField(tr("Перевод"), 270, 150);
    m_bookField = makeField(tr("Книга"), 270, 150);
    auto *chapterField = makeField(tr("Глава"), 130, 80);
    auto *fromField = makeField(tr("С"), 110, 70);
    auto *toField = makeField(tr("По"), 110, 70);
    pickerRow->addStretch();
    refLayout->addLayout(pickerRow);

    m_translationBox = m_translationField->combo();
    m_bookBox = m_bookField->combo();
    m_chapterBox = chapterField->combo();
    m_fromBox = fromField->combo();
    m_toBox = toField->combo();

    connect(m_bookBox, &QComboBox::currentIndexChanged, this, &BiblePanel::onBookChanged);
    connect(m_chapterBox, &QComboBox::currentIndexChanged, this, &BiblePanel::onChapterChanged);
    connect(m_fromBox, &QComboBox::currentIndexChanged, this, &BiblePanel::onRangeChanged);
    connect(m_toBox, &QComboBox::currentIndexChanged, this, &BiblePanel::onRangeChanged);

    // design.pen no longer has a "Search Row" under the picker on the "По
    // тексту" page (removed in a later revision) — full-text search now
    // lives only on the "По поиску" tab's own search box.
    const QString searchPlaceholder = tr("Поиск в тексте книг, своих заметках, назв. слайдов, плейлистах...");

    // design.pen dropped the bordered "card" look entirely: the verse detail
    // now sits directly on the page background, and the verses box grows
    // with its content instead of a fixed card height — a "Bottom Spacer"
    // (fill_container) between the divider and the slides section absorbs
    // whatever room is left, pinning the slides block to the page bottom.
    auto *bodyLayout = new QVBoxLayout;
    bodyLayout->setSpacing(16);

    // ---- Verse Detail Column (design.pen node TfeFx) ----
    // Matches design.pen node QhAVp ("Verse Detail Head"): title on the
    // left, translation label + favorite button grouped on the right
    // (space-between).
    auto *verseHead = new QHBoxLayout;
    m_refTitle = new QLabel;
    m_refTitle->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: 700; color: %1;").arg(Theme::TextDarkPrimary));
    verseHead->addWidget(m_refTitle);
    // "Недавние места" (Настройки → Библия → Количество недавних мест).
    m_recentButton = new QPushButton;
    m_recentButton->setFlat(true);
    m_recentButton->setCursor(Qt::PointingHandCursor);
    m_recentButton->setFixedSize(30, 30);
    m_recentButton->setIcon(IconProvider::icon(QStringLiteral("clock"), QColor(Theme::TextDarkSecondary), 17));
    m_recentButton->setIconSize(QSize(17, 17));
    m_recentButton->setToolTip(tr("Недавние места"));
    m_recentButton->setStyleSheet(QStringLiteral("QPushButton { border: none; background: transparent; border-radius: 8px; }"
                                                 "QPushButton:hover { background: #f3f4f6; }"));
    connect(m_recentButton, &QPushButton::clicked, this, &BiblePanel::showRecentMenu);
    verseHead->addSpacing(6);
    verseHead->addWidget(m_recentButton);
    verseHead->addStretch();

    m_translationCaption = new QLabel;
    m_translationCaption->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: 400; color: %1;").arg(Theme::TextDarkSecondary));
    verseHead->addWidget(m_translationCaption);

    // design.pen node d3SHa ("Favorite Button") — relocated here from the
    // page header, relabeled "В избранное".
    m_favoriteButton = makeOutlineButton(QStringLiteral("star"), tr("В избранное"));
    connect(m_favoriteButton, &QPushButton::clicked, this, [this]() {
        if (m_currentBook == 0)
            return;
        ContentItem item = currentItem();
        item.favorite = true;
        emit saveToLibraryRequested(item);
    });
    verseHead->addSpacing(14);
    verseHead->addWidget(m_favoriteButton);
    bodyLayout->addLayout(verseHead);

    // Matches design.pen node IAk7I ("Verses Box"): fit-content height, 6px
    // top padding, 16px gap between verse rows, no border/card. Still a
    // QScrollArea with both scrollbars forced off rather than a plain
    // QWidget: a chapter can run to 30+ verses, and unlike the design's own
    // mock (a short 6-verse chapter, which fits without scrolling and just
    // leaves the flexible space below larger), the real page has nowhere
    // to grow — a plain fixed-height QWidget can't gracefully clip a child
    // whose required height vastly exceeds it (Qt's layout fails to give it
    // any paintable geometry at all, so the verse text disappears entirely
    // instead of being cut off). QScrollArea's viewport clips correctly
    // regardless of content size; hiding both scrollbars keeps the visual
    // identical to the design while the mouse wheel still quietly reaches a
    // long chapter's tail.
    auto *versesScroll = new QScrollArea;
    versesScroll->setWidgetResizable(true);
    versesScroll->setFrameShape(QFrame::NoFrame);
    versesScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    versesScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    versesScroll->viewport()->setStyleSheet(QStringLiteral("QWidget#qt_scrollarea_viewport { background: #ffffff; }"));
    auto *versesContainer = new QWidget;
    versesContainer->setStyleSheet(QStringLiteral("background: transparent;"));
    m_versesLayout = new QVBoxLayout(versesContainer);
    m_versesLayout->setContentsMargins(0, 6, 0, 0);
    m_versesLayout->setSpacing(16);
    versesScroll->setWidget(versesContainer);
    // setWidget() turns on the content's own (gray) palette fill; let the white viewport show.
    versesContainer->setAutoFillBackground(false);
    bodyLayout->addWidget(versesScroll, 1);

    // Body Divider (design.pen node YyCXb)
    auto *bodyDivider = new QWidget;
    bodyDivider->setFixedHeight(1);
    bodyDivider->setStyleSheet(QStringLiteral("background: %1;").arg(Theme::BorderLight));
    bodyLayout->addWidget(bodyDivider);

    // design.pen's "Bottom Spacer" (hTz4O, height: fill_container) is
    // already covered by versesScroll's own stretch factor above — it's the
    // one expanding item in this layout, so it absorbs whatever room is
    // left and pins the slides section below to the page bottom.

    // ---- Slides Head Row (design.pen node Z9TgeV) ----
    auto *slidesHeadRow = new QHBoxLayout;
    m_slidesHeading = new QLabel;
    m_slidesHeading->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 700; color: %1;").arg(Theme::TextDarkPrimary));
    slidesHeadRow->addWidget(m_slidesHeading);
    slidesHeadRow->addStretch();
    m_slidesCollapseButton = new QPushButton;
    m_slidesCollapseButton->setFlat(true);
    m_slidesCollapseButton->setCursor(Qt::PointingHandCursor);
    m_slidesCollapseButton->setFixedSize(24, 24);
    m_slidesCollapseButton->setIconSize(QSize(16, 16));
    m_slidesCollapseButton->setStyleSheet(QStringLiteral("border: none; background: transparent;"));
    connect(m_slidesCollapseButton, &QPushButton::clicked, this, [this]() { setSlidesCollapsed(!m_slidesCollapsed); });
    slidesHeadRow->addWidget(m_slidesCollapseButton);
    bodyLayout->addLayout(slidesHeadRow);

    // ---- Slides Row (design.pen node lu0J0) ----
    auto *slidesScroll = new QScrollArea;
    slidesScroll->setWidgetResizable(true);
    slidesScroll->setFrameShape(QFrame::NoFrame);
    // 124 (card 100 + 8 gap + number label) is design.pen's own number for
    // its no-scroll 6-slide example, but it leaves no room for the
    // horizontal scrollbar that appears for any chapter with more slides
    // than fit on one screen (i.e. almost every real chapter) — that
    // scrollbar was eating into the fixed height and clipping the slide
    // number label right off the bottom of every row.
    slidesScroll->setFixedHeight(148);
    slidesScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    slidesScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    slidesScroll->viewport()->setStyleSheet(QStringLiteral("QWidget#qt_scrollarea_viewport { background: #ffffff; }"));
    auto *slidesContainer = new QWidget;
    m_slidesRow = new QHBoxLayout(slidesContainer);
    m_slidesRow->setContentsMargins(2, 2, 2, 2);
    m_slidesRow->setSpacing(14);
    slidesScroll->setWidget(slidesContainer);
    // setWidget() turns on the content's own (gray) palette fill; let the white viewport show.
    slidesContainer->setAutoFillBackground(false);
    bodyLayout->addWidget(slidesScroll);
    m_slidesScroll = slidesScroll;
    setSlidesCollapsed(false);

    // ---- Slide Actions Row (design.pen node dw9AW) ----
    auto *actionsRow = new QHBoxLayout;
    auto *leftActions = new QHBoxLayout;
    leftActions->setSpacing(10);
    auto *addSlideButton = makePrimaryButton(QStringLiteral("plus"), tr("Добавить слайд"));
    auto *copySlideButton = makeOutlineIconButton(QStringLiteral("copy"), tr("Дублировать слайд"));
    auto *deleteSlideButton = makeOutlineIconButton(QStringLiteral("trash-2"), tr("Удалить слайд"));
    connect(addSlideButton, &QPushButton::clicked, this, [this]() {
        QStringList slides = m_slides;
        slides.insert(qMin(m_selectedSlide + 1, slides.size()), QString());
        applySlideTextChange(slides);
        selectSlide(m_selectedSlide + 1);
    });
    connect(copySlideButton, &QPushButton::clicked, this, [this]() {
        if (m_slides.isEmpty())
            return;
        QStringList slides = m_slides;
        slides.insert(m_selectedSlide + 1, slides.at(m_selectedSlide));
        applySlideTextChange(slides);
        selectSlide(m_selectedSlide + 1);
    });
    connect(deleteSlideButton, &QPushButton::clicked, this, [this]() {
        if (m_slides.size() <= 1)
            return;
        QStringList slides = m_slides;
        slides.removeAt(m_selectedSlide);
        applySlideTextChange(slides);
        selectSlide(qMin(m_selectedSlide, slides.size() - 1));
    });
    leftActions->addWidget(addSlideButton);
    leftActions->addWidget(copySlideButton);
    leftActions->addWidget(deleteSlideButton);
    actionsRow->addLayout(leftActions);
    actionsRow->addStretch();

    auto *rightActions = new QHBoxLayout;
    rightActions->setSpacing(10);
    auto *previewButton = makeOutlineButton(QStringLiteral("settings"), tr("Предпросмотр"));
    connect(previewButton, &QPushButton::clicked, this, [this]() {
        if (m_currentBook != 0)
            emit previewRequested(currentItem(), m_selectedSlide);
    });
    rightActions->addWidget(previewButton);

    auto *onScreenButton = new ChevronButton(QStringLiteral("monitor"), tr("На экран"), QColor(Theme::TextLightPrimary));
    onScreenButton->setObjectName(QStringLiteral("PrimaryButton"));
    connect(onScreenButton, &ChevronButton::clicked, this, &BiblePanel::triggerGoLive);
    rightActions->addWidget(onScreenButton);
    actionsRow->addLayout(rightActions);

    bodyLayout->addLayout(actionsRow);
    refLayout->addLayout(bodyLayout, 1);

    m_tabStack->insertWidget(TabReference, refPage);

    // ---- Page: По поиску (full-text search) ----
    auto *searchPage = new QWidget;
    auto *searchPageLayout = new QVBoxLayout(searchPage);
    searchPageLayout->setContentsMargins(0, 0, 0, 0);
    searchPageLayout->setSpacing(14);

    auto *searchRow = new QHBoxLayout;
    searchRow->setSpacing(10);
    auto *searchBoxWrap = new QFrame;
    searchBoxWrap->setObjectName(QStringLiteral("SearchBox"));
    auto *searchBoxLayout = new QHBoxLayout(searchBoxWrap);
    searchBoxLayout->setContentsMargins(12, 0, 12, 0);
    searchBoxLayout->setSpacing(8);
    auto *searchIcon = new QLabel;
    searchIcon->setPixmap(IconProvider::pixmap(QStringLiteral("search"), QColor(Theme::TextDarkSecondary), 16));
    m_searchBox = new QLineEdit;
    m_searchBox->setPlaceholderText(searchPlaceholder);
    m_searchBox->setObjectName(QStringLiteral("SearchField")); // Ctrl+F (Горячие клавиши → Поиск)
    // "Хранить историю поиска": earlier queries are offered as you type.
    m_searchHistory = new QStringListModel(QSettings().value(QStringLiteral("bible/recentSearches")).toStringList(), this);
    auto *completer = new QCompleter(m_searchHistory, this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    m_searchBox->setCompleter(completer);
    connect(m_searchBox, &QLineEdit::returnPressed, this, [this]() { rememberSearch(m_searchBox->text()); });
    m_searchBox->setFrame(false);
    searchBoxLayout->addWidget(searchIcon);
    searchBoxLayout->addWidget(m_searchBox, 1);
    connect(m_searchBox, &QLineEdit::textChanged, this, &BiblePanel::runSearch);
    searchRow->addWidget(searchBoxWrap, 1);
    searchRow->addWidget(makeOutlineIconButton(QStringLiteral("list-filter"), tr("Фильтр"),
                                                QColor(Theme::TextDarkSecondary), 16));
    searchPageLayout->addLayout(searchRow);

    auto *resultsScroll = new QScrollArea;
    resultsScroll->setWidgetResizable(true);
    resultsScroll->setFrameShape(QFrame::NoFrame);
    resultsScroll->setStyleSheet(QStringLiteral("QScrollArea { background: #ffffff; border: none; }"));
    resultsScroll->viewport()->setStyleSheet(QStringLiteral("QWidget#qt_scrollarea_viewport { background: #ffffff; }"));
    auto *resultsContainer = new QWidget;
    m_searchResults = new QVBoxLayout(resultsContainer);
    m_searchResults->setContentsMargins(0, 0, 0, 0);
    m_searchResults->setSpacing(2);
    m_searchResults->addStretch();
    resultsScroll->setWidget(resultsContainer);
    // setWidget() turns on the content's own (gray) palette fill; let the white viewport show.
    resultsContainer->setAutoFillBackground(false);
    searchPageLayout->addWidget(resultsScroll, 1);

    m_tabStack->insertWidget(TabSearch, searchPage);

    setStyleSheet(QStringLiteral(R"(
        QWidget#BiblePanel { background: #ffffff; }
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
        QFrame#PrimaryButton {
            background: %3; border: none; border-radius: 9px;
        }
        QPushButton#PrimaryButton:hover, QFrame#PrimaryButton:hover { background: #255ed1; }
        QPushButton#TabButton {
            background: transparent; border: none; border-bottom: 2px solid transparent;
            padding: 0 0 10px 0; font-size: 14px; font-weight: 500; color: %4;
        }
        QPushButton#TabButton:checked { color: %3; font-weight: 600; border-bottom: 2px solid %3; }
        QComboBox#FieldBox {
            border: 1px solid %2; border-radius: 9px; padding: 10px 30px 10px 12px;
            font-size: 14px; font-weight: 500; color: %1; background: #ffffff;
        }
        QComboBox#FieldBox::drop-down { width: 0px; border: none; }
        QFrame#SearchBox { background: #ffffff; border: 1px solid %2; border-radius: 9px; min-height: 38px; }
        QLineEdit { border: none; background: transparent; font-size: 13px; color: %1; }
        QPushButton#PlaceRow {
            text-align: left; border: none; background: transparent; border-radius: 9px;
            padding: 10px 12px; font-size: 13.5px; font-weight: 600; color: %1;
        }
        QPushButton#PlaceRow:hover { background: #f3f4f6; color: %3; }
    )").arg(Theme::TextDarkPrimary, Theme::BorderLight, Theme::AccentBlue, Theme::TextDarkSecondary));

    reload();
}

void BiblePanel::reload()
{
    m_translation = m_repo.defaultTranslation();
    const QString alt = AppSettings::value(AppSettings::BibleAltTranslation).toString();
    m_altTranslation = AppSettings::value(AppSettings::BibleAltEnabled).toBool() && alt != m_translation
            && m_repo.translations().contains(alt)
        ? alt : QString();
    m_recentButton->setVisible(AppSettings::value(AppSettings::BibleKeepHistory).toBool());
    if (!AppSettings::value(AppSettings::BibleKeepHistory).toBool()) {
        QSettings().remove(QStringLiteral("bible/recentPlaces"));
        QSettings().remove(QStringLiteral("bible/recentSearches"));
    }
    if (m_searchHistory)
        m_searchHistory->setStringList(QSettings().value(QStringLiteral("bible/recentSearches")).toStringList());
    const QString translationLabel = m_translation.isEmpty() ? tr("Нет перевода") : m_translation;
    m_translationField->setItems({translationLabel});

    populateBooks();

    const bool hasData = !m_books.isEmpty();
    m_bookBox->setEnabled(hasData);
    m_chapterBox->setEnabled(hasData);
    m_fromBox->setEnabled(hasData);
    m_toBox->setEnabled(hasData);

    if (hasData)
        onBookChanged();
}

void BiblePanel::populateBooks()
{
    m_books = m_repo.books(m_translation);
    QStringList bookNames;
    bookNames.reserve(m_books.size());
    for (const BibleBook &book : std::as_const(m_books))
        bookNames << book.name;
    m_bookField->setItems(bookNames);
}

QString BiblePanel::bookName(int bookNum) const
{
    for (const BibleBook &book : m_books)
        if (book.num == bookNum)
            return book.name;
    return {};
}

void BiblePanel::onBookChanged()
{
    if (m_books.isEmpty())
        return;
    const int index = qBound(0, m_bookBox->currentIndex(), m_books.size() - 1);
    const BibleBook &book = m_books.at(index);
    m_currentBook = book.num;

    m_chapterBox->blockSignals(true);
    m_chapterBox->clear();
    for (int c = 1; c <= book.chapterCount; ++c)
        m_chapterBox->addItem(QString::number(c));
    m_chapterBox->blockSignals(false);

    onChapterChanged();
}

void BiblePanel::onChapterChanged()
{
    if (m_currentBook == 0)
        return;
    m_currentChapter = m_chapterBox->currentIndex() + 1;
    if (m_currentChapter <= 0)
        m_currentChapter = 1;

    const int count = m_repo.verseCount(m_translation, m_currentBook, m_currentChapter);

    m_fromBox->blockSignals(true);
    m_toBox->blockSignals(true);
    m_fromBox->clear();
    m_toBox->clear();
    for (int v = 1; v <= count; ++v) {
        m_fromBox->addItem(QString::number(v));
        m_toBox->addItem(QString::number(v));
    }
    m_fromBox->setCurrentIndex(0);
    m_toBox->setCurrentIndex(qMax(0, count - 1));
    m_fromBox->blockSignals(false);
    m_toBox->blockSignals(false);

    m_currentFrom = 1;
    m_currentTo = count;
    refreshVerseView();
}

void BiblePanel::onRangeChanged()
{
    if (m_currentBook == 0 || m_fromBox->currentIndex() < 0 || m_toBox->currentIndex() < 0)
        return;
    m_currentFrom = m_fromBox->currentIndex() + 1;
    m_currentTo = m_toBox->currentIndex() + 1;
    if (m_currentTo < m_currentFrom) {
        m_currentTo = m_currentFrom;
        m_toBox->blockSignals(true);
        m_toBox->setCurrentIndex(m_toBox->currentIndex() < m_fromBox->currentIndex() ? m_fromBox->currentIndex() : m_toBox->currentIndex());
        m_toBox->blockSignals(false);
    }
    refreshVerseView();
}

void BiblePanel::selectReference(int bookNum, int chapter, int fromVerse, int toVerse)
{
    const int bookIndex = [&]() {
        for (int i = 0; i < m_books.size(); ++i)
            if (m_books.at(i).num == bookNum)
                return i;
        return -1;
    }();
    if (bookIndex < 0)
        return;

    m_bookBox->blockSignals(true);
    m_bookBox->setCurrentIndex(bookIndex);
    m_bookBox->blockSignals(false);
    m_currentBook = bookNum;

    const BibleBook &book = m_books.at(bookIndex);
    m_chapterBox->blockSignals(true);
    m_chapterBox->clear();
    for (int c = 1; c <= book.chapterCount; ++c)
        m_chapterBox->addItem(QString::number(c));
    m_chapterBox->setCurrentIndex(qBound(1, chapter, book.chapterCount) - 1);
    m_chapterBox->blockSignals(false);
    m_currentChapter = chapter;

    const int count = m_repo.verseCount(m_translation, bookNum, chapter);
    m_fromBox->blockSignals(true);
    m_toBox->blockSignals(true);
    m_fromBox->clear();
    m_toBox->clear();
    for (int v = 1; v <= count; ++v) {
        m_fromBox->addItem(QString::number(v));
        m_toBox->addItem(QString::number(v));
    }
    m_currentFrom = qBound(1, fromVerse, qMax(1, count));
    m_currentTo = qBound(m_currentFrom, toVerse, qMax(1, count));
    m_fromBox->setCurrentIndex(m_currentFrom - 1);
    m_toBox->setCurrentIndex(m_currentTo - 1);
    m_fromBox->blockSignals(false);
    m_toBox->blockSignals(false);

    refreshVerseView();

    if (m_tabStack->currentIndex() != TabReference) {
        m_tabReference->setChecked(true);
        m_tabStack->setCurrentIndex(TabReference);
    }
}

void BiblePanel::refreshVerseView()
{
    QLayoutItem *child;
    while ((child = m_versesLayout->takeAt(0)) != nullptr) {
        if (child->widget())
            child->widget()->deleteLater();
        delete child;
    }

    const QList<BibleVerse> verses = m_repo.verses(m_translation, m_currentBook, m_currentChapter, m_currentFrom, m_currentTo);

    const QString book = bookName(m_currentBook);
    m_refTitle->setText(m_currentFrom == m_currentTo
        ? tr("%1 %2:%3").arg(book).arg(m_currentChapter).arg(m_currentFrom)
        : tr("%1 %2:%3–%4").arg(book).arg(m_currentChapter).arg(m_currentFrom).arg(m_currentTo));
    m_translationCaption->setText(m_translation);

    for (const BibleVerse &verse : verses) {
        auto *row = new QWidget;
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(14);
        // Matches design.pen node W0lMTI ("Verse Row"): the number is an
        // auto-width label of the same weight/size family as the verse
        // text, left-aligned — not a fixed-width right-aligned column.
        auto *number = new QLabel(QString::number(verse.verse));
        number->setStyleSheet(QStringLiteral("color: %1; font-size: 16px; font-weight: 700;").arg(Theme::TextDarkPrimary));
        auto *text = new QLabel(verse.text);
        text->setWordWrap(true);
        text->setStyleSheet(QStringLiteral("color: %1; font-size: 16px;").arg(Theme::TextDarkPrimary));
        rowLayout->addWidget(number, 0, Qt::AlignTop);
        rowLayout->addWidget(text, 1);
        m_versesLayout->addWidget(row);
    }

    // Настройки → Библия: one slide per verse or the whole passage on one,
    // verse numbers as superscripts (¹⁶), and the alternative translation
    // under each verse.
    const bool split = AppSettings::value(AppSettings::BibleSplitVerses).toBool();
    const bool numbers = AppSettings::value(AppSettings::BibleVerseNumbers).toBool()
        || AppSettings::value(DisplaySettings::styleKey(ContentType::BibleVerse, QStringLiteral("separateVerseNumber"))).toBool();
    QList<BibleVerse> altVerses;
    if (!m_altTranslation.isEmpty())
        altVerses = m_repo.verses(m_altTranslation, m_currentBook, m_currentChapter, m_currentFrom, m_currentTo);
    const auto superscript = [](int number) {
        static const QString digits = QStringLiteral("⁰¹²³⁴⁵⁶⁷⁸⁹");
        QString result;
        for (const QChar c : QString::number(number))
            result += digits.at(c.digitValue());
        return result;
    };
    QStringList parts;
    for (int i = 0; i < verses.size(); ++i) {
        QString part = numbers ? superscript(verses.at(i).verse) + QLatin1Char(' ') + verses.at(i).text : verses.at(i).text;
        for (const BibleVerse &altVerse : std::as_const(altVerses)) {
            if (altVerse.verse == verses.at(i).verse)
                part += QLatin1Char('\n') + altVerse.text;
        }
        parts << part;
    }
    if (parts.isEmpty())
        m_slides = QStringList{QString()};
    else if (split)
        m_slides = parts;
    else
        m_slides = QStringList{parts.join(QLatin1Char(' '))};
    m_slides = DisplaySettings::splitTextSlides(ContentType::BibleVerse, m_slides);
    if (m_slides.isEmpty() || !m_slides.last().isEmpty()) {
        m_slides << QString();
    }
    rememberPlace();

    m_selectedSlide = 0;
    m_slidesHeading->setText(tr("Слайды (%1)").arg(m_slides.size()));
    rebuildSlides();
}

void BiblePanel::applySlideTextChange(const QStringList &newSlides)
{
    m_slides = newSlides;
    m_slidesHeading->setText(tr("Слайды (%1)").arg(m_slides.size()));
    rebuildSlides();
}

void BiblePanel::rebuildSlides()
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
        columnLayout->setAlignment(Qt::AlignHCenter);

        auto *card = new SlideCard(m_slides.at(i).isEmpty() ? tr("(пусто)") : m_slides.at(i));
        card->setFixedSize(145, 100);
        card->setSelected(i == m_selectedSlide);
        connect(card, &SlideCard::clicked, this, [this, i]() { selectSlide(i); });

        auto *number = new QLabel(QString::number(i + 1));
        number->setAlignment(Qt::AlignCenter);
        number->setStyleSheet(QStringLiteral("color: %1; font-size: 13px; font-weight: 600;").arg(Theme::TextDarkSecondary));

        columnLayout->addWidget(card);
        columnLayout->addWidget(number);
        m_slidesRow->addWidget(column);
    }
}

void BiblePanel::selectSlide(int index)
{
    m_selectedSlide = qBound(0, index, qMax(0, m_slides.size() - 1));
    rebuildSlides();
}

void BiblePanel::runSearch(const QString &text)
{
    QLayoutItem *child;
    while ((child = m_searchResults->takeAt(0)) != nullptr) {
        if (child->widget())
            child->widget()->deleteLater();
        delete child;
    }

    const QList<BibleSearchHit> hits = m_repo.search(m_translation, text, 60);
    for (const BibleSearchHit &hit : hits) {
        QString snippet = hit.text;
        if (snippet.size() > 90)
            snippet = snippet.left(90) + QStringLiteral("…");
        const QString label = tr("%1 %2:%3 — %4").arg(hit.bookName).arg(hit.chapter).arg(hit.verse).arg(snippet);
        auto *row = makePlaceRow(label);
        connect(row, &QPushButton::clicked, this, [this, hit, text]() {
            rememberSearch(text);
            selectReference(hit.bookNum, hit.chapter, hit.verse, hit.verse);
        });
        m_searchResults->insertWidget(m_searchResults->count() - 1, row);
    }
}

void BiblePanel::rememberPlace()
{
    if (!AppSettings::value(AppSettings::BibleKeepHistory).toBool() || m_currentBook == 0)
        return;
    const QString place = QStringLiteral("%1|%2|%3|%4").arg(m_currentBook).arg(m_currentChapter).arg(m_currentFrom).arg(m_currentTo);
    QStringList places = QSettings().value(QStringLiteral("bible/recentPlaces")).toStringList();
    places.removeAll(place);
    places.prepend(place);
    const int limit = qBound(1, AppSettings::value(AppSettings::BibleRecentCount).toInt(), 50);
    QSettings().setValue(QStringLiteral("bible/recentPlaces"), QStringList(places.mid(0, limit)));
}

void BiblePanel::rememberSearch(const QString &text)
{
    const QString query = text.trimmed();
    if (query.size() < 2 || !AppSettings::value(AppSettings::BibleKeepHistory).toBool())
        return;
    QStringList searches = QSettings().value(QStringLiteral("bible/recentSearches")).toStringList();
    searches.removeAll(query);
    searches.prepend(query);
    searches = searches.mid(0, qBound(1, AppSettings::value(AppSettings::BibleRecentCount).toInt(), 50));
    QSettings().setValue(QStringLiteral("bible/recentSearches"), searches);
    m_searchHistory->setStringList(searches);
}

void BiblePanel::showRecentMenu()
{
    QMenu menu(this);
    const int limit = qBound(1, AppSettings::value(AppSettings::BibleRecentCount).toInt(), 50);
    const QStringList places = QSettings().value(QStringLiteral("bible/recentPlaces")).toStringList().mid(0, limit);
    for (const QString &place : places) {
        const QStringList parts = place.split(QLatin1Char('|'));
        if (parts.size() != 4)
            continue;
        const int book = parts.at(0).toInt();
        const int chapter = parts.at(1).toInt();
        const int from = parts.at(2).toInt();
        const int to = parts.at(3).toInt();
        const QString label = from == to ? tr("%1 %2:%3").arg(bookName(book)).arg(chapter).arg(from)
                                         : tr("%1 %2:%3–%4").arg(bookName(book)).arg(chapter).arg(from).arg(to);
        menu.addAction(label, this, [this, book, chapter, from, to]() { selectReference(book, chapter, from, to); });
    }
    if (menu.isEmpty())
        menu.addAction(tr("Пока пусто"))->setEnabled(false);
    menu.exec(m_recentButton->mapToGlobal(QPoint(0, m_recentButton->height() + 4)));
}

void BiblePanel::setSlidesCollapsed(bool collapsed)
{
    m_slidesCollapsed = collapsed;
    m_slidesCollapseButton->setIcon(IconProvider::icon(collapsed ? QStringLiteral("chevron-down") : QStringLiteral("chevron-up"),
                                                        QColor(Theme::TextDarkSecondary), 16));
    m_slidesCollapseButton->setToolTip(collapsed ? tr("Показать слайды") : tr("Скрыть слайды"));

    // Same as SlideStripPanel: drive setFixedHeight() by hand so the strip
    // stays rigid at every step instead of being squeezed by the layout.
    if (!m_slidesCollapseAnimation) {
        m_slidesCollapseAnimation = new QVariantAnimation(this);
        m_slidesCollapseAnimation->setDuration(180);
        m_slidesCollapseAnimation->setEasingCurve(QEasingCurve::InOutQuad);
        connect(m_slidesCollapseAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            m_slidesScroll->setFixedHeight(value.toInt());
        });
    }
    m_slidesCollapseAnimation->stop();
    m_slidesCollapseAnimation->setStartValue(m_slidesScroll->height());
    m_slidesCollapseAnimation->setEndValue(collapsed ? 0 : 148);
    m_slidesCollapseAnimation->start();
}

void BiblePanel::triggerGoLive()
{
    if (m_currentBook != 0)
        emit goLiveRequested(currentItem(), m_selectedSlide);
}

ContentItem BiblePanel::currentItem() const
{
    ContentItem item;
    item.type = ContentType::BibleVerse;
    item.refBook = AppSettings::value(AppSettings::BibleRefFormat).toString() == QLatin1String("short")
        ? BibleRepository::abbreviation(m_currentBook, bookName(m_currentBook))
        : bookName(m_currentBook);
    item.refLocation = m_currentFrom == m_currentTo
        ? QStringLiteral("%1:%2").arg(m_currentChapter).arg(m_currentFrom)
        : QStringLiteral("%1:%2–%3").arg(m_currentChapter).arg(m_currentFrom).arg(m_currentTo);
    item.text = m_slides.join(QStringLiteral("\n\n"));
    return item;
}

#include "BiblePanel.moc"
