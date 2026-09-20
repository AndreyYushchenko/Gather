#pragma once

#include "core/ContentItem.h"

#include <QString>

enum class SlideKind {
    Empty,
    Text,
    Photo,
    Video,
    Black
};

struct SlideContent {
    SlideKind kind = SlideKind::Empty;
    QString text;
    QString imagePath;
    // Which per-type display profile (font/size) a Text slide should use —
    // see DisplaySettings. Meaningless for other kinds.
    ContentType sourceType = ContentType::Song;

    // Optional photo/video shown behind the text on a Text slide.
    BackgroundType backgroundType = BackgroundType::None;
    QString backgroundPath;

    // Video slide only: imagePath holds the local file path or http(s) URL.
    // Looped playback is muted and meant as an ambient background ("Как фон
    // (в цикле)" in the Video screen); non-looped is the main, audible
    // output ("Прямой показ на экран").
    bool videoLoop = false;
};
