#pragma once

#include "core/ContentItem.h"
#include "core/PlaylistRepository.h"

#include <QHash>
#include <QList>
#include <QWidget>
#include <optional>

class ContentRepository;
class ClickFrame;
class DropdownField;
class ElideLabel;
class QLabel;
class QLineEdit;
class QListWidget;
class QScrollArea;
class QStackedWidget;
class QVBoxLayout;
class PlaylistCover;

// "Плейлисты" (design.pen node flrNK): search, "Добавить плейлист",
// list/grid toggle and sort; the playlist list on the left and the selected
// playlist's card on the right — cover, description, date / count / length,
// and the "Элементы плейлиста" table (drag to reorder, copy, delete).
// "Запустить плейлист" puts it on the projector as a running playlist:
// Вперёд past an item's last slide moves on to the next item.
class PlaylistsPanel : public QWidget {
    Q_OBJECT
public:
    PlaylistsPanel(ContentRepository *contentRepository, PlaylistRepository *repository, QWidget *parent = nullptr);

    // Re-reads playlists (and their entries) from the database.
    void reload();
    // F5: starts the selected playlist from the selected row.
    void goLiveSelected();
    std::optional<ContentItem> selectedItem() const;
    // Which playlist entry is on the projector (-1 = none), for the table.
    void setLiveEntry(int playlistId, int index);
    // "Добавить плейлист".
    void createPlaylist();
    int selectedId() const { return m_selectedId; }
    void selectById(int id) { selectPlaylist(id); }

signals:
    void goLivePlaylistRequested(const QList<ContentItem> &items, int index, int playlistId);
    void previewRequested(const ContentItem &item);
    // Number of playlists (sidebar badge).
    void countChanged(int count);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *buildToolbar();
    QWidget *buildDetail();
    void setGridView(bool grid);
    QList<Playlist> visiblePlaylists() const;
    const Playlist *playlist(int id) const;
    QList<PlaylistEntry> entries(int id) const { return m_entries.value(id); }

    void rebuildList();
    void rebuildGrid();
    void showSelected();
    void rebuildTable();
    void selectPlaylist(int id);
    int selectedRowIndex() const;
    void startPlaylist(int fromIndex);

    void showPlaylistMenu(int id, const QPoint &globalPos);
    void showRowMenu(int rowId, const QPoint &globalPos);
    void renamePlaylist(int id);
    void editDescription(int id);
    void chooseCover(int id);
    void deletePlaylist(int id);
    void addItems();
    void editDuration(int rowId);
    // Fits the detail card to its width (cover size, button labels).
    void layoutDetail(int width);

    ContentRepository *m_contentRepository;
    PlaylistRepository *m_repository;
    QList<Playlist> m_playlists;
    QHash<int, QList<PlaylistEntry>> m_entries;

    QString m_search;
    int m_sort = 0; // 0 newest, 1 oldest, 2 by name
    bool m_gridView = false;
    int m_selectedId = -1;
    int m_selectedRowId = -1;
    int m_livePlaylistId = -1;
    int m_liveIndex = -1;

    QLineEdit *m_searchEdit = nullptr;
    ClickFrame *m_listButton = nullptr;
    ClickFrame *m_gridButton = nullptr;
    DropdownField *m_sortField = nullptr;
    QStackedWidget *m_viewStack = nullptr;

    QScrollArea *m_listScroll = nullptr;
    QWidget *m_listWidget = nullptr;
    QVBoxLayout *m_listLayout = nullptr;
    QHash<int, QWidget *> m_listCards;
    QScrollArea *m_gridScroll = nullptr;
    QWidget *m_gridWidget = nullptr;

    QStackedWidget *m_detailStack = nullptr;
    QLabel *m_detailEmpty = nullptr;
    PlaylistCover *m_cover = nullptr;
    ElideLabel *m_detailTitle = nullptr;
    QLabel *m_detailDescription = nullptr;
    QLabel *m_detailDate = nullptr;
    QWidget *m_detailDateBox = nullptr;
    QLabel *m_detailCount = nullptr;
    QLabel *m_detailDuration = nullptr;
    QWidget *m_tableHeader = nullptr;
    QListWidget *m_table = nullptr;
    QLabel *m_tableEmpty = nullptr;
    QWidget *m_detailCard = nullptr;
    QList<QPair<QLabel *, QString>> m_actionLabels; // label, full text
};
