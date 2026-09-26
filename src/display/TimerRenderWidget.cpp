#include "TimerRenderWidget.h"
#include "ui/Theme.h"

#include <QFontMetricsF>
#include <QHash>
#include <QLinearGradient>
#include <QMediaPlayer>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>
#include <QtMath>

namespace TimerPainter {

namespace {

QFont timerFont(const QString &family, qreal pixelSize, QFont::Weight weight)
{
    QFont font(family);
    font.setPixelSize(qMax(1, qRound(pixelSize)));
    font.setWeight(weight);
    // Tabular digits, so a running countdown doesn't jitter sideways.
    font.setFeature(QFont::Tag("tnum"), 1);
    return font;
}

Qt::Alignment horizontalAlignment(TimerAlign align)
{
    switch (align) {
    case TimerAlign::Left: return Qt::AlignLeft;
    case TimerAlign::Right: return Qt::AlignRight;
    case TimerAlign::Center: break;
    }
    return Qt::AlignHCenter;
}

void drawCover(QPainter &painter, const QRectF &bounds, const QPixmap &image)
{
    const QSizeF source = image.size();
    const qreal scale = qMax(bounds.width() / source.width(), bounds.height() / source.height());
    const QSizeF cropped(bounds.width() / scale, bounds.height() / scale);
    painter.drawPixmap(bounds, image,
                       QRectF(QPointF((source.width() - cropped.width()) / 2, (source.height() - cropped.height()) / 2), cropped));
}

// Returns true when a photo/video is behind the text (dimming + shadow apply).
bool paintBackground(QPainter &painter, const QRectF &bounds, const TimerScreen &screen, const QPixmap &image,
                     bool videoUnderneath)
{
    const QColor dim(0, 0, 0, qBound(0, screen.dimPercent, 90) * 255 / 100);
    if (videoUnderneath) {
        painter.fillRect(bounds, dim);
        return true;
    }
    if (screen.background == TimerBackground::Gradient) {
        // design.pen's "Градиент": #7c6fd6 at the top fading to #2b2350.
        QLinearGradient gradient(bounds.topLeft(), bounds.bottomLeft());
        gradient.setColorAt(0, QColor(0x7c, 0x6f, 0xd6));
        gradient.setColorAt(1, QColor(0x2b, 0x23, 0x50));
        painter.fillRect(bounds, gradient);
        return false;
    }
    if (screen.background != TimerBackground::Dark && !image.isNull()) {
        drawCover(painter, bounds, image);
        painter.fillRect(bounds, dim);
        return true;
    }
    painter.fillRect(bounds, QColor(0x12, 0x15, 0x1c));
    return false;
}

struct TextBlock {
    QString text;
    QFont font;
    QColor color;
    qreal height = 0;
    bool wrap = false;
};

} // namespace

void paintAnalogClock(QPainter &painter, const QRectF &bounds, const QTime &time, const ClockPalette &palette)
{
    const qreal diameter = qMin(bounds.width(), bounds.height());
    const qreal unit = diameter / 140.0; // design.pen's clock is 140px across
    const QPointF center = bounds.center();
    const qreal radius = diameter / 2;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);

    painter.setPen(QPen(palette.faceStroke, 2 * unit));
    painter.setBrush(palette.faceFill);
    painter.drawEllipse(center, radius - unit, radius - unit);

    painter.setPen(palette.numbers);
    painter.setFont(timerFont(Theme::fontFamily(), 13 * unit, QFont::DemiBold));
    const qreal numberDistance = radius - 14 * unit;
    const QList<QPair<int, qreal>> numbers = {{12, 0}, {3, 90}, {6, 180}, {9, 270}};
    for (const auto &number : numbers) {
        const qreal angle = qDegreesToRadians(number.second);
        const QPointF at(center.x() + numberDistance * qSin(angle), center.y() - numberDistance * qCos(angle));
        painter.drawText(QRectF(at.x() - 12 * unit, at.y() - 9 * unit, 24 * unit, 18 * unit), Qt::AlignCenter,
                         QString::number(number.first));
    }

    const auto drawHand = [&](qreal degrees, qreal length, qreal width, const QColor &color) {
        const qreal angle = qDegreesToRadians(degrees);
        QPen pen(color, width * unit);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.drawLine(center, QPointF(center.x() + length * unit * qSin(angle), center.y() - length * unit * qCos(angle)));
    };
    const qreal seconds = time.second();
    const qreal minutes = time.minute() + seconds / 60.0;
    const qreal hours = (time.hour() % 12) + minutes / 60.0;
    drawHand(hours * 30, 34, 2.5, palette.hands);
    drawHand(minutes * 6, 48, 2, palette.hands);
    drawHand(seconds * 6, 52, 1.5, palette.secondHand);

    painter.setPen(Qt::NoPen);
    painter.setBrush(palette.hands);
    painter.drawEllipse(center, 4 * unit, 4 * unit);
    painter.restore();
}

void paintSlide(QPainter &painter, const QRectF &bounds, const TimerSlide &slide, qint64 nowMs,
                const QPixmap &backgroundImage, bool videoUnderneath)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const TimerScreen &screen = slide.screen;
    const bool photoBehind = paintBackground(painter, bounds, screen, backgroundImage, videoUnderneath);
    const bool shadow = screen.textShadow && (photoBehind || screen.background == TimerBackground::Gradient);

    const TimerFrame frame = TimerFormat::frame(slide, nowMs);
    const QString family = TimerFormat::resolvedFontFamily(screen);
    const QColor color = TimerFormat::textColor(screen.textColor);
    QColor softColor = color;
    softColor.setAlpha(205);

    const qreal h = bounds.height();
    const qreal scale = TimerFormat::fontScale(screen.fontSize);
    const qreal mainSize = h * scale;
    const QRectF area = bounds.adjusted(bounds.width() * 0.06, h * 0.08, -bounds.width() * 0.06, -h * 0.1);
    const Qt::Alignment hAlign = horizontalAlignment(screen.align);
    const qreal gap = mainSize * 0.12;

    QList<TextBlock> above;
    QList<TextBlock> below;
    if (!frame.title.trimmed().isEmpty()) {
        TextBlock title{frame.title.toUpper(), timerFont(family, mainSize * 0.24, QFont::DemiBold), softColor};
        title.height = QFontMetricsF(title.font).height();
        above << title;
    }

    TextBlock mainBlock;
    if (frame.showDigital && !frame.primary.isEmpty()) {
        qreal size = frame.isMessage || frame.showAnalog ? mainSize * 0.5 : mainSize;
        const QColor mainColor = frame.mainColor.isValid() ? frame.mainColor : color;
        mainBlock = TextBlock{frame.primary, timerFont(family, size, QFont::Bold), mainColor, 0, frame.isMessage};
        if (frame.isMessage) {
            // Shrink a long message until it fits.
            for (int attempt = 0; attempt < 12; ++attempt) {
                const QRectF needed = QFontMetricsF(mainBlock.font).boundingRect(area, Qt::TextWordWrap | hAlign, mainBlock.text);
                if (needed.height() <= area.height() * 0.8 && needed.width() <= area.width())
                    break;
                size *= 0.85;
                mainBlock.font = timerFont(family, size, QFont::Bold);
            }
            mainBlock.height = QFontMetricsF(mainBlock.font).boundingRect(area, Qt::TextWordWrap | hAlign, mainBlock.text).height();
        } else {
            const qreal width = QFontMetricsF(mainBlock.font).horizontalAdvance(mainBlock.text);
            if (width > area.width())
                mainBlock.font = timerFont(family, size * area.width() / width, QFont::Bold);
            mainBlock.height = QFontMetricsF(mainBlock.font).height();
        }
    }

    for (const QString &line : frame.below) {
        TextBlock block{line, timerFont(family, mainSize * 0.2, QFont::Medium), softColor};
        const qreal width = QFontMetricsF(block.font).horizontalAdvance(line);
        if (width > area.width())
            block.font = timerFont(family, mainSize * 0.2 * area.width() / width, QFont::Medium);
        block.height = QFontMetricsF(block.font).height();
        below << block;
    }

    qreal clockDiameter = 0;
    if (frame.showAnalog)
        clockDiameter = qMin(area.width(), h * (frame.showDigital ? 0.5 : 0.72) * scale / 0.29);

    qreal total = 0;
    int parts = 0;
    for (const TextBlock &block : std::as_const(above)) { total += block.height; ++parts; }
    if (clockDiameter > 0) { total += clockDiameter; ++parts; }
    if (mainBlock.height > 0) { total += mainBlock.height; ++parts; }
    for (const TextBlock &block : std::as_const(below)) { total += block.height; ++parts; }
    total += gap * qMax(0, parts - 1);
    if (total > area.height() && clockDiameter > 0) {
        // Too tall (huge font + texts + clock): the clock is the elastic part.
        const qreal squeeze = qMin(clockDiameter, total - area.height());
        clockDiameter -= squeeze;
        total -= squeeze;
    }

    qreal y = area.center().y() - total / 2;
    const auto drawText = [&](const TextBlock &block) {
        const QRectF rect(area.left(), y, area.width(), block.height);
        const int flags = hAlign | Qt::AlignVCenter | (block.wrap ? Qt::TextWordWrap : 0);
        painter.setFont(block.font);
        if (shadow) {
            painter.setPen(QColor(0, 0, 0, 120));
            painter.drawText(rect.translated(0, qMax<qreal>(1, block.font.pixelSize() * 0.045)), flags, block.text);
        }
        painter.setPen(block.color);
        painter.drawText(rect, flags, block.text);
        y += block.height + gap;
    };

    for (const TextBlock &block : std::as_const(above))
        drawText(block);

    if (clockDiameter > 0) {
        qreal x = area.center().x() - clockDiameter / 2;
        if (screen.align == TimerAlign::Left)
            x = area.left();
        else if (screen.align == TimerAlign::Right)
            x = area.right() - clockDiameter;
        QColor faceFill = color;
        faceFill.setAlpha(28);
        QColor faceStroke = color;
        faceStroke.setAlpha(110);
        const ClockPalette palette{faceFill, faceStroke, softColor, color, QColor(Theme::AccentBlue)};
        paintAnalogClock(painter, QRectF(x, y, clockDiameter, clockDiameter), frame.clockTime, palette);
        y += clockDiameter + gap;
    }

    if (mainBlock.height > 0)
        drawText(mainBlock);
    for (const TextBlock &block : std::as_const(below))
        drawText(block);

    if (frame.progress >= 0) {
        // design.pen "Progress Track": thin bar near the bottom edge.
        const qreal thickness = qMax<qreal>(2, h * 0.012);
        const QRectF track(bounds.left() + bounds.width() * 0.055, bounds.bottom() - h * 0.07,
                           bounds.width() * 0.89, thickness);
        QColor trackColor = color;
        trackColor.setAlpha(55);
        painter.setPen(Qt::NoPen);
        painter.setBrush(trackColor);
        painter.drawRoundedRect(track, thickness / 2, thickness / 2);
        painter.setBrush(frame.mainColor.isValid() ? frame.mainColor : color);
        painter.drawRoundedRect(QRectF(track.topLeft(), QSizeF(track.width() * frame.progress, thickness)),
                                thickness / 2, thickness / 2);
    }

    painter.restore();
}

} // namespace TimerPainter

// One decoder per video file, shared by every TimerRenderWidget showing it
// (projector, mini preview, editor preview). The sink callback only keeps
// the newest frame — converting every frame to a QImage right there, three
// players at once, backed up the queued frame signals and leaked gigabytes.
// Conversion happens lazily, at most once per new frame, when painting.
class SharedVideo : public QObject {
public:
    static SharedVideo *acquire(const QString &path)
    {
        SharedVideo *video = registry().value(path);
        if (!video) {
            video = new SharedVideo(path);
            registry().insert(path, video);
        }
        ++video->m_users;
        return video;
    }

    static void release(SharedVideo *video)
    {
        if (--video->m_users > 0)
            return;
        registry().remove(video->m_path);
        video->m_player->stop();
        video->deleteLater();
    }

    QImage currentImage()
    {
        if (m_dirty) {
            m_dirty = false;
            const QImage image = m_latest.toImage();
            if (!image.isNull())
                m_image = image;
            m_latest = QVideoFrame(); // drop the decoder's buffer right away
        }
        return m_image;
    }

private:
    explicit SharedVideo(const QString &path)
        : m_path(path)
    {
        // No QAudioOutput attached: silent, an ambient background.
        m_player = new QMediaPlayer(this);
        m_sink = new QVideoSink(this);
        m_player->setVideoSink(m_sink);
        m_player->setLoops(QMediaPlayer::Infinite);
        connect(m_sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
            m_latest = frame;
            m_dirty = true;
        });
        m_player->setSource(QUrl::fromLocalFile(path));
        m_player->play();
    }

    static QHash<QString, SharedVideo *> &registry()
    {
        static QHash<QString, SharedVideo *> videos;
        return videos;
    }

    QString m_path;
    int m_users = 0;
    QMediaPlayer *m_player = nullptr;
    QVideoSink *m_sink = nullptr;
    QVideoFrame m_latest;
    QImage m_image;
    bool m_dirty = false;
};

TimerRenderWidget::TimerRenderWidget(QWidget *parent)
    : QWidget(parent)
{
    m_tick = new QTimer(this);
    m_tick->setInterval(250);
    connect(m_tick, &QTimer::timeout, this, qOverload<>(&QWidget::update));
}

TimerRenderWidget::~TimerRenderWidget()
{
    if (m_video)
        SharedVideo::release(m_video);
}

void TimerRenderWidget::setSlide(const TimerSlide &slide)
{
    m_slide = slide;
    const QString imagePath = TimerFormat::backgroundImagePath(slide.screen);
    if (imagePath != m_loadedImagePath) {
        m_loadedImagePath = imagePath;
        m_backgroundImage = imagePath.isEmpty() ? QPixmap() : QPixmap(imagePath);
    }
    updateVideo();
    update();
}

void TimerRenderWidget::setPlayVideo(bool play)
{
    m_playVideo = play;
    updateVideo();
}

void TimerRenderWidget::updateVideo()
{
    const QString path = m_playVideo ? TimerFormat::backgroundVideoPath(m_slide.screen) : QString();
    if (path == m_videoPath)
        return;
    m_videoPath = path;
    if (m_video)
        SharedVideo::release(m_video);
    m_video = path.isEmpty() ? nullptr : SharedVideo::acquire(path);
    // Video needs a smoother repaint than the 4×/s a ticking clock does.
    m_tick->setInterval(m_video ? 40 : 250);
}

void TimerRenderWidget::setCornerRadius(int radius)
{
    m_cornerRadius = radius;
    update();
}

void TimerRenderWidget::setSelected(bool selected)
{
    if (m_selected == selected)
        return;
    m_selected = selected;
    update();
}

void TimerRenderWidget::setLiveBadge(bool live)
{
    if (m_live == live)
        return;
    m_live = live;
    update();
}


void TimerRenderWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF bounds = rect();

    if (m_cornerRadius > 0) {
        QPainterPath clip;
        clip.addRoundedRect(bounds, m_cornerRadius, m_cornerRadius);
        painter.setClipPath(clip);
    }

    const QImage videoFrame = m_video ? m_video->currentImage() : QImage();
    const bool video = !videoFrame.isNull();
    if (video) {
        const QSizeF source = videoFrame.size();
        const qreal scale = qMax(bounds.width() / source.width(), bounds.height() / source.height());
        const QSizeF cropped(bounds.width() / scale, bounds.height() / scale);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(bounds, videoFrame,
                          QRectF(QPointF((source.width() - cropped.width()) / 2, (source.height() - cropped.height()) / 2), cropped));
    }
    TimerPainter::paintSlide(painter, bounds, m_slide, QDateTime::currentMSecsSinceEpoch(), m_backgroundImage, video);

    if (m_live) {
        // design.pen "Live Badge": red pill, white dot + "В ЭФИРЕ".
        QFont font(Theme::fontFamily());
        font.setPixelSize(qMax(7, int(height() * 0.085)));
        font.setBold(true);
        const QString text = tr("В ЭФИРЕ");
        const qreal textWidth = QFontMetricsF(font).horizontalAdvance(text);
        const qreal pad = font.pixelSize() * 0.6;
        const qreal dot = font.pixelSize() * 0.6;
        const QRectF badge(6, 6, pad * 2 + dot + pad * 0.7 + textWidth, font.pixelSize() * 1.6);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0xef, 0x44, 0x44));
        painter.drawRoundedRect(badge, 6, 6);
        painter.setBrush(Qt::white);
        painter.drawEllipse(QRectF(badge.left() + pad, badge.center().y() - dot / 2, dot, dot));
        painter.setPen(Qt::white);
        painter.setFont(font);
        painter.drawText(badge.adjusted(pad + dot + pad * 0.7, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
    }

    if (m_selected) {
        painter.setClipping(false);
        painter.setPen(QPen(QColor(Theme::AccentBlue), 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(bounds.adjusted(1, 1, -1, -1), qMax(0, m_cornerRadius - 1), qMax(0, m_cornerRadius - 1));
    }
}

void TimerRenderWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    m_tick->start();
}

void TimerRenderWidget::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    m_tick->stop();
}
