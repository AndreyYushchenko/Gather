#include "LibraryListPanel.h"
#include "IconProvider.h"
#include "Theme.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QStyledItemDelegate>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace {
QString ruCount(int n, const QString &one, const QString &few, const QString &many)
{
    const int mod10 = n % 10;
    const int mod100 = n % 100;
    if (mod10 == 1 && mod100 != 11)
        return one;
    if (mod10 >= 2 && mod10 <= 4 && (mod100 < 10 || mod100 >= 20))
        return few;
    return many;
}
}

namespace {

enum RowRole {
    IdRole = Qt::UserRole,
    SelectedRole,
    FavoriteRole,
    NumberRole,
    SubtitleRole,
};

constexpr int RowHeight = 54; // design.pen "Song Row": 54 tall, 2 apart

// Paints one library row (number, title, type, star) straight onto the
// list — no child widgets per row. With a real QFrame + labels + button per
// row, opening Песни (770 songs) took seconds; painting only the visible
// rows makes it instant regardless of library size.
class LibraryRowDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    static QRect starRect(const QRect &row)
    {
        return QRect(row.right() - 12 - 24 + 1, row.center().y() - 12, 24, 24);
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override
    {
        return QSize(option.rect.width(), RowHeight);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const bool selected = index.data(SelectedRole).toBool();
        const bool favorite = index.data(FavoriteRole).toBool();
        const QString number = index.data(NumberRole).toString();
        const QRect row = option.rect;

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        if (selected) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(Theme::AccentBlueBg));
            painter->drawRoundedRect(row, Theme::radius(9), Theme::radius(9));
        }

        QFont bold = option.font;
        bold.setPixelSize(14);
        bold.setWeight(QFont::DemiBold);
        const QFontMetrics boldMetrics(bold);
        const QColor accent(Theme::AccentBlue);
        int left = row.left() + 12;
        const int titleTop = row.top() + 10;

        if (!number.isEmpty()) {
            const QString numberText = number + QStringLiteral(".");
            painter->setFont(bold);
            painter->setPen(selected ? accent : QColor(Theme::TextDarkSecondary));
            painter->drawText(QRect(left, row.top(), boldMetrics.horizontalAdvance(numberText) + 2, row.height()),
                              Qt::AlignLeft | Qt::AlignVCenter, numberText);
            left += boldMetrics.horizontalAdvance(numberText) + 10;
        }

        const int textRight = starRect(row).left() - 10;
        painter->setFont(bold);
        painter->setPen(selected ? accent : QColor(Theme::TextDarkPrimary));
        painter->drawText(QRect(left, titleTop, textRight - left, boldMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                          boldMetrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, textRight - left));

        QFont small = option.font;
        small.setPixelSize(12);
        painter->setFont(small);
        painter->setPen(QColor(Theme::TextDarkSecondary));
        painter->drawText(QRect(left, titleTop + boldMetrics.height() + 2, textRight - left, QFontMetrics(small).height()),
                          Qt::AlignLeft | Qt::AlignVCenter, index.data(SubtitleRole).toString());

        const QRect star = starRect(row);
        painter->drawPixmap(star.center().x() - 8, star.center().y() - 8,
                            IconProvider::pixmap(QStringLiteral("star"), selected ? accent : QColor(Theme::TextDarkSecondary), 17, favorite));
        painter->restore();
    }
};

} // namespace

LibraryListPanel::LibraryListPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("LibraryListPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    // 360 is this panel's *maximum* width (its design.pen size), not fixed —
    // see DisplayControlPanel's matching comment for why.
    setMinimumWidth(280);
    setMaximumWidth(360);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 18, 16, 18);
    layout->setSpacing(14);

    auto *searchRow = new QHBoxLayout;
    searchRow->setSpacing(10);
    m_search = new QLineEdit;
    m_search->setObjectName(QStringLiteral("SearchField")); // Ctrl+F (Горячие клавиши → Поиск)
    m_search->setPlaceholderText(tr("Поиск по названию или тексту..."));
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
    m_sortButton->setLayoutDirection(Qt::RightToLeft); // icon after text
    sortRow->addWidget(m_sortButton);
    sortRow->addStretch();
    m_countLabel = new QLabel;
    m_countLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 12.5px; font-weight: 500;").arg(Theme::TextDarkSecondary));
    sortRow->addWidget(m_countLabel);
    layout->addLayout(sortRow);

    auto *sortMenu = new QMenu(m_sortButton);
    QAction *newestAction = sortMenu->addAction(tr("Сначала новые"));
    QAction *alphaAction = sortMenu->addAction(tr("По алфавиту (А-Я)"));
    QAction *numberAction = sortMenu->addAction(tr("По номеру (1-770...)"));
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
    connect(numberAction, &QAction::triggered, this, [this]() {
        m_sortOrder = SortOrder::Number;
        updateSortLabel();
        emit filtersChanged();
    });
    m_sortButton->setMenu(sortMenu);
    updateSortLabel();

    m_list = new QListWidget;
    m_list->setObjectName(QStringLiteral("LibraryList"));
    m_list->setSpacing(2);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setSelectionMode(QAbstractItemView::NoSelection);
    m_list->setFocusPolicy(Qt::NoFocus);
    // Qt's default (ScrollPerItem) jumps a whole row per wheel notch, so
    // row widgets (title, star, ...) appear to "teleport" between fixed
    // positions instead of gliding together — ScrollPerPixel moves the
    // whole row, star included, in the same smooth motion as everything else.
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    // This list only ever scrolls vertically — rows/tiles wrap, they don't
    // run off sideways — so a horizontal scrollbar should never appear.
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setUniformItemSizes(true);
    m_list->setItemDelegate(new LibraryRowDelegate(m_list));
    m_list->viewport()->setCursor(Qt::PointingHandCursor);
    m_list->viewport()->installEventFilter(this);
    layout->addWidget(m_list, 1);

    m_searchDebounce = new QTimer(this);
    m_searchDebounce->setSingleShot(true);
    m_searchDebounce->setInterval(250);
    connect(m_searchDebounce, &QTimer::timeout, this, &LibraryListPanel::filtersChanged);
    // Rebuilding the row list on every keystroke (each row is a real
    // widget, and the library can hold hundreds of them) made typing feel
    // laggy — wait for a short pause instead of filtering on every key.
    connect(m_search, &QLineEdit::textChanged, this, [this]() { m_searchDebounce->start(); });
    connect(m_filterButton, &QPushButton::toggled, this, [this](bool on) {
        m_favoritesOnly = on;
        emit filtersChanged();
    });

    setStyleSheet(QStringLiteral(R"(
        QWidget#LibraryListPanel { background: %1; border: 1px solid %2; border-radius: 14px; }
        QLineEdit#SearchField {
            background: #ffffff; border: 1px solid %2; border-radius: 9px;
            padding: 9px 12px; font-size: 13px; color: %3;
        }
        QPushButton#FilterButton {
            background: #ffffff; border: 1px solid %2; border-radius: 9px;
        }
        QPushButton#FilterButton:checked { border-color: #f5a623; }
        QPushButton#SortButton {
            background: transparent; border: none; padding: 0;
            font-size: 12.5px; font-weight: 500; color: %4;
        }
        QPushButton#SortButton::menu-indicator { image: none; width: 0; }
        QListWidget#LibraryList { background: transparent; border: none; }
        QListWidget#LibraryList { outline: none; }
        QListWidget#LibraryList::item { background: transparent; border: none; outline: none; }
        QListWidget#LibraryList::item:hover, QListWidget#LibraryList::item:selected, QListWidget#LibraryList::item:focus { background: transparent; outline: none; }
    )").arg(Theme::BgPanel, Theme::BorderLight, Theme::TextDarkPrimary, Theme::TextDarkSecondary));
}

void LibraryListPanel::updateSortLabel()
{
    QString label;
    switch (m_sortOrder) {
    case SortOrder::Alphabetical: label = tr("Сортировка: по алфавиту (А-Я)"); break;
    case SortOrder::Number: label = tr("Сортировка: по номеру"); break;
    case SortOrder::DateAddedDesc: default: label = tr("Сортировка: сначала новые"); break;
    }
    m_sortButton->setText(label);
}

void LibraryListPanel::setCategory(ContentType type)
{
    m_category = type;
}

void LibraryListPanel::setItems(const QList<ContentItem> &items)
{
    m_items = items;

    const std::optional<int> previousSelection = selectedItemId();

    m_list->setUpdatesEnabled(false);
    m_list->clear();
    for (const ContentItem &item : m_items) {
        const bool hasNumber = item.type == ContentType::Song && !item.refLocation.isEmpty();
        auto *listItem = new QListWidgetItem(hasNumber ? item.title : item.displayTitle());
        listItem->setData(IdRole, item.id);
        listItem->setData(FavoriteRole, item.favorite);
        listItem->setData(NumberRole, hasNumber ? item.refLocation : QString());
        listItem->setData(SubtitleRole, contentTypeDisplayName(item.type));
        m_list->addItem(listItem);
    }
    m_list->setUpdatesEnabled(true);

    m_countLabel->setText(tr("%1 %2").arg(m_items.size()).arg(ruCount(m_items.size(), tr("запись"), tr("записи"), tr("записей"))));

    if (previousSelection.has_value())
        selectItemById(*previousSelection);
    else
        emit itemSelected(std::nullopt);
}

void LibraryListPanel::setItemFavorite(int id, bool favorite)
{
    for (ContentItem &item : m_items) {
        if (item.id == id) {
            item.favorite = favorite;
            break;
        }
    }
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem *listItem = m_list->item(i);
        if (listItem->data(IdRole).toInt() == id) {
            listItem->setData(FavoriteRole, favorite);
            break;
        }
    }
}

std::optional<int> LibraryListPanel::selectedItemId() const
{
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(SelectedRole).toBool())
            return m_list->item(i)->data(IdRole).toInt();
    }
    return std::nullopt;
}

void LibraryListPanel::selectItemById(int id)
{
    bool found = false;
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem *listItem = m_list->item(i);
        const bool matches = listItem->data(IdRole).toInt() == id;
        if (listItem->data(SelectedRole).toBool() != matches)
            listItem->setData(SelectedRole, matches);
        if (matches) {
            found = true;
            m_list->scrollToItem(listItem);
        }
    }
    emit itemSelected(found ? std::optional<int>(id) : std::nullopt);
}

bool LibraryListPanel::eventFilter(QObject *watched, QEvent *event)
{
    // Row clicks: the star toggles the favorite, anywhere else selects.
    if (watched == m_list->viewport() && event->type() == QEvent::MouseButtonPress) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            if (QListWidgetItem *item = m_list->itemAt(mouse->position().toPoint())) {
                const int id = item->data(IdRole).toInt();
                if (LibraryRowDelegate::starRect(m_list->visualItemRect(item)).contains(mouse->position().toPoint()))
                    emit favoriteToggled(id);
                else
                    selectItemById(id);
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

QString LibraryListPanel::searchText() const
{
    return m_search->text();
}

SortOrder LibraryListPanel::sortOrder() const
{
    return m_sortOrder;
}

