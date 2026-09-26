#pragma once

#include "TimerScreen.h"
#include "core/ContentItem.h"

#include <QString>
#include <QStringList>

enum class SlideKind {
    Empty,
    Text,
    Photo,
    Video,
    Timer
};

struct SlideContent {
    SlideKind kind = SlideKind::Empty;
    QString text;
    QString reference;
    QString imagePath;
    // Which per-type display profile (font/size) a Text slide should use —
    // see DisplaySettings. Meaningless for other kinds.
    ContentType sourceType = ContentType::Song;

    // Announcement layout: a big bold heading above smaller `text` lines,
    // with the announcement's own alignment and size instead of the global
    // DisplaySettings ones, and the background dimmed so the text reads.
    QString heading;
    bool hasOwnAlignment = false;
    TextAlign align = TextAlign::Center;
    double textScale = 1.0;

    // Songs: "Нумеровать куплеты" puts the verse number in a corner, and
    // "Показывать аккорды" keeps ChordPro chords ([Am]) to draw above the
    // words.
    QString cornerLabel;
    bool chords = false;
    // "Показывать *** в конце песни": the last line of `text` is "***" and
    // sits this many text lines below the words (0 = directly under them);
    // negative when there are no stars.
    double starsGapLines = -1;
    // Every slide of the same song as it is shown, so all of them get one
    // font size (the one that fits the longest) instead of each slide being
    // fitted on its own — which made the size jump from slide to slide.
    QStringList fitGroup;

    // Optional photo/video shown behind the text on a Text slide.
    BackgroundType backgroundType = BackgroundType::None;
    QString backgroundPath;

    // Video slide only: imagePath holds the local file path or http(s) URL.
    // Looped playback is muted and meant as an ambient background ("Как фон
    // (в цикле)" in the Video screen); non-looped is the main, audible
    // output ("Прямой показ на экран").
    bool videoLoop = false;

    // Timer slide only: a clock/countdown/stopwatch/message screen from the
    // "Таймеры и время" page. Each output renders it live from the wall
    // clock, so it only needs re-sending when the screen or timer state
    // changes.
    TimerSlide timer;
};
