#include "BiblePanel.h"
#include "IconProvider.h"
#include "Theme.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

constexpr int TabByRef = 0;
constexpr int TabByText = 1;

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

// A clickable slide thumbnail, mirroring DetailPanel's SlideCard.
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
        setStyleSheet(selected
            ? QStringLiteral("SlideCard { background: #1c2a4a; border: 2px solid %1; border-radius: 10px; }").arg(Theme::AccentBlue)
            : QStringLiteral("SlideCard { background: #212b3f; border: 2px solid transparent; border-radius: 10px; }"));
    }

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *) override { emit clicked(); }

private:
    QLabel *m_text = nullptr;
};

// A left-aligned clickable row used for recent/popular places and search hits.
QPushButton *makePlaceRow(const QString &text)
{
    auto *button = new QPushButton(text);
    button->setObjectName(QStringLiteral("PlaceRow"));
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

struct PopularPlace { const char *book; int chapter; int from; int to; };
const PopularPlace kPopularPlaces[] = {
    {"Псалми", 22, 1, 6},
    {"Псалми", 1, 1, 6},
    {"Від Матвія", 6, 9, 13},
    {"Ісая", 40, 28, 31},
    {"До Євреїв", 11, 1, 3},
};

} // namespace

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

    // ---- Header ----
    auto *headerRow = new QHBoxLayout;
    headerRow->setSpacing(10);
    auto *title = new QLabel(tr("Библия"));
    title->setStyleSheet(QStringLiteral("font-size: 24px; font-weight: 700; color: %1;").arg(Theme::TextDarkPrimary));
    headerRow->addWidget(title);
    headerRow->addStretch();

    m_favoriteButton = makeOutlineButton(QStringLiteral("star"), tr("Добавить в избранное"));
    connect(m_favoriteButton, &QPushButton::clicked, this, [this]() {
        if (m_currentBook == 0)
            return;
        ContentItem item = currentItem();
        item.favorite = true;
        emit saveToLibraryRequested(item);
    });
    headerRow->addWidget(m_favoriteButton);

    auto *moreButton = makeOutlineIconButton(QStringLiteral("ellipsis-vertical"), tr("Ещё"));
    headerRow->addWidget(moreButton);
    root->addLayout(headerRow);

    // ---- Tabs ----
    auto *tabsRow = new QHBoxLayout;
    tabsRow->setSpacing(28);
    m_tabByRef = new QPushButton(tr("По ссылке"));
    m_tabByRef->setIcon(IconProvider::icon(QStringLiteral("link"), QColor(Theme::AccentBlue), 16));
    m_tabByText = new QPushButton(tr("По тексту"));
    m_tabByText->setIcon(IconProvider::icon(QStringLiteral("search"), QColor(Theme::TextDarkSecondary), 16));
    for (QPushButton *tab : {m_tabByRef, m_tabByText}) {
        tab->setCheckable(true);
        tab->setObjectName(QStringLiteral("TabButton"));
        tab->setCursor(Qt::PointingHandCursor);
        tab->setIconSize(QSize(16, 16));
        tabsRow->addWidget(tab);
    }
    tabsRow->addStretch();
    root->addLayout(tabsRow);

    auto *tabsDivider = new QWidget;
    tabsDivider->setFixedHeight(1);
    tabsDivider->setStyleSheet(QStringLiteral("background: %1;").arg(Theme::BorderLight));
    root->addWidget(tabsDivider);

    auto *tabGroup = new QButtonGroup(this);
    tabGroup->addButton(m_tabByRef, TabByRef);
    tabGroup->addButton(m_tabByText, TabByText);
    m_tabByRef->setChecked(true);

    m_tabStack = new QStackedWidget;
    root->addWidget(m_tabStack, 1);
    connect(tabGroup, &QButtonGroup::idClicked, m_tabStack, &QStackedWidget::setCurrentIndex);

    // ---- Page: by reference ----
    auto *refPage = new QWidget;
    auto *refLayout = new QVBoxLayout(refPage);
    refLayout->setContentsMargins(0, 0, 0, 0);
    refLayout->setSpacing(18);

    auto *pickerRow = new QHBoxLayout;
    pickerRow->setSpacing(14);

    auto makeField = [&](const QString &label, int width) -> QComboBox * {
        auto *wrap = new QWidget;
        wrap->setFixedWidth(width);
        auto *wrapLayout = new QVBoxLayout(wrap);
        wrapLayout->setContentsMargins(0, 0, 0, 0);
        wrapLayout->setSpacing(6);
        auto *labelWidget = new QLabel(label);
        labelWidget->setStyleSheet(QStringLiteral("color: %1; font-size: 12.5px;").arg(Theme::TextDarkSecondary));
        auto *combo = new QComboBox;
        combo->setObjectName(QStringLiteral("FieldBox"));
        combo->setFixedHeight(36);
        wrapLayout->addWidget(labelWidget);
        wrapLayout->addWidget(combo);
        pickerRow->addWidget(wrap);
        return combo;
    };

    m_translationBox = makeField(tr("Перевод"), 270);
    m_bookBox = makeField(tr("Книга"), 270);
    m_chapterBox = makeField(tr("Глава"), 130);
    m_fromBox = makeField(tr("С"), 110);
    m_toBox = makeField(tr("По"), 110);
    pickerRow->addStretch();
    refLayout->addLayout(pickerRow);

    connect(m_bookBox, &QComboBox::currentIndexChanged, this, &BiblePanel::onBookChanged);
    connect(m_chapterBox, &QComboBox::currentIndexChanged, this, &BiblePanel::onChapterChanged);
    connect(m_fromBox, &QComboBox::currentIndexChanged, this, &BiblePanel::onRangeChanged);
    connect(m_toBox, &QComboBox::currentIndexChanged, this, &BiblePanel::onRangeChanged);

    auto *bodyRow = new QHBoxLayout;
    bodyRow->setSpacing(24);

    // Places column
    auto *placesColumn = new QWidget;
    placesColumn->setFixedWidth(320);
    auto *placesLayout = new QVBoxLayout(placesColumn);
    placesLayout->setContentsMargins(0, 0, 0, 0);
    placesLayout->setSpacing(20);

    auto makeSection = [&](const QString &iconName, const QString &title) -> QVBoxLayout * {
        auto *section = new QVBoxLayout;
        section->setSpacing(8);
        auto *head = new QHBoxLayout;
        head->setSpacing(8);
        auto *icon = new QLabel;
        icon->setPixmap(IconProvider::pixmap(iconName, QColor(Theme::TextDarkSecondary), 15));
        auto *label = new QLabel(title);
        label->setStyleSheet(QStringLiteral("color: %1; font-size: 13.5px; font-weight: 600;").arg(Theme::TextDarkSecondary));
        head->addWidget(icon);
        head->addWidget(label);
        head->addStretch();
        section->addLayout(head);
        placesLayout->addLayout(section);
        return section;
    };

    auto *recentSection = makeSection(QStringLiteral("history"), tr("Недавние места"));
    auto *recentList = new QVBoxLayout;
    recentList->setSpacing(2);
    recentSection->addLayout(recentList);
    m_recentList = recentList;

    auto *popularSection = makeSection(QStringLiteral("flame"), tr("Популярные места"));
    auto *popularList = new QVBoxLayout;
    popularList->setSpacing(2);
    popularSection->addLayout(popularList);
    for (const PopularPlace &place : kPopularPlaces) {
        const QString label = place.from == place.to
            ? tr("%1 %2:%3").arg(QString::fromUtf8(place.book)).arg(place.chapter).arg(place.from)
            : tr("%1 %2:%3–%4").arg(QString::fromUtf8(place.book)).arg(place.chapter).arg(place.from).arg(place.to);
        auto *row = makePlaceRow(label);
        const QString bookNameCopy = QString::fromUtf8(place.book);
        const int chapter = place.chapter, from = place.from, to = place.to;
        connect(row, &QPushButton::clicked, this, [this, bookNameCopy, chapter, from, to]() {
            for (const BibleBook &book : std::as_const(m_books)) {
                if (book.name == bookNameCopy) {
                    selectReference(book.num, chapter, from, to);
                    break;
                }
            }
        });
        popularList->addWidget(row);
    }
    placesLayout->addStretch();
    bodyRow->addWidget(placesColumn);

    // Verse detail column
    auto *verseColumn = new QWidget;
    auto *verseLayout = new QVBoxLayout(verseColumn);
    verseLayout->setContentsMargins(0, 0, 0, 0);
    verseLayout->setSpacing(12);

    auto *verseHead = new QHBoxLayout;
    auto *refTitleCol = new QVBoxLayout;
    refTitleCol->setSpacing(2);
    m_refTitle = new QLabel;
    m_refTitle->setStyleSheet(QStringLiteral("font-size: 19px; font-weight: 700; color: %1;").arg(Theme::TextDarkPrimary));
    m_translationCaption = new QLabel;
    m_translationCaption->setStyleSheet(QStringLiteral("font-size: 12.5px; color: %1;").arg(Theme::TextDarkSecondary));
    refTitleCol->addWidget(m_refTitle);
    refTitleCol->addWidget(m_translationCaption);
    verseHead->addLayout(refTitleCol);
    verseHead->addStretch();
    verseLayout->addLayout(verseHead);

    auto *versesScroll = new QScrollArea;
    versesScroll->setWidgetResizable(true);
    versesScroll->setFrameShape(QFrame::NoFrame);
    versesScroll->setObjectName(QStringLiteral("LyricsBox"));
    auto *versesContainer = new QWidget;
    versesContainer->setStyleSheet(QStringLiteral("background: transparent;"));
    m_versesLayout = new QVBoxLayout(versesContainer);
    m_versesLayout->setContentsMargins(4, 4, 4, 4);
    m_versesLayout->setSpacing(10);
    m_versesLayout->addStretch();
    versesScroll->setWidget(versesContainer);
    verseLayout->addWidget(versesScroll, 1);

    auto *slidesHeadRow = new QHBoxLayout;
    m_slidesHeading = new QLabel;
    m_slidesHeading->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 700; color: %1;").arg(Theme::TextDarkPrimary));
    slidesHeadRow->addWidget(m_slidesHeading);
    slidesHeadRow->addStretch();
    m_splitCheckBox = new QCheckBox(tr("Разделить по стихам"));
    m_splitCheckBox->setChecked(true);
    m_splitCheckBox->setStyleSheet(QStringLiteral("font-size: 13px; color: %1;").arg(Theme::TextDarkPrimary));
    connect(m_splitCheckBox, &QCheckBox::toggled, this, [this](bool) { refreshVerseView(); });
    slidesHeadRow->addWidget(m_splitCheckBox);
    verseLayout->addLayout(slidesHeadRow);

    auto *slidesScroll = new QScrollArea;
    slidesScroll->setWidgetResizable(true);
    slidesScroll->setFrameShape(QFrame::NoFrame);
    slidesScroll->setFixedHeight(140);
    slidesScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    slidesScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *slidesContainer = new QWidget;
    m_slidesRow = new QHBoxLayout(slidesContainer);
    m_slidesRow->setContentsMargins(2, 2, 2, 2);
    m_slidesRow->setSpacing(14);
    slidesScroll->setWidget(slidesContainer);
    verseLayout->addWidget(slidesScroll);

    auto *actionsRow = new QHBoxLayout;
    actionsRow->setSpacing(10);
    auto *addSlideButton = makeOutlineButton(QStringLiteral("plus"), tr("Добавить слайд"));
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
    actionsRow->addWidget(addSlideButton);
    actionsRow->addWidget(copySlideButton);
    actionsRow->addWidget(deleteSlideButton);
    actionsRow->addStretch();

    auto *previewButton = makeOutlineButton(QStringLiteral("eye"), tr("Предпросмотр"));
    connect(previewButton, &QPushButton::clicked, this, [this]() {
        if (m_currentBook != 0)
            emit previewRequested(currentItem(), m_selectedSlide);
    });
    actionsRow->addWidget(previewButton);

    auto *onScreenButton = new QPushButton(tr("На экран"));
    onScreenButton->setObjectName(QStringLiteral("PrimaryButton"));
    onScreenButton->setCursor(Qt::PointingHandCursor);
    onScreenButton->setIcon(IconProvider::icon(QStringLiteral("monitor"), QColor(Theme::TextLightPrimary), 15));
    onScreenButton->setIconSize(QSize(15, 15));
    connect(onScreenButton, &QPushButton::clicked, this, &BiblePanel::triggerGoLive);
    actionsRow->addWidget(onScreenButton);

    verseLayout->addLayout(actionsRow);
    bodyRow->addWidget(verseColumn, 1);
    refLayout->addLayout(bodyRow, 1);

    m_tabStack->insertWidget(TabByRef, refPage);

    // ---- Page: by text ----
    auto *textPage = new QWidget;
    auto *textLayout = new QVBoxLayout(textPage);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(14);

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
    m_searchBox->setPlaceholderText(tr("Поиск по тексту или ссылке (например: Ин 3:16)"));
    m_searchBox->setFrame(false);
    searchBoxLayout->addWidget(searchIcon);
    searchBoxLayout->addWidget(m_searchBox, 1);
    connect(m_searchBox, &QLineEdit::textChanged, this, &BiblePanel::runSearch);
    searchRow->addWidget(searchBoxWrap, 1);
    textLayout->addLayout(searchRow);

    auto *resultsScroll = new QScrollArea;
    resultsScroll->setWidgetResizable(true);
    resultsScroll->setFrameShape(QFrame::NoFrame);
    auto *resultsContainer = new QWidget;
    m_searchResults = new QVBoxLayout(resultsContainer);
    m_searchResults->setContentsMargins(0, 0, 0, 0);
    m_searchResults->setSpacing(2);
    m_searchResults->addStretch();
    resultsScroll->setWidget(resultsContainer);
    textLayout->addWidget(resultsScroll, 1);

    m_tabStack->insertWidget(TabByText, textPage);

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
        QPushButton#PrimaryButton:hover { background: #255ed1; }
        QPushButton#TabButton {
            background: transparent; border: none; border-bottom: 2px solid transparent;
            padding: 0 0 10px 0; font-size: 14px; font-weight: 500; color: %4;
        }
        QPushButton#TabButton:checked { color: %3; font-weight: 600; border-bottom: 2px solid %3; }
        QScrollArea#LyricsBox { border: 1px solid %2; border-radius: 12px; }
        QComboBox#FieldBox {
            border: 1px solid %2; border-radius: 9px; padding: 6px 10px;
            font-size: 13.5px; color: %1; background: #ffffff;
        }
        QFrame#SearchBox { background: #ffffff; border: 1px solid %2; border-radius: 9px; min-height: 38px; }
        QLineEdit { border: none; background: transparent; font-size: 13px; color: %1; }
        QPushButton#PlaceRow {
            text-align: left; border: none; background: transparent; border-radius: 8px;
            padding: 8px 6px; font-size: 13.5px; color: %1;
        }
        QPushButton#PlaceRow:hover { background: #f3f4f6; color: %3; }
    )").arg(Theme::TextDarkPrimary, Theme::BorderLight, Theme::AccentBlue, Theme::TextDarkSecondary));

    reload();
}

void BiblePanel::reload()
{
    m_translation = m_repo.defaultTranslation();
    m_translationBox->clear();
    m_translationBox->addItem(m_translation.isEmpty() ? tr("Нет перевода") : m_translation);

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
    m_bookBox->blockSignals(true);
    m_bookBox->clear();
    for (const BibleBook &book : std::as_const(m_books))
        m_bookBox->addItem(book.name);
    m_bookBox->blockSignals(false);
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
    pushRecent(m_currentBook, m_currentChapter, m_currentFrom, m_currentTo);
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
    pushRecent(m_currentBook, m_currentChapter, m_currentFrom, m_currentTo);
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
    pushRecent(m_currentBook, m_currentChapter, m_currentFrom, m_currentTo);

    if (m_tabStack->currentIndex() != TabByRef) {
        m_tabByRef->setChecked(true);
        m_tabStack->setCurrentIndex(TabByRef);
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
        rowLayout->setSpacing(10);
        auto *number = new QLabel(QString::number(verse.verse));
        number->setFixedWidth(20);
        number->setAlignment(Qt::AlignTop | Qt::AlignRight);
        number->setStyleSheet(QStringLiteral("color: %1; font-size: 12.5px; font-weight: 600;").arg(Theme::AccentBlue));
        auto *text = new QLabel(verse.text);
        text->setWordWrap(true);
        text->setStyleSheet(QStringLiteral("color: %1; font-size: 14.5px;").arg(Theme::TextDarkPrimary));
        rowLayout->addWidget(number);
        rowLayout->addWidget(text, 1);
        m_versesLayout->insertWidget(m_versesLayout->count() - 1, row);
    }

    m_slides = verses.isEmpty()
        ? QStringList{QString()}
        : (m_splitCheckBox->isChecked()
               ? [&]() { QStringList s; for (const BibleVerse &v : verses) s << v.text; return s; }()
               : QStringList{[&]() { QStringList parts; for (const BibleVerse &v : verses) parts << v.text; return parts.join(QStringLiteral(" ")); }()});

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
}

void BiblePanel::selectSlide(int index)
{
    m_selectedSlide = qBound(0, index, qMax(0, m_slides.size() - 1));
    rebuildSlides();
}

void BiblePanel::pushRecent(int bookNum, int chapter, int fromVerse, int toVerse)
{
    for (int i = m_recent.size() - 1; i >= 0; --i) {
        const RecentEntry &e = m_recent.at(i);
        if (e.book == bookNum && e.chapter == chapter && e.from == fromVerse && e.to == toVerse)
            m_recent.removeAt(i);
    }
    m_recent.prepend({bookNum, chapter, fromVerse, toVerse});
    while (m_recent.size() > 5)
        m_recent.removeLast();
    rebuildRecentList();
}

void BiblePanel::rebuildRecentList()
{
    QLayoutItem *child;
    while ((child = m_recentList->takeAt(0)) != nullptr) {
        if (child->widget())
            child->widget()->deleteLater();
        delete child;
    }

    for (const RecentEntry &entry : std::as_const(m_recent)) {
        const QString book = bookName(entry.book);
        const QString label = entry.from == entry.to
            ? tr("%1 %2:%3").arg(book).arg(entry.chapter).arg(entry.from)
            : tr("%1 %2:%3–%4").arg(book).arg(entry.chapter).arg(entry.from).arg(entry.to);
        auto *row = makePlaceRow(label);
        connect(row, &QPushButton::clicked, this, [this, entry]() {
            selectReference(entry.book, entry.chapter, entry.from, entry.to);
        });
        m_recentList->addWidget(row);
    }
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
        connect(row, &QPushButton::clicked, this, [this, hit]() {
            selectReference(hit.bookNum, hit.chapter, hit.verse, hit.verse);
        });
        m_searchResults->insertWidget(m_searchResults->count() - 1, row);
    }
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
    item.refBook = bookName(m_currentBook);
    item.refLocation = m_currentFrom == m_currentTo
        ? QStringLiteral("%1:%2").arg(m_currentChapter).arg(m_currentFrom)
        : QStringLiteral("%1:%2–%3").arg(m_currentChapter).arg(m_currentFrom).arg(m_currentTo);
    item.text = m_slides.join(QStringLiteral("\n\n"));
    return item;
}

#include "BiblePanel.moc"
