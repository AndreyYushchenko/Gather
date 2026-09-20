#pragma once

#include <QString>
#include <QDate>
#include <QDateTime>
#include <QStringList>

enum class ContentType {
    Song,
    BibleVerse,
    Announcement,
    Photo,
    Video
};

// Background shown behind the slide text for Song/BibleVerse/Announcement
// items (a Photo item's own image is already the whole slide, so it never
// has one of these).
enum class BackgroundType {
    None,
    Photo,
    Video
};

QString contentTypeToDbString(ContentType type);
ContentType contentTypeFromDbString(const QString &value);
QString contentTypeDisplayName(ContentType type);

QString backgroundTypeToDbString(BackgroundType type);
BackgroundType backgroundTypeFromDbString(const QString &value);

struct ContentItem {
    int id = -1;
    ContentType type = ContentType::Song;

    QString title;      // Song: name; Announcement: title; BibleVerse: unused (built from refBook/refLocation)
    QString text;        // Song: lyrics; BibleVerse: verse text; Announcement: body

    QString refBook;         // BibleVerse only
    QString refLocation;     // BibleVerse: e.g. "3:16"; Song: number in its songbook/collection, e.g. "120"

    QDate expiryDate;         // Announcement only; null = no expiry

    QString imagePath;   // Photo: image file; Video: local file path, or an http(s) URL (e.g. YouTube) for an embedded source
    QString caption;     // Photo only

    // Song/BibleVerse/Announcement only: an optional photo or looping video
    // shown behind the slide text.
    BackgroundType backgroundType = BackgroundType::None;
    QString backgroundPath;

    bool favorite = false;
    QString notes;       // free-form operator notes, any category

    QDateTime createdAt;

    // Splits song/verse text into slides on blank lines.
    QStringList slides() const;

    QString displayTitle() const;
};
