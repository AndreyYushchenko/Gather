#include "PlaylistListPanel.h"
#include "IconProvider.h"
#include "Theme.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
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
} // namespace

// Not in an anonymous namespace: moc cannot generate metaobject code for
// Q_OBJECT classes declared inside one.
class PlaylistRowWidget : public QFrame {
    Q_OBJECT
public:
    explicit PlaylistRowWidget(const Playlist &playlist, QWidget *parent = nullptr)
        : QFrame(parent)
        , m_id(playlist.id)
        , m_favorite(playlist.favorite)
    {
        setFrameShape(QFrame::NoFrame);
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(12, 10, 12, 10);
        layout->setSpacing(10);

        auto *textCol = new QVBoxLayout;
        textCol->setSpacing(2);
        m_titleLabel = new QLabel(playlist.name);
        m_titleLabel->setStyleSheet(QStringLiteral("font-weight: 600; font-size: 14px;"));
        auto *subtitle = new QLabel(QObject::tr("%1 %2").arg(playlist.itemCount)
            .arg(ruCount(playlist.itemCount, QObject::tr("элемент"), QObject::tr("элемента"), QObject::tr("элементов"))));
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
        setStyleSheet(selected
                          ? QStringLiteral("PlaylistRowWidget { background: %1; border-radius: 9px; }").arg(Theme::AccentBlueBg)
                          : QStringLiteral("PlaylistRowWidget { background: transparent; border-radius: 9px; }"));
        m_titleLabel->setStyleSheet(QStringLiteral("font-weight: 600; font-size: 14px; color: %1;")
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

PlaylistListPanel::PlaylistListPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("PlaylistListPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(360);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 18, 16, 18);
    layout->setSpacing(14);

    auto *searchRow = new QHBoxLayout;
    searchRow->setSpacing(10);
    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Поиск по плейлистам..."));
    m_search->setObjectName(QStringLiteral("SearchBox"));
    m_search->addAction(IconProvider::icon(QStringLiteral("search"), QColor(Theme::TextDarkSecondary), 16),
                         QLineEdit::LeadingPosition);
    searchRow->addWidget(m_search, 1);

    m_addButton = new QPushButton;
    m_addButton->setObjectName(QStringLiteral("FilterButton"));
    m_addButton->setFixedSize(38, 38);
    m_addButton->setIconSize(QSize(16, 16));
    m_addButton->setIcon(IconProvider::icon(QStringLiteral("plus"), QColor(Theme::TextDarkSecondary), 16));
    m_addButton->setToolTip(tr("Новый плейлист"));
    m_addButton->setCursor(Qt::PointingHandCursor);
    connect(m_addButton, &QPushButton::clicked, this, &PlaylistListPanel::createRequested);
    searchRow->addWidget(m_addButton);
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
    m_list->setObjectName(QStringLiteral("PlaylistList"));
    m_list->setSpacing(2);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setSelectionMode(QAbstractItemView::NoSelection);
    m_list->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(m_list, 1);

    connect(m_search, &QLineEdit::textChanged, this, &PlaylistListPanel::filtersChanged);

    setStyleSheet(QStringLiteral(R"(
        QWidget#PlaylistListPanel { background: %1; border-right: 1px solid %2; }
        QLineEdit#SearchBox {
            background: #ffffff; border: 1px solid %2; border-radius: 9px;
            padding: 9px 12px; font-size: 13px; color: %3;
        }
        QPushButton#FilterButton {
            background: #ffffff; border: 1px solid %2; border-radius: 9px;
        }
        QPushButton#SortButton {
            background: transparent; border: none; padding: 0;
            font-size: 12.5px; font-weight: 500; color: %4;
        }
        QPushButton#SortButton::menu-indicator { image: none; width: 0; }
        QListWidget#PlaylistList { background: transparent; border: none; }
        QListWidget#PlaylistList::item { border: none; }
    )").arg(Theme::BgPanel, Theme::BorderLight, Theme::TextDarkPrimary, Theme::TextDarkSecondary));
}

void PlaylistListPanel::updateSortLabel()
{
    m_sortButton->setText(m_sortOrder == SortOrder::Alphabetical
        ? tr("Сортировка: по алфавиту (А-Я)")
        : tr("Сортировка: сначала новые"));
}

void PlaylistListPanel::setPlaylists(const QList<Playlist> &playlists)
{
    m_playlists = playlists;
    const std::optional<int> previousSelection = selectedPlaylistId();

    // See LibraryListPanel::setItems for why: without this the list
    // repaints/relayouts after every single addItem() below.
    m_list->setUpdatesEnabled(false);
    m_list->clear();
    for (const Playlist &playlist : m_playlists) {
        auto *row = new PlaylistRowWidget(playlist);
        connect(row, &PlaylistRowWidget::clicked, this, [this](int id) { selectPlaylistById(id); });
        connect(row, &PlaylistRowWidget::starClicked, this, &PlaylistListPanel::favoriteToggled);

        auto *listItem = new QListWidgetItem;
        listItem->setSizeHint(row->sizeHint());
        listItem->setData(Qt::UserRole, playlist.id);
        m_list->addItem(listItem);
        m_list->setItemWidget(listItem, row);
    }
    m_list->setUpdatesEnabled(true);

    m_countLabel->setText(tr("%1 %2").arg(m_playlists.size())
        .arg(ruCount(m_playlists.size(), tr("плейлист"), tr("плейлиста"), tr("плейлистов"))));

    if (previousSelection.has_value())
        selectPlaylistById(*previousSelection);
    else
        emit playlistSelected(std::nullopt);
}

std::optional<int> PlaylistListPanel::selectedPlaylistId() const
{
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->data(Qt::UserRole + 1).toBool())
            return m_list->item(i)->data(Qt::UserRole).toInt();
    return std::nullopt;
}

void PlaylistListPanel::selectPlaylistById(int id)
{
    bool found = false;
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem *listItem = m_list->item(i);
        auto *row = qobject_cast<PlaylistRowWidget *>(m_list->itemWidget(listItem));
        const bool matches = listItem->data(Qt::UserRole).toInt() == id;
        if (row)
            row->setSelected(matches);
        listItem->setData(Qt::UserRole + 1, matches);
        if (matches) {
            found = true;
            m_list->scrollToItem(listItem);
        }
    }
    emit playlistSelected(found ? std::optional<int>(id) : std::nullopt);
}

QString PlaylistListPanel::searchText() const
{
    return m_search->text();
}

SortOrder PlaylistListPanel::sortOrder() const
{
    return m_sortOrder;
}

#include "PlaylistListPanel.moc"
