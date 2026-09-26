#pragma once

#include <QString>

class QApplication;

// Colours are variables, not constants: Theme::apply() switches them to
// the dark scheme (Настройки → Внешний вид → Цветовая схема) once at start,
// before any widget is built.
namespace Theme {

inline QString BgDark = QStringLiteral("#141a26");
inline QString BgDark2 = QStringLiteral("#1b2333");
inline QString BgDark3 = QStringLiteral("#212b3f");
inline QString BorderDark = QStringLiteral("#2a3448");
inline QString BgWhite = QStringLiteral("#ffffff");
inline QString BgPanel = QStringLiteral("#fbfbfc");
inline QString BorderLight = QStringLiteral("#e7e9ee");
inline QString TextDarkPrimary = QStringLiteral("#151a23");
inline QString TextDarkSecondary = QStringLiteral("#7a8190");
inline QString TextLightPrimary = QStringLiteral("#ffffff");
inline QString TextLightSecondary = QStringLiteral("#8b93a8");
inline QString AccentBlue = QStringLiteral("#2f6feb");
inline QString AccentBlueBg = QStringLiteral("#eaf1ff");
inline QString AccentGreen = QStringLiteral("#22c55e");

// Light-page surfaces used by the painted cards and rows.
inline QString SurfaceAlt = QStringLiteral("#f5f6f8");     // text-only cards, table header
inline QString SurfaceSubtle = QStringLiteral("#f7f8fa");  // list cards, hover
inline QString SurfaceMuted = QStringLiteral("#f1f3f6");   // image placeholders
inline QString SurfaceHover = QStringLiteral("#f3f4f6");
inline QString Placeholder = QStringLiteral("#b4bac6");    // placeholder icons

bool isDark();
// Picks the scheme from the settings and sets the variables above, the
// application palette and (dark) the Fusion style; installs the filter
// that adapts hand-written style sheets. Call before building any UI.
void apply(QApplication &app);
// Rewrites a style sheet's light-scheme colours (and corner radii when
// "Закруглённые углы" is off) for the current settings.
QString adaptStyleSheet(const QString &css);
// A corner radius, honouring "Закруглённые углы".
double radius(double r);

// Picks "Inter" when installed, otherwise a sane system fallback.
QString fontFamily();

// Thin custom scrollbars (app-wide, replaces the native OS ones).
QString scrollBarCss();

} // namespace Theme
