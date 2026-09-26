#include "TimerScreen.h"
#include "core/Database.h"
#include "ui/Theme.h"

#include <QCoreApplication>
#include <QFile>
#include <QFontDatabase>
#include <QLocale>
#include <QTimeZone>

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("TimerFormat", text);
}

template <typename Enum>
Enum enumValue(const QJsonObject &json, const char *key, Enum fallback, int count)
{
    const int value = json.value(QLatin1String(key)).toInt(static_cast<int>(fallback));
    return value >= 0 && value < count ? static_cast<Enum>(value) : fallback;
}

QString capitalized(QString text)
{
    if (!text.isEmpty())
        text[0] = text.at(0).toUpper();
    return text;
}

QString two(qint64 value)
{
    return QStringLiteral("%1").arg(value, 2, 10, QLatin1Char('0'));
}

constexpr qint64 Minute = 60 * 1000;
// After a clock-time target passes, keep treating it as "ended" (end
// message / counting up) for this long before moving on to the next one.
constexpr qint64 EndedGraceMs = 60 * Minute;

} // namespace

// ---- JSON ------------------------------------------------------------------

QJsonObject TimerScreen::toJson() const
{
    return QJsonObject{
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), name},
        {QStringLiteral("type"), static_cast<int>(type)},
        {QStringLiteral("face"), static_cast<int>(face)},
        {QStringLiteral("use24h"), use24h},
        {QStringLiteral("showSeconds"), showSeconds},
        {QStringLiteral("timeZone"), QString::fromUtf8(timeZoneId)},
        {QStringLiteral("target"), static_cast<int>(target)},
        {QStringLiteral("durationMs"), double(durationMs)},
        {QStringLiteral("targetTime"), targetTime.toString(QStringLiteral("HH:mm"))},
        {QStringLiteral("repeatWeekday"), repeatWeekday},
        {QStringLiteral("targetDateTime"), targetDateTime.toString(Qt::ISODate)},
        {QStringLiteral("format"), static_cast<int>(format)},
        {QStringLiteral("title"), title},
        {QStringLiteral("subtitle"), subtitle},
        {QStringLiteral("endMessage"), endMessage},
        {QStringLiteral("message"), message},
        {QStringLiteral("background"), static_cast<int>(background)},
        {QStringLiteral("customBackgroundPath"), customBackgroundPath},
        {QStringLiteral("fontFamily"), fontFamily},
        {QStringLiteral("textColor"), static_cast<int>(textColor)},
        {QStringLiteral("fontSize"), static_cast<int>(fontSize)},
        {QStringLiteral("align"), static_cast<int>(align)},
        {QStringLiteral("dimPercent"), dimPercent},
        {QStringLiteral("textShadow"), textShadow},
        {QStringLiteral("showProgress"), showProgress},
        {QStringLiteral("endAction"), static_cast<int>(endAction)},
        {QStringLiteral("switchToScreenId"), switchToScreenId},
        {QStringLiteral("soundEnabled"), soundEnabled},
        {QStringLiteral("soundIndex"), soundIndex},
        {QStringLiteral("warnEnabled"), warnEnabled},
        {QStringLiteral("warnYellowMinutes"), warnYellowMinutes},
        {QStringLiteral("warnRedMinutes"), warnRedMinutes},
        {QStringLiteral("autoStart"), autoStart},
        {QStringLiteral("scheduleEnabled"), scheduleEnabled},
        {QStringLiteral("scheduleTime"), scheduleTime.toString(QStringLiteral("HH:mm"))},
        {QStringLiteral("scheduleWeekday"), scheduleWeekday},
    };
}

TimerScreen TimerScreen::fromJson(const QJsonObject &json)
{
    const TimerScreen defaults;
    TimerScreen s;
    const auto str = [&](const char *key) { return json.value(QLatin1String(key)).toString(); };
    const auto boolean = [&](const char *key, bool fallback) { return json.value(QLatin1String(key)).toBool(fallback); };
    const auto integer = [&](const char *key, int fallback) { return json.value(QLatin1String(key)).toInt(fallback); };
    const auto clockTime = [&](const char *key, const QTime &fallback) {
        const QTime t = QTime::fromString(str(key), QStringLiteral("HH:mm"));
        return t.isValid() ? t : fallback;
    };

    s.id = str("id");
    s.name = str("name");
    s.type = enumValue(json, "type", defaults.type, 5);
    s.face = enumValue(json, "face", defaults.face, 3);
    s.use24h = boolean("use24h", defaults.use24h);
    s.showSeconds = boolean("showSeconds", defaults.showSeconds);
    s.timeZoneId = str("timeZone").toUtf8();
    s.target = enumValue(json, "target", defaults.target, 3);
    s.durationMs = qint64(json.value(QStringLiteral("durationMs")).toDouble(double(defaults.durationMs)));
    s.targetTime = clockTime("targetTime", defaults.targetTime);
    s.repeatWeekday = qBound(0, integer("repeatWeekday", 0), 7);
    s.targetDateTime = QDateTime::fromString(str("targetDateTime"), Qt::ISODate);
    s.format = enumValue(json, "format", defaults.format, 5);
    s.title = str("title");
    s.subtitle = str("subtitle");
    s.endMessage = str("endMessage");
    s.message = str("message");
    s.background = enumValue(json, "background", defaults.background, 7);
    s.customBackgroundPath = str("customBackgroundPath");
    s.fontFamily = str("fontFamily");
    s.textColor = enumValue(json, "textColor", defaults.textColor, 4);
    s.fontSize = enumValue(json, "fontSize", defaults.fontSize, 4);
    s.align = enumValue(json, "align", defaults.align, 3);
    s.dimPercent = qBound(0, integer("dimPercent", defaults.dimPercent), 90);
    s.textShadow = boolean("textShadow", defaults.textShadow);
    s.showProgress = boolean("showProgress", defaults.showProgress);
    s.endAction = enumValue(json, "endAction", defaults.endAction, 4);
    s.switchToScreenId = str("switchToScreenId");
    s.soundEnabled = boolean("soundEnabled", defaults.soundEnabled);
    s.soundIndex = qMax(0, integer("soundIndex", 0));
    s.warnEnabled = boolean("warnEnabled", defaults.warnEnabled);
    s.warnYellowMinutes = qMax(1, integer("warnYellowMinutes", defaults.warnYellowMinutes));
    s.warnRedMinutes = qMax(1, integer("warnRedMinutes", defaults.warnRedMinutes));
    s.autoStart = boolean("autoStart", defaults.autoStart);
    s.scheduleEnabled = boolean("scheduleEnabled", defaults.scheduleEnabled);
    s.scheduleTime = clockTime("scheduleTime", defaults.scheduleTime);
    s.scheduleWeekday = qBound(0, integer("scheduleWeekday", 0), 7);
    if (!s.targetDateTime.isValid())
        s.targetDateTime = QDateTime(QDate::currentDate().addDays(7), QTime(10, 0));
    return s;
}

QJsonObject TimerRunState::toJson() const
{
    return QJsonObject{{QStringLiteral("running"), running},
                       {QStringLiteral("anchor"), double(anchorMs)},
                       {QStringLiteral("value"), double(valueMs)}};
}

TimerRunState TimerRunState::fromJson(const QJsonObject &json)
{
    TimerRunState run;
    run.running = json.value(QStringLiteral("running")).toBool();
    run.anchorMs = qint64(json.value(QStringLiteral("anchor")).toDouble());
    run.valueMs = qint64(json.value(QStringLiteral("value")).toDouble());
    return run;
}

namespace TimerFormat {

// ---- Names -----------------------------------------------------------------

QString typeName(TimerScreenType type)
{
    switch (type) {
    case TimerScreenType::Time: return tr("Время");
    case TimerScreenType::TimeDate: return tr("Время и дата");
    case TimerScreenType::Countdown: return tr("Обратный отсчёт");
    case TimerScreenType::Stopwatch: return tr("Секундомер");
    case TimerScreenType::Message: return tr("Сообщение");
    }
    return {};
}

QString typeIcon(TimerScreenType type)
{
    switch (type) {
    case TimerScreenType::Time: return QStringLiteral("clock");
    case TimerScreenType::TimeDate: return QStringLiteral("calendar-days");
    case TimerScreenType::Countdown: return QStringLiteral("hourglass");
    case TimerScreenType::Stopwatch: return QStringLiteral("timer");
    case TimerScreenType::Message: return QStringLiteral("message-square");
    }
    return {};
}

QString targetName(CountdownTarget target)
{
    switch (target) {
    case CountdownTarget::Duration: return tr("На время");
    case CountdownTarget::TimeOfDay: return tr("До времени сегодня");
    case CountdownTarget::DateTime: return tr("До даты и времени");
    }
    return {};
}

QString formatName(CountdownFormat format)
{
    switch (format) {
    case CountdownFormat::Auto: return tr("Авто");
    case CountdownFormat::DaysTime: return tr("Дни + время");
    case CountdownFormat::HoursMinutesSeconds: return tr("ЧЧ:ММ:СС");
    case CountdownFormat::MinutesSeconds: return tr("ММ:СС");
    case CountdownFormat::Minutes: return tr("Только минуты");
    }
    return {};
}

QString endActionName(TimerEndAction action)
{
    switch (action) {
    case TimerEndAction::ShowMessage: return tr("Показать сообщение");
    case TimerEndAction::CountUp: return tr("Считать дальше (+00:45)");
    case TimerEndAction::SwitchScreen: return tr("Переключить на экран");
    case TimerEndAction::Nothing: return tr("Ничего");
    }
    return {};
}

QString faceName(ClockFace face)
{
    switch (face) {
    case ClockFace::Analog: return tr("Аналоговые");
    case ClockFace::Digital: return tr("Цифровые");
    case ClockFace::Both: return tr("Оба");
    }
    return {};
}

QString backgroundName(TimerBackground background)
{
    switch (background) {
    case TimerBackground::Dark: return tr("Тёмный");
    case TimerBackground::Mountains: return tr("Горы");
    case TimerBackground::Gradient: return tr("Градиент");
    case TimerBackground::ChurchHall: return tr("Зал церкви");
    case TimerBackground::BlueWaves: return tr("Синие волны");
    case TimerBackground::WarmGlow: return tr("Тёплый свет");
    case TimerBackground::Custom: return tr("Свой фон");
    }
    return {};
}

bool isVideoBackground(TimerBackground background)
{
    return background == TimerBackground::BlueWaves || background == TimerBackground::WarmGlow;
}

QString textColorName(TimerTextColor color)
{
    switch (color) {
    case TimerTextColor::White: return tr("Белый");
    case TimerTextColor::Black: return tr("Чёрный");
    case TimerTextColor::Yellow: return tr("Жёлтый");
    case TimerTextColor::Blue: return tr("Голубой");
    }
    return {};
}

QString fontSizeName(TimerFontSize size)
{
    switch (size) {
    case TimerFontSize::Small: return tr("Маленький");
    case TimerFontSize::Medium: return tr("Средний");
    case TimerFontSize::Large: return tr("Большой");
    case TimerFontSize::Huge: return tr("Очень большой");
    }
    return {};
}

QString alignName(TimerAlign align)
{
    switch (align) {
    case TimerAlign::Left: return tr("Слева");
    case TimerAlign::Center: return tr("По центру");
    case TimerAlign::Right: return tr("Справа");
    }
    return {};
}

QString alignIcon(TimerAlign align)
{
    switch (align) {
    case TimerAlign::Left: return QStringLiteral("align-left");
    case TimerAlign::Center: return QStringLiteral("align-center");
    case TimerAlign::Right: return QStringLiteral("align-right");
    }
    return {};
}

QStringList weekdayChoices()
{
    return {tr("Каждый день"), tr("Каждый понедельник"), tr("Каждый вторник"), tr("Каждую среду"),
            tr("Каждый четверг"), tr("Каждую пятницу"), tr("Каждую субботу"), tr("Каждое воскресенье")};
}

QList<QPair<QString, QString>> fontChoices()
{
    QList<QPair<QString, QString>> choices{{QString(), tr("Как в приложении")}};
    const QStringList installed = QFontDatabase::families();
    for (const QString &family : {QStringLiteral("Montserrat"), QStringLiteral("Inter"), QStringLiteral("Segoe UI"),
                                  QStringLiteral("Bahnschrift"), QStringLiteral("Arial"), QStringLiteral("Georgia"),
                                  QStringLiteral("Times New Roman"), QStringLiteral("Impact")}) {
        if (installed.contains(family))
            choices.append({family, family});
    }
    return choices;
}

QColor textColor(TimerTextColor color)
{
    switch (color) {
    case TimerTextColor::White: return QColor(0xff, 0xff, 0xff);
    case TimerTextColor::Black: return QColor(0x11, 0x13, 0x18);
    case TimerTextColor::Yellow: return QColor(0xfa, 0xcc, 0x15);
    case TimerTextColor::Blue: return QColor(0x7c, 0xc4, 0xff);
    }
    return Qt::white;
}

QColor backgroundSwatch(TimerBackground background)
{
    switch (background) {
    case TimerBackground::Dark: return QColor(0x11, 0x13, 0x18);
    case TimerBackground::Mountains: return QColor(0x3a, 0x3f, 0x5c);
    case TimerBackground::Gradient: return QColor(0x7c, 0x6f, 0xd6);
    case TimerBackground::ChurchHall: return QColor(0x8a, 0x6a, 0x4a);
    case TimerBackground::BlueWaves: return QColor(0x2f, 0x6f, 0xeb);
    case TimerBackground::WarmGlow: return QColor(0xf5, 0x9e, 0x0b);
    case TimerBackground::Custom: return QColor(0xfb, 0xfb, 0xfc);
    }
    return Qt::black;
}

QString backgroundImagePath(const TimerScreen &screen)
{
    switch (screen.background) {
    case TimerBackground::Mountains: return QStringLiteral(":/backgrounds/mountains.jpg");
    case TimerBackground::ChurchHall: return QStringLiteral(":/backgrounds/church_hall.jpg");
    case TimerBackground::BlueWaves: return QStringLiteral(":/backgrounds/blue_waves_thumb.png");
    case TimerBackground::WarmGlow: return QStringLiteral(":/backgrounds/warm_glow_thumb.png");
    case TimerBackground::Custom: return screen.customBackgroundPath;
    case TimerBackground::Dark:
    case TimerBackground::Gradient:
        break;
    }
    return {};
}

QString backgroundVideoPath(const TimerScreen &screen)
{
    if (!isVideoBackground(screen.background))
        return {};
    // QMediaPlayer (and the OBS page's <video>) need a real file, not a
    // :/ resource, so the bundled clip is copied out once. The clips are
    // cut as seamless loops (last frames cross-faded into the first); bump
    // the "loop2" tag whenever they change so old copies aren't reused.
    const QString name = screen.background == TimerBackground::BlueWaves ? QStringLiteral("blue_waves.mp4")
                                                                         : QStringLiteral("warm_glow.mp4");
    const QString path = Database::backgroundsDir() + QStringLiteral("/builtin-loop2-") + name;
    if (!QFile::exists(path))
        QFile::copy(QStringLiteral(":/backgrounds/") + name, path);
    return path;
}

double fontScale(TimerFontSize size)
{
    // Large matches design.pen's thumbnails (26px time on an 88px card).
    switch (size) {
    case TimerFontSize::Small: return 0.17;
    case TimerFontSize::Medium: return 0.23;
    case TimerFontSize::Large: return 0.29;
    case TimerFontSize::Huge: return 0.36;
    }
    return 0.29;
}

QString resolvedFontFamily(const TimerScreen &screen)
{
    return screen.fontFamily.isEmpty() ? Theme::fontFamily() : screen.fontFamily;
}

// ---- Time formatting -------------------------------------------------------

QDateTime clockNow(const TimerScreen &screen)
{
    const QDateTime utc = QDateTime::currentDateTimeUtc();
    if (screen.timeZoneId.isEmpty())
        return utc.toLocalTime();
    const QTimeZone zone(screen.timeZoneId);
    return zone.isValid() ? utc.toTimeZone(zone) : utc.toLocalTime();
}

QString time(const QDateTime &dateTime, bool use24h, bool withSeconds)
{
    const QTime t = dateTime.time();
    if (use24h)
        return t.toString(withSeconds ? QStringLiteral("HH:mm:ss") : QStringLiteral("HH:mm"));
    const int hour12 = t.hour() % 12 == 0 ? 12 : t.hour() % 12;
    QString text = QStringLiteral("%1:%2").arg(hour12).arg(two(t.minute()));
    if (withSeconds)
        text += QLatin1Char(':') + two(t.second());
    return text + (t.hour() < 12 ? QStringLiteral(" AM") : QStringLiteral(" PM"));
}

QString date(const QDate &date)
{
    // "12 апреля 2025 г." — "MMMM" after a day number is the genitive form.
    return QLocale().toString(date, tr("d MMMM yyyy 'г.'"));
}

QString weekday(const QDate &date)
{
    return capitalized(QLocale().dayName(date.dayOfWeek(), QLocale::LongFormat));
}

QString durationText(qint64 ms, CountdownFormat format)
{
    const bool over = ms < 0;
    const qint64 abs = qAbs(ms);
    // Counting down rounds up (15:00 at the start, 00:00 only when truly
    // done); counting up past zero rounds down.
    const qint64 seconds = over ? abs / 1000 : (abs + 999) / 1000;
    const qint64 days = seconds / 86400;
    const qint64 hours = (seconds % 86400) / 3600;
    const qint64 minutes = (seconds % 3600) / 60;
    const qint64 secs = seconds % 60;

    QString text;
    switch (format) {
    case CountdownFormat::Auto:
        if (days > 0)
            text = tr("%1 дн %2:%3").arg(days).arg(two(hours), two(minutes));
        else if (hours > 0)
            text = QStringLiteral("%1:%2:%3").arg(hours).arg(two(minutes), two(secs));
        else
            text = QStringLiteral("%1:%2").arg(two(minutes), two(secs));
        break;
    case CountdownFormat::DaysTime:
        text = tr("%1 дн %2:%3:%4").arg(days).arg(two(hours), two(minutes), two(secs));
        break;
    case CountdownFormat::HoursMinutesSeconds:
        text = QStringLiteral("%1:%2:%3").arg(two(seconds / 3600), two(minutes), two(secs));
        break;
    case CountdownFormat::MinutesSeconds:
        text = QStringLiteral("%1:%2").arg(two(seconds / 60), two(secs));
        break;
    case CountdownFormat::Minutes:
        text = tr("%1 мин").arg(over ? seconds / 60 : (seconds + 59) / 60);
        break;
    }
    return over ? QStringLiteral("+") + text : text;
}

// ---- Countdown maths -------------------------------------------------------

QDateTime nextTarget(const TimerScreen &screen, qint64 nowMs)
{
    if (screen.target == CountdownTarget::DateTime)
        return screen.targetDateTime;
    if (screen.target != CountdownTarget::TimeOfDay)
        return {};

    const QDateTime now = QDateTime::fromMSecsSinceEpoch(nowMs);
    const auto occursOn = [&](const QDate &day) {
        return screen.repeatWeekday == 0 || day.dayOfWeek() == screen.repeatWeekday;
    };
    // The one that just passed still counts for a while (end message /
    // "+00:45"), so an evening service doesn't flip to next week's target
    // the second it starts.
    for (int back = 0; back <= 7; ++back) {
        const QDate day = now.date().addDays(-back);
        if (!occursOn(day))
            continue;
        const QDateTime candidate(day, screen.targetTime);
        if (candidate <= now) {
            if (candidate.msecsTo(now) < EndedGraceMs)
                return candidate;
            break;
        }
    }
    for (int ahead = 0; ahead <= 7; ++ahead) {
        const QDate day = now.date().addDays(ahead);
        if (!occursOn(day))
            continue;
        const QDateTime candidate(day, screen.targetTime);
        if (candidate > now)
            return candidate;
    }
    return {};
}

qint64 countdownRemaining(const TimerSlide &slide, qint64 nowMs)
{
    const TimerScreen &screen = slide.screen;
    if (screen.target == CountdownTarget::Duration)
        return slide.run.running ? slide.run.anchorMs - nowMs : slide.run.valueMs;
    const QDateTime target = nextTarget(screen, nowMs);
    return target.isValid() ? target.toMSecsSinceEpoch() - nowMs : 0;
}

qint64 stopwatchElapsed(const TimerRunState &run, qint64 nowMs)
{
    return run.running ? qMax<qint64>(0, nowMs - run.anchorMs) : run.valueMs;
}

bool isCountingDown(const TimerSlide &slide)
{
    if (slide.screen.type == TimerScreenType::Stopwatch)
        return slide.run.running;
    if (!slide.screen.isCountdown())
        return false;
    return slide.screen.target != CountdownTarget::Duration || slide.run.running;
}

// ---- Frame -----------------------------------------------------------------

TimerFrame frame(const TimerSlide &slide, qint64 nowMs)
{
    const TimerScreen &screen = slide.screen;
    TimerFrame f;
    f.title = screen.title;

    switch (screen.type) {
    case TimerScreenType::Time:
    case TimerScreenType::TimeDate: {
        const QDateTime now = clockNow(screen);
        f.clockTime = now.time();
        f.showAnalog = screen.face != ClockFace::Digital;
        f.showDigital = screen.face != ClockFace::Analog;
        f.primary = time(now, screen.use24h, screen.showSeconds);
        if (screen.type == TimerScreenType::TimeDate)
            f.below << date(now.date()) << weekday(now.date());
        break;
    }
    case TimerScreenType::Countdown: {
        const qint64 remaining = countdownRemaining(slide, nowMs);
        qint64 total = 0;
        if (screen.target == CountdownTarget::Duration)
            total = qMax(screen.durationMs, slide.run.running ? 0 : slide.run.valueMs);
        else if (screen.target == CountdownTarget::TimeOfDay)
            total = EndedGraceMs; // the bar fills over the final hour

        if (remaining > 0) {
            f.primary = durationText(remaining, screen.format);
            if (screen.warnEnabled) {
                if (remaining <= qint64(screen.warnRedMinutes) * Minute)
                    f.mainColor = QColor(0xef, 0x44, 0x44);
                else if (remaining <= qint64(screen.warnYellowMinutes) * Minute)
                    f.mainColor = QColor(0xfa, 0xcc, 0x15);
            }
            if (screen.showProgress && total > 0)
                f.progress = qBound(0.0, 1.0 - double(remaining) / double(total), 1.0);
        } else {
            switch (screen.endAction) {
            case TimerEndAction::ShowMessage:
                if (!screen.endMessage.trimmed().isEmpty()) {
                    f.primary = screen.endMessage;
                    f.isMessage = true;
                    f.title.clear();
                } else {
                    f.primary = durationText(0, screen.format);
                }
                break;
            case TimerEndAction::CountUp:
                f.primary = durationText(remaining, screen.format);
                f.mainColor = QColor(0xef, 0x44, 0x44);
                break;
            case TimerEndAction::SwitchScreen:
            case TimerEndAction::Nothing:
                f.primary = durationText(0, screen.format);
                break;
            }
            if (screen.showProgress && total > 0)
                f.progress = 1.0;
        }
        break;
    }
    case TimerScreenType::Stopwatch: {
        // Reuse the countdown formats; a negative value means "rounded
        // down", which is what a stopwatch wants. Drop the "+" it adds.
        const qint64 elapsed = stopwatchElapsed(slide.run, nowMs);
        f.primary = durationText(-elapsed, screen.format);
        if (f.primary.startsWith(QLatin1Char('+')))
            f.primary.remove(0, 1);
        break;
    }
    case TimerScreenType::Message:
        f.primary = screen.message;
        f.isMessage = true;
        break;
    }

    if (!screen.subtitle.trimmed().isEmpty())
        f.below << screen.subtitle;
    return f;
}

QString summary(const TimerScreen &screen)
{
    QString text = typeName(screen.type);
    switch (screen.type) {
    case TimerScreenType::Time:
    case TimerScreenType::TimeDate:
        text += QStringLiteral(" · %1").arg(screen.use24h ? tr("24 ч") : tr("12 ч"));
        if (screen.face != ClockFace::Digital)
            text += QStringLiteral(" · %1").arg(faceName(screen.face).toLower());
        break;
    case TimerScreenType::Countdown:
        switch (screen.target) {
        case CountdownTarget::Duration:
            text += QStringLiteral(" · %1").arg(durationText(screen.durationMs, CountdownFormat::Auto));
            break;
        case CountdownTarget::TimeOfDay:
            text += tr(" · до %1").arg(screen.targetTime.toString(QStringLiteral("HH:mm")));
            if (screen.repeatWeekday != 0)
                text += QStringLiteral(" (%1)").arg(weekdayChoices().value(screen.repeatWeekday).toLower());
            break;
        case CountdownTarget::DateTime:
            text += tr(" · до %1").arg(screen.targetDateTime.toString(QStringLiteral("dd.MM.yyyy HH:mm")));
            break;
        }
        break;
    case TimerScreenType::Stopwatch:
    case TimerScreenType::Message:
        break;
    }
    return text;
}

QString timeZoneLabel(const QByteArray &id)
{
    const QDateTime utc = QDateTime::currentDateTimeUtc();
    const QTimeZone zone = id.isEmpty() ? QTimeZone::systemTimeZone() : QTimeZone(id);
    const int offsetSeconds = zone.isValid() ? zone.offsetFromUtc(utc) : 0;
    const int hours = offsetSeconds / 3600;
    const int minutes = qAbs(offsetSeconds % 3600) / 60;
    QString gmt = QStringLiteral("GMT%1%2").arg(offsetSeconds < 0 ? QStringLiteral("-") : QStringLiteral("+")).arg(qAbs(hours));
    if (minutes)
        gmt += QLatin1Char(':') + two(minutes);

    static const QList<QPair<QByteArray, const char *>> names = {
        {"UTC", "UTC"}, {"Europe/Kyiv", "Киев"}, {"Europe/Kiev", "Киев"}, {"Europe/Moscow", "Москва"},
        {"Europe/Minsk", "Минск"}, {"Europe/Warsaw", "Варшава"}, {"Europe/Berlin", "Берлин"},
        {"Europe/London", "Лондон"}, {"America/New_York", "Нью-Йорк"},
    };
    QString name = tr("Местное время");
    for (const auto &entry : names) {
        if (entry.first == id)
            name = tr(entry.second);
    }
    return QStringLiteral("%1 (%2)").arg(name, gmt);
}

QList<QByteArray> timeZoneChoices()
{
    // Older tz databases (and some Windows ICU builds) only know Kyiv by its
    // pre-2022 spelling.
    const QByteArray kyiv = QTimeZone::isTimeZoneIdAvailable("Europe/Kyiv") ? QByteArray("Europe/Kyiv")
                                                                             : QByteArray("Europe/Kiev");
    QList<QByteArray> ids{QByteArray()};
    for (const QByteArray &id : {QByteArray("UTC"), kyiv, QByteArray("Europe/Moscow"), QByteArray("Europe/Minsk"),
                                 QByteArray("Europe/Warsaw"), QByteArray("Europe/Berlin"), QByteArray("Europe/London"),
                                 QByteArray("America/New_York")}) {
        if (QTimeZone::isTimeZoneIdAvailable(id))
            ids << id;
    }
    return ids;
}

} // namespace TimerFormat
