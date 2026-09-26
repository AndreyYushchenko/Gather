#pragma once

#include "core/ContentItem.h"

#include <QString>
#include <QStringList>
#include <Qt>
#include <QVariant>
#include <functional>

// Persisted (QSettings-backed) presentation styling — the "Внешний вид
// слайдов" rows in Настройки → Показ / Библия actually drive rendering
// through this, rather than being decorative. Read by SlideRenderWidget
// (projector window + mini preview), DisplayServer (OBS browser-source
// HTML) and SlideContentBuilder (verse-label / bible-reference stripping)
// so all outputs stay in sync.
//
// V2 textStyle() resolves independent song/Bible presentation profiles.
// The legacy font/scale helpers below remain for other output types.
// Pending settings previews can supply a reader without writing QSettings.
namespace DisplaySettings {

enum class Alignment { Center, Left, Right };

struct TextStyle {
    QString family;
    int fontSize = 72;
    int weight = 600;
    double letterSpacing = 0;
    int lineHeight = 120;
    QString textCase;
    QString alignment;
    int maxLines = 4;
    int margin = 8;
    int maxWidth = 90;
    bool autoFit = true;
    bool wordWrap = true;
    bool shadow = true;
    int shadowOpacity = 60;
    bool outline = false;
    int outlineWidth = 2;
    bool dim = true;
    int dimOpacity = 40;
    bool bible = false;
};

QString styleKey(ContentType type, const QString &field);
TextStyle textStyle(ContentType type, const std::function<QVariant(const QString &)> &read = {});
QString transformText(const QString &text, const TextStyle &style);
QStringList splitTextSlides(ContentType type, const QStringList &slides);
BackgroundType backgroundType(const QString &value);
QString backgroundPath(const QString &value);

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

// "Фоновое изображение по умолчанию" for songs and verses without their
// own background: None, or a photo/video path (built-in videos are copied
// out of the resources to a real file so every player can open them).
BackgroundType defaultBackgroundType();
QString defaultBackgroundPath();

// "Тень текста для читаемости".
bool textShadow();
// "Анимация перехода": "fade" | "none" | "slide", and its length.
QString transition();
int transitionMs();

// Bible verses: whether the reference (book, chapter:verse) is shown on
// the projected slide, not just in the operator's panel.
bool showBibleReference();
void setShowBibleReference(bool show);

} // namespace DisplaySettings
