#include "DisplaySettings.h"
#include "Theme.h"

#include <QFontDatabase>
#include <QSettings>

namespace DisplaySettings {

namespace {

QString profileKey(ContentType type)
{
    return type == ContentType::BibleVerse ? QStringLiteral("bible") : QStringLiteral("song");
}

QString keyFor(ContentType type, const QString &name)
{
    return QStringLiteral("display/%1/%2").arg(profileKey(type), name);
}

constexpr auto kAlignKey = "display/alignment";
constexpr auto kVerseLabelsKey = "display/showVerseLabels";
constexpr auto kBibleRefKey = "display/showBibleReference";

} // namespace

QStringList availableFonts()
{
    return {QStringLiteral("Montserrat"), QStringLiteral("Inter"), QStringLiteral("Segoe UI")};
}

QString fontFamily(ContentType type)
{
    const QString saved = QSettings().value(keyFor(type, QStringLiteral("fontFamily"))).toString();
    if (!saved.isEmpty() && QFontDatabase::families().contains(saved))
        return saved;
    return Theme::fontFamily();
}

void setFontFamily(ContentType type, const QString &family)
{
    QSettings().setValue(keyFor(type, QStringLiteral("fontFamily")), family);
}

QString preferredFontFamily(ContentType type)
{
    const QString saved = QSettings().value(keyFor(type, QStringLiteral("fontFamily"))).toString();
    return saved.isEmpty() ? availableFonts().constFirst() : saved;
}

int textScalePercent(ContentType type)
{
    const int saved = QSettings().value(keyFor(type, QStringLiteral("textScalePercent")), 100).toInt();
    return qBound(50, saved, 200);
}

void setTextScalePercent(ContentType type, int percent)
{
    QSettings().setValue(keyFor(type, QStringLiteral("textScalePercent")), qBound(50, percent, 200));
}

int fontPointSize(ContentType type, int referenceHeight)
{
    const double base = referenceHeight / 9.0;
    return qBound(6, qRound(base * textScalePercent(type) / 100.0), 140);
}

double cssFontSizeVw(ContentType type)
{
    return 6.0 * textScalePercent(type) / 100.0;
}

Alignment alignment()
{
    return QSettings().value(QLatin1String(kAlignKey)).toString() == QLatin1String("left")
        ? Alignment::Left : Alignment::Center;
}

void setAlignment(Alignment align)
{
    QSettings().setValue(QLatin1String(kAlignKey), align == Alignment::Left ? QStringLiteral("left") : QStringLiteral("center"));
}

Qt::Alignment qtAlignment()
{
    return alignment() == Alignment::Left ? (Qt::AlignLeft | Qt::AlignVCenter) : Qt::AlignCenter;
}

QString cssTextAlign()
{
    return alignment() == Alignment::Left ? QStringLiteral("left") : QStringLiteral("center");
}

bool showVerseLabels()
{
    return QSettings().value(QLatin1String(kVerseLabelsKey), false).toBool();
}

void setShowVerseLabels(bool show)
{
    QSettings().setValue(QLatin1String(kVerseLabelsKey), show);
}

bool showBibleReference()
{
    return QSettings().value(QLatin1String(kBibleRefKey), true).toBool();
}

void setShowBibleReference(bool show)
{
    QSettings().setValue(QLatin1String(kBibleRefKey), show);
}

void resetAll()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("display"));
    settings.remove(QString());
    settings.endGroup();
}

} // namespace DisplaySettings
