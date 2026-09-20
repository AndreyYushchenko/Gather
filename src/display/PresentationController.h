#pragma once

#include "SlideContent.h"
#include "core/ContentItem.h"

#include <QObject>
#include <QStringList>
#include <optional>

class DisplayWindow;
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
    void stepForward();
    void stepBack();

    void setBlack(bool black);
    void toggleBlack();
    bool isBlack() const { return m_black; }

    void toggleFrozen();
    bool isFrozen() const { return m_frozen; }

    bool hasLiveItem() const { return m_liveItem.has_value(); }
    int liveSlideIndex() const { return m_liveSlideIndex; }
    int liveSlideCount() const { return m_liveSlides.size(); }

    bool isDisplayWindowVisible() const;
    void raiseDisplayWindow();
    QString activeScreenName() const;
    DisplayServer *server() const { return m_server; }

    // Re-applies font/size/alignment (DisplaySettings) to whatever is
    // currently shown, without changing content — even while frozen.
    void refreshDisplayStyle();

signals:
    void contentChanged(const SlideContent &content);
    void blackChanged(bool black);
    void frozenChanged(bool frozen);
    void liveSlideChanged(int index, int count);
    void displayWindowVisibilityChanged(bool visible);

private:
    void ensureDisplayWindow();
    void applyCurrent();
    SlideContent computeContent() const;

    std::optional<ContentItem> m_liveItem;
    QStringList m_liveSlides;
    int m_liveSlideIndex = 0;
    bool m_liveLoopVideo = false;
    bool m_black = false;
    bool m_frozen = false;

    DisplayWindow *m_window = nullptr;
    DisplayServer *m_server = nullptr;
};
