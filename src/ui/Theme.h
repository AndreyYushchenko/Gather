#pragma once

#include <QString>

namespace Theme {

inline const QString BgDark = QStringLiteral("#141a26");
inline const QString BgDark2 = QStringLiteral("#1b2333");
inline const QString BgDark3 = QStringLiteral("#212b3f");
inline const QString BorderDark = QStringLiteral("#2a3448");
inline const QString BgWhite = QStringLiteral("#ffffff");
inline const QString BgPanel = QStringLiteral("#fbfbfc");
inline const QString BorderLight = QStringLiteral("#e7e9ee");
inline const QString TextDarkPrimary = QStringLiteral("#151a23");
inline const QString TextDarkSecondary = QStringLiteral("#7a8190");
inline const QString TextLightPrimary = QStringLiteral("#ffffff");
inline const QString TextLightSecondary = QStringLiteral("#8b93a8");
inline const QString AccentBlue = QStringLiteral("#2f6feb");
inline const QString AccentBlueBg = QStringLiteral("#eaf1ff");
inline const QString AccentGreen = QStringLiteral("#22c55e");

// Picks "Inter" when installed, otherwise a sane system fallback.
QString fontFamily();

} // namespace Theme
