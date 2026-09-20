#include "PlaylistRepository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {
ContentItem itemFromRecord(QSqlQuery &query)
{
    ContentItem item;
    item.id = query.value(QStringLiteral("id")).toInt();
    item.type = contentTypeFromDbString(query.value(QStringLiteral("type")).toString());
    item.title = query.value(QStringLiteral("title")).toString();
    item.text = query.value(QStringLiteral("text")).toString();
    item.refBook = query.value(QStringLiteral("ref_book")).toString();
    item.refLocation = query.value(QStringLiteral("ref_location")).toString();
    item.imagePath = query.value(QStringLiteral("image_path")).toString();
    item.caption = query.value(QStringLiteral("caption")).toString();
    item.favorite = query.value(QStringLiteral("favorite")).toBool();
    item.createdAt = QDateTime::fromString(query.value(QStringLiteral("created_at")).toString(), Qt::ISODate);
    return item;
}
}

Playlist PlaylistRepository::fromRecord(QSqlQuery &query)
{
    Playlist playlist;
    playlist.id = query.value(QStringLiteral("id")).toInt();
    playlist.name = query.value(QStringLiteral("name")).toString();
    playlist.favorite = query.value(QStringLiteral("favorite")).toBool();
    playlist.createdAt = QDateTime::fromString(query.value(QStringLiteral("created_at")).toString(), Qt::ISODate);
    playlist.itemCount = query.value(QStringLiteral("item_count")).toInt();
    return playlist;
}

QList<Playlist> PlaylistRepository::fetch(const QString &searchText, SortOrder order) const
{
    QList<Playlist> result;

    QString sql = QStringLiteral(R"(
        SELECT p.*, (SELECT COUNT(*) FROM playlist_items pi WHERE pi.playlist_id = p.id) AS item_count
        FROM playlists p WHERE 1=1
    )");
    if (!searchText.trimmed().isEmpty())
        sql += QStringLiteral(" AND p.name LIKE :search");
    sql += order == SortOrder::Alphabetical
               ? QStringLiteral(" ORDER BY p.name COLLATE NOCASE ASC")
               : QStringLiteral(" ORDER BY p.created_at DESC");

    QSqlQuery query;
    query.prepare(sql);
    if (!searchText.trimmed().isEmpty())
        query.bindValue(QStringLiteral(":search"), QStringLiteral("%%1%").arg(searchText.trimmed()));

    if (!query.exec())
        return result;

    while (query.next())
        result << fromRecord(query);
    return result;
}

std::optional<Playlist> PlaylistRepository::findById(int id) const
{
    QSqlQuery query;
    query.prepare(QStringLiteral(R"(
        SELECT p.*, (SELECT COUNT(*) FROM playlist_items pi WHERE pi.playlist_id = p.id) AS item_count
        FROM playlists p WHERE p.id = :id
    )"));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec() || !query.next())
        return std::nullopt;
    return fromRecord(query);
}

bool PlaylistRepository::add(Playlist &playlist) const
{
    playlist.createdAt = QDateTime::currentDateTime();
    QSqlQuery query;
    query.prepare(QStringLiteral("INSERT INTO playlists (name, favorite, created_at) VALUES (:name, :favorite, :created_at)"));
    query.bindValue(QStringLiteral(":name"), playlist.name);
    query.bindValue(QStringLiteral(":favorite"), playlist.favorite);
    query.bindValue(QStringLiteral(":created_at"), playlist.createdAt.toString(Qt::ISODate));
    if (!query.exec())
        return false;
    playlist.id = query.lastInsertId().toInt();
    return true;
}

bool PlaylistRepository::rename(int id, const QString &name) const
{
    QSqlQuery query;
    query.prepare(QStringLiteral("UPDATE playlists SET name = :name WHERE id = :id"));
    query.bindValue(QStringLiteral(":name"), name);
    query.bindValue(QStringLiteral(":id"), id);
    return query.exec();
}

bool PlaylistRepository::remove(int id) const
{
    QSqlQuery cleanup;
    cleanup.prepare(QStringLiteral("DELETE FROM playlist_items WHERE playlist_id = :id"));
    cleanup.bindValue(QStringLiteral(":id"), id);
    cleanup.exec();

    QSqlQuery query;
    query.prepare(QStringLiteral("DELETE FROM playlists WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);
    return query.exec();
}

bool PlaylistRepository::setFavorite(int id, bool favorite) const
{
    QSqlQuery query;
    query.prepare(QStringLiteral("UPDATE playlists SET favorite = :favorite WHERE id = :id"));
    query.bindValue(QStringLiteral(":favorite"), favorite);
    query.bindValue(QStringLiteral(":id"), id);
    return query.exec();
}

bool PlaylistRepository::duplicate(int id, const QString &newName) const
{
    Playlist copy;
    copy.name = newName;
    if (!add(copy))
        return false;

    QSqlQuery query;
    query.prepare(QStringLiteral("SELECT item_id, position FROM playlist_items WHERE playlist_id = :id ORDER BY position"));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec())
        return false;

    while (query.next()) {
        QSqlQuery insert;
        insert.prepare(QStringLiteral("INSERT INTO playlist_items (playlist_id, item_id, position) VALUES (:playlist_id, :item_id, :position)"));
        insert.bindValue(QStringLiteral(":playlist_id"), copy.id);
        insert.bindValue(QStringLiteral(":item_id"), query.value(0).toInt());
        insert.bindValue(QStringLiteral(":position"), query.value(1).toInt());
        insert.exec();
    }
    return true;
}

QList<PlaylistEntry> PlaylistRepository::entries(int playlistId) const
{
    QList<PlaylistEntry> result;
    QSqlQuery query;
    query.prepare(QStringLiteral(R"(
        SELECT pi.id AS row_id, pi.position AS position, i.*
        FROM playlist_items pi JOIN items i ON i.id = pi.item_id
        WHERE pi.playlist_id = :id
        ORDER BY pi.position
    )"));
    query.bindValue(QStringLiteral(":id"), playlistId);
    if (!query.exec())
        return result;

    while (query.next()) {
        PlaylistEntry entry;
        entry.rowId = query.value(QStringLiteral("row_id")).toInt();
        entry.position = query.value(QStringLiteral("position")).toInt();
        entry.item = itemFromRecord(query);
        result << entry;
    }
    return result;
}

bool PlaylistRepository::appendItem(int playlistId, int itemId) const
{
    QSqlQuery maxQuery;
    maxQuery.prepare(QStringLiteral("SELECT COALESCE(MAX(position), -1) + 1 FROM playlist_items WHERE playlist_id = :id"));
    maxQuery.bindValue(QStringLiteral(":id"), playlistId);
    if (!maxQuery.exec() || !maxQuery.next())
        return false;
    const int nextPosition = maxQuery.value(0).toInt();

    QSqlQuery query;
    query.prepare(QStringLiteral("INSERT INTO playlist_items (playlist_id, item_id, position) VALUES (:playlist_id, :item_id, :position)"));
    query.bindValue(QStringLiteral(":playlist_id"), playlistId);
    query.bindValue(QStringLiteral(":item_id"), itemId);
    query.bindValue(QStringLiteral(":position"), nextPosition);
    return query.exec();
}

bool PlaylistRepository::removeEntry(int rowId) const
{
    QSqlQuery query;
    query.prepare(QStringLiteral("DELETE FROM playlist_items WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), rowId);
    return query.exec();
}

bool PlaylistRepository::reorder(int playlistId, const QList<int> &orderedRowIds) const
{
    Q_UNUSED(playlistId);
    for (int i = 0; i < orderedRowIds.size(); ++i) {
        QSqlQuery query;
        query.prepare(QStringLiteral("UPDATE playlist_items SET position = :position WHERE id = :id"));
        query.bindValue(QStringLiteral(":position"), i);
        query.bindValue(QStringLiteral(":id"), orderedRowIds.at(i));
        if (!query.exec())
            return false;
    }
    return true;
}
