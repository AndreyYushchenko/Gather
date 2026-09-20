#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

// Renders the bundled Lucide SVG icons (resources/icons, embedded via the
// Qt resource system) at any size/color, by swapping their `currentColor`
// stroke for the requested color before rasterizing. Matches the icon set
// named in design.pen ("library": "lucide").
namespace IconProvider {

// `filled` also swaps the icon's `fill="none"` for a solid fill in `color`
// (used for the favorite star once it's toggled on).
QPixmap pixmap(const QString &name, const QColor &color, int size = 24, bool filled = false);
QIcon icon(const QString &name, const QColor &color, int size = 24, bool filled = false);

} // namespace IconProvider
