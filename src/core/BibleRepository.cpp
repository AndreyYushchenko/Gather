#include "BibleRepository.h"
#include "AppSettings.h"

#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVariant>

QString BibleRepository::defaultTranslation() const
{
    const QStringList all = translations();
    const QString chosen = AppSettings::value(AppSettings::BibleTranslation).toString();
    if (all.contains(chosen))
        return chosen;
    return all.value(0);
}

QStringList BibleRepository::translations() const
{
    QStringList result;
    QSqlQuery query(QStringLiteral("SELECT DISTINCT translation FROM bible_books ORDER BY translation"));
    while (query.next())
        result << query.value(0).toString();
    return result;
}

QList<BibleRepository::TranslationInfo> BibleRepository::translationInfos() const
{
    QList<TranslationInfo> result;
    QSqlQuery query(QStringLiteral(
        "SELECT b.translation, COUNT(*), (SELECT COUNT(*) FROM bible_verses v WHERE v.translation = b.translation) "
        "FROM bible_books b GROUP BY b.translation ORDER BY b.translation"));
    while (query.next())
        result.append({query.value(0).toString(), query.value(1).toInt(), query.value(2).toInt()});
    return result;
}

bool BibleRepository::removeTranslation(const QString &translation) const
{
    QSqlDatabase db = QSqlDatabase::database();
    db.transaction();
    QSqlQuery verses;
    verses.prepare(QStringLiteral("DELETE FROM bible_verses WHERE translation = :t"));
    verses.bindValue(QStringLiteral(":t"), translation);
    QSqlQuery books;
    books.prepare(QStringLiteral("DELETE FROM bible_books WHERE translation = :t"));
    books.bindValue(QStringLiteral(":t"), translation);
    if (!verses.exec() || !books.exec()) {
        db.rollback();
        return false;
    }
    return db.commit();
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

QString BibleRepository::abbreviation(int bookNum, const QString &fullName)
{
    // Books 1–66 in the usual Protestant order.
    static const char *const ukrainian[] = {
        "Бут.", "Вих.", "Лев.", "Чис.", "Повт.", "ІсН.", "Суд.", "Рут", "1 Сам.", "2 Сам.", "1 Цар.", "2 Цар.", "1 Хр.", "2 Хр.",
        "Езд.", "Неєм.", "Ест.", "Йов", "Пс.", "Пр.", "Екл.", "Пісн.", "Іс.", "Єр.", "Плач", "Єз.", "Дан.", "Ос.", "Йоїл", "Ам.",
        "Овд.", "Йон.", "Мих.", "Наум", "Ав.", "Соф.", "Ог.", "Зах.", "Мал.", "Мт.", "Мр.", "Лк.", "Ів.", "Дії", "Рим.", "1 Кор.",
        "2 Кор.", "Гал.", "Еф.", "Флп.", "Кол.", "1 Сол.", "2 Сол.", "1 Тим.", "2 Тим.", "Тит", "Флм.", "Євр.", "Як.", "1 Пет.",
        "2 Пет.", "1 Ів.", "2 Ів.", "3 Ів.", "Юд.", "Об.",
    };
    static const char *const russian[] = {
        "Быт.", "Исх.", "Лев.", "Чис.", "Втор.", "Нав.", "Суд.", "Руфь", "1 Цар.", "2 Цар.", "3 Цар.", "4 Цар.", "1 Пар.", "2 Пар.",
        "Езд.", "Неем.", "Есф.", "Иов", "Пс.", "Притч.", "Еккл.", "Песн.", "Ис.", "Иер.", "Плач", "Иез.", "Дан.", "Ос.", "Иоил.", "Ам.",
        "Авд.", "Ион.", "Мих.", "Наум", "Авв.", "Соф.", "Агг.", "Зах.", "Мал.", "Мф.", "Мк.", "Лк.", "Ин.", "Деян.", "Рим.", "1 Кор.",
        "2 Кор.", "Гал.", "Еф.", "Флп.", "Кол.", "1 Фес.", "2 Фес.", "1 Тим.", "2 Тим.", "Тит", "Флм.", "Евр.", "Иак.", "1 Пет.",
        "2 Пет.", "1 Ин.", "2 Ин.", "3 Ин.", "Иуд.", "Откр.",
    };
    if (bookNum < 1 || bookNum > 66)
        return fullName;
    static const QRegularExpression ukrainianLetters(QStringLiteral("[іїєґІЇЄҐ']"));
    const bool isUkrainian = fullName.contains(ukrainianLetters) || fullName.startsWith(QStringLiteral("Від "));
    return QString::fromUtf8(isUkrainian ? ukrainian[bookNum - 1] : russian[bookNum - 1]);
}
