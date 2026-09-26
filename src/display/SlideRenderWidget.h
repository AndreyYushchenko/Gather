#pragma once

#include "SlideContent.h"

#include <QPixmap>
#include <QWidget>

class QLabel;
class QMediaPlayer;
class QAudioOutput;
class QVideoWidget;
class QGraphicsDropShadowEffect;
class TimerRenderWidget;
class TextSlideWidget;

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

    // The player of a live local video (null until one has played) — the
    // right panel's transport controls drive it.
    QMediaPlayer *mediaPlayer() const { return m_mediaPlayer; }
    QAudioOutput *audioOutput() const { return m_audioOutput; }
    // Previews (the control panel's mini preview) stay silent so the
    // projector is the only thing playing sound.
    void setAlwaysMuted(bool muted) { m_alwaysMuted = muted; }
    // The control panel's mini preview: shows the "Sermon / Нет сигнала"
    // placeholder when nothing is live, instead of the idle screen the
    // projector shows ("Экран между службами").
    void setPreviewMode(bool preview) { m_previewMode = preview; }

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void applyLabel();
    void applyBackground();
    void layoutOverlayChildren();
    // Cross-fades / slides from what's shown now ("Анимация перехода").
    void startTransition();

    SlideContent m_content;
    // Decoded images are kept while the same file stays on screen: stepping
    // through a song's slides used to re-read and re-scale its background
    // from disk on every slide (twice — projector and mini preview).
    QPixmap m_photoPixmap;       // full-slide image for SlideKind::Photo
    QString m_photoPath;
    QPixmap m_photoScaled;       // m_photoPixmap fitted to the widget
    QSize m_photoScaledFor;
    QPixmap m_backgroundPixmap;  // image behind text (dimmed if needed)
    QString m_backgroundKey;     // what m_backgroundPixmap was made from
    QSize m_backgroundScaledFor; // widget size m_backgroundLabel was filled for
    QString m_currentVideoPath;
    bool m_currentVideoMuted = false;

    QLabel *m_label = nullptr;
    TextSlideWidget *m_styledText = nullptr;
    QWidget *m_backgroundShade = nullptr;
    QLabel *m_backgroundLabel = nullptr;
    QVideoWidget *m_videoWidget = nullptr;
    QMediaPlayer *m_mediaPlayer = nullptr;
    QAudioOutput *m_audioOutput = nullptr;
    QGraphicsDropShadowEffect *m_textShadow = nullptr;
    TimerRenderWidget *m_timerWidget = nullptr;
    bool m_alwaysMuted = false;
    bool m_previewMode = false;
    bool m_hasContent = false;
    QWidget *m_transitionLabel = nullptr;
    QLabel *m_cornerLabel = nullptr;
    class QVariantAnimation *m_transitionAnimation = nullptr;
};
