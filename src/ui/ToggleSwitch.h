#pragma once

#include <QAbstractButton>

// A custom-painted pill toggle (blue track, white knob) matching the
// design's Toggle component. A native QCheckBox styled via QSS
// (::indicator { border-radius: ... }) doesn't render as a clean rounded
// pill/circle on Windows, so this paints itself instead.
class ToggleSwitch : public QAbstractButton {
    Q_OBJECT
public:
    explicit ToggleSwitch(QWidget *parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
};
