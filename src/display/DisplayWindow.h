#pragma once

#include "SlideContent.h"

#include <QWidget>

class QScreen;
class SlideRenderWidget;

// Output window shown on the projector / second monitor. Which monitor,
// fullscreen or not and the frame size come from Настройки → Показ.
class DisplayWindow : public QWidget {
    Q_OBJECT
public:
    explicit DisplayWindow(QWidget *parent = nullptr);

    void setContent(const SlideContent &content);
    // `controlScreen` is where the operator's window is; the projector goes
    // to another monitor when there is one.
    void showOnBestScreen(QScreen *controlScreen = nullptr);
    void relayout();
    QString activeScreenName() const;
    SlideRenderWidget *renderWidget() const { return m_render; }

    static QScreen *targetScreen(QScreen *controlScreen);
    // Invalid = "Автоматически".
    static QSize fixedResolution();

signals:
    void closed();

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    SlideRenderWidget *m_render = nullptr;
};
