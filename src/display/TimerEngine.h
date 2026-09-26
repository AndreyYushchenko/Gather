#pragma once

#include "TimerScreen.h"

#include <QDate>
#include <QHash>
#include <QList>
#include <QObject>

class PresentationController;
class QTimer;

// Owns the timer screens and their run states and does everything that
// happens over time: start/pause/reset, reaching zero (sound, switching to
// another screen), warning thresholds live in TimerFormat::frame(), the
// "По расписанию" auto-show, and keeping the live output in sync. The UI
// (TimersPanel) only edits screens and calls these methods, so behaviour
// can be extended here without touching the UI and vice versa.
class TimerEngine : public QObject {
    Q_OBJECT
public:
    explicit TimerEngine(QObject *parent = nullptr);

    void setController(PresentationController *controller);

    const QList<TimerScreen> &screens() const { return m_screens; }
    int indexOf(const QString &id) const;
    TimerScreen screen(const QString &id) const;
    TimerRunState run(const QString &id) const { return m_runs.value(id); }
    TimerSlide slide(const QString &id) const;

    void updateScreen(const TimerScreen &screen);
    QString addScreen(TimerScreen screen, int index = -1);
    void removeScreen(const QString &id);
    void moveScreen(const QString &id, int newIndex);

    void start(const QString &id);
    void pause(const QString &id);
    void reset(const QString &id);
    // "Быстро подправить": shifts a duration countdown/stopwatch, or moves a
    // clock-time/date target.
    void adjust(const QString &id, qint64 deltaMs);

    void goLive(const QString &id, bool forceStart = false);
    // The screen currently on the outputs, or empty.
    QString liveScreenId() const;

    static TimerScreen makeScreen(const QString &name, TimerScreenType type);

signals:
    void screensChanged();                     // added / removed / reordered
    void screenChanged(const QString &id);     // settings of one screen
    void runStateChanged(const QString &id);   // started / paused / reset / ended
    void liveChanged();

private:
    void tick();
    void onCountdownEnded(const QString &id);
    void pushLive(const QString &id);
    void setRun(const QString &id, const TimerRunState &run);
    void load();
    void save() const;
    void saveRuns() const;
    static QList<TimerScreen> defaultScreens();

    PresentationController *m_controller = nullptr;
    QList<TimerScreen> m_screens;
    QHash<QString, TimerRunState> m_runs;
    QHash<QString, qint64> m_lastRemaining;
    QHash<QString, QDate> m_scheduleFiredOn;
    QString m_lastLiveId;
    QTimer *m_tick = nullptr;
};
