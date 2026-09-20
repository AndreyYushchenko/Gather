#pragma once

#include "SlideContent.h"

#include <QPixmap>
#include <QWidget>

class QLabel;
class QMediaPlayer;
class QAudioOutput;
class QVideoWidget;
class QGraphicsDropShadowEffect;

// Renders a single slide (big centered text, full-screen photo, or black).
// Used both full-size in the projector window and scaled down as a mini
// preview in the control panel, so font size adapts to widget height.
// A Text slide can additionally carry a photo or looping muted video shown
// behind the text (see ContentItem::backgroundType).
class SlideRenderWidget : public QWidget {
    Q_OBJECT
public:
    explicit SlideRenderWidget(QWidget *parent = nullptr);

    void setContent(const SlideContent &content);

    // Re-applies font/size/alignment from DisplaySettings without changing
    // content — called when the operator changes those settings live.
    void refreshStyle();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void applyLabel();
    void applyBackground();
    void layoutOverlayChildren();

    SlideContent m_content;
    QPixmap m_photoPixmap;       // full-slide image for SlideKind::Photo
    QPixmap m_backgroundPixmap;  // scaled-to-cover image behind text
    QString m_currentVideoPath;
    bool m_currentVideoMuted = false;

    QLabel *m_label = nullptr;
    QLabel *m_backgroundLabel = nullptr;
    QVideoWidget *m_videoWidget = nullptr;
    QMediaPlayer *m_mediaPlayer = nullptr;
    QAudioOutput *m_audioOutput = nullptr;
    QGraphicsDropShadowEffect *m_textShadow = nullptr;
};
