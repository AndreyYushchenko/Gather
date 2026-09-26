#include "OldNameMigration.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>

namespace OldNameMigration {

namespace {

const QString OldName = QStringLiteral("Gather");

// Where Sermon keeps its library, and where Gather kept it (same parent).
QString newDataDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString oldDataDir()
{
    // <AppData>/Sermon/Sermon → <AppData>/Gather/Gather
    const QString appData = QFileInfo(QFileInfo(newDataDir()).path()).path();
    return appData + QLatin1Char('/') + OldName + QLatin1Char('/') + OldName;
}

QString renamed(QString name)
{
    return name.replace(QStringLiteral("gather"), QStringLiteral("sermon"));
}

bool copyTree(const QString &from, const QString &to)
{
    QDir().mkpath(to);
    QDirIterator it(from, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    bool ok = true;
    while (it.hasNext()) {
        const QString source = it.next();
        const QString target = to + QLatin1Char('/') + QDir(from).relativeFilePath(source);
        QDir().mkpath(QFileInfo(target).path());
        // A failed earlier attempt may have left this file behind; QFile::copy
        // never overwrites, so without this the retry would fail forever.
        QFile::remove(target);
        ok = QFile::copy(source, target) && ok;
    }
    return ok;
}

} // namespace

void migrateSettings()
{
    QSettings fresh;
    QSettings old(OldName, OldName);
    if (!fresh.allKeys().isEmpty() || old.allKeys().isEmpty())
        return;
    const QString oldDir = oldDataDir();
    const QString newDir = newDataDir();
    for (const QString &key : old.allKeys()) {
        QVariant value = old.value(key);
        // Paths into the old library folder (logo, backups, …) follow it.
        if (value.typeId() == QMetaType::QString)
            value = value.toString()
                        .replace(oldDir, newDir, Qt::CaseInsensitive)
                        .replace(QDir::toNativeSeparators(oldDir), QDir::toNativeSeparators(newDir), Qt::CaseInsensitive);
        fresh.setValue(key, value);
    }
    fresh.sync();
}

void migrateData()
{
    const QString newDir = newDataDir();
    const QString oldDir = oldDataDir();
    if (QFile::exists(newDir + QStringLiteral("/sermon.db")) || !QFile::exists(oldDir + QStringLiteral("/gather.db")))
        return;

    // Move the whole folder (photos, videos, backgrounds, backups) when
    // possible; copy it when Windows won't allow the move.
    QDir().mkpath(QFileInfo(newDir).path());
    QDir().rmdir(newDir); // only if it was created empty
    if (!QDir().rename(oldDir, newDir) && !copyTree(oldDir, newDir))
        return;
    QDir().rmdir(QFileInfo(oldDir).path()); // the empty "Gather" folder

    // gather.db → sermon.db, gather-before-restore-… → sermon-before-restore-…
    for (const QFileInfo &file : QDir(newDir).entryInfoList(QDir::Files))
        if (file.fileName().startsWith(QStringLiteral("gather")))
            QFile::rename(file.filePath(), file.path() + QLatin1Char('/') + renamed(file.fileName()));
    // Backup folders and the database copy inside each of them.
    const QString backups = newDir + QStringLiteral("/backups");
    for (const QFileInfo &dir : QDir(backups).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QString path = dir.filePath();
        if (dir.fileName().startsWith(QStringLiteral("gather"))) {
            const QString to = backups + QLatin1Char('/') + renamed(dir.fileName());
            if (QDir().rename(path, to))
                path = to;
        }
        QFile::rename(path + QStringLiteral("/gather.db"), path + QStringLiteral("/sermon.db"));
    }

    // Photos, backgrounds and covers are stored with their full path.
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("old-name-migration"));
        db.setDatabaseName(newDir + QStringLiteral("/sermon.db"));
        if (db.open()) {
            for (const auto &[from, to] : {std::pair{oldDir, newDir},
                                           std::pair{QDir::toNativeSeparators(oldDir), QDir::toNativeSeparators(newDir)}}) {
                const QStringList statements{
                    QStringLiteral("UPDATE items SET image_path = REPLACE(image_path, :from, :to), "
                                   "background_path = REPLACE(background_path, :from, :to)"),
                    QStringLiteral("UPDATE playlists SET cover_path = REPLACE(cover_path, :from, :to)"),
                };
                for (const QString &statement : statements) {
                    QSqlQuery query(db);
                    query.prepare(statement);
                    query.bindValue(QStringLiteral(":from"), from);
                    query.bindValue(QStringLiteral(":to"), to);
                    query.exec();
                }
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(QStringLiteral("old-name-migration"));
}

} // namespace OldNameMigration
