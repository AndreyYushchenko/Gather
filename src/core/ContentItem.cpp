#include "ContentItem.h"

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
    return result;
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
