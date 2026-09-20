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

    // On success, sets item.id and item.createdAt.
    bool add(ContentItem &item) const;
    bool update(const ContentItem &item) const;
    bool remove(int id) const;
    bool setFavorite(int id, bool favorite) const;
    bool setNotes(int id, const QString &notes) const;

private:
    static ContentItem fromRecord(QSqlQuery &query);
};
