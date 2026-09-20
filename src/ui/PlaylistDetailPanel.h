#pragma once

#include "core/ContentRepository.h"
#include "core/PlaylistRepository.h"

#include <QWidget>
#include <optional>

class QLabel;
class QListWidget;
class QPushButton;

// Center panel for the Playlists screen: header (rename/star/delete), the
// ordered "Порядок показа" list (drag to reorder, remove per row), and
// add/copy/delete/preview/go-live actions mirroring DetailPanel's slide row.
class PlaylistDetailPanel : public QWidget {
    Q_OBJECT
public:
    explicit PlaylistDetailPanel(QWidget *parent = nullptr);

    // The repository backing "add item" pulls from the whole library.
    void setLibraryItems(const QList<ContentItem> &items);
    void showPlaylist(const std::optional<Playlist> &playlist, const QList<PlaylistEntry> &entries);

    // Sends the currently selected entry live, exactly like clicking
    // "На экран" — used by the app-wide F5 shortcut.
    void triggerGoLive();

signals:
    void renameRequested(int id, QString newName);
    void deleteRequested(int id);
    void favoriteToggleRequested(int id);
    void duplicateRequested(int id);
    void addItemRequested(int playlistId, int itemId);
    void removeEntryRequested(int rowId);
    void reorderRequested(int playlistId, QList<int> orderedRowIds);
    void previewRequested(const ContentItem &item, int slideIndex);
    void goLiveRequested(const ContentItem &item, int slideIndex);

private:
    void buildUi();
    void rebuildList();
    void selectRow(int rowId);

    std::optional<Playlist> m_playlist;
    QList<PlaylistEntry> m_entries;
    QList<ContentItem> m_libraryItems;
    int m_selectedRowId = -1;

    QLabel *m_emptyState = nullptr;
    QWidget *m_content = nullptr;
    QLabel *m_titleLabel = nullptr;
    QPushButton *m_starButton = nullptr;
    QLabel *m_metaLabel = nullptr;
    QListWidget *m_list = nullptr;
};
