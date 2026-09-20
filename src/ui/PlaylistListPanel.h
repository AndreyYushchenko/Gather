#pragma once

#include "core/ContentRepository.h"
#include "core/PlaylistRepository.h"

#include <QWidget>
#include <optional>

class QLineEdit;
class QPushButton;
class QLabel;
class QListWidget;

// Left "playlists" column: search, sort, and the row list — mirrors
// LibraryListPanel's structure but for playlists/services instead of items.
class PlaylistListPanel : public QWidget {
    Q_OBJECT
public:
    explicit PlaylistListPanel(QWidget *parent = nullptr);

    void setPlaylists(const QList<Playlist> &playlists);
    std::optional<int> selectedPlaylistId() const;
    void selectPlaylistById(int id);
    QString searchText() const;
    SortOrder sortOrder() const;

signals:
    void filtersChanged();
    void playlistSelected(std::optional<int> id);
    void favoriteToggled(int id);
    void createRequested();

private:
    void updateSortLabel();

    QList<Playlist> m_playlists;
    SortOrder m_sortOrder = SortOrder::DateAddedDesc;

    QLineEdit *m_search = nullptr;
    QPushButton *m_addButton = nullptr;
    QPushButton *m_sortButton = nullptr;
    QLabel *m_countLabel = nullptr;
    QListWidget *m_list = nullptr;
};
