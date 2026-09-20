#include "BibleRepository.h"

#include <QSqlQuery>
#include <QVariant>

QString BibleRepository::defaultTranslation() const
{
    QSqlQuery query(QStringLiteral("SELECT translation FROM bible_books ORDER BY translation LIMIT 1"));
    if (query.next())
        return query.value(0).toString();
    return {};
}

QList<BibleBook> BibleRepository::books(const QString &translation) const
{
    QList<BibleBook> result;
    QSqlQuery query;
    query.prepare(QStringLiteral(
        "SELECT book_num, book_name, chapter_count FROM bible_books "
        "WHERE translation = :t ORDER BY book_num"));
    query.bindValue(QStringLiteral(":t"), translation);
    if (!query.exec())
        return result;

    while (query.next()) {
        BibleBook book;
        book.num = query.value(0).toInt();
        book.name = query.value(1).toString();
        book.chapterCount = query.value(2).toInt();
        result << book;
    }
    return result;
}

int BibleRepository::totalVerseCount(const QString &translation) const
{
    QSqlQuery query;
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM bible_verses WHERE translation = :t"));
    query.bindValue(QStringLiteral(":t"), translation);
    if (!query.exec() || !query.next())
        return 0;
    return query.value(0).toInt();
}

int BibleRepository::verseCount(const QString &translation, int bookNum, int chapter) const
{
    QSqlQuery query;
    query.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM bible_verses WHERE translation = :t AND book_num = :b AND chapter = :c"));
    query.bindValue(QStringLiteral(":t"), translation);
    query.bindValue(QStringLiteral(":b"), bookNum);
    query.bindValue(QStringLiteral(":c"), chapter);
    if (!query.exec() || !query.next())
        return 0;
    return query.value(0).toInt();
}

QList<BibleVerse> BibleRepository::verses(const QString &translation, int bookNum, int chapter, int fromVerse, int toVerse) const
{
    QList<BibleVerse> result;
    QSqlQuery query;
    query.prepare(QStringLiteral(
        "SELECT verse, text FROM bible_verses "
        "WHERE translation = :t AND book_num = :b AND chapter = :c AND verse >= :from_v AND verse <= :to_v "
        "ORDER BY verse"));
    query.bindValue(QStringLiteral(":t"), translation);
    query.bindValue(QStringLiteral(":b"), bookNum);
    query.bindValue(QStringLiteral(":c"), chapter);
    query.bindValue(QStringLiteral(":from_v"), fromVerse);
    query.bindValue(QStringLiteral(":to_v"), toVerse);
    if (!query.exec())
        return result;

    while (query.next()) {
        BibleVerse verse;
        verse.verse = query.value(0).toInt();
        verse.text = query.value(1).toString();
        result << verse;
    }
    return result;
}

QList<BibleSearchHit> BibleRepository::search(const QString &translation, const QString &query_, int limit) const
{
    QList<BibleSearchHit> result;
    if (query_.trimmed().isEmpty())
        return result;

    QSqlQuery query;
    query.prepare(QStringLiteral(
        "SELECT v.book_num, b.book_name, v.chapter, v.verse, v.text "
        "FROM bible_verses v JOIN bible_books b ON b.translation = v.translation AND b.book_num = v.book_num "
        "WHERE v.translation = :t AND (v.text LIKE :q OR b.book_name LIKE :q) "
        "ORDER BY v.book_num, v.chapter, v.verse LIMIT :limit"));
    query.bindValue(QStringLiteral(":t"), translation);
    query.bindValue(QStringLiteral(":q"), QStringLiteral("%%1%").arg(query_.trimmed()));
    query.bindValue(QStringLiteral(":limit"), limit);
    if (!query.exec())
        return result;

    while (query.next()) {
        BibleSearchHit hit;
        hit.bookNum = query.value(0).toInt();
        hit.bookName = query.value(1).toString();
        hit.chapter = query.value(2).toInt();
        hit.verse = query.value(3).toInt();
        hit.text = query.value(4).toString();
        result << hit;
    }
    return result;
}
