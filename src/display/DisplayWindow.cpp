#include "DisplayWindow.h"
#include "SlideRenderWidget.h"
#include "core/AppSettings.h"

#include <QCloseEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QWindow>

DisplayWindow::DisplayWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(tr("Sermon — показ"));
    setWindowFlag(Qt::Window, true);
    // Never take the keyboard from the operator's window: showing this one
    // used to activate it, and the hotkeys then went here instead.
    setWindowFlag(Qt::WindowDoesNotAcceptFocus, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);

    m_render = new SlideRenderWidget(this);
    setContent(SlideContent{});
}

void DisplayWindow::setContent(const SlideContent &content)
{
    m_render->setContent(content);
}

QSize DisplayWindow::fixedResolution()
{
    const QString value = AppSettings::value(AppSettings::DisplayResolution).toString();
    const QStringList parts = value.split(QLatin1Char('x'));
    if (parts.size() != 2)
        return QSize();
    return QSize(parts.at(0).toInt(), parts.at(1).toInt());
}

QScreen *DisplayWindow::targetScreen(QScreen *controlScreen)
{
    const QList<QScreen *> screens = QGuiApplication::screens();
    const QString chosen = AppSettings::value(AppSettings::DisplayScreen).toString();
    for (QScreen *screen : screens) {
        if (screen->name() == chosen)
            return screen;
    }
    // "Автоматически": any monitor other than the one the operator uses.
    for (QScreen *screen : screens) {
        if (screen != controlScreen)
            return screen;
    }
    return controlScreen ? controlScreen : QGuiApplication::primaryScreen();
}

void DisplayWindow::showOnBestScreen(QScreen *controlScreen)
{
    if (!controlScreen)
        controlScreen = QGuiApplication::primaryScreen();
    QScreen *target = targetScreen(controlScreen);
    const QSize resolution = fixedResolution();

    // Put the native window on the target screen *before* sizing and showing
    // it: sized first, it was laid out for the old screen's DPI and could open
    // on the wrong monitor, partly off-screen or not covering it fully.
    const bool wantFullScreen = target != controlScreen && AppSettings::value(AppSettings::AutoFullscreen).toBool();
    if (wantFullScreen && isVisible() && isFullScreen() && screen() == target)
        return; // already where it belongs; re-showing would flicker
    if (!windowHandle())
        create();
    if (isFullScreen())
        setWindowState(Qt::WindowNoState);

    if (target != controlScreen) {
        // A monitor of its own (the projector): fullscreen there, unless
        // "Автоматически включать полный экран" is off.
        windowHandle()->setScreen(target);
        if (wantFullScreen) {
            setGeometry(target->geometry());
            showFullScreen();
        } else {
            const QRect avail = target->availableGeometry();
            QSize size = resolution.isValid() ? resolution / target->devicePixelRatio() : avail.size() * 0.7;
            size = size.boundedTo(avail.size());
            setGeometry(QRect(avail.center() - QPoint(size.width() / 2, size.height() / 2), size));
            showNormal();
        }
        return;
    }

    // Same monitor as the control window (e.g. a single-screen laptop):
    // fullscreening would bury the controls, so show a modest preview
    // window tucked in a corner instead.
    const QRect avail = controlScreen->availableGeometry();
    const int width = avail.width() * 0.4;
    const int height = width * 9 / 16;
    const QRect geometry(avail.right() - width - 24, avail.bottom() - height - 24, width, height);
    windowHandle()->setScreen(controlScreen);
    setGeometry(geometry);
    showNormal();
}

void DisplayWindow::relayout()
{
    // "Разрешение": a fixed 16:9 frame letterboxed inside the window;
    // "Автоматически" fills it.
    const QSize resolution = fixedResolution();
    if (!resolution.isValid()) {
        m_render->setGeometry(rect());
        return;
    }
    const QSize fitted = resolution.scaled(size(), Qt::KeepAspectRatio);
    m_render->setGeometry(QRect(QPoint((width() - fitted.width()) / 2, (height() - fitted.height()) / 2), fitted));
}

void DisplayWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    relayout();
}

QString DisplayWindow::activeScreenName() const
{
    QScreen *screen = this->screen();
    return screen ? screen->name() : tr("неизвестно");
}

void DisplayWindow::closeEvent(QCloseEvent *event)
{
    QWidget::closeEvent(event);
    emit closed();
}
