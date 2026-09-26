#pragma once

#include <QString>

// Настройки → Резервная копия / База данных: backups of the library
// (sermon.db + photos + slide backgrounds) into dated folders, the
// automatic schedule, restoring, and database optimisation.
namespace BackupManager {

// Makes "sermon-backup-…" (manual) or "sermon-autobackup-…" (automatic) in
// AppSettings::backupDir(). Records the date/result for the settings page.
bool createBackup(bool automatic, QString *resultPath, QString *error);

// Automatic backups beyond "Хранить копий" are deleted, oldest first.
void pruneAutomaticBackups();

// Whether the schedule ("Ежедневно"/"Еженедельно") says one is due now.
bool automaticBackupDue();

// Replaces the library with the one in `dbPath` (a sermon.db from a
// backup folder; its photos/ and backgrounds/ next to it come along). The
// app must be restarted afterwards.
bool restore(const QString &dbPath, QString *error);

// VACUUM + ANALYZE. Returns the size saved in bytes (may be 0).
qint64 optimizeDatabase(QString *error);

} // namespace BackupManager
