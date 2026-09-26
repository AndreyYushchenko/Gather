#include "PlaylistRepository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {

// Qt 6 binds a null QString as SQL NULL; the text columns are NOT NULL, so
// a playlist without a description or cover was refused.
QString notNull(const QString &value)
{
    return value.isNull() ? QStringLiteral("") : value;
}

} // namespace

Playlist PlaylistRepository::fromRecord(QSqlQuery &query)
{
    Playlist playlist;
    playlist.id = query.value(QStringLiteral("id")).toInt();
    playlist.name = query.value(QStringLiteral("name")).toString();
    playlist.description = query.value(QStringLiteral("description")).toString();
    playlist.coverPath = query.value(QStringLiteral("cover_path")).toString();
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
    query.prepare(QStringLiteral("INSERT INTO playlists (name, description, cover_path, favorite, created_at) "
                                 "VALUES (:name, :description, :cover_path, :favorite, :created_at)"));
    query.bindValue(QStringLiteral(":name"), notNull(playlist.name));
    query.bindValue(QStringLiteral(":description"), notNull(playlist.description));
    query.bindValue(QStringLiteral(":cover_path"), notNull(playlist.coverPath));
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
    query.bindValue(QStringLiteral(":name"), notNull(name));
    query.bindValue(QStringLiteral(":id"), id);
    return query.exec();
}

bool PlaylistRepository::setDescription(int id, const QString &description) const
{
    QSqlQuery query;
    query.prepare(QStringLiteral("UPDATE playlists SET description = :description WHERE id = :id"));
    query.bindValue(QStringLiteral(":description"), notNull(description));
    query.bindValue(QStringLiteral(":id"), id);
    return query.exec();
}

bool PlaylistRepository::setCover(int id, const QString &path) const
{
    QSqlQuery query;
    query.prepare(QStringLiteral("UPDATE playlists SET cover_path = :cover_path WHERE id = :id"));
    query.bindValue(QStringLiteral(":cover_path"), notNull(path));
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
    if (const auto original = findById(id)) {
        copy.description = original->description;
        copy.coverPath = original->coverPath;
    }
    if (!add(copy))
        return false;

    QSqlQuery query;
    query.prepare(QStringLiteral("SELECT item_id, position, duration_sec FROM playlist_items WHERE playlist_id = :id ORDER BY position"));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec())
        return false;

    while (query.next()) {
        QSqlQuery insert;
        insert.prepare(QStringLiteral("INSERT INTO playlist_items (playlist_id, item_id, position, duration_sec) "
                                      "VALUES (:playlist_id, :item_id, :position, :duration_sec)"));
        insert.bindValue(QStringLiteral(":playlist_id"), copy.id);
        insert.bindValue(QStringLiteral(":item_id"), query.value(0).toInt());
        insert.bindValue(QStringLiteral(":position"), query.value(1).toInt());
        insert.bindValue(QStringLiteral(":duration_sec"), query.value(2).toInt());
        insert.exec();
    }
    return true;
}

QList<PlaylistEntry> PlaylistRepository::entries(int playlistId) const
{
    QList<PlaylistEntry> result;
    QSqlQuery query;
    query.prepare(QStringLiteral(R"(
        SELECT pi.id AS row_id, pi.position AS position, pi.duration_sec AS duration_sec, i.*
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
        entry.durationSec = query.value(QStringLiteral("duration_sec")).toInt();
        entry.item = ContentRepository::fromRecord(query);
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

bool PlaylistRepository::setEntryDuration(int rowId, int seconds) const
{
    QSqlQuery query;
    query.prepare(QStringLiteral("UPDATE playlist_items SET duration_sec = :seconds WHERE id = :id"));
    query.bindValue(QStringLiteral(":seconds"), qMax(0, seconds));
    query.bindValue(QStringLiteral(":id"), rowId);
    return query.exec();
}

bool PlaylistRepository::duplicateEntry(int rowId) const
{
    QSqlQuery source;
    source.prepare(QStringLiteral("SELECT playlist_id, item_id, position, duration_sec FROM playlist_items WHERE id = :id"));
    source.bindValue(QStringLiteral(":id"), rowId);
    if (!source.exec() || !source.next())
        return false;
    const int playlistId = source.value(0).toInt();
    const int position = source.value(2).toInt();

    QSqlQuery shift;
    shift.prepare(QStringLiteral("UPDATE playlist_items SET position = position + 1 WHERE playlist_id = :playlist_id AND position > :position"));
    shift.bindValue(QStringLiteral(":playlist_id"), playlistId);
    shift.bindValue(QStringLiteral(":position"), position);
    if (!shift.exec())
        return false;

    QSqlQuery insert;
    insert.prepare(QStringLiteral("INSERT INTO playlist_items (playlist_id, item_id, position, duration_sec) "
                                  "VALUES (:playlist_id, :item_id, :position, :duration_sec)"));
    insert.bindValue(QStringLiteral(":playlist_id"), playlistId);
    insert.bindValue(QStringLiteral(":item_id"), source.value(1).toInt());
    insert.bindValue(QStringLiteral(":position"), position + 1);
    insert.bindValue(QStringLiteral(":duration_sec"), source.value(3).toInt());
    return insert.exec();
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
