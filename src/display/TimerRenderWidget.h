#pragma once

#include "TimerScreen.h"

#include <QImage>
#include <QPixmap>
#include <QWidget>

class QPainter;
class QTimer;
class SharedVideo;

namespace TimerPainter {

struct ClockPalette {
    QColor faceFill;
    QColor faceStroke;
    QColor numbers;
    QColor hands;
    QColor secondHand;
};

// design.pen's 140×140 clock face: 12/3/6/9 numerals, hour, minute and blue
// second hand, scaled to whatever square `bounds` is.
void paintAnalogClock(QPainter &painter, const QRectF &bounds, const QTime &time, const ClockPalette &palette);

// One timer screen at `nowMs`: background, title, clock/numbers/message,
// lines below and the progress bar. `backgroundImage` is the pre-loaded
// image for photo backgrounds; with `videoUnderneath` the background is
// left transparent (only the dimming is drawn) so a playing video shows
// through.
void paintSlide(QPainter &painter, const QRectF &bounds, const TimerSlide &slide, qint64 nowMs,
                const QPixmap &backgroundImage, bool videoUnderneath = false);

} // namespace TimerPainter

// Live, self-ticking rendering of a TimerSlide: the projector window and
// mini preview (inside SlideRenderWidget), the editor's big preview and the
// "Мои экраны" thumbnails.
class TimerRenderWidget : public QWidget {
    Q_OBJECT
public:
    explicit TimerRenderWidget(QWidget *parent = nullptr);
    ~TimerRenderWidget() override;

    void setSlide(const TimerSlide &slide);
    const TimerSlide &slide() const { return m_slide; }

    void setCornerRadius(int radius);
    void setSelected(bool selected);
    // A red "В ЭФИРЕ" badge in the corner (thumbnails of the live screen).
    void setLiveBadge(bool live);
    // Video backgrounds play inside this widget (decoded frames painted
    // under the text — a QVideoWidget would cover the text instead). Off
    // for thumbnails, which show the still frame.
    void setPlayVideo(bool play);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    TimerSlide m_slide;
    QString m_loadedImagePath;
    QPixmap m_backgroundImage;
    int m_cornerRadius = 0;
    bool m_selected = false;
    bool m_live = false;
    bool m_playVideo = true;
    QString m_videoPath;
    SharedVideo *m_video = nullptr;
    QTimer *m_tick = nullptr;

    void updateVideo();
};
