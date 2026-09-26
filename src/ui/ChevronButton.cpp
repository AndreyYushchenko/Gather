#include "ChevronButton.h"
#include "IconProvider.h"
#include "Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>

ChevronButton::ChevronButton(const QString &iconName, const QString &text, const QColor &color, QWidget *parent)
    : QFrame(parent)
{
    setCursor(Qt::PointingHandCursor);
    setFrameShape(QFrame::NoFrame);
    // Styles itself directly, like SidebarNavRow/SlideCard/ShortcutRow
    // elsewhere in this app — a plain QFrame's QSS background/padding
    // isn't reliably painted when it's only set via an *external*
    // stylesheet matching on objectName; self-styling is the pattern
    // already proven to work in this codebase.
    setStyleSheet(QStringLiteral(
        "ChevronButton { background: %1; border: none; border-radius: 9px; }")
            .arg(Theme::AccentBlue));

    // Matches design.pen's On Screen Button padding [9,16] exactly. Note:
    // this is this widget's ONLY source of internal spacing — do not also
    // give a "QFrame#PrimaryButton" QSS rule a `padding` anywhere, it would
    // stack on top of these margins (Qt applies QSS padding to shrink the
    // contentsRect that this layout's margins are then added to again),
    // roughly doubling the button's size.
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 9, 16, 9);
    layout->setSpacing(8);

    auto *icon = new QLabel(this);
    icon->setStyleSheet(QStringLiteral("background: transparent;"));
    icon->setPixmap(IconProvider::pixmap(iconName, color, 15));
    layout->addWidget(icon);

    auto *label = new QLabel(text, this);
    label->setStyleSheet(QStringLiteral("background: transparent; color: %1; font-weight: 600; font-size: 13.5px;").arg(color.name()));
    layout->addWidget(label);

    auto *chevron = new QLabel(this);
    chevron->setStyleSheet(QStringLiteral("background: transparent;"));
    chevron->setPixmap(IconProvider::pixmap(QStringLiteral("chevron-down"), color, 14));
    layout->addWidget(chevron);
}

void ChevronButton::mousePressEvent(QMouseEvent *)
{
    emit clicked();
}
