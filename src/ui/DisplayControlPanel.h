#pragma once

#include "core/ContentItem.h"
#include "display/SlideContent.h"

#include <QWidget>

class PresentationController;
class SlideRenderWidget;
class ShortcutRow;
class QuickListCard;
class QLabel;
class QPushButton;
class QSlider;

// Right dark panel: mirrors what's live on the projector, lets the operator
// step through slides / blackout / freeze without touching the library, and
// shows the local OBS browser-source address.
class DisplayControlPanel : public QWidget {
    Q_OBJECT
public:
    explicit DisplayControlPanel(QWidget *parent = nullptr);

    void setController(PresentationController *controller);

    // Called by whoever owns this panel once they've resolved what "the
    // current song" is — this panel has no view into the library selection.
    void addSongToQuickList(const ContentItem &item);

    // design.pen's Timers screen shows this panel without the quick list.
    void setQuickListVisible(bool visible);
    // Key captions on «Скрыть» / «Пауза» (Настройки → Горячие клавиши).
    void setShortcutLabels(const QString &hide, const QString &pause);

signals:
    void addCurrentSongRequested();

private:
    void updateStatus();
    void updateVideoControls();
    void positionSlideCounter();
    void updatePlaylistBox();
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

    QuickListCard *m_quickList = nullptr;

    // design.pen "Now Next Box": shown while a playlist is running.
    QWidget *m_playlistBox = nullptr;
    QLabel *m_nowLabel = nullptr;
    QLabel *m_nextLabel = nullptr;

    QWidget *m_videoControls = nullptr;
    QSlider *m_videoSlider = nullptr;
    QPushButton *m_videoPlayButton = nullptr;
    QLabel *m_videoTimeLabel = nullptr;
    QPushButton *m_videoMuteButton = nullptr;
    bool m_liveLocalVideo = false;
    // Last states drawn, so the 4×/s tick and every slide change don't
    // re-render icons and re-apply style sheets that haven't changed.
    int m_shownPlaying = -1;
    int m_shownMuted = -1;
    int m_shownWindowVisible = -1;
    int m_shownServerRunning = -1;

};
