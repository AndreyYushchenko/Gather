#pragma once

#include "SlideContent.h"

#include <QWidget>

class SlideRenderWidget;

// Fullscreen output window shown on the projector / second monitor.
class DisplayWindow : public QWidget {
    Q_OBJECT
public:
    explicit DisplayWindow(QWidget *parent = nullptr);

    void setContent(const SlideContent &content);
    void showOnBestScreen();
    QString activeScreenName() const;

signals:
    void closed();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    SlideRenderWidget *m_render = nullptr;
};
