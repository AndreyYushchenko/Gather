#pragma once

#include "ContentItem.h"

#include <QList>
#include <QMap>
#include <QSqlQuery>
#include <QString>
#include <optional>

enum class SortOrder {
    DateAddedDesc,
    Alphabetical,
    // Numeric order of ref_location (the songbook number songs are imported
    // with, e.g. "120") — falls back to title for items without one.
    Number
};

class ContentRepository {
public:
    // filterType: nullopt = all categories.
    // includeExpired: when false, announcements past their expiry date are left out.
    QList<ContentItem> fetch(const QString &searchText,
                              std::optional<ContentType> filterType,
                              SortOrder order,
                              bool includeExpired = false) const;

    std::optional<ContentItem> findById(int id) const;

    // Number of (non-expired) items per category, for the sidebar badges.
    QMap<ContentType, int> categoryCounts() const;
    // Names of the songbooks songs were imported from (items.ref_book on
    // songs), sorted.
    QStringList songCollections() const;
    // Every songbook with its number of songs; "" = songs without one.
    QList<QPair<QString, int>> songCollectionCounts() const;
    // Deletes a songbook's songs (and their places in playlists).
    bool removeSongCollection(const QString &collection) const;
    // A songbook's songs in songbook order (by number, then title).
    QList<ContentItem> songsInCollection(const QString &collection) const;

    // On success, sets item.id and item.createdAt.
    bool add(ContentItem &item) const;
    bool update(const ContentItem &item) const;
    bool remove(int id) const;
    bool setFavorite(int id, bool favorite) const;
    bool setNotes(int id, const QString &notes) const;

    // Also used by PlaylistRepository for its joined item rows.
    static ContentItem fromRecord(QSqlQuery &query);
};
