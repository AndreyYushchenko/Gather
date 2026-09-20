#pragma once

#include <QList>
#include <QString>

struct BibleBook {
    int num = 0;
    QString name;
    int chapterCount = 0;
};

struct BibleVerse {
    int verse = 0;
    QString text;
};

struct BibleSearchHit {
    int bookNum = 0;
    QString bookName;
    int chapter = 0;
    int verse = 0;
    QString text;
};

// Read-only access to the bible_books/bible_verses tables (seeded via
// scripts/import_bible.py from a .spb translation dump).
class BibleRepository {
public:
    // Empty string if no translation has been imported yet.
    QString defaultTranslation() const;

    QList<BibleBook> books(const QString &translation) const;
    int totalVerseCount(const QString &translation) const;
    int verseCount(const QString &translation, int bookNum, int chapter) const;
    QList<BibleVerse> verses(const QString &translation, int bookNum, int chapter, int fromVerse, int toVerse) const;
    QList<BibleSearchHit> search(const QString &translation, const QString &query, int limit = 100) const;
};
