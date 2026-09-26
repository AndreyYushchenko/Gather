#pragma once

#include "ContentItem.h"
#include "ContentRepository.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <optional>

struct Playlist {
    int id = -1;
    QString name;
    QString description;
    QString coverPath;   // chosen cover image; empty = derived from the entries
    bool favorite = false;
    QDateTime createdAt;
    int itemCount = 0;
};

struct PlaylistEntry {
    int rowId = -1;      // playlist_items.id, used to remove/reorder this slot
    int position = 0;
    int durationSec = 0; // planned length; 0 = not set
    ContentItem item;    // the referenced library item, joined in
};

class PlaylistRepository {
public:
    QList<Playlist> fetch(const QString &searchText, SortOrder order) const;
    std::optional<Playlist> findById(int id) const;

    bool add(Playlist &playlist) const;
    bool rename(int id, const QString &name) const;
    bool setDescription(int id, const QString &description) const;
    bool setCover(int id, const QString &path) const;
    bool remove(int id) const;
    bool setFavorite(int id, bool favorite) const;
    bool duplicate(int id, const QString &newName) const;

    QList<PlaylistEntry> entries(int playlistId) const;
    bool appendItem(int playlistId, int itemId) const;
    bool removeEntry(int rowId) const;
    bool setEntryDuration(int rowId, int seconds) const;
    // Inserts a copy of an entry right after it.
    bool duplicateEntry(int rowId) const;
    // Full ordered list of playlist_items row ids, front to back.
    bool reorder(int playlistId, const QList<int> &orderedRowIds) const;

private:
    static Playlist fromRecord(class QSqlQuery &query);
};
