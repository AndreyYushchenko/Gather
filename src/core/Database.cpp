#include "Database.h"

#include <QDir>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QDebug>

QString Database::dataDir()
{
    const QString isolated = qEnvironmentVariable("SERMON_PROFILE_DIR");
    const QString dir = !qEnvironmentVariable("SERMON_PROFILE").isEmpty() && !isolated.isEmpty()
        ? isolated : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir;
}

QString Database::photosDir()
{
    const QString dir = dataDir() + QStringLiteral("/photos");
    QDir().mkpath(dir);
    return dir;
}

QString Database::backgroundsDir()
{
    const QString dir = dataDir() + QStringLiteral("/backgrounds");
    QDir().mkpath(dir);
    return dir;
}

QString Database::videosDir()
{
    const QString dir = dataDir() + QStringLiteral("/videos");
    QDir().mkpath(dir);
    return dir;
}

bool Database::open()
{
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"));
    db.setDatabaseName(dataDir() + QStringLiteral("/sermon.db"));

    if (!db.open()) {
        qWarning() << "Failed to open database:" << db.lastError().text();
        return false;
    }

    return ensureSchema();
}

bool Database::ensureSchema()
{
    QSqlQuery query;
    const bool ok = query.exec(QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS items (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            type TEXT NOT NULL,
            title TEXT NOT NULL DEFAULT '',
            text TEXT NOT NULL DEFAULT '',
            ref_book TEXT NOT NULL DEFAULT '',
            ref_location TEXT NOT NULL DEFAULT '',
            expiry_date TEXT,
            image_path TEXT NOT NULL DEFAULT '',
            caption TEXT NOT NULL DEFAULT '',
            favorite INTEGER NOT NULL DEFAULT 0,
            notes TEXT NOT NULL DEFAULT '',
            background_type TEXT NOT NULL DEFAULT 'none',
            background_path TEXT NOT NULL DEFAULT '',
            created_at TEXT NOT NULL
        )
    )"));

    if (!ok) {
        qWarning() << "Failed to create schema:" << query.lastError().text();
        return false;
    }

    // Added after the initial release; add them for databases created earlier.
    QSet<QString> existingColumns;
    QSqlQuery info(QStringLiteral("PRAGMA table_info(items)"));
    while (info.next())
        existingColumns.insert(info.value(QStringLiteral("name")).toString());

    if (!existingColumns.contains(QStringLiteral("favorite")))
        query.exec(QStringLiteral("ALTER TABLE items ADD COLUMN favorite INTEGER NOT NULL DEFAULT 0"));
    if (!existingColumns.contains(QStringLiteral("notes")))
        query.exec(QStringLiteral("ALTER TABLE items ADD COLUMN notes TEXT NOT NULL DEFAULT ''"));
    if (!existingColumns.contains(QStringLiteral("background_type")))
        query.exec(QStringLiteral("ALTER TABLE items ADD COLUMN background_type TEXT NOT NULL DEFAULT 'none'"));
    if (!existingColumns.contains(QStringLiteral("background_path")))
        query.exec(QStringLiteral("ALTER TABLE items ADD COLUMN background_path TEXT NOT NULL DEFAULT ''"));

    if (!existingColumns.contains(QStringLiteral("style")))
        query.exec(QStringLiteral("ALTER TABLE items ADD COLUMN style TEXT NOT NULL DEFAULT ''"));

    query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_items_type ON items(type)"));
    query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_items_title ON items(title)"));

    // Bible text (book/chapter/verse), seeded separately via
    // scripts/import_bible.py from a .spb translation dump.
    query.exec(QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS bible_books (
            translation TEXT NOT NULL,
            book_num INTEGER NOT NULL,
            book_name TEXT NOT NULL,
            chapter_count INTEGER NOT NULL,
            PRIMARY KEY (translation, book_num)
        )
    )"));
    query.exec(QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS bible_verses (
            translation TEXT NOT NULL,
            book_num INTEGER NOT NULL,
            chapter INTEGER NOT NULL,
            verse INTEGER NOT NULL,
            text TEXT NOT NULL,
            PRIMARY KEY (translation, book_num, chapter, verse)
        )
    )"));
    query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_bible_verses_lookup ON bible_verses(translation, book_num, chapter)"));

    // Playlists: an ordered sequence of existing library items for a service.
    query.exec(QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS playlists (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            favorite INTEGER NOT NULL DEFAULT 0,
            created_at TEXT NOT NULL
        )
    )"));
    query.exec(QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS playlist_items (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            playlist_id INTEGER NOT NULL REFERENCES playlists(id) ON DELETE CASCADE,
            item_id INTEGER NOT NULL REFERENCES items(id) ON DELETE CASCADE,
            position INTEGER NOT NULL
        )
    )"));
    query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_playlist_items_playlist ON playlist_items(playlist_id, position)"));

    // Songs imported from a numbered songbook before collections existed
    // join the default one, so "Активный сборник" has something to pick.
    query.exec(QStringLiteral("UPDATE items SET ref_book = 'Основной сборник' "
                              "WHERE type = 'song' AND ref_book = '' AND ref_location <> ''"));

    // Added with the redesigned Playlists screen.
    const auto addColumn = [](const QString &table, const QString &column, const QString &definition) {
        QSqlQuery columns(QStringLiteral("PRAGMA table_info(%1)").arg(table));
        while (columns.next()) {
            if (columns.value(QStringLiteral("name")).toString() == column)
                return;
        }
        QSqlQuery().exec(QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 %3").arg(table, column, definition));
    };
    addColumn(QStringLiteral("playlists"), QStringLiteral("description"), QStringLiteral("TEXT NOT NULL DEFAULT ''"));
    addColumn(QStringLiteral("playlists"), QStringLiteral("cover_path"), QStringLiteral("TEXT NOT NULL DEFAULT ''"));
    // Planned length of one entry in seconds; 0 = not set (a video's own
    // length is used instead).
    addColumn(QStringLiteral("playlist_items"), QStringLiteral("duration_sec"), QStringLiteral("INTEGER NOT NULL DEFAULT 0"));

    return true;
}
