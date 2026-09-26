#include "QuickListCard.h"
#include "IconProvider.h"
#include "Theme.h"

#include <QAbstractItemModel>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QSet>
#include <QVBoxLayout>

namespace {

// Row content: 10px padding + a 20px action button (the tallest child) +
// 10px padding. Fixed rather than left to sizeHint(), which — read right
// after constructing the row, before it's actually laid out — can report a
// smaller value than what the row paints, undersizing the list and letting
// the last row bleed into whatever sits below it.
constexpr int kRowHeight = 40;
// Past this many rows, the list scrolls internally instead of growing the
// card forever and pushing the footer off the bottom of the window.
constexpr int kRowsAreaHeight = 215;

QPushButton *headerIconButton(const QString &iconName, const QColor &color)
{
    auto *button = new QPushButton;
    button->setFlat(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedSize(16, 16);
    button->setIconSize(QSize(16, 16));
    button->setIcon(IconProvider::icon(iconName, color, 16));
    button->setStyleSheet(QStringLiteral("border: none; background: transparent; padding: 0px;"));
    return button;
}

} // namespace

// Not in an anonymous namespace: moc cannot generate metaobject code for
// Q_OBJECT classes declared inside one.
class QuickListRow : public QFrame {
    Q_OBJECT
public:
    QuickListRow(const ContentItem &item, int rowId, int order, bool current, QWidget *parent = nullptr)
        : QFrame(parent)
        , m_rowId(rowId)
    {
        setFrameShape(QFrame::NoFrame);
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(kRowHeight);

        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(10, 10, 10, 10);
        layout->setSpacing(10);

        m_handleIcon = new QLabel(this);
        layout->addWidget(m_handleIcon);

        m_numberLabel = new QLabel(QStringLiteral("%1.").arg(order), this);
        layout->addWidget(m_numberLabel);

        m_titleLabel = new QLabel(item.displayTitle(), this);
        layout->addWidget(m_titleLabel, 1);

        m_actionButton = new QPushButton(this);
        m_actionButton->setFlat(true);
        m_actionButton->setCursor(Qt::PointingHandCursor);
        m_actionButton->setFixedSize(20, 20);
        m_actionButton->setIconSize(QSize(15, 15));
        m_actionButton->setStyleSheet(QStringLiteral("border: none; background: transparent;"));
        connect(m_actionButton, &QPushButton::clicked, this, [this]() { emit actionClicked(m_rowId); });
        layout->addWidget(m_actionButton);

        setCurrent(current);
    }

    int rowId() const { return m_rowId; }

    void setCurrent(bool current)
    {
        m_current = current;
        setStyleSheet(current
            ? QStringLiteral("QuickListRow { background: %1; border-radius: 9px; }").arg(Theme::AccentBlue)
            : QStringLiteral("QuickListRow { background: transparent; border-radius: 9px; }"));

        const QColor white(Qt::white);
        const QColor dimWhite(255, 255, 255, 160);
        const QString secondary = Theme::TextLightSecondary;
        const QString primary = Theme::TextLightPrimary;

        m_handleIcon->setPixmap(IconProvider::pixmap(QStringLiteral("grip-vertical"),
                                                        current ? dimWhite : QColor(secondary), 14));
        m_numberLabel->setStyleSheet(QStringLiteral("background: transparent; font-size: 13.5px; font-weight: 600; color: %1;")
                                          .arg(current ? QStringLiteral("#ffffff") : secondary));
        m_titleLabel->setStyleSheet(QStringLiteral("background: transparent; font-size: 13.5px; font-weight: %1; color: %2;")
                                         .arg(current ? QStringLiteral("700") : QStringLiteral("500"),
                                              current ? QStringLiteral("#ffffff") : primary));
        m_actionButton->setIcon(IconProvider::icon(current ? QStringLiteral("play") : QStringLiteral("x"),
                                                      current ? white : QColor(secondary), 15));
    }

signals:
    void clicked(int rowId);
    void actionClicked(int rowId);

protected:
    void mousePressEvent(QMouseEvent *) override { emit clicked(m_rowId); }

private:
    int m_rowId;
    bool m_current = false;
    QLabel *m_handleIcon = nullptr;
    QLabel *m_numberLabel = nullptr;
    QLabel *m_titleLabel = nullptr;
    QPushButton *m_actionButton = nullptr;
};

QuickListCard::QuickListCard(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("QuickListCard"));
    setAttribute(Qt::WA_StyledBackground, true);
    buildUi();
}

void QuickListCard::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(14);

    auto *headerRow = new QHBoxLayout;
    auto *headerLeft = new QHBoxLayout;
    headerLeft->setSpacing(8);
    auto *listIcon = new QLabel;
    listIcon->setPixmap(IconProvider::pixmap(QStringLiteral("list-checks"), QColor(Theme::TextLightPrimary), 16));
    headerLeft->addWidget(listIcon, 0, Qt::AlignVCenter);
    m_titleLabel = new QLabel;
    m_titleLabel->setStyleSheet(QStringLiteral("background: transparent; color: %1; font-weight: 700; font-size: 14.5px;").arg(Theme::TextLightPrimary));
    headerLeft->addWidget(m_titleLabel, 0, Qt::AlignVCenter);
    headerRow->addLayout(headerLeft);
    headerRow->addStretch();

    auto *headerRight = new QHBoxLayout;
    headerRight->setSpacing(12);
    m_collapseButton = headerIconButton(QStringLiteral("chevron-up"), QColor(Theme::TextLightSecondary));
    m_collapseButton->setToolTip(tr("Свернуть список"));
    connect(m_collapseButton, &QPushButton::clicked, this, [this]() { setCollapsed(!m_collapsed); });
    headerRight->addWidget(m_collapseButton, 0, Qt::AlignVCenter);

    auto *clearButton = headerIconButton(QStringLiteral("trash-2"), QColor(Theme::TextLightSecondary));
    clearButton->setToolTip(tr("Очистить список"));
    connect(clearButton, &QPushButton::clicked, this, [this]() {
        if (m_entries.isEmpty())
            return;
        const auto reply = QMessageBox::question(this, tr("Очистить список"), tr("Очистить весь быстрый список?"));
        if (reply != QMessageBox::Yes)
            return;
        m_entries.clear();
        m_currentRowId = -1;
        rebuildList();
        updateTitle();
    });
    headerRight->addWidget(clearButton, 0, Qt::AlignVCenter);
    headerRow->addLayout(headerRight);
    layout->addLayout(headerRow);

    m_body = new QWidget;
    auto *bodyLayout = new QVBoxLayout(m_body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(14);

    m_list = new QListWidget;
    m_list->setObjectName(QStringLiteral("QuickList"));
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setSpacing(2);
    m_list->setSelectionMode(QAbstractItemView::NoSelection);
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    bodyLayout->addWidget(m_list);
    connect(m_list->model(), &QAbstractItemModel::rowsMoved, this, [this]() {
        QList<Entry> reordered;
        QSet<int> seenIds;
        for (int i = 0; i < m_list->count(); ++i) {
            if (auto *row = qobject_cast<QuickListRow *>(m_list->itemWidget(m_list->item(i)))) {
                for (const Entry &entry : std::as_const(m_entries)) {
                    if (entry.rowId == row->rowId()) {
                        reordered << entry;
                        seenIds.insert(entry.rowId);
                        break;
                    }
                }
            }
        }
        // Never drop an entry just because a row widget couldn't be matched
        // up mid-drag — anything missed above is appended, not lost.
        for (const Entry &entry : std::as_const(m_entries)) {
            if (!seenIds.contains(entry.rowId))
                reordered << entry;
        }
        m_entries = reordered;
        rebuildList();
        updateTitle();
    });

    auto *addButton = new QPushButton;
    addButton->setObjectName(QStringLiteral("QuickListAddButton"));
    addButton->setCursor(Qt::PointingHandCursor);
    addButton->setIcon(IconProvider::icon(QStringLiteral("plus"), QColor(Theme::TextLightPrimary), 15));
    addButton->setIconSize(QSize(15, 15));
    addButton->setText(tr("Добавить текущую песню"));
    connect(addButton, &QPushButton::clicked, this, &QuickListCard::addCurrentSongRequested);
    bodyLayout->addWidget(addButton);

    layout->addWidget(m_body);

    setStyleSheet(QStringLiteral(R"(
        QFrame#QuickListCard { background: %1; border: 2px solid %2; border-radius: 14px; }
        QListWidget#QuickList { background: transparent; border: none; }
        QListWidget#QuickList { outline: none; }
        QListWidget#QuickList::item { background: transparent; border: none; outline: none; }
        QListWidget#QuickList::item:hover, QListWidget#QuickList::item:selected, QListWidget#QuickList::item:focus { background: transparent; outline: none; }
        QPushButton#QuickListAddButton {
            background: %1; border: 1px solid %3; border-radius: 9px;
            padding: 11px 14px; color: %4; font-weight: 600; font-size: 13.5px;
        }
        QPushButton#QuickListAddButton:hover { background: %5; }
    )").arg(Theme::BgDark2, Theme::AccentBlue, Theme::BorderDark, Theme::TextLightPrimary, Theme::BgDark3));

    updateTitle();
    rebuildList();
}

void QuickListCard::addItem(const ContentItem &item)
{
    for (const Entry &entry : std::as_const(m_entries)) {
        if (entry.item.id == item.id) {
            QMessageBox::information(this, tr("Быстрый список"),
                                      tr("«%1» уже в быстром списке.").arg(item.displayTitle()));
            return;
        }
    }
    m_entries << Entry{m_nextRowId++, item};
    rebuildList();
    updateTitle();
}

void QuickListCard::rebuildList()
{
    m_list->setUpdatesEnabled(false);
    // QListWidget::clear() does not delete widgets set via setItemWidget() —
    // without detaching+deleting them first, they leak and keep painting at
    // their old position, overlapping whatever ends up there next.
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem *oldItem = m_list->item(i);
        QWidget *oldWidget = m_list->itemWidget(oldItem);
        m_list->removeItemWidget(oldItem);
        delete oldWidget;
    }
    m_list->clear();
    int order = 1;
    for (const Entry &entry : std::as_const(m_entries)) {
        auto *row = new QuickListRow(entry.item, entry.rowId, order++, entry.rowId == m_currentRowId);
        connect(row, &QuickListRow::clicked, this, [this](int rowId) {
            setCurrentRow(rowId);
            for (const Entry &e : std::as_const(m_entries)) {
                if (e.rowId == rowId) {
                    emit goLiveRequested(e.item, 0);
                    break;
                }
            }
        });
        connect(row, &QuickListRow::actionClicked, this, [this](int rowId) {
            if (rowId == m_currentRowId) {
                for (const Entry &e : std::as_const(m_entries)) {
                    if (e.rowId == rowId) {
                        emit goLiveRequested(e.item, 0);
                        break;
                    }
                }
                return;
            }
            for (int i = 0; i < m_entries.size(); ++i) {
                if (m_entries[i].rowId == rowId) {
                    m_entries.removeAt(i);
                    break;
                }
            }
            rebuildList();
            updateTitle();
        });

        auto *listItem = new QListWidgetItem;
        listItem->setSizeHint(QSize(0, kRowHeight));
        listItem->setFlags(listItem->flags() | Qt::ItemIsDragEnabled);
        m_list->addItem(listItem);
        m_list->setItemWidget(listItem, row);
    }
    m_list->doItemsLayout();
    m_list->setUpdatesEnabled(true);
    m_list->viewport()->update();

    // design.pen "Quick List Rows": a fixed 215px area (five rows), so the
    // card keeps its 333px size whether the queue is empty or long — a long
    // queue scrolls inside it.
    m_list->setFixedHeight(kRowsAreaHeight);
}

void QuickListCard::updateTitle()
{
    m_titleLabel->setText(tr("Быстрый список (%1)").arg(m_entries.size()));
}

void QuickListCard::setCollapsed(bool collapsed)
{
    m_collapsed = collapsed;
    m_body->setVisible(!collapsed);
    m_collapseButton->setIcon(IconProvider::icon(collapsed ? QStringLiteral("chevron-down") : QStringLiteral("chevron-up"),
                                                   QColor(Theme::TextLightSecondary), 16));
}

void QuickListCard::setCurrentRow(int rowId)
{
    if (m_currentRowId == rowId)
        return;
    m_currentRowId = rowId;
    for (int i = 0; i < m_list->count(); ++i) {
        if (auto *row = qobject_cast<QuickListRow *>(m_list->itemWidget(m_list->item(i))))
            row->setCurrent(row->rowId() == rowId);
    }
}

#include "QuickListCard.moc"
