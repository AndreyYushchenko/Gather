#include "DisplayWindow.h"
#include "SlideRenderWidget.h"

#include <QCloseEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QScreen>
#include <QVBoxLayout>
#include <QWindow>

DisplayWindow::DisplayWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(tr("Gather — показ"));
    setWindowFlag(Qt::Window, true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_render = new SlideRenderWidget(this);
    layout->addWidget(m_render);

    setContent(SlideContent{});
}

void DisplayWindow::setContent(const SlideContent &content)
{
    m_render->setContent(content);
}

void DisplayWindow::showOnBestScreen()
{
    const QList<QScreen *> screens = QGuiApplication::screens();

    if (screens.size() > 1) {
        // Real two-monitor setup: this is the projector output, so it
        // belongs fullscreen on the second screen.
        QScreen *target = screens.at(1);
        setGeometry(target->geometry());
        showFullScreen();
        windowHandle()->setScreen(target);
        setGeometry(target->geometry());
        return;
    }

    // Single-monitor setup (e.g. while developing/testing): fullscreening
    // over the only screen would bury the control window underneath it.
    // Show a normal, modestly sized preview window tucked in a corner
    // instead, and don't steal focus from whatever the operator is doing.
    QScreen *screen = screens.first();
    const QRect avail = screen->availableGeometry();
    const int width = avail.width() * 0.4;
    const int height = width * 9 / 16;
    const QRect geometry(avail.right() - width - 24, avail.bottom() - height - 24, width, height);
    setWindowState(Qt::WindowNoState);
    setGeometry(geometry);
    show();
}

QString DisplayWindow::activeScreenName() const
{
    QScreen *screen = this->screen();
    return screen ? screen->name() : tr("неизвестно");
}

void DisplayWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(event);
}

void DisplayWindow::closeEvent(QCloseEvent *event)
{
    QWidget::closeEvent(event);
    emit closed();
}
