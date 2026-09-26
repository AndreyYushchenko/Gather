#include "ToggleSwitch.h"
#include "Theme.h"

#include <QPainter>

namespace {
constexpr int TrackWidth = 40;
constexpr int TrackHeight = 22;
constexpr int KnobMargin = 3;
}

ToggleSwitch::ToggleSwitch(QWidget *parent)
    : QAbstractButton(parent)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(sizeHint());
}

QSize ToggleSwitch::sizeHint() const
{
    return QSize(TrackWidth, TrackHeight);
}

void ToggleSwitch::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);

    const QRectF track(0, 0, width(), height());
    const qreal radius = track.height() / 2.0;
    painter.setBrush(isChecked() ? QColor(Theme::AccentBlue) : QColor(Theme::isDark() ? QStringLiteral("#39445a") : QStringLiteral("#d1d5db")));
    painter.drawRoundedRect(track, Theme::radius(radius), Theme::radius(radius));

    const qreal knobDiameter = track.height() - KnobMargin * 2;
    const qreal knobX = isChecked() ? track.width() - knobDiameter - KnobMargin : KnobMargin;
    painter.setBrush(Qt::white);
    painter.drawEllipse(QRectF(knobX, KnobMargin, knobDiameter, knobDiameter));
}
