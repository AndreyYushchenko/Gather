#include "TimerEngine.h"
#include "PresentationController.h"
#include "ui/TimerSound.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QTimer>
#include <QUuid>

namespace {

constexpr auto ScreensKey = "timers/v2/screens";
constexpr auto RunsKey = "timers/v2/runs";

qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

} // namespace

TimerEngine::TimerEngine(QObject *parent)
    : QObject(parent)
{
    load();
    m_tick = new QTimer(this);
    m_tick->setInterval(250);
    connect(m_tick, &QTimer::timeout, this, &TimerEngine::tick);
    m_tick->start();
}

void TimerEngine::setController(PresentationController *controller)
{
    m_controller = controller;
    connect(controller, &PresentationController::contentChanged, this, [this]() {
        const QString live = liveScreenId();
        if (live != m_lastLiveId) {
            m_lastLiveId = live;
            emit liveChanged();
        }
    });
}

// ---- Screens ---------------------------------------------------------------

int TimerEngine::indexOf(const QString &id) const
{
    for (int i = 0; i < m_screens.size(); ++i) {
        if (m_screens.at(i).id == id)
            return i;
    }
    return -1;
}

TimerScreen TimerEngine::screen(const QString &id) const
{
    const int index = indexOf(id);
    return index >= 0 ? m_screens.at(index) : TimerScreen{};
}

TimerSlide TimerEngine::slide(const QString &id) const
{
    return TimerSlide{screen(id), m_runs.value(id)};
}

void TimerEngine::updateScreen(const TimerScreen &updated)
{
    const int index = indexOf(updated.id);
    if (index < 0)
        return;
    const TimerScreen old = m_screens.at(index);
    m_screens[index] = updated;
    save();

    // A new duration only replaces a countdown that isn't running.
    TimerRunState run = m_runs.value(updated.id);
    if (updated.durationMs != old.durationMs && !run.running && updated.type == TimerScreenType::Countdown) {
        run.valueMs = updated.durationMs;
        m_runs.insert(updated.id, run);
        saveRuns();
        emit runStateChanged(updated.id);
    }
    emit screenChanged(updated.id);
    pushLive(updated.id);
}

QString TimerEngine::addScreen(TimerScreen screen, int index)
{
    if (screen.id.isEmpty() || indexOf(screen.id) >= 0)
        screen.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (index < 0 || index > m_screens.size())
        index = m_screens.size();
    m_screens.insert(index, screen);
    m_runs.insert(screen.id, TimerRunState{false, 0, screen.durationMs});
    save();
    saveRuns();
    emit screensChanged();
    return screen.id;
}

void TimerEngine::removeScreen(const QString &id)
{
    const int index = indexOf(id);
    if (index < 0 || m_screens.size() <= 1)
        return;
    m_screens.removeAt(index);
    m_runs.remove(id);
    m_lastRemaining.remove(id);
    save();
    saveRuns();
    emit screensChanged();
}

void TimerEngine::moveScreen(const QString &id, int newIndex)
{
    const int index = indexOf(id);
    newIndex = qBound(0, newIndex, int(m_screens.size()) - 1);
    if (index < 0 || index == newIndex)
        return;
    m_screens.move(index, newIndex);
    save();
    emit screensChanged();
}

TimerScreen TimerEngine::makeScreen(const QString &name, TimerScreenType type)
{
    TimerScreen screen;
    screen.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    screen.name = name;
    screen.type = type;
    screen.targetDateTime = QDateTime(QDate::currentDate().addDays(7), QTime(10, 0));
    return screen;
}

QList<TimerScreen> TimerEngine::defaultScreens()
{
    // design.pen "Мои экраны": ready-made examples to start from.
    TimerScreen time = makeScreen(tr("Только время"), TimerScreenType::Time);

    TimerScreen service = makeScreen(tr("Молодёжка — до начала"), TimerScreenType::Countdown);
    service.target = CountdownTarget::TimeOfDay;
    service.targetTime = QTime(19, 0);
    service.title = tr("До начала");
    service.subtitle = tr("Молодёжное служение");
    service.endMessage = tr("Начинаем! 🔥");
    service.warnEnabled = true;

    TimerScreen camp = makeScreen(tr("До лагеря"), TimerScreenType::Countdown);
    camp.target = CountdownTarget::DateTime;
    camp.targetDateTime = QDateTime(QDate::currentDate().addDays(12), QTime(10, 0));
    camp.title = tr("До лагеря");
    camp.showProgress = false;

    TimerScreen breakScreen = makeScreen(tr("Перерыв"), TimerScreenType::Countdown);
    breakScreen.durationMs = 10 * 60 * 1000;
    breakScreen.title = tr("Перерыв");
    breakScreen.endMessage = tr("Возвращаемся!");
    breakScreen.warnEnabled = true;
    breakScreen.warnYellowMinutes = 2;
    breakScreen.autoStart = true;

    TimerScreen analog = makeScreen(tr("Аналоговые часы"), TimerScreenType::Time);
    analog.face = ClockFace::Analog;

    TimerScreen message = makeScreen(tr("Начинаем!"), TimerScreenType::Message);
    message.message = tr("Начинаем! 🔥");
    message.background = TimerBackground::Gradient;

    return {time, service, camp, breakScreen, analog, message};
}

// ---- Timers ----------------------------------------------------------------

void TimerEngine::setRun(const QString &id, const TimerRunState &run)
{
    m_runs.insert(id, run);
    saveRuns();
    emit runStateChanged(id);
    pushLive(id);
}

void TimerEngine::start(const QString &id)
{
    const TimerScreen s = screen(id);
    const TimerRunState run = m_runs.value(id);
    if (!s.hasStartStop() || run.running)
        return;
    const qint64 now = nowMs();
    if (s.type == TimerScreenType::Stopwatch) {
        setRun(id, TimerRunState{true, now - run.valueMs, 0});
        return;
    }
    const qint64 remaining = run.valueMs > 0 ? run.valueMs : s.durationMs;
    m_lastRemaining.insert(id, remaining);
    setRun(id, TimerRunState{true, now + remaining, 0});
}

void TimerEngine::pause(const QString &id)
{
    const TimerScreen s = screen(id);
    const TimerRunState run = m_runs.value(id);
    if (!s.hasStartStop() || !run.running)
        return;
    const qint64 now = nowMs();
    if (s.type == TimerScreenType::Stopwatch)
        setRun(id, TimerRunState{false, 0, TimerFormat::stopwatchElapsed(run, now)});
    else
        setRun(id, TimerRunState{false, 0, run.anchorMs - now});
}

void TimerEngine::reset(const QString &id)
{
    const TimerScreen s = screen(id);
    if (s.type == TimerScreenType::Stopwatch)
        setRun(id, TimerRunState{});
    else if (s.hasStartStop()) {
        m_lastRemaining.insert(id, s.durationMs);
        setRun(id, TimerRunState{false, 0, s.durationMs});
    }
}

void TimerEngine::adjust(const QString &id, qint64 deltaMs)
{
    TimerScreen s = screen(id);
    TimerRunState run = m_runs.value(id);
    const qint64 now = nowMs();

    if (s.type == TimerScreenType::Stopwatch) {
        if (run.running)
            run.anchorMs = qMin(now, run.anchorMs - deltaMs);
        else
            run.valueMs = qMax<qint64>(0, run.valueMs + deltaMs);
        setRun(id, run);
        return;
    }
    if (!s.isCountdown())
        return;

    switch (s.target) {
    case CountdownTarget::Duration:
        if (run.running)
            run.anchorMs += deltaMs;
        else
            run.valueMs = qMax<qint64>(0, run.valueMs + deltaMs);
        m_lastRemaining.insert(id, run.running ? run.anchorMs - now : run.valueMs);
        setRun(id, run);
        break;
    case CountdownTarget::TimeOfDay:
        s.targetTime = s.targetTime.addSecs(int(deltaMs / 1000));
        updateScreen(s);
        break;
    case CountdownTarget::DateTime:
        s.targetDateTime = s.targetDateTime.addMSecs(deltaMs);
        updateScreen(s);
        break;
    }
}

// ---- Output ----------------------------------------------------------------

QString TimerEngine::liveScreenId() const
{
    return m_controller && m_controller->isTimerLive() ? m_controller->liveTimerScreenId() : QString();
}

void TimerEngine::goLive(const QString &id, bool forceStart)
{
    if (!m_controller || indexOf(id) < 0)
        return;
    const TimerScreen s = screen(id);
    if (s.hasStartStop() && (s.autoStart || forceStart) && !m_runs.value(id).running) {
        // Starting an ended countdown starts it over.
        if (s.isCountdown() && m_runs.value(id).valueMs <= 0)
            m_runs.insert(id, TimerRunState{false, 0, s.durationMs});
        start(id);
    }
    m_controller->goLiveTimer(slide(id));
}

void TimerEngine::pushLive(const QString &id)
{
    if (m_controller && liveScreenId() == id)
        m_controller->updateLiveTimer(slide(id));
}

void TimerEngine::tick()
{
    const qint64 now = nowMs();
    const QDateTime local = QDateTime::fromMSecsSinceEpoch(now);
    const QList<TimerScreen> screens = m_screens; // goLive() below may emit and re-enter

    for (const TimerScreen &s : screens) {
        if (s.isCountdown()) {
            const qint64 remaining = TimerFormat::countdownRemaining(slide(s.id), now);
            const qint64 previous = m_lastRemaining.value(s.id, remaining);
            m_lastRemaining.insert(s.id, remaining);
            if (previous > 0 && remaining <= 0)
                onCountdownEnded(s.id);
        }

        if (s.scheduleEnabled && m_scheduleFiredOn.value(s.id) != local.date()
            && (s.scheduleWeekday == 0 || local.date().dayOfWeek() == s.scheduleWeekday)
            && local.time().hour() == s.scheduleTime.hour() && local.time().minute() == s.scheduleTime.minute()) {
            m_scheduleFiredOn.insert(s.id, local.date());
            goLive(s.id);
        }
    }
}

void TimerEngine::onCountdownEnded(const QString &id)
{
    const TimerScreen s = screen(id);
    const TimerRunState run = m_runs.value(id);
    const bool live = liveScreenId() == id;
    // A clock-time countdown passes its target every day; only react when
    // it's actually on screen. A started duration countdown always reacts.
    if (!live && !(s.target == CountdownTarget::Duration && run.running))
        return;

    if (s.soundEnabled)
        TimerSound::play(s.soundIndex);

    if (s.target == CountdownTarget::Duration && s.endAction != TimerEndAction::CountUp)
        setRun(id, TimerRunState{false, 0, 0});
    else
        emit runStateChanged(id);

    if (s.endAction == TimerEndAction::SwitchScreen && live && indexOf(s.switchToScreenId) >= 0)
        goLive(s.switchToScreenId);
}

// ---- Persistence -----------------------------------------------------------

void TimerEngine::load()
{
    QSettings settings;
    const QJsonArray screens = QJsonDocument::fromJson(settings.value(QLatin1String(ScreensKey)).toByteArray()).array();
    for (const QJsonValue &value : screens) {
        TimerScreen s = TimerScreen::fromJson(value.toObject());
        if (s.id.isEmpty() || indexOf(s.id) >= 0)
            s.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_screens << s;
    }
    if (m_screens.isEmpty())
        m_screens = defaultScreens();

    const QJsonObject runs = QJsonDocument::fromJson(settings.value(QLatin1String(RunsKey)).toByteArray()).object();
    for (const TimerScreen &s : std::as_const(m_screens)) {
        m_runs.insert(s.id, runs.contains(s.id) ? TimerRunState::fromJson(runs.value(s.id).toObject())
                                                : TimerRunState{false, 0, s.durationMs});
    }
}

void TimerEngine::save() const
{
    QJsonArray array;
    for (const TimerScreen &s : m_screens)
        array.append(s.toJson());
    QSettings().setValue(QLatin1String(ScreensKey), QJsonDocument(array).toJson(QJsonDocument::Compact));
}

void TimerEngine::saveRuns() const
{
    // Kept across restarts, so closing the app mid-countdown doesn't lose it.
    QJsonObject object;
    for (auto it = m_runs.constBegin(); it != m_runs.constEnd(); ++it)
        object.insert(it.key(), it.value().toJson());
    QSettings().setValue(QLatin1String(RunsKey), QJsonDocument(object).toJson(QJsonDocument::Compact));
}
