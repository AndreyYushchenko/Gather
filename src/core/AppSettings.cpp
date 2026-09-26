#include "AppSettings.h"
#include "Database.h"

#include <QHash>
#include <QSettings>

namespace AppSettings {

namespace {

const QHash<QString, QVariant> &defaults()
{
    static const QHash<QString, QVariant> table = {
        {Theme, QStringLiteral("light")},
        {Language, QStringLiteral("ru")},
        {StartupSection, QStringLiteral("last")},
        {RememberItem, true},
        {AutosaveEnabled, true},
        {AutosaveInterval, 60},
        {UiScale, 100},
        {LineStars, false},
        {LineStarsGap, 0},
        {AutoFullscreen, true},
        {DefaultBackground, QString()},
        {Alignment, QStringLiteral("center")},
        {RememberDisplayWindow, true},

        {DisplayScreen, QString()},
        {DisplayResolution, QStringLiteral("auto")},
        {SongFont, QStringLiteral("Montserrat")},
        {SongScale, 100},
        {TextShadow, true},
        {Transition, QStringLiteral("fade")},
        {TransitionMs, 300},
        {ShowLogo, true},
        {LogoPath, QString()},
        {IdleScreen, QStringLiteral("logo")},

        {BibleFont, QStringLiteral("Segoe UI")},
        {BibleScale, 100},
        {BibleShowReference, true},
        {BibleTranslation, QString()},
        {BibleAltEnabled, false},
        {BibleAltTranslation, QString()},
        {BibleRefFormat, QStringLiteral("full")},
        {BibleVerseNumbers, true},
        {BibleSplitVerses, true},
        {BibleKeepHistory, true},
        {BibleRecentCount, 10},

        {ActiveCollection, QString()},
        {DefaultCollection, QString()},
        {ShowChords, false},
        {AutoChorus, true},
        {NumberVerses, true},
        {ShowVerseLabels, false},
        {ImportFormat, QStringLiteral("sps")},
        {ImportDir, QString()},

        {LastOptimize, QString()},

        {KeyPause, QStringLiteral("Space")},
        {KeyNext, QStringLiteral("Right")},
        {KeyPrev, QStringLiteral("Left")},
        {KeyGoLive, QStringLiteral("F5")},
        {KeyEndShow, QStringLiteral("Esc")},
        {KeySearch, QStringLiteral("Ctrl+F")},
        {KeyAdd, QStringLiteral("Ctrl+N")},

        {ObsEnabled, false},
        {ObsHost, QStringLiteral("localhost")},
        {ObsPort, 4455},
        {ObsPassword, QString()},
        {ObsScene, QString()},

        {BackupAuto, true},
        {BackupFrequency, QStringLiteral("daily")},
        {BackupKeep, 7},
        {BackupDir, QString()},
        {BackupLast, QString()},
        {BackupLastOk, false},

        {Density, QStringLiteral("normal")},
        {Rounded, true},
        {UiFont, QStringLiteral("Inter")},

        {LastSection, QStringLiteral("songs")},
        {DisplayWindowWasOpen, false},
    };
    return table;
}

} // namespace

QVariant defaultValue(const QString &key)
{
    // V2 has independent presentation profiles. The legacy font/scale keys
    // remain valid, so existing installations keep their chosen typefaces.
    if (key.startsWith(QStringLiteral("display/song/")) || key.startsWith(QStringLiteral("display/bible/"))) {
        const bool bible = key.startsWith(QStringLiteral("display/bible/"));
        const QHash<QString, QVariant> textDefaults = {
            {QStringLiteral("presentationFontFamily"), QStringLiteral("Arial")},
            {QStringLiteral("fontSize"), 72}, {QStringLiteral("fontWeight"), bible ? 400 : 600},
            {QStringLiteral("letterSpacing"), 0.0},
            {QStringLiteral("lineHeight"), 120}, {QStringLiteral("textCase"), QStringLiteral("original")},
            {QStringLiteral("alignment"), QStringLiteral("center")}, {QStringLiteral("maxLines"), 4},
            {QStringLiteral("margin"), bible ? 5 : 8}, {QStringLiteral("maxWidth"), bible ? 80 : 90},
            {QStringLiteral("autoFit"), true}, {QStringLiteral("wordWrap"), true},
            {QStringLiteral("splitLong"), true}, {QStringLiteral("separateChorus"), true},
            {QStringLiteral("separateVerseNumber"), false}, {QStringLiteral("shadow"), true},
            {QStringLiteral("shadowOpacity"), bible ? 50 : 60}, {QStringLiteral("outline"), false},
            {QStringLiteral("outlineWidth"), 2}, {QStringLiteral("dim"), true},
            {QStringLiteral("dimOpacity"), 40},
            {QStringLiteral("background"), bible ? QStringLiteral("builtin:mountains") : QStringLiteral("builtin:worship")},
        };
        const QString field = key.section(QLatin1Char('/'), -1);
        if (textDefaults.contains(field))
            return textDefaults.value(field);
    }
    return defaults().value(key);
}

QVariant value(const QString &key)
{
    const QSettings settings;
    if (!settings.contains(key) && (key.startsWith(QStringLiteral("display/song/")) || key.startsWith(QStringLiteral("display/bible/")))) {
        const QString field = key.section(QLatin1Char('/'), -1);
        const QString oldKey = field == QLatin1String("alignment") ? Alignment
            : field == QLatin1String("shadow") ? TextShadow
            : field == QLatin1String("presentationFontFamily") ? SongFont
            : field == QLatin1String("background") ? DefaultBackground : QString();
        if (!oldKey.isEmpty() && settings.contains(oldKey))
            return settings.value(oldKey);
        if (field == QLatin1String("fontSize")) {
            const QString scaleKey = key.startsWith(QStringLiteral("display/bible/")) ? BibleScale : SongScale;
            return qRound(72 * settings.value(scaleKey, 100).toDouble() / 100.0);
        }
    }
    return QSettings().value(key, defaultValue(key));
}

void setValue(const QString &key, const QVariant &value)
{
    QSettings().setValue(key, value);
}

QKeySequence shortcut(const QString &key)
{
    return QKeySequence::fromString(value(key).toString(), QKeySequence::PortableText);
}

bool needsRestart(const QString &key)
{
    return key == Theme || key == Language || key == UiScale || key == Density || key == Rounded || key == UiFont;
}

QString backupDir()
{
    const QString chosen = value(BackupDir).toString();
    return chosen.isEmpty() ? Database::dataDir() + QStringLiteral("/backups") : chosen;
}

} // namespace AppSettings
