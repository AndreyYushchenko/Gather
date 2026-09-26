#pragma once

#include <QColor>
#include <QFrame>
#include <QString>

// A button with a leading icon, a label, and a trailing chevron-down —
// matches the design's "На экран ⌄" affordance. Plain QPushButton can't
// render this: it only ever places one icon, always before the text.
// A plain QFrame rather than QPushButton: nesting QLabel text inside a
// QSS-styled QPushButton garbles ClearType text rendering on Windows (see
// the same note on Sidebar's SidebarNavRow), so this paints its own labels
// and handles its own click instead.
class ChevronButton : public QFrame {
    Q_OBJECT
public:
    ChevronButton(const QString &iconName, const QString &text, const QColor &color, QWidget *parent = nullptr);

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *event) override;
};
