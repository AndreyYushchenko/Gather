#include "Theme.h"
#include "core/AppSettings.h"

#include <QApplication>
#include <QEvent>
#include <QFontDatabase>
#include <QHash>
#include <QPalette>
#include <QRegularExpression>
#include <QStringList>
#include <QStyleFactory>
#include <QWidget>

namespace {

bool g_dark = false;
bool g_rounded = true;

// Light-scheme colour → dark-scheme colour, per kind of property, so white
// text on a blue button stays white while a white card turns dark.
QHash<QString, QString> g_backgrounds;
QHash<QString, QString> g_texts;
QHash<QString, QString> g_borders;

// Re-applies adaptStyleSheet() to every style sheet the app sets, so the
// hundreds of hand-written light-scheme style sheets follow the scheme
// without each being rewritten.
class StyleSheetThemer : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::StyleChange && event->type() != QEvent::Polish)
            return false;
        auto *widget = qobject_cast<QWidget *>(watched);
        if (!widget)
            return false;
        const QString css = widget->styleSheet();
        if (css.isEmpty() || widget->property("_themedCss").toString() == css)
            return false;
        const QString adapted = Theme::adaptStyleSheet(css);
        widget->setProperty("_themedCss", adapted);
        if (adapted != css)
            widget->setStyleSheet(adapted);
        return false;
    }
};

} // namespace

bool Theme::isDark()
{
    return g_dark;
}

double Theme::radius(double r)
{
    return g_rounded ? r : qMin(r, 3.0);
}

void Theme::apply(QApplication &app)
{
    g_dark = AppSettings::value(AppSettings::Theme).toString() == QLatin1String("dark");
    g_rounded = AppSettings::value(AppSettings::Rounded).toBool();

    if (g_dark) {
        const QString surface = QStringLiteral("#161c29");
        g_backgrounds = {
            {QStringLiteral("#ffffff"), surface},
            {QStringLiteral("#fbfbfc"), QStringLiteral("#171e2b")},
            {QStringLiteral("#f9fafb"), QStringLiteral("#1a2131")},
            {QStringLiteral("#f7f8fa"), QStringLiteral("#1a2131")},
            {QStringLiteral("#f5f6f8"), QStringLiteral("#1c2434")},
            {QStringLiteral("#f4f5f7"), QStringLiteral("#1c2434")},
            {QStringLiteral("#f3f4f6"), QStringLiteral("#212a3b")},
            {QStringLiteral("#f1f3f6"), QStringLiteral("#1f2737")},
            {QStringLiteral("#eaf1ff"), QStringLiteral("#1c2b4d")},
            {QStringLiteral("#e8f0fe"), QStringLiteral("#1c2b4d")},
            {QStringLiteral("#e6f6ec"), QStringLiteral("#133024")},
            {QStringLiteral("#e8f9ee"), QStringLiteral("#133024")},
            {QStringLiteral("#fdeaea"), QStringLiteral("#3a1c1f")},
            {QStringLiteral("#fee2e2"), QStringLiteral("#3a1c1f")},
            {QStringLiteral("#fdf1e3"), QStringLiteral("#3a2a16")},
            {QStringLiteral("#f1eafe"), QStringLiteral("#2a2144")},
            {QStringLiteral("#fef9c3"), QStringLiteral("#383314")},
            {QStringLiteral("#e7e9ee"), QStringLiteral("#283247")},
            {QStringLiteral("#d1d5db"), QStringLiteral("#39445a")},
            {QStringLiteral("#9ca3af"), QStringLiteral("#56627a")},
            {QStringLiteral("#9db8f2"), QStringLiteral("#2a4273")},
        };
        g_texts = {
            {QStringLiteral("#151a23"), QStringLiteral("#e6e9f0")},
            {QStringLiteral("#7a8190"), QStringLiteral("#98a2b6")},
            {QStringLiteral("#4b5563"), QStringLiteral("#b8c0cd")},
            {QStringLiteral("#333c4d"), QStringLiteral("#c5ccd8")},
        };
        g_borders = {
            {QStringLiteral("#e7e9ee"), QStringLiteral("#283247")},
            {QStringLiteral("#e5e7eb"), QStringLiteral("#283247")},
            {QStringLiteral("#d1d5db"), QStringLiteral("#39445a")},
            {QStringLiteral("#c9ced8"), QStringLiteral("#39445a")},
            {QStringLiteral("#ffffff"), QStringLiteral("#283247")},
        };

        BgDark = QStringLiteral("#0f141e");
        BgDark2 = QStringLiteral("#161d2b");
        BgDark3 = QStringLiteral("#1c2435");
        BorderDark = QStringLiteral("#263044");
        BgWhite = surface;
        BgPanel = QStringLiteral("#171e2b");
        BorderLight = QStringLiteral("#283247");
        TextDarkPrimary = QStringLiteral("#e6e9f0");
        TextDarkSecondary = QStringLiteral("#98a2b6");
        AccentBlueBg = QStringLiteral("#1c2b4d");
        SurfaceAlt = QStringLiteral("#1c2434");
        SurfaceSubtle = QStringLiteral("#1a2131");
        SurfaceMuted = QStringLiteral("#1f2737");
        SurfaceHover = QStringLiteral("#212a3b");
        Placeholder = QStringLiteral("#4f5a70");

        app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        QPalette palette;
        const QColor base(surface);
        const QColor text(TextDarkPrimary);
        palette.setColor(QPalette::Window, base);
        palette.setColor(QPalette::WindowText, text);
        palette.setColor(QPalette::Base, base);
        palette.setColor(QPalette::AlternateBase, QColor(SurfaceAlt));
        palette.setColor(QPalette::Text, text);
        palette.setColor(QPalette::Button, QColor(SurfaceAlt));
        palette.setColor(QPalette::ButtonText, text);
        palette.setColor(QPalette::ToolTipBase, QColor(SurfaceHover));
        palette.setColor(QPalette::ToolTipText, text);
        palette.setColor(QPalette::Highlight, QColor(AccentBlue));
        palette.setColor(QPalette::HighlightedText, Qt::white);
        palette.setColor(QPalette::PlaceholderText, QColor(QStringLiteral("#6b7489")));
        palette.setColor(QPalette::Link, QColor(AccentBlue));
        palette.setColor(QPalette::Mid, QColor(BorderLight));
        palette.setColor(QPalette::Midlight, QColor(SurfaceHover));
        palette.setColor(QPalette::Dark, QColor(BgDark));
        palette.setColor(QPalette::Light, QColor(SurfaceHover));
        palette.setColor(QPalette::Disabled, QPalette::Text, QColor(QStringLiteral("#5d667a")));
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(QStringLiteral("#5d667a")));
        palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(QStringLiteral("#5d667a")));
        app.setPalette(palette);
    }

    if (g_dark || !g_rounded)
        app.installEventFilter(new StyleSheetThemer(&app));
}

QString Theme::adaptStyleSheet(const QString &css)
{
    if (!g_dark && g_rounded)
        return css;
    QString result = css;
    if (g_dark) {
        // property: value — only colours inside the value are touched.
        static const QRegularExpression declaration(
            QStringLiteral(R"(((?:selection-)?background(?:-color)?|(?<![-\w])color|border(?:-(?:top|bottom|left|right))?(?:-color)?)\s*:\s*([^;}]*))"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression hex(QStringLiteral(R"(#[0-9a-fA-F]{6}(?![0-9a-fA-F]))"));
        QString out;
        qsizetype last = 0;
        auto it = declaration.globalMatch(result);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            const QString property = match.captured(1).toLower();
            const QHash<QString, QString> &map = property.startsWith(QLatin1String("border")) ? g_borders
                : property.contains(QLatin1String("background"))                               ? g_backgrounds
                                                                                                : g_texts;
            QString value = match.captured(2);
            QString mapped;
            qsizetype pos = 0;
            auto colours = hex.globalMatch(value);
            while (colours.hasNext()) {
                const QRegularExpressionMatch colour = colours.next();
                mapped += value.mid(pos, colour.capturedStart() - pos);
                const QString key = colour.captured(0).toLower();
                mapped += map.value(key, colour.captured(0));
                pos = colour.capturedEnd();
            }
            mapped += value.mid(pos);
            out += result.mid(last, match.capturedStart(2) - last) + mapped;
            last = match.capturedEnd(2);
        }
        out += result.mid(last);
        result = out;
    }
    if (!g_rounded) {
        static const QRegularExpression corner(QStringLiteral(R"((border(?:-(?:top|bottom)-(?:left|right))?-radius\s*:\s*)(\d+(?:\.\d+)?)px)"));
        QString out;
        qsizetype last = 0;
        auto it = corner.globalMatch(result);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            const double value = match.captured(2).toDouble();
            out += result.mid(last, match.capturedStart(2) - last);
            out += QString::number(value >= 6 ? 3 : value);
            last = match.capturedEnd(2);
        }
        out += result.mid(last);
        result = out;
    }
    return result;
}

QString Theme::fontFamily()
{
    static const QString resolved = [] {
        const QStringList families = QFontDatabase::families();
        // Настройки → Внешний вид → "Основной шрифт", when installed.
        const QString chosen = AppSettings::value(AppSettings::UiFont).toString();
        if (!chosen.isEmpty() && families.contains(chosen))
            return chosen;
        if (families.contains(QStringLiteral("Inter")))
            return QStringLiteral("Inter");
#ifdef Q_OS_WIN
        return QStringLiteral("Segoe UI");
#else
        return QStringLiteral("Sans Serif");
#endif
    }();
    return resolved;
}

QString Theme::scrollBarCss()
{
    return adaptStyleSheet(QStringLiteral(R"(
        QScrollBar:vertical { background: transparent; width: 10px; margin: 2px 1px; }
        QScrollBar::handle:vertical { background: #d1d5db; border-radius: 4px; min-height: 30px; }
        QScrollBar::handle:vertical:hover { background: #9ca3af; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
        QScrollBar:horizontal { background: transparent; height: 10px; margin: 1px 2px; }
        QScrollBar::handle:horizontal { background: #d1d5db; border-radius: 4px; min-width: 30px; }
        QScrollBar::handle:horizontal:hover { background: #9ca3af; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }
        QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }
    )"));
}
