#include "BackupManager.h"
#include "AppSettings.h"
#include "Database.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

namespace BackupManager {

namespace {

bool copyDir(const QString &source, const QString &destination)
{
    if (!QDir(source).exists())
        return true;
    QDir().mkpath(destination);
    QDirIterator it(source, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        const QString target = destination + QLatin1Char('/') + QDir(source).relativeFilePath(file);
        QDir().mkpath(QFileInfo(target).path());
        QFile::remove(target);
        if (!QFile::copy(file, target))
            return false;
    }
    return true;
}

void record(bool ok)
{
    AppSettings::setValue(AppSettings::BackupLast, QDateTime::currentDateTime().toString(Qt::ISODate));
    AppSettings::setValue(AppSettings::BackupLastOk, ok);
}

} // namespace

bool createBackup(bool automatic, QString *resultPath, QString *error)
{
    const QString root = AppSettings::backupDir();
    const QString name = (automatic ? QStringLiteral("sermon-autobackup-") : QStringLiteral("sermon-backup-"))
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString dir = root + QLatin1Char('/') + name;
    if (!QDir().mkpath(dir)) {
        if (error)
            *error = QObject::tr("Не удалось создать папку %1").arg(QDir::toNativeSeparators(dir));
        record(false);
        return false;
    }

    // VACUUM INTO writes a consistent copy even while the app has the
    // database open.
    QSqlQuery vacuum;
    const QString dbCopy = dir + QStringLiteral("/sermon.db");
    if (!vacuum.exec(QStringLiteral("VACUUM INTO '%1'").arg(QString(dbCopy).replace(QLatin1Char('\''), QStringLiteral("''"))))) {
        if (error)
            *error = vacuum.lastError().text();
        record(false);
        return false;
    }
    const bool files = copyDir(Database::photosDir(), dir + QStringLiteral("/photos"))
        && copyDir(Database::backgroundsDir(), dir + QStringLiteral("/backgrounds"));
    if (!files && error)
        *error = QObject::tr("База сохранена, но не все фото/фоны удалось скопировать.");
    record(files);
    if (resultPath)
        *resultPath = dir;
    if (automatic)
        pruneAutomaticBackups();
    return files;
}

void pruneAutomaticBackups()
{
    const int keep = qMax(1, AppSettings::value(AppSettings::BackupKeep).toInt());
    QDir root(AppSettings::backupDir());
    const QStringList autos = root.entryList({QStringLiteral("sermon-autobackup-*")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
    for (int i = keep; i < autos.size(); ++i)
        QDir(root.filePath(autos.at(i))).removeRecursively();
}

bool automaticBackupDue()
{
    if (!AppSettings::value(AppSettings::BackupAuto).toBool())
        return false;
    const QString frequency = AppSettings::value(AppSettings::BackupFrequency).toString();
    if (frequency == QLatin1String("manual"))
        return false;
    const QDateTime last = QDateTime::fromString(AppSettings::value(AppSettings::BackupLast).toString(), Qt::ISODate);
    if (!last.isValid())
        return true;
    const qint64 days = last.daysTo(QDateTime::currentDateTime());
    return frequency == QLatin1String("weekly") ? days >= 7 : days >= 1;
}

bool restore(const QString &dbPath, QString *error)
{
    const QFileInfo source(dbPath);
    if (!source.exists()) {
        if (error)
            *error = QObject::tr("Файл не найден.");
        return false;
    }
    const QString target = Database::dataDir() + QStringLiteral("/sermon.db");
    // Keep the current library next to it, just in case.
    const QString safety = Database::dataDir() + QStringLiteral("/sermon-before-restore-")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")) + QStringLiteral(".db");
    {
        QSqlDatabase::database().close();
    }
    QFile::copy(target, safety);
    QFile::remove(target);
    if (!QFile::copy(source.absoluteFilePath(), target)) {
        QFile::copy(safety, target);
        if (error)
            *error = QObject::tr("Не удалось скопировать базу данных.");
        Database::open();
        return false;
    }
    copyDir(source.absolutePath() + QStringLiteral("/photos"), Database::photosDir());
    copyDir(source.absolutePath() + QStringLiteral("/backgrounds"), Database::backgroundsDir());
    return Database::open();
}

qint64 optimizeDatabase(QString *error)
{
    const QString path = Database::dataDir() + QStringLiteral("/sermon.db");
    const qint64 before = QFileInfo(path).size();
    QSqlQuery query;
    if (!query.exec(QStringLiteral("VACUUM")) || !query.exec(QStringLiteral("ANALYZE"))) {
        if (error)
            *error = query.lastError().text();
        return -1;
    }
    AppSettings::setValue(AppSettings::LastOptimize, QDateTime::currentDateTime().toString(Qt::ISODate));
    return qMax<qint64>(0, before - QFileInfo(path).size());
}

} // namespace BackupManager
