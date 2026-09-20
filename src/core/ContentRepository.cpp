#include "ContentRepository.h"

#include <QDateTime>
#include <QSqlError>
#include <QVariant>
#include <QDebug>

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

bool ContentRepository::add(ContentItem &item) const
{
    item.createdAt = QDateTime::currentDateTime();

    QSqlQuery query;
    query.prepare(QStringLiteral(R"(
        INSERT INTO items (type, title, text, ref_book, ref_location, expiry_date, image_path, caption, favorite, notes, background_type, background_path, created_at)
        VALUES (:type, :title, :text, :ref_book, :ref_location, :expiry_date, :image_path, :caption, :favorite, :notes, :background_type, :background_path, :created_at)
    )"));

    query.bindValue(QStringLiteral(":type"), contentTypeToDbString(item.type));
    query.bindValue(QStringLiteral(":title"), item.title);
    query.bindValue(QStringLiteral(":text"), item.text);
    query.bindValue(QStringLiteral(":ref_book"), item.refBook);
    query.bindValue(QStringLiteral(":ref_location"), item.refLocation);
    query.bindValue(QStringLiteral(":expiry_date"),
                     item.expiryDate.isValid() ? item.expiryDate.toString(Qt::ISODate) : QVariant(QMetaType(QMetaType::QString)));
    query.bindValue(QStringLiteral(":image_path"), item.imagePath);
    query.bindValue(QStringLiteral(":caption"), item.caption);
    query.bindValue(QStringLiteral(":favorite"), item.favorite);
    query.bindValue(QStringLiteral(":notes"), item.notes);
    query.bindValue(QStringLiteral(":background_type"), backgroundTypeToDbString(item.backgroundType));
    query.bindValue(QStringLiteral(":background_path"), item.backgroundPath);
    query.bindValue(QStringLiteral(":created_at"), item.createdAt.toString(Qt::ISODate));

    if (!query.exec()) {
        qWarning() << "add failed:" << query.lastError().text();
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
            background_path = :background_path
        WHERE id = :id
    )"));

    query.bindValue(QStringLiteral(":type"), contentTypeToDbString(item.type));
    query.bindValue(QStringLiteral(":title"), item.title);
    query.bindValue(QStringLiteral(":text"), item.text);
    query.bindValue(QStringLiteral(":ref_book"), item.refBook);
    query.bindValue(QStringLiteral(":ref_location"), item.refLocation);
    query.bindValue(QStringLiteral(":expiry_date"),
                     item.expiryDate.isValid() ? item.expiryDate.toString(Qt::ISODate) : QVariant(QMetaType(QMetaType::QString)));
    query.bindValue(QStringLiteral(":image_path"), item.imagePath);
    query.bindValue(QStringLiteral(":caption"), item.caption);
    query.bindValue(QStringLiteral(":favorite"), item.favorite);
    query.bindValue(QStringLiteral(":notes"), item.notes);
    query.bindValue(QStringLiteral(":background_type"), backgroundTypeToDbString(item.backgroundType));
    query.bindValue(QStringLiteral(":background_path"), item.backgroundPath);
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
    query.bindValue(QStringLiteral(":notes"), notes);
    query.bindValue(QStringLiteral(":id"), id);

    if (!query.exec()) {
        qWarning() << "setNotes failed:" << query.lastError().text();
        return false;
    }
    return true;
}
