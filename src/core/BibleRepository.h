#pragma once

#include <QList>
#include <QString>
#include <QStringList>

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
    // The "Основной перевод" setting when it's installed, else the first
    // one; empty if no translation has been imported yet.
    QString defaultTranslation() const;
    QStringList translations() const;
    struct TranslationInfo { QString name; int books = 0; int verses = 0; };
    QList<TranslationInfo> translationInfos() const;
    bool removeTranslation(const QString &translation) const;

    QList<BibleBook> books(const QString &translation) const;
    int verseCount(const QString &translation, int bookNum, int chapter) const;
    QList<BibleVerse> verses(const QString &translation, int bookNum, int chapter, int fromVerse, int toVerse) const;
    QList<BibleSearchHit> search(const QString &translation, const QString &query, int limit = 100) const;

    // Short book name for "Формат ссылки → Сокращение" (Ukrainian or
    // Russian style, picked from the full name's spelling).
    static QString abbreviation(int bookNum, const QString &fullName);
};
