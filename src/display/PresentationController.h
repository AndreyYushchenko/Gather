#pragma once

#include "SlideContent.h"
#include "core/ContentItem.h"

#include <QObject>
#include <QStringList>
#include <optional>

class DisplayWindow;
class QWidget;
class QMediaPlayer;
class QAudioOutput;
class DisplayServer;

// Owns the projector window and the local OBS server, and holds the single
// "what's live right now" state so the control panel, the projector window
// and the OBS feed always agree.
class PresentationController : public QObject {
    Q_OBJECT
public:
    explicit PresentationController(QObject *parent = nullptr);
    ~PresentationController() override;

    void goLive(const ContentItem &item, int slideIndex = 0, bool loopVideo = false);
    // "Запустить плейлист": shows items[index]; Вперёд past an item's last
    // slide moves on to the next item, Назад before its first slide goes
    // back to the previous one.
    void goLivePlaylist(const QList<ContentItem> &items, int index, int playlistId);
    bool isPlaylistLive() const { return !m_livePlaylist.isEmpty(); }
    int livePlaylistId() const { return m_livePlaylistId; }
    int livePlaylistIndex() const { return m_livePlaylistIndex; }
    const QList<ContentItem> &livePlaylist() const { return m_livePlaylist; }
    // Puts a "Таймеры и время" screen on the outputs instead of a library item.
    void goLiveTimer(const TimerSlide &slide);
    // A photo slideshow: each photo is one "slide", stepped through with
    // Назад/Вперёд (Photos screen's "Добавить в показ").
    void goLivePhotos(const QList<ContentItem> &photos, int startIndex = 0);
    // Re-sends the live timer screen (new settings or start/pause/reset) —
    // a no-op unless a timer screen is what's currently live.
    void updateLiveTimer(const TimerSlide &slide);
    bool isTimerLive() const { return m_liveTimer.has_value(); }
    QString liveTimerScreenId() const { return m_liveTimer ? m_liveTimer->screen.id : QString(); }
    void stepForward();
    void stepBack();

    void toggleFrozen();
    bool isFrozen() const { return m_frozen; }

    // The library item on screen (-1 for none, a timer or a photo slideshow).
    int liveItemId() const { return m_liveItem && !m_liveTimer && m_livePhotos.isEmpty() ? m_liveItem->id : -1; }
    bool hasLiveItem() const { return m_liveItem.has_value() || m_liveTimer.has_value(); }
    int liveSlideIndex() const { return m_liveSlideIndex; }
    int liveSlideCount() const { return m_liveSlides.size(); }

    bool isDisplayWindowVisible() const;
    void raiseDisplayWindow();
    QString activeScreenName() const;
    DisplayServer *server() const { return m_server; }
    // The projector window's player while a local video is live, else null.
    QMediaPlayer *liveVideoPlayer() const;
    QAudioOutput *liveAudioOutput() const;
    // Projector window: fullscreen ⇄ windowed.
    void toggleDisplayFullScreen();

    // The operator's window, so the projector can go to another monitor.
    void setControlWindow(QWidget *window) { m_controlWindow = window; }
    // Opens the projector window without changing what's (not) live.
    void openDisplayWindow();
    // "Завершить показ": nothing live any more (the idle screen shows).
    void endShow();
    // Настройки → Показ saved: restyle the slide; `placementChanged` (monitor,
    // resolution, fullscreen) also moves/resizes the projector window.
    void applyDisplaySettings(bool placementChanged);

    // Re-applies font/size/alignment (DisplaySettings) to whatever is
    // currently shown, without changing content — even while frozen.
    void refreshDisplayStyle();

signals:
    void contentChanged(const SlideContent &content);
    void frozenChanged(bool frozen);
    void liveSlideChanged(int index, int count);
    void displayWindowVisibilityChanged(bool visible);
    // A playlist started/stopped or moved to another item.
    void playlistPositionChanged();

private:
    void ensureDisplayWindow();
    // A monitor was plugged in or unplugged (projector HDMI dropping out).
    void onScreensChanged();
    void showItem(const ContentItem &item, int slideIndex, bool loopVideo);
    void stopPlaylist();
    void applyCurrent();
    SlideContent computeContent() const;

    std::optional<ContentItem> m_liveItem;
    std::optional<TimerSlide> m_liveTimer;
    QList<ContentItem> m_livePhotos;
    QList<ContentItem> m_livePlaylist;
    int m_livePlaylistIndex = -1;
    int m_livePlaylistId = -1;
    QStringList m_liveSlides;
    int m_liveSlideIndex = 0;
    bool m_liveLoopVideo = false;
    bool m_hidden = false;
    bool m_frozen = false;

    DisplayWindow *m_window = nullptr;
    QWidget *m_controlWindow = nullptr;
    DisplayServer *m_server = nullptr;
};
