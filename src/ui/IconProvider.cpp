#include "IconProvider.h"

#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QPainter>
#include <QSvgRenderer>

namespace {

QByteArray rawSvg(const QString &name)
{
    QFile file(QStringLiteral(":/icons/%1.svg").arg(name));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

// Every row/button in every list re-requests the same handful of
// (name, color, size, filled) combinations on every rebuild (list refresh,
// favorite toggle, category switch, ...). Rendering each one meant a fresh
// resource read + XML parse + QSvgRenderer + QPainter pass every single
// time, with no reuse — a major contributor to the UI feeling laggy on
// anything but tiny lists. The distinct combinations are few and bounded
// (finite icon names/colors/sizes in the theme), so caching forever is safe.
QHash<QString, QPixmap> &pixmapCache()
{
    static QHash<QString, QPixmap> cache;
    return cache;
}

} // namespace

QPixmap IconProvider::pixmap(const QString &name, const QColor &color, int size, bool filled)
{
    const QString key = name + QLatin1Char('|') + color.name(QColor::HexArgb) + QLatin1Char('|')
        + QString::number(size) + QLatin1Char('|') + (filled ? QStringLiteral("1") : QStringLiteral("0"));

    QHash<QString, QPixmap> &cache = pixmapCache();
    const auto cached = cache.constFind(key);
    if (cached != cache.constEnd())
        return *cached;

    QByteArray svg = rawSvg(name);
    if (svg.isEmpty())
        return QPixmap();

    svg.replace("currentColor", color.name().toUtf8());
    if (filled)
        svg.replace("fill=\"none\"", "fill=\"" + color.name().toUtf8() + "\"");

    const qreal dpr = qGuiApp ? qGuiApp->devicePixelRatio() : 1.0;
    QPixmap pix(qRound(size * dpr), qRound(size * dpr));
    pix.fill(Qt::transparent);
    pix.setDevicePixelRatio(dpr);

    QSvgRenderer renderer(svg);
    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter, QRectF(0, 0, size, size));

    cache.insert(key, pix);
    return pix;
}

QIcon IconProvider::icon(const QString &name, const QColor &color, int size, bool filled)
{
    return QIcon(pixmap(name, color, size, filled));
}
