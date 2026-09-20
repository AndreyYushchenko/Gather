#include "SlideContentBuilder.h"
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
    const QStringList lines = slideText.split(QLatin1Char('\n'));
    if (lines.isEmpty() || !verseLabelPattern().match(lines.first()).hasMatch())
        return slideText;
    return QStringList(lines.mid(1)).join(QLatin1Char('\n')).trimmed();
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

    if (item.type == ContentType::Song && !DisplaySettings::showVerseLabels())
        text = stripLeadingVerseLabel(text);

    if (item.type == ContentType::BibleVerse && DisplaySettings::showBibleReference() && !item.refBook.isEmpty()) {
        const QString reference = item.refLocation.isEmpty() ? item.refBook
            : QStringLiteral("%1 %2").arg(item.refBook, item.refLocation);
        text = reference + QStringLiteral("\n\n") + text;
    }

    content.text = text;
    content.backgroundType = item.backgroundType;
    content.backgroundPath = item.backgroundPath;
    return content;
}
