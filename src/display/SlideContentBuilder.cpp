#include "SlideContentBuilder.h"
#include "core/AppSettings.h"
#include "ui/DisplaySettings.h"

#include <QRegularExpression>

namespace {

// Matches a line that's just a stanza label ("Куплет 1.", "Приспів:",
// "Припев", "Chorus", ...) with nothing else on it, as found verbatim in
// imported songbook text (see MainWindow's .sps/.spb parser).
const QRegularExpression &verseLabelPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(^\s*(Куплет|Приспів|Припев|Verse|Chorus|Bridge)\.?:?\s*\d*\.?\s*$)"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

QString stripLeadingVerseLabel(const QString &slideText)
{
    QStringList lines = slideText.split(QLatin1Char('\n'));
    lines.removeIf([](const QString &line) { return verseLabelPattern().match(line).hasMatch(); });
    return lines.join(QLatin1Char('\n')).trimmed();
}

} // namespace

SlideContent buildSlideContent(const ContentItem &item, const QStringList &slides, int slideIndex, bool loopVideo)
{
    SlideContent content;
    content.sourceType = item.type;

    if (item.type == ContentType::Photo) {
        content.kind = SlideKind::Photo;
        content.imagePath = item.imagePath;
        return content;
    }

    if (item.type == ContentType::Video) {
        content.kind = SlideKind::Video;
        content.imagePath = item.imagePath;
        content.videoLoop = loopVideo;
        return content;
    }

    content.kind = SlideKind::Text;
    QString text = slides.value(slideIndex);

    // A song slide's text as it goes on screen (labels, chords, "***").
    const bool showLabels = DisplaySettings::showVerseLabels();
    const bool showChords = AppSettings::value(AppSettings::ShowChords).toBool();
    const bool lineStars = AppSettings::value(AppSettings::LineStars).toBool();
    const auto shownSongText = [&](int index) {
        QString shown = slides.value(index);
        if (!showLabels)
            shown = stripLeadingVerseLabel(shown);
        if (!showChords)
            shown = stripChords(shown);
        if (lineStars && index == slides.size() - 1)
            shown += QStringLiteral("\n***");
        return shown;
    };

    if (item.type == ContentType::Song && !showLabels)
        text = stripLeadingVerseLabel(text);

    if (item.type == ContentType::Song) {
        if (showChords)
            content.chords = hasChords(text);
        else
            text = stripChords(text);
        if (AppSettings::value(AppSettings::NumberVerses).toBool()) {
            const QString label = songSlideLabels(slides).value(slideIndex);
            if (label.startsWith(QObject::tr("Куплет")))
                content.cornerLabel = label.section(QLatin1Char(' '), -1);
        }
    }

    // "Показывать *** в конце песни": a cue for the vocalists.
    if (item.type == ContentType::Song && lineStars) {
        if (slideIndex == slides.size() - 1) {
            // One line break, not a blank line: the space above the stars is
            // Настройки → Общие → "Отступ перед ***" (none by default).
            text += QStringLiteral("\n***");
            content.starsGapLines = qBound(0, AppSettings::value(AppSettings::LineStarsGap).toInt(), 200) / 100.0;
        }
    }

    if (item.type == ContentType::BibleVerse && DisplaySettings::showBibleReference() && !item.refBook.isEmpty()) {
        const QString reference = item.refLocation.isEmpty() ? item.refBook
            : QStringLiteral("%1 %2").arg(item.refBook, item.refLocation);
        content.reference = reference;
    }

    if (item.type == ContentType::BibleVerse
        && AppSettings::value(DisplaySettings::styleKey(item.type, QStringLiteral("separateVerseNumber"))).toBool()) {
        static const QRegularExpression number(QStringLiteral(R"(^([⁰¹²³⁴⁵⁶⁷⁸⁹]+)\s+)"));
        const auto match = number.match(text);
        if (match.hasMatch()) {
            QString normal;
            const QString digits = QStringLiteral("⁰¹²³⁴⁵⁶⁷⁸⁹");
            for (const QChar c : match.captured(1)) normal += QString::number(digits.indexOf(c));
            text.replace(0, match.capturedLength(), normal + QLatin1Char('\n'));
        }
    }
    content.text = text;
    if (item.type == ContentType::Song && slides.size() > 1) {
        for (int i = 0; i < slides.size(); ++i)
            content.fitGroup << shownSongText(i);
    }
    if (item.type == ContentType::Announcement) {
        content.heading = item.title.trimmed();
        content.text = item.text.trimmed();
        content.hasOwnAlignment = true;
        content.align = item.textAlign;
        content.textScale = item.textSize < 0 ? 0.8 : item.textSize > 0 ? 1.25 : 1.0;
    }
    content.backgroundType = item.backgroundType;
    content.backgroundPath = item.backgroundPath;
    // "Фоновое изображение по умолчанию" for songs and verses without one
    // of their own (a text-only announcement stays plain on purpose).
    if (content.backgroundType == BackgroundType::None
        && (item.type == ContentType::Song || item.type == ContentType::BibleVerse)) {
        const QString background = AppSettings::value(DisplaySettings::styleKey(item.type, QStringLiteral("background"))).toString();
        content.backgroundType = DisplaySettings::backgroundType(background);
        content.backgroundPath = DisplaySettings::backgroundPath(background);
    }
    return content;
}
