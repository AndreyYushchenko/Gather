#pragma once

#include <QString>

// Opens (creating if needed) the local SQLite database and makes sure the
// schema exists. The app keeps a single default QSqlDatabase connection.
class Database {
public:
    static bool open();
    static QString dataDir();
    static QString photosDir();
    static QString backgroundsDir();
    static QString videosDir();

private:
    static bool ensureSchema();
};
