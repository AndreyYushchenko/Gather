#include "ContentItem.h"
#include "AppSettings.h"
#include "ui/DisplaySettings.h"

#include <QObject>
#include <QRegularExpression>

QString contentTypeToDbString(ContentType type)
{
    switch (type) {
    case ContentType::Song: return QStringLiteral("song");
    case ContentType::BibleVerse: return QStringLiteral("verse");
    case ContentType::Announcement: return QStringLiteral("announcement");
    case ContentType::Photo: return QStringLiteral("photo");
    case ContentType::Video: return QStringLiteral("video");
    }
    return {};
}

ContentType contentTypeFromDbString(const QString &value)
{
    if (value == QLatin1String("verse")) return ContentType::BibleVerse;
    if (value == QLatin1String("announcement")) return ContentType::Announcement;
    if (value == QLatin1String("photo")) return ContentType::Photo;
    if (value == QLatin1String("video")) return ContentType::Video;
    return ContentType::Song;
}

QString backgroundTypeToDbString(BackgroundType type)
{
    switch (type) {
    case BackgroundType::None: return QStringLiteral("none");
    case BackgroundType::Photo: return QStringLiteral("photo");
    case BackgroundType::Video: return QStringLiteral("video");
    }
    return {};
}

BackgroundType backgroundTypeFromDbString(const QString &value)
{
    if (value == QLatin1String("photo")) return BackgroundType::Photo;
    if (value == QLatin1String("video")) return BackgroundType::Video;
    return BackgroundType::None;
}

QString contentTypeDisplayName(ContentType type)
{
    switch (type) {
    case ContentType::Song: return QObject::tr("Песня");
    case ContentType::BibleVerse: return QObject::tr("Стих из Библии");
    case ContentType::Announcement: return QObject::tr("Объявление");
    case ContentType::Photo: return QObject::tr("Фото");
    case ContentType::Video: return QObject::tr("Видео");
    }
    return {};
}

QStringList ContentItem::slides() const
{
    QStringList result;
    const QStringList rawParagraphs = text.split(QRegularExpression(QStringLiteral("\\n\\s*\\n")));
    for (const QString &paragraph : rawParagraphs) {
        const QString trimmed = paragraph.trimmed();
        if (!trimmed.isEmpty())
            result << trimmed;
    }
    if (result.isEmpty() && !text.trimmed().isEmpty())
        result << text.trimmed();

    // "Автоматически определять припев" (Настройки → Песни и сборники).
    if (type != ContentType::Song || !AppSettings::value(AppSettings::AutoChorus).toBool())
        return result;

    // Songs written as "Куплет 1 / Припев / Куплет 2 / Куплет 3" only spell
    // the chorus out once, but it's meant to repeat after every verse — so
    // insert it after each "Куплет" slide that isn't already followed by it.
    const QString chorusLabel1 = QStringLiteral("Припев");
    const QString chorusLabel2 = QStringLiteral("Приспів");
    const QString verseLabel = QStringLiteral("Куплет");

    QString chorus;
    for (const QString &slide : result) {
        if (slide.startsWith(chorusLabel1, Qt::CaseInsensitive) || slide.startsWith(chorusLabel2, Qt::CaseInsensitive)) {
            chorus = slide;
            break;
        }
    }
    if (chorus.isEmpty())
        return result;

    QStringList withChorus;
    for (int i = 0; i < result.size(); ++i) {
        const QString &slide = result.at(i);
        withChorus << slide;
        if (slide.startsWith(verseLabel, Qt::CaseInsensitive)) {
            const bool nextIsChorus = (i + 1 < result.size()) && result.at(i + 1) == chorus;
            if (!nextIsChorus)
                withChorus << chorus;
        }
    }
    return withChorus;
}

QStringList ContentItem::presentationSlides() const
{
    QStringList pages = slides();
    if (type == ContentType::Song
        && !AppSettings::value(DisplaySettings::styleKey(type, QStringLiteral("separateChorus"))).toBool()) {
        QStringList combined;
        static const QRegularExpression chorus(QStringLiteral(R"(^\s*(Припев|Приспів|Chorus)[:.\s])"), QRegularExpression::CaseInsensitiveOption);
        for (const QString &page : pages) {
            if (!combined.isEmpty() && chorus.match(page).hasMatch()) combined.last() += QLatin1Char('\n') + page;
            else combined << page;
        }
        pages = combined;
    }
    return DisplaySettings::splitTextSlides(type, pages);
}

QString ContentItem::displayTitle() const
{
    switch (type) {
    case ContentType::BibleVerse:
        return refLocation.isEmpty() ? refBook : QStringLiteral("%1 %2").arg(refBook, refLocation);
    case ContentType::Song:
        // refLocation doubles as the song's number in its songbook/collection
        // (e.g. imported from a numbered hymnal) so operators can jump to it
        // by number during a service, same as they would in the printed book.
        return refLocation.isEmpty() ? title : QStringLiteral("№%1  %2").arg(refLocation, title);
    default:
        return title;
    }
}

namespace {

const QRegularExpression &chordPattern()
{
    // [C], [Am7], [F#m/C#], [Hsus4] … — not arbitrary bracketed words.
    static const QRegularExpression pattern(QStringLiteral(R"(\[([A-H][#b]?(?:m|maj|min|dim|aug|sus|add)?\d*(?:sus\d|add\d)?(?:/[A-H][#b]?)?)\])"));
    return pattern;
}

} // namespace

bool hasChords(const QString &text)
{
    return chordPattern().match(text).hasMatch();
}

QString stripChords(const QString &text)
{
    QString result = text;
    result.remove(chordPattern());
    return result;
}

QStringList songSlideLabels(const QStringList &slides)
{
    static const QRegularExpression verse(QStringLiteral(R"(^\s*(Куплет|Verse)\.?\s*(\d+)?)"),
                                          QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    static const QRegularExpression chorus(QStringLiteral(R"(^\s*(Припев|Приспів|Chorus))"),
                                           QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    static const QRegularExpression bridge(QStringLiteral(R"(^\s*(Бридж|Bridge))"),
                                           QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    QStringList labels;
    int verseNumber = 0;
    for (const QString &slide : slides) {
        const QString firstLine = slide.section(QLatin1Char('\n'), 0, 0);
        const QRegularExpressionMatch verseMatch = verse.match(firstLine);
        if (verseMatch.hasMatch()) {
            verseNumber = verseMatch.captured(2).isEmpty() ? verseNumber + 1 : verseMatch.captured(2).toInt();
            labels << QObject::tr("Куплет %1").arg(verseNumber);
        } else if (chorus.match(firstLine).hasMatch() || (slides.count(slide) > 1 && !slide.trimmed().isEmpty())) {
            labels << QObject::tr("Припев");
        } else if (bridge.match(firstLine).hasMatch()) {
            labels << QObject::tr("Бридж");
        } else if (slide.trimmed().isEmpty()) {
            labels << QString();
        } else {
            labels << QObject::tr("Куплет %1").arg(++verseNumber);
        }
    }
    return labels;
}
