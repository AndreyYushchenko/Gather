#include "ContentRepository.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QVariant>
#include <QDebug>

namespace {

// Qt 6 binds a null QString as SQL NULL, and every text column here is
// NOT NULL: a song (no image, caption, notes …) was refused on insert, so
// importing a songbook or adding a song silently saved nothing.
QString notNull(const QString &value)
{
    return value.isNull() ? QStringLiteral("") : value;
}


// items.style: the announcement layout options as a small JSON object.
QString styleToJson(const ContentItem &item)
{
    if (item.type != ContentType::Announcement)
        return QString();
    QJsonObject style;
    style[QStringLiteral("align")] = item.textAlign == TextAlign::Left ? QStringLiteral("left")
        : item.textAlign == TextAlign::Right ? QStringLiteral("right") : QStringLiteral("center");
    style[QStringLiteral("size")] = item.textSize;
    style[QStringLiteral("category")] = item.category;
    return QString::fromUtf8(QJsonDocument(style).toJson(QJsonDocument::Compact));
}

void styleFromJson(ContentItem &item, const QString &json)
{
    const QJsonObject style = QJsonDocument::fromJson(json.toUtf8()).object();
    const QString align = style.value(QStringLiteral("align")).toString();
    item.textAlign = align == QLatin1String("left") ? TextAlign::Left
        : align == QLatin1String("right") ? TextAlign::Right : TextAlign::Center;
    item.textSize = qBound(-1, style.value(QStringLiteral("size")).toInt(), 1);
    item.category = style.value(QStringLiteral("category")).toString();
}

} // namespace

ContentItem ContentRepository::fromRecord(QSqlQuery &query)
{
    ContentItem item;
    item.id = query.value(QStringLiteral("id")).toInt();
    item.type = contentTypeFromDbString(query.value(QStringLiteral("type")).toString());
    item.title = query.value(QStringLiteral("title")).toString();
    item.text = query.value(QStringLiteral("text")).toString();
    item.refBook = query.value(QStringLiteral("ref_book")).toString();
    item.refLocation = query.value(QStringLiteral("ref_location")).toString();

    const QVariant expiry = query.value(QStringLiteral("expiry_date"));
    if (!expiry.isNull() && expiry.isValid() && !expiry.toString().isEmpty())
        item.expiryDate = QDate::fromString(expiry.toString(), Qt::ISODate);

    item.imagePath = query.value(QStringLiteral("image_path")).toString();
    item.caption = query.value(QStringLiteral("caption")).toString();
    item.backgroundType = backgroundTypeFromDbString(query.value(QStringLiteral("background_type")).toString());
    item.backgroundPath = query.value(QStringLiteral("background_path")).toString();
    item.favorite = query.value(QStringLiteral("favorite")).toBool();
    item.notes = query.value(QStringLiteral("notes")).toString();
    styleFromJson(item, query.value(QStringLiteral("style")).toString());
    item.createdAt = QDateTime::fromString(query.value(QStringLiteral("created_at")).toString(), Qt::ISODate);

    return item;
}

QList<ContentItem> ContentRepository::fetch(const QString &searchText,
                                             std::optional<ContentType> filterType,
                                             SortOrder order,
                                             bool includeExpired) const
{
    QList<ContentItem> result;

    QString sql = QStringLiteral("SELECT * FROM items WHERE 1=1");
    if (filterType.has_value())
        sql += QStringLiteral(" AND type = :type");
    if (!searchText.trimmed().isEmpty())
        sql += QStringLiteral(" AND (title LIKE :search OR text LIKE :search OR ref_book LIKE :search OR ref_location LIKE :search)");
    if (!includeExpired)
        sql += QStringLiteral(" AND (expiry_date IS NULL OR expiry_date = '' OR date(expiry_date) >= date('now'))");

    switch (order) {
    case SortOrder::Alphabetical:
        sql += QStringLiteral(" ORDER BY title COLLATE NOCASE ASC");
        break;
    case SortOrder::Number:
        sql += QStringLiteral(" ORDER BY CAST(ref_location AS INTEGER) ASC, title COLLATE NOCASE ASC");
        break;
    case SortOrder::DateAddedDesc:
        sql += QStringLiteral(" ORDER BY created_at DESC");
        break;
    }

    QSqlQuery query;
    query.prepare(sql);
    if (filterType.has_value())
        query.bindValue(QStringLiteral(":type"), contentTypeToDbString(*filterType));
    if (!searchText.trimmed().isEmpty())
        query.bindValue(QStringLiteral(":search"), QStringLiteral("%%1%").arg(searchText.trimmed()));

    if (!query.exec()) {
        qWarning() << "fetch failed:" << query.lastError().text();
        return result;
    }

    while (query.next())
        result << fromRecord(query);

    return result;
}

std::optional<ContentItem> ContentRepository::findById(int id) const
{
    QSqlQuery query;
    query.prepare(QStringLiteral("SELECT * FROM items WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);

    if (!query.exec() || !query.next())
        return std::nullopt;

    return fromRecord(query);
}

QMap<ContentType, int> ContentRepository::categoryCounts() const
{
    QMap<ContentType, int> counts;

    QSqlQuery query(QStringLiteral(R"(
        SELECT type, COUNT(*) AS cnt FROM items
        WHERE expiry_date IS NULL OR expiry_date = '' OR date(expiry_date) >= date('now')
        GROUP BY type
    )"));

    while (query.next())
        counts[contentTypeFromDbString(query.value(QStringLiteral("type")).toString())] =
            query.value(QStringLiteral("cnt")).toInt();

    return counts;
}

QStringList ContentRepository::songCollections() const
{
    QStringList result;
    QSqlQuery query(QStringLiteral("SELECT DISTINCT ref_book FROM items WHERE type = 'song' AND ref_book <> '' ORDER BY ref_book COLLATE NOCASE"));
    while (query.next())
        result << query.value(0).toString();
    return result;
}

QList<QPair<QString, int>> ContentRepository::songCollectionCounts() const
{
    QList<QPair<QString, int>> result;
    QSqlQuery query(QStringLiteral("SELECT ref_book, COUNT(*) FROM items WHERE type = 'song' "
                                   "GROUP BY ref_book ORDER BY ref_book = '', ref_book COLLATE NOCASE"));
    while (query.next())
        result.append({query.value(0).toString(), query.value(1).toInt()});
    return result;
}

bool ContentRepository::removeSongCollection(const QString &collection) const
{
    QSqlDatabase db = QSqlDatabase::database();
    db.transaction();
    // No foreign keys in this database: take the songs out of playlists too.
    QSqlQuery entries;
    entries.prepare(QStringLiteral("DELETE FROM playlist_items WHERE item_id IN "
                                   "(SELECT id FROM items WHERE type = 'song' AND ref_book = :book)"));
    entries.bindValue(QStringLiteral(":book"), notNull(collection));
    QSqlQuery songs;
    songs.prepare(QStringLiteral("DELETE FROM items WHERE type = 'song' AND ref_book = :book"));
    songs.bindValue(QStringLiteral(":book"), notNull(collection));
    if (!entries.exec() || !songs.exec()) {
        qWarning() << "removeSongCollection failed:" << entries.lastError().text() << songs.lastError().text();
        db.rollback();
        return false;
    }
    return db.commit();
}

QList<ContentItem> ContentRepository::songsInCollection(const QString &collection) const
{
    QList<ContentItem> result;
    QSqlQuery query;
    query.prepare(QStringLiteral("SELECT * FROM items WHERE type = 'song' AND ref_book = :book "
                                 "ORDER BY CAST(ref_location AS INTEGER) = 0, CAST(ref_location AS INTEGER), title COLLATE NOCASE"));
    query.bindValue(QStringLiteral(":book"), notNull(collection));
    if (!query.exec())
        return result;
    while (query.next())
        result << fromRecord(query);
    return result;
}

bool ContentRepository::add(ContentItem &item) const
{
    item.createdAt = QDateTime::currentDateTime();

    QSqlQuery query;
    bool prepOk = query.prepare(QStringLiteral(R"(
        INSERT INTO items (type, title, text, ref_book, ref_location, expiry_date, image_path, caption, favorite, notes, background_type, background_path, style, created_at)
        VALUES (:type, :title, :text, :ref_book, :ref_location, :expiry_date, :image_path, :caption, :favorite, :notes, :background_type, :background_path, :style, :created_at)
    )"));
    if (!prepOk) {
        qWarning() << "add prepare failed:" << query.lastError().text();
        return false;
    }

    query.bindValue(QStringLiteral(":type"), contentTypeToDbString(item.type));
    query.bindValue(QStringLiteral(":title"), notNull(item.title));
    query.bindValue(QStringLiteral(":text"), notNull(item.text));
    query.bindValue(QStringLiteral(":ref_book"), notNull(item.refBook));
    query.bindValue(QStringLiteral(":ref_location"), notNull(item.refLocation));
    query.bindValue(QStringLiteral(":expiry_date"),
                     item.expiryDate.isValid() ? item.expiryDate.toString(Qt::ISODate) : QVariant(QMetaType(QMetaType::QString)));
    query.bindValue(QStringLiteral(":image_path"), notNull(item.imagePath));
    query.bindValue(QStringLiteral(":caption"), notNull(item.caption));
    query.bindValue(QStringLiteral(":favorite"), item.favorite);
    query.bindValue(QStringLiteral(":notes"), notNull(item.notes));
    query.bindValue(QStringLiteral(":background_type"), backgroundTypeToDbString(item.backgroundType));
    query.bindValue(QStringLiteral(":background_path"), notNull(item.backgroundPath));
    query.bindValue(QStringLiteral(":style"), notNull(styleToJson(item)));
    query.bindValue(QStringLiteral(":created_at"), item.createdAt.toString(Qt::ISODate));

    if (!query.exec()) {
        // Callers tell the operator; the details are for the log.
        qWarning() << "add failed:" << contentTypeToDbString(item.type) << item.title << query.lastError().text();
        return false;
    }

    item.id = query.lastInsertId().toInt();
    return true;
}

bool ContentRepository::update(const ContentItem &item) const
{
    QSqlQuery query;
    query.prepare(QStringLiteral(R"(
        UPDATE items SET
            type = :type,
            title = :title,
            text = :text,
            ref_book = :ref_book,
            ref_location = :ref_location,
            expiry_date = :expiry_date,
            image_path = :image_path,
            caption = :caption,
            favorite = :favorite,
            notes = :notes,
            background_type = :background_type,
            background_path = :background_path,
            style = :style
        WHERE id = :id
    )"));

    query.bindValue(QStringLiteral(":type"), contentTypeToDbString(item.type));
    query.bindValue(QStringLiteral(":title"), notNull(item.title));
    query.bindValue(QStringLiteral(":text"), notNull(item.text));
    query.bindValue(QStringLiteral(":ref_book"), notNull(item.refBook));
    query.bindValue(QStringLiteral(":ref_location"), notNull(item.refLocation));
    query.bindValue(QStringLiteral(":expiry_date"),
                     item.expiryDate.isValid() ? item.expiryDate.toString(Qt::ISODate) : QVariant(QMetaType(QMetaType::QString)));
    query.bindValue(QStringLiteral(":image_path"), notNull(item.imagePath));
    query.bindValue(QStringLiteral(":caption"), notNull(item.caption));
    query.bindValue(QStringLiteral(":favorite"), item.favorite);
    query.bindValue(QStringLiteral(":notes"), notNull(item.notes));
    query.bindValue(QStringLiteral(":background_type"), backgroundTypeToDbString(item.backgroundType));
    query.bindValue(QStringLiteral(":background_path"), notNull(item.backgroundPath));
    query.bindValue(QStringLiteral(":style"), notNull(styleToJson(item)));
    query.bindValue(QStringLiteral(":id"), item.id);

    if (!query.exec()) {
        qWarning() << "update failed:" << query.lastError().text();
        return false;
    }

    return true;
}

bool ContentRepository::remove(int id) const
{
    QSqlQuery query;
    query.prepare(QStringLiteral("DELETE FROM items WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);

    if (!query.exec()) {
        qWarning() << "remove failed:" << query.lastError().text();
        return false;
    }

    return true;
}

bool ContentRepository::setFavorite(int id, bool favorite) const
{
    QSqlQuery query;
    query.prepare(QStringLiteral("UPDATE items SET favorite = :favorite WHERE id = :id"));
    query.bindValue(QStringLiteral(":favorite"), favorite);
    query.bindValue(QStringLiteral(":id"), id);

    if (!query.exec()) {
        qWarning() << "setFavorite failed:" << query.lastError().text();
        return false;
    }
    return true;
}

bool ContentRepository::setNotes(int id, const QString &notes) const
{
    QSqlQuery query;
    query.prepare(QStringLiteral("UPDATE items SET notes = :notes WHERE id = :id"));
    query.bindValue(QStringLiteral(":notes"), notNull(notes));
    query.bindValue(QStringLiteral(":id"), id);

    if (!query.exec()) {
        qWarning() << "setNotes failed:" << query.lastError().text();
        return false;
    }
    return true;
}
