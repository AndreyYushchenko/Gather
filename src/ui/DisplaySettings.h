#pragma once

#include "core/ContentItem.h"

#include <QString>
#include <QStringList>
#include <Qt>

// Persisted (QSettings-backed) presentation styling — the "Внешний вид
// слайдов" rows in Настройки → Показ / Библия actually drive rendering
// through this, rather than being decorative. Read by SlideRenderWidget
// (projector window + mini preview), DisplayServer (OBS browser-source
// HTML) and SlideContentBuilder (verse-label / bible-reference stripping)
// so all outputs stay in sync.
//
// Font and text scale are two separate profiles — "Bible" for
// ContentType::BibleVerse, "Song" for everything else (songs, announcements,
// photos) — so the operator can size Bible text differently from song
// lyrics instead of one shared setting driving both.
namespace DisplaySettings {

enum class Alignment { Center, Left };

QStringList availableFonts();

// Resolved font family: the saved choice if installed, otherwise
// Theme::fontFamily()'s system fallback.
QString fontFamily(ContentType type);
void setFontFamily(ContentType type, const QString &family);
// The raw saved preference (for populating the settings combo box) — unlike
// fontFamily(), doesn't fall back if the font isn't actually installed.
QString preferredFontFamily(ContentType type);

// Text scale as a percentage of the default size (50–200, default 100).
int textScalePercent(ContentType type);
void setTextScalePercent(ContentType type, int percent);
// Point size for a slide of the given widget height, honoring the current scale.
int fontPointSize(ContentType type, int referenceHeight);
// Relative size for viewport-width-based (vw) CSS units, honoring the current scale.
double cssFontSizeVw(ContentType type);

Alignment alignment();
void setAlignment(Alignment align);
Qt::Alignment qtAlignment();
QString cssTextAlign();

// Songs: whether "Куплет 1" / "Приспів" / "Припев" style leading labels
// (as found in imported songbook text) are kept on the projected slide.
bool showVerseLabels();
void setShowVerseLabels(bool show);

// Bible verses: whether the reference (book, chapter:verse) is shown on
// the projected slide, not just in the operator's panel.
bool showBibleReference();
void setShowBibleReference(bool show);

} // namespace DisplaySettings
