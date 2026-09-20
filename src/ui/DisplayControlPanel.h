#pragma once

#include "display/SlideContent.h"

#include <QWidget>

class PresentationController;
class SlideRenderWidget;
class ShortcutRow;
class QLabel;
class QPushButton;

// Right dark panel: mirrors what's live on the projector, lets the operator
// step through slides / blackout / freeze without touching the library, and
// shows the local OBS browser-source address.
class DisplayControlPanel : public QWidget {
    Q_OBJECT
public:
    explicit DisplayControlPanel(QWidget *parent = nullptr);

    void setController(PresentationController *controller);

private:
    void updateStatus();
    void positionSlideCounter();
    QLabel *makeStatusDot();
    ShortcutRow *makeShortcutRow(const QString &iconName, const QString &label, const QString &shortcut);

    PresentationController *m_controller = nullptr;

    SlideRenderWidget *m_miniPreview = nullptr;
    QLabel *m_slideCounterOverlay = nullptr;
    QLabel *m_statusDot = nullptr;
    QLabel *m_statusLabel = nullptr;

    QPushButton *m_backButton = nullptr;
    QPushButton *m_forwardButton = nullptr;
    ShortcutRow *m_blackButton = nullptr;
    ShortcutRow *m_pauseButton = nullptr;

    QLabel *m_obsUrlLabel = nullptr;
    QLabel *m_obsStatusDot = nullptr;
    QLabel *m_obsStatusLabel = nullptr;
};
