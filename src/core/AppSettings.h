#pragma once

#include <QKeySequence>
#include <QString>
#include <QStringList>
#include <QVariant>

// Every persisted preference from the "Настройки" screen, in one place:
// the QSettings key and its default. The settings screen edits these (and
// only writes them on "Сохранить"); the rest of the app reads them through
// value() so a missing key always falls back to the same default.
namespace AppSettings {

// ---- Общие ----
inline const QString Theme = QStringLiteral("appearance/theme");                  // "light" | "dark"
inline const QString Language = QStringLiteral("ui/language");                    // "ru" | "uk" | "en"
inline const QString StartupSection = QStringLiteral("startup/openSection");      // "last" | "songs" | "bible" | ...
inline const QString RememberItem = QStringLiteral("startup/rememberItem");       // bool
inline const QString AutosaveEnabled = QStringLiteral("autosave/enabled");        // bool
inline const QString AutosaveInterval = QStringLiteral("autosave/intervalSec");   // int seconds
inline const QString UiScale = QStringLiteral("ui/scalePercent");                 // 100 | 110 | 125 | 150
inline const QString LineStars = QStringLiteral("songs/lineStars");               // bool
inline const QString LineStarsGap = QStringLiteral("songs/lineStarsGap");         // % of a text line above "***" (0 = right under the words)
inline const QString AutoFullscreen = QStringLiteral("display/autoFullscreen");   // bool
inline const QString DefaultBackground = QStringLiteral("display/defaultBackground"); // "" | "builtin:<name>" | file path
inline const QString Alignment = QStringLiteral("display/alignment");             // "center" | "left" | "right"
inline const QString RememberDisplayWindow = QStringLiteral("display/rememberPreview"); // bool

// ---- Показ ----
inline const QString DisplayScreen = QStringLiteral("display/screen");            // "" = auto, else QScreen::name()
inline const QString DisplayResolution = QStringLiteral("display/resolution");    // "auto" | "1920x1080" | "1280x720"
inline const QString SongFont = QStringLiteral("display/song/fontFamily");
inline const QString SongScale = QStringLiteral("display/song/textScalePercent");
inline const QString TextShadow = QStringLiteral("display/textShadow");           // bool
inline const QString Transition = QStringLiteral("display/transition");           // "fade" | "none" | "slide"
inline const QString TransitionMs = QStringLiteral("display/transitionMs");       // 150 | 300 | 600
inline const QString ShowLogo = QStringLiteral("display/showLogo");               // bool
inline const QString LogoPath = QStringLiteral("display/logoPath");               // "" = Sermon logo
inline const QString IdleScreen = QStringLiteral("display/idleScreen");           // "logo" | "black" | "none"

// ---- Библия ----
inline const QString BibleFont = QStringLiteral("display/bible/fontFamily");
inline const QString BibleScale = QStringLiteral("display/bible/textScalePercent");
inline const QString BibleShowReference = QStringLiteral("display/showBibleReference"); // bool
inline const QString BibleTranslation = QStringLiteral("bible/translation");      // "" = first available
inline const QString BibleAltEnabled = QStringLiteral("bible/altEnabled");        // bool
inline const QString BibleAltTranslation = QStringLiteral("bible/altTranslation");
inline const QString BibleRefFormat = QStringLiteral("bible/refFormat");          // "full" | "short"
inline const QString BibleVerseNumbers = QStringLiteral("bible/verseNumbers");    // bool
inline const QString BibleSplitVerses = QStringLiteral("bible/splitVerses");      // bool
inline const QString BibleKeepHistory = QStringLiteral("bible/keepHistory");      // bool
inline const QString BibleRecentCount = QStringLiteral("bible/recentCount");      // int

// ---- Песни и сборники ----
inline const QString ActiveCollection = QStringLiteral("songs/activeCollection"); // "" = all
inline const QString DefaultCollection = QStringLiteral("songs/defaultCollection"); // songbook for new songs; "" = "Мои песни"
inline const QString ShowChords = QStringLiteral("songs/showChords");             // bool
inline const QString AutoChorus = QStringLiteral("songs/autoChorus");             // bool
inline const QString NumberVerses = QStringLiteral("songs/numberVerses");         // bool
inline const QString ShowVerseLabels = QStringLiteral("display/showVerseLabels"); // bool
inline const QString ImportFormat = QStringLiteral("songs/importFormat");         // "sps" | "cho" | "txt"
inline const QString ImportDir = QStringLiteral("songs/importDir");

// ---- База данных ----
inline const QString LastOptimize = QStringLiteral("db/lastOptimize");            // ISO date-time

// ---- Горячие клавиши ----
inline const QString KeyPause = QStringLiteral("hotkeys/pause");
inline const QString KeyNext = QStringLiteral("hotkeys/next");
inline const QString KeyPrev = QStringLiteral("hotkeys/prev");
inline const QString KeyGoLive = QStringLiteral("hotkeys/goLive");
inline const QString KeyEndShow = QStringLiteral("hotkeys/endShow");
inline const QString KeySearch = QStringLiteral("hotkeys/search");
inline const QString KeyAdd = QStringLiteral("hotkeys/add");

// ---- OBS и сеть ----
inline const QString ObsEnabled = QStringLiteral("obs/enabled");                  // bool
inline const QString ObsHost = QStringLiteral("obs/host");
inline const QString ObsPort = QStringLiteral("obs/port");                        // int
inline const QString ObsPassword = QStringLiteral("obs/password");
inline const QString ObsScene = QStringLiteral("obs/scene");                      // "" = don't switch

// ---- Резервная копия ----
inline const QString BackupAuto = QStringLiteral("backup/auto");                  // bool
inline const QString BackupFrequency = QStringLiteral("backup/frequency");        // "daily" | "weekly" | "manual"
inline const QString BackupKeep = QStringLiteral("backup/keep");                  // int
inline const QString BackupDir = QStringLiteral("backup/dir");                    // "" = <data>/backups
inline const QString BackupLast = QStringLiteral("backup/last");                  // ISO date-time
inline const QString BackupLastOk = QStringLiteral("backup/lastOk");              // bool

// ---- Внешний вид ----
inline const QString Density = QStringLiteral("appearance/density");              // "normal" | "compact"
inline const QString Rounded = QStringLiteral("appearance/rounded");              // bool
inline const QString UiFont = QStringLiteral("appearance/font");                  // font family

// ---- Remembered state (not on the settings screen) ----
inline const QString LastSection = QStringLiteral("state/lastSection");
inline const QString DisplayWindowWasOpen = QStringLiteral("state/displayWindowOpen");

QVariant defaultValue(const QString &key);
QVariant value(const QString &key);
void setValue(const QString &key, const QVariant &value);

QKeySequence shortcut(const QString &key);

// Keys whose change only takes effect after a restart (theme, language,
// scale, density, corners, interface font).
bool needsRestart(const QString &key);

// Where automatic/manual backups go.
QString backupDir();

} // namespace AppSettings
