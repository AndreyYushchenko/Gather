#pragma once

// Sermon was called "Gather" before. Its settings (registry "Gather\Gather")
// and library folder (AppData "Gather/Gather", gather.db) are taken over once,
// the first time Sermon starts, so nothing is lost with the new name.
namespace OldNameMigration {

// Settings: before QApplication exists (the UI scale is read that early).
void migrateSettings();
// Library folder, database file name and the file paths stored in it:
// after QApplication exists (the SQLite driver needs it), before the
// database is opened.
void migrateData();

} // namespace OldNameMigration
