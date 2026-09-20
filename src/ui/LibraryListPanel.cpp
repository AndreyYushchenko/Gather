#include "LibraryListPanel.h"
#include "IconProvider.h"
#include "Theme.h"

#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPixmap>
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

// Not in an anonymous namespace: moc cannot generate metaobject code for
// Q_OBJECT classes declared inside one.
class LibraryRowWidget : public QFrame {
    Q_OBJECT
public:
    explicit LibraryRowWidget(const ContentItem &item, bool gridMode, QWidget *parent = nullptr)
        : QFrame(parent)
        , m_id(item.id)
        , m_favorite(item.favorite)
        , m_gridMode(gridMode)
    {
        setFrameShape(QFrame::NoFrame);

        if (gridMode) {
            setFixedSize(158, 117);
            auto *layout = new QVBoxLayout(this);
            layout->setContentsMargins(6, 6, 6, 6);
            layout->setSpacing(6);

            auto *thumb = new QLabel;
            thumb->setFixedSize(146, 82);
            thumb->setAlignment(Qt::AlignCenter);
            thumb->setStyleSheet(QStringLiteral("background: %1; border-radius: 8px;").arg(Theme::BgWhite));
            if (!item.imagePath.isEmpty() && QFileInfo::exists(item.imagePath)) {
                const QPixmap source(item.imagePath);
                thumb->setPixmap(source.scaled(146, 82, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
            } else {
                thumb->setPixmap(IconProvider::pixmap(QStringLiteral("image"), QColor(Theme::TextDarkSecondary), 22));
            }
            layout->addWidget(thumb);

            m_titleLabel = new QLabel(item.displayTitle());
            m_titleLabel->setStyleSheet(QStringLiteral("font-weight: 600; font-size: 12.5px;"));
            m_titleLabel->setAlignment(Qt::AlignLeft);
            layout->addWidget(m_titleLabel);
            setSelected(false);
            return;
        }

        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(12, 10, 12, 10);
        layout->setSpacing(10);

        auto *textCol = new QVBoxLayout;
        textCol->setSpacing(2);
        m_titleLabel = new QLabel(item.displayTitle());
        m_titleLabel->setStyleSheet(QStringLiteral("font-weight: 600; font-size: 14px;"));
        auto *subtitle = new QLabel(contentTypeDisplayName(item.type));
        subtitle->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(Theme::TextDarkSecondary));
        textCol->addWidget(m_titleLabel);
        textCol->addWidget(subtitle);
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
        // Scoped to LibraryRowWidget: an unscoped rule would cascade its
        // border-radius down onto the child QLabels too.
        setStyleSheet(selected
                          ? QStringLiteral("LibraryRowWidget { background: %1; border-radius: 9px; }").arg(Theme::AccentBlueBg)
                          : QStringLiteral("LibraryRowWidget { background: transparent; border-radius: 9px; }"));
        m_titleLabel->setStyleSheet(QStringLiteral("font-weight: 600; font-size: %1px; color: %2;")
                                         .arg(m_gridMode ? QStringLiteral("12.5") : QStringLiteral("14"),
                                              selected ? Theme::AccentBlue : Theme::TextDarkPrimary));
        if (m_starButton) {
            const QColor starColor(selected ? Theme::AccentBlue : Theme::TextDarkSecondary);
            m_starButton->setIcon(IconProvider::icon(QStringLiteral("star"), starColor, 17, m_favorite));
        }
    }

signals:
    void clicked(int id);
    void starClicked(int id);

protected:
    void mousePressEvent(QMouseEvent *) override { emit clicked(m_id); }

private:
    int m_id;
    bool m_favorite;
    bool m_gridMode;
    bool m_selected = false;
    bool m_selectionApplied = false;
    QLabel *m_titleLabel = nullptr;
    QPushButton *m_starButton = nullptr;
};

LibraryListPanel::LibraryListPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("LibraryListPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(360);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 18, 16, 18);
    layout->setSpacing(14);

    auto *searchRow = new QHBoxLayout;
    searchRow->setSpacing(10);
    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Поиск по названию или тексту..."));
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
        QWidget#LibraryListPanel { background: %1; border-right: 1px solid %2; }
        QLineEdit#SearchBox {
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
        QListWidget#LibraryList::item { border: none; }
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

    const bool gridMode = m_category == ContentType::Photo;
    if (gridMode) {
        m_list->setViewMode(QListView::IconMode);
        m_list->setFlow(QListView::LeftToRight);
        m_list->setWrapping(true);
        m_list->setResizeMode(QListView::Adjust);
        m_list->setGridSize(QSize(164, 123));
        m_list->setMovement(QListView::Static);
    } else {
        m_list->setViewMode(QListView::ListMode);
        m_list->setFlow(QListView::TopToBottom);
        m_list->setWrapping(false);
        m_list->setGridSize(QSize());
    }

    // Rebuilding can mean hundreds of real widgets; without this the list
    // repaints/relayouts after every single addItem(), which is the other
    // big contributor to the lag (on top of the per-keystroke rebuilds the
    // search debounce above avoids).
    m_list->setUpdatesEnabled(false);
    m_list->clear();
    for (const ContentItem &item : m_items) {
        auto *row = new LibraryRowWidget(item, gridMode);
        connect(row, &LibraryRowWidget::clicked, this, [this](int id) { selectItemById(id); });
        connect(row, &LibraryRowWidget::starClicked, this, &LibraryListPanel::favoriteToggled);

        auto *listItem = new QListWidgetItem;
        listItem->setSizeHint(row->sizeHint());
        listItem->setData(Qt::UserRole, item.id);
        m_list->addItem(listItem);
        m_list->setItemWidget(listItem, row);
    }
    m_list->setUpdatesEnabled(true);

    m_countLabel->setText(tr("%1 %2").arg(m_items.size()).arg(ruCount(m_items.size(), tr("запись"), tr("записи"), tr("записей"))));

    if (previousSelection.has_value())
        selectItemById(*previousSelection);
    else
        emit itemSelected(std::nullopt);
}

std::optional<int> LibraryListPanel::selectedItemId() const
{
    for (int i = 0; i < m_list->count(); ++i) {
        auto *row = qobject_cast<LibraryRowWidget *>(m_list->itemWidget(m_list->item(i)));
        if (row && m_list->item(i)->data(Qt::UserRole + 1).toBool())
            return m_list->item(i)->data(Qt::UserRole).toInt();
    }
    return std::nullopt;
}

void LibraryListPanel::selectItemById(int id)
{
    bool found = false;
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem *listItem = m_list->item(i);
        auto *row = qobject_cast<LibraryRowWidget *>(m_list->itemWidget(listItem));
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

QString LibraryListPanel::searchText() const
{
    return m_search->text();
}

SortOrder LibraryListPanel::sortOrder() const
{
    return m_sortOrder;
}

#include "LibraryListPanel.moc"
