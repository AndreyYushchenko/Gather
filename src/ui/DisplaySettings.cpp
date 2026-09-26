#include "DisplaySettings.h"
#include "Theme.h"
#include "core/AppSettings.h"
#include "core/Database.h"

#include <QFile>
#include <QFileInfo>

#include <QFontDatabase>
#include <QSettings>
#include <QFont>
#include <QTextLayout>
#include <QRegularExpression>

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

// QFontDatabase::families() builds the whole list on every call, and the
// text style is read for every slide shown.
bool fontInstalled(const QString &family)
{
    static const QStringList families = QFontDatabase::families();
    return families.contains(family);
}

} // namespace

QStringList availableFonts()
{
    return {QStringLiteral("Montserrat"), QStringLiteral("Inter"), QStringLiteral("Segoe UI")};
}

QString styleKey(ContentType type, const QString &field)
{
    // The older song font was also used by announcements. V2 keeps its own
    // font so changing songs cannot restyle an announcement already on air.
    if (type == ContentType::Song && field == QLatin1String("fontFamily"))
        return keyFor(type, QStringLiteral("presentationFontFamily"));
    return keyFor(type, field);
}

TextStyle textStyle(ContentType type, const std::function<QVariant(const QString &)> &reader)
{
    const auto read = [&](const QString &field) { return reader ? reader(styleKey(type, field)) : AppSettings::value(styleKey(type, field)); };
    TextStyle s;
    s.bible = type == ContentType::BibleVerse;
    s.family = read(QStringLiteral("fontFamily")).toString();
    if (!fontInstalled(s.family)) s.family = Theme::fontFamily();
    s.fontSize = qBound(16, read(QStringLiteral("fontSize")).toInt(), 200);
    s.weight = qBound(100, read(QStringLiteral("fontWeight")).toInt(), 900);
    s.letterSpacing = qBound(-5.0, read(QStringLiteral("letterSpacing")).toDouble(), 30.0);
    s.lineHeight = qBound(80, read(QStringLiteral("lineHeight")).toInt(), 250);
    s.textCase = read(QStringLiteral("textCase")).toString();
    s.alignment = read(QStringLiteral("alignment")).toString();
    s.maxLines = qBound(1, read(QStringLiteral("maxLines")).toInt(), 20);
    s.margin = qBound(0, read(QStringLiteral("margin")).toInt(), 25);
    s.maxWidth = qBound(30, read(QStringLiteral("maxWidth")).toInt(), 100);
    s.autoFit = read(QStringLiteral("autoFit")).toBool();
    s.wordWrap = read(QStringLiteral("wordWrap")).toBool();
    s.shadow = read(QStringLiteral("shadow")).toBool();
    s.shadowOpacity = qBound(0, read(QStringLiteral("shadowOpacity")).toInt(), 100);
    s.outline = read(QStringLiteral("outline")).toBool();
    s.outlineWidth = qBound(1, read(QStringLiteral("outlineWidth")).toInt(), 10);
    s.dim = read(QStringLiteral("dim")).toBool();
    s.dimOpacity = qBound(0, read(QStringLiteral("dimOpacity")).toInt(), 100);
    return s;
}

QString transformText(const QString &text, const TextStyle &style)
{
    if (style.textCase == QLatin1String("upper")) return text.toUpper();
    if (style.textCase == QLatin1String("lower")) return text.toLower();
    return text;
}

QStringList splitTextSlides(ContentType type, const QStringList &slides)
{
    if ((type != ContentType::Song && type != ContentType::BibleVerse)
        || !AppSettings::value(styleKey(type, QStringLiteral("splitLong"))).toBool()) return slides;
    const TextStyle style = textStyle(type);
    QFont font(style.family);
    font.setPixelSize(style.fontSize);
    font.setWeight(QFont::Weight(style.weight));
    if (style.textCase == QLatin1String("upper")) font.setCapitalization(QFont::AllUppercase);
    if (style.textCase == QLatin1String("lower")) font.setCapitalization(QFont::AllLowercase);
    font.setLetterSpacing(style.bible ? QFont::PercentageSpacing : QFont::AbsoluteSpacing,
                          style.bible ? 100 + style.letterSpacing : style.letterSpacing);
    const qreal width = 1920.0 * qMin(style.maxWidth, 100 - 2 * style.margin) / 100;
    static const QRegularExpression label(QStringLiteral(R"(^\s*(Куплет|Припев|Приспів|Verse|Chorus|Bridge)\.?\s*\d*[:.]?\s*$)"), QRegularExpression::CaseInsensitiveOption);
    // Splits `lines` into as few slides as fit maxLines, all about the same
    // length (8 lines at max 6 → 4 + 4, not 6 + 2).
    const auto splitEvenly = [&style](const QStringList &lines, const QString &heading, QStringList &out) {
        const int parts = (int(lines.size()) + style.maxLines - 1) / style.maxLines;
        const int perPart = (int(lines.size()) + parts - 1) / parts;
        for (int i = 0; i < lines.size(); i += perPart)
            out << (heading.isEmpty() ? QString() : heading + QLatin1Char('\n')) + lines.mid(i, perPart).join(QLatin1Char('\n'));
    };
    QStringList result;
    for (const QString &slide : slides) {
        QStringList lines = slide.split(QLatin1Char('\n'));
        QString heading;
        if (type == ContentType::Song && !lines.isEmpty() && label.match(lines.first()).hasMatch()) heading = lines.takeFirst();
        // A song keeps its own line breaks, exactly as in the songbook: it is
        // only ever split between lines, never inside one. (Splitting at the
        // wrap points of the set font size used to cut lines into fragments
        // and save them as separate lines — the text ended up in a narrow
        // column.) The renderer shrinks the font so every line fits whole.
        if (type == ContentType::Song) {
            if (lines.size() <= style.maxLines)
                result << slide;
            else
                splitEvenly(lines, heading, result);
            continue;
        }
        QStringList visualLines;
        for (const QString &line : lines) {
            if (!style.wordWrap || line.isEmpty()) { visualLines << line; continue; }
            QTextLayout layout(line, font);
            QTextOption option;
            option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
            layout.setTextOption(option);
            layout.beginLayout();
            while (true) {
                QTextLine textLine = layout.createLine();
                if (!textLine.isValid()) break;
                textLine.setLineWidth(width);
                visualLines << layout.text().mid(textLine.textStart(), textLine.textLength()).trimmed();
            }
            layout.endLayout();
        }
        // Preserve original source text until a split is actually necessary.
        if (visualLines.size() <= style.maxLines) { result << slide; continue; }
        splitEvenly(visualLines, heading, result);
    }
    return result;
}

QString fontFamily(ContentType type)
{
    const QString saved = QSettings().value(keyFor(type, QStringLiteral("fontFamily"))).toString();
    if (!saved.isEmpty() && fontInstalled(saved))
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
    const QString saved = QSettings().value(QLatin1String(kAlignKey)).toString();
    return saved == QLatin1String("left") ? Alignment::Left : saved == QLatin1String("right") ? Alignment::Right : Alignment::Center;
}

void setAlignment(Alignment align)
{
    QSettings().setValue(QLatin1String(kAlignKey), align == Alignment::Left ? QStringLiteral("left")
                                                   : align == Alignment::Right ? QStringLiteral("right") : QStringLiteral("center"));
}

Qt::Alignment qtAlignment()
{
    switch (alignment()) {
    case Alignment::Left: return Qt::AlignLeft | Qt::AlignVCenter;
    case Alignment::Right: return Qt::AlignRight | Qt::AlignVCenter;
    case Alignment::Center: break;
    }
    return Qt::AlignCenter;
}

QString cssTextAlign()
{
    switch (alignment()) {
    case Alignment::Left: return QStringLiteral("left");
    case Alignment::Right: return QStringLiteral("right");
    case Alignment::Center: break;
    }
    return QStringLiteral("center");
}

namespace {

// Built-in background → resource; videos also need a real file.
QString builtinPath(const QString &name, bool *isVideo)
{
    if (name == QLatin1String("worship")) {
        *isVideo = false;
        return QStringLiteral(":/backgrounds/worship.png");
    }
    if (name == QLatin1String("mountains") || name == QLatin1String("church_hall")) {
        *isVideo = false;
        return QStringLiteral(":/backgrounds/%1.jpg").arg(name);
    }
    if (name == QLatin1String("blue_waves") || name == QLatin1String("warm_glow")) {
        *isVideo = true;
        // Same file the timer screens use (TimerFormat::backgroundVideoPath).
        const QString file = name + QStringLiteral(".mp4");
        const QString path = Database::backgroundsDir() + QStringLiteral("/builtin-loop2-") + file;
        if (!QFile::exists(path)) {
            QFile::copy(QStringLiteral(":/backgrounds/") + file, path);
            QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser);
        }
        return path;
    }
    return QString();
}

bool isVideoFile(const QString &path)
{
    static const QStringList videos = {QStringLiteral("mp4"), QStringLiteral("mov"), QStringLiteral("mkv"), QStringLiteral("webm"),
                                       QStringLiteral("avi"), QStringLiteral("m4v"), QStringLiteral("wmv")};
    return videos.contains(QFileInfo(path).suffix().toLower());
}

} // namespace

BackgroundType defaultBackgroundType()
{
    return backgroundType(AppSettings::value(AppSettings::DefaultBackground).toString());
}

BackgroundType backgroundType(const QString &value)
{
    if (value.isEmpty())
        return BackgroundType::None;
    if (value.startsWith(QLatin1String("builtin:"))) {
        bool video = false;
        return builtinPath(value.mid(8), &video).isEmpty() ? BackgroundType::None : video ? BackgroundType::Video : BackgroundType::Photo;
    }
    if (!QFile::exists(value))
        return BackgroundType::None;
    return isVideoFile(value) ? BackgroundType::Video : BackgroundType::Photo;
}

QString defaultBackgroundPath()
{
    return backgroundPath(AppSettings::value(AppSettings::DefaultBackground).toString());
}

QString backgroundPath(const QString &value)
{
    if (value.startsWith(QLatin1String("builtin:"))) {
        bool video = false;
        return builtinPath(value.mid(8), &video);
    }
    return value;
}

bool textShadow()
{
    return AppSettings::value(AppSettings::TextShadow).toBool();
}

QString transition()
{
    return AppSettings::value(AppSettings::Transition).toString();
}

int transitionMs()
{
    return qBound(50, AppSettings::value(AppSettings::TransitionMs).toInt(), 2000);
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

} // namespace DisplaySettings
