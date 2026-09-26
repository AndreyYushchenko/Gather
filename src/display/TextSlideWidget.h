#pragma once

#include "ui/DisplaySettings.h"
#include <QPixmap>
#include <QWidget>

// Shared text renderer for settings previews, the projector and live preview.
// Typography is expressed in pixels on a 1920 × 1080 canvas.
class TextSlideWidget : public QWidget {
public:
    explicit TextSlideWidget(QWidget *parent = nullptr);
    // `starsGapLines` >= 0: the last line is the "***" end-of-song cue,
    // set that many text lines below the words (see SlideContent).
    // `fitGroup`: the other slides of the same song; the text gets the size
    // that fits all of them, so it doesn't change from slide to slide.
    void setText(const QString &text, const QString &reference, bool chords,
                 const DisplaySettings::TextStyle &style, double starsGapLines = -1,
                 const QStringList &fitGroup = {});

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void drawText(QPainter &painter);

    // Laying the text out (auto-fit tries several sizes) and its 9-pass
    // shadow are far too slow for every repaint — a fade repaints this 60×/s.
    QPixmap m_cache;
    QString m_text;
    QString m_reference;
    bool m_chords = false;
    double m_starsGapLines = -1;
    QStringList m_fitGroup;
    // Size fitted to the whole group, and what it was computed for.
    int m_groupSize = -1;
    QString m_groupSizeKey;
    DisplaySettings::TextStyle m_style;
};
