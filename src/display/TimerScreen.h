#pragma once

#include <QByteArray>
#include <QColor>
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTime>

// Data model behind "Таймеры и время" (design.pen "Sermon App - Timers v2").
//
// Every TimerScreen is self-contained: what it shows (clock / countdown /
// stopwatch / message), its texts, its look and its behaviour (what happens
// at zero, warning colours, schedule). Its countdown/stopwatch run state is
// kept separately (TimerRunState, owned by TimerEngine) so editing a screen
// never disturbs a running timer.
//
// TimerFormat::frame() turns a screen + run state + "now" into the exact
// lines to draw. Every output (projector, previews, thumbnails, OBS page)
// renders from that one function, so a new option only has to be added in
// one place to show up everywhere.
//
// Adding a new option: add a field here (+ toJson/fromJson), use it in
// TimerFormat::frame() or TimerPainter, and add a control in TimersPanel.

enum class TimerScreenType { Time, TimeDate, Countdown, Stopwatch, Message };
enum class ClockFace { Analog, Digital, Both };
enum class CountdownTarget { Duration, TimeOfDay, DateTime };
enum class CountdownFormat { Auto, DaysTime, HoursMinutesSeconds, MinutesSeconds, Minutes };
enum class TimerEndAction { ShowMessage, CountUp, SwitchScreen, Nothing };
enum class TimerBackground { Dark, Mountains, Gradient, ChurchHall, BlueWaves, WarmGlow, Custom };
enum class TimerTextColor { White, Black, Yellow, Blue };
enum class TimerFontSize { Small, Medium, Large, Huge };
enum class TimerAlign { Left, Center, Right };

struct TimerScreen {
    QString id;
    QString name;
    TimerScreenType type = TimerScreenType::Time;

    // Clock (Time / TimeDate)
    ClockFace face = ClockFace::Digital;
    bool use24h = true;
    bool showSeconds = false;
    QByteArray timeZoneId; // empty = this computer's time zone

    // Countdown
    CountdownTarget target = CountdownTarget::Duration;
    qint64 durationMs = 15 * 60 * 1000;
    QTime targetTime = QTime(19, 0);
    int repeatWeekday = 0; // TimeOfDay: 0 = every day, 1..7 = Qt::DayOfWeek
    QDateTime targetDateTime;
    CountdownFormat format = CountdownFormat::Auto;

    // Text
    QString title;      // above the numbers
    QString subtitle;   // below the numbers
    QString endMessage; // countdown reached zero (ShowMessage)
    QString message;    // Message screens

    // Look
    TimerBackground background = TimerBackground::Mountains;
    QString customBackgroundPath;
    QString fontFamily; // empty = app font
    TimerTextColor textColor = TimerTextColor::White;
    TimerFontSize fontSize = TimerFontSize::Large;
    TimerAlign align = TimerAlign::Center;
    int dimPercent = 30; // darkening over photo/video backgrounds
    bool textShadow = true;
    bool showProgress = true;

    // Behaviour
    TimerEndAction endAction = TimerEndAction::ShowMessage;
    QString switchToScreenId;
    bool soundEnabled = true;
    int soundIndex = 0;
    bool warnEnabled = false;
    int warnYellowMinutes = 5;
    int warnRedMinutes = 1;
    bool autoStart = false;
    bool scheduleEnabled = false;
    QTime scheduleTime = QTime(18, 45);
    int scheduleWeekday = 0; // 0 = every day, 1..7 = Qt::DayOfWeek

    bool isClock() const { return type == TimerScreenType::Time || type == TimerScreenType::TimeDate; }
    bool isCountdown() const { return type == TimerScreenType::Countdown; }
    // Countdowns to a clock time/date run by themselves; only duration
    // countdowns and stopwatches have start/pause.
    bool hasStartStop() const
    {
        return type == TimerScreenType::Stopwatch
            || (type == TimerScreenType::Countdown && target == CountdownTarget::Duration);
    }

    QJsonObject toJson() const;
    static TimerScreen fromJson(const QJsonObject &json);
};

// Start/pause state of a duration countdown or a stopwatch.
// Countdown: running → anchorMs is the epoch time it reaches zero (the
// remaining time may go negative when counting up past zero); paused →
// valueMs is the remaining time. Stopwatch: running → anchorMs is the
// (virtual) start time; paused → valueMs is the elapsed time.
struct TimerRunState {
    bool running = false;
    qint64 anchorMs = 0;
    qint64 valueMs = 0;

    QJsonObject toJson() const;
    static TimerRunState fromJson(const QJsonObject &json);
};

struct TimerSlide {
    TimerScreen screen;
    TimerRunState run;
};

// Exactly what one timer screen shows at one moment.
struct TimerFrame {
    QString title;
    QString primary; // the big line ("main" is a macro on Windows)
    QStringList below; // date / weekday lines, then the subtitle
    QColor mainColor;  // invalid = the screen's text colour
    double progress = -1; // 0..1, or -1 for no progress bar
    bool showDigital = true;
    bool showAnalog = false;
    QTime clockTime;
    bool isMessage = false;
};

namespace TimerFormat {

QString typeName(TimerScreenType type);
QString typeIcon(TimerScreenType type);
QString targetName(CountdownTarget target);
QString formatName(CountdownFormat format);
QString endActionName(TimerEndAction action);
QString faceName(ClockFace face);
QString backgroundName(TimerBackground background);
bool isVideoBackground(TimerBackground background);
QString textColorName(TimerTextColor color);
QString fontSizeName(TimerFontSize size);
QString alignName(TimerAlign align);
QString alignIcon(TimerAlign align);
QStringList weekdayChoices(); // 0 = "Каждый день", 1..7 = "Каждый понедельник"...
// (family, display name); the first entry is the app default (empty family).
QList<QPair<QString, QString>> fontChoices();

QColor textColor(TimerTextColor color);
QColor backgroundSwatch(TimerBackground background);
// Still image for a background (for video backgrounds: its thumbnail).
QString backgroundImagePath(const TimerScreen &screen);
// Playable local file for video backgrounds, else empty.
QString backgroundVideoPath(const TimerScreen &screen);
double fontScale(TimerFontSize size);
QString resolvedFontFamily(const TimerScreen &screen);

QDateTime clockNow(const TimerScreen &screen);
QString time(const QDateTime &dateTime, bool use24h, bool withSeconds = false);
QString date(const QDate &date);
QString weekday(const QDate &date);
QString durationText(qint64 ms, CountdownFormat format);

// Countdown maths. Remaining may be negative once the target has passed.
QDateTime nextTarget(const TimerScreen &screen, qint64 nowMs);
qint64 countdownRemaining(const TimerSlide &slide, qint64 nowMs);
qint64 stopwatchElapsed(const TimerRunState &run, qint64 nowMs);
bool isCountingDown(const TimerSlide &slide);

TimerFrame frame(const TimerSlide &slide, qint64 nowMs);

// One-line description ("Обратный отсчёт · до 19:00").
QString summary(const TimerScreen &screen);

QString timeZoneLabel(const QByteArray &id);
QList<QByteArray> timeZoneChoices();

} // namespace TimerFormat
