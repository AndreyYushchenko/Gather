#include "SlideRenderWidget.h"
#include "ui/DisplaySettings.h"

#include <QAudioOutput>
#include <QColor>
#include <QFont>
#include <QGraphicsDropShadowEffect>
#include <QLabel>
#include <QMediaPlayer>
#include <QResizeEvent>
#include <QUrl>
#include <QVideoWidget>

namespace {

// Scales a pixmap to fill (and crop to) the target size, like CSS
// "background-size: cover", so backgrounds never letterbox behind text.
QPixmap scaledCover(const QPixmap &source, const QSize &target)
{
    if (source.isNull() || target.isEmpty())
        return QPixmap();

    const QPixmap scaled = source.scaled(target, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const int x = (scaled.width() - target.width()) / 2;
    const int y = (scaled.height() - target.height()) / 2;
    return scaled.copy(x, y, target.width(), target.height());
}

} // namespace

SlideRenderWidget::SlideRenderWidget(QWidget *parent)
    : QWidget(parent)
{
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(0x0b, 0x0e, 0x14));
    setPalette(pal);

    m_backgroundLabel = new QLabel(this);
    m_backgroundLabel->setScaledContents(false);
    m_backgroundLabel->hide();

    m_label = new QLabel(this);
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setWordWrap(true);

    m_textShadow = new QGraphicsDropShadowEffect(m_label);
    m_textShadow->setColor(QColor(0, 0, 0, 220));
    m_textShadow->setBlurRadius(18);
    m_textShadow->setOffset(0, 2);
    m_textShadow->setEnabled(false);
    m_label->setGraphicsEffect(m_textShadow);

    layoutOverlayChildren();
    applyLabel();
}

void SlideRenderWidget::setContent(const SlideContent &content)
{
    m_content = content;

    if (content.kind == SlideKind::Photo)
        m_photoPixmap.load(content.imagePath);
    else
        m_photoPixmap = QPixmap();

    if (content.kind == SlideKind::Text && content.backgroundType == BackgroundType::Photo
        && !content.backgroundPath.isEmpty())
        m_backgroundPixmap.load(content.backgroundPath);
    else
        m_backgroundPixmap = QPixmap();

    applyBackground();
    applyLabel();
}

void SlideRenderWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    layoutOverlayChildren();
    applyLabel();
}

void SlideRenderWidget::refreshStyle()
{
    applyLabel();
}

void SlideRenderWidget::layoutOverlayChildren()
{
    m_backgroundLabel->setGeometry(rect());
    if (m_videoWidget)
        m_videoWidget->setGeometry(rect());
    m_label->setGeometry(rect());

    if (!m_backgroundPixmap.isNull())
        m_backgroundLabel->setPixmap(scaledCover(m_backgroundPixmap, size()));
}

void SlideRenderWidget::applyBackground()
{
    // Two unrelated (mutually exclusive, since they depend on different
    // SlideKinds) cases share the same QMediaPlayer/QVideoWidget: a looping
    // muted video behind Text-slide content, and a Video slide's own local
    // file played as the main, audible output. A remote (http/https) Video
    // source — e.g. a YouTube link — can't be decoded by QMediaPlayer, so it
    // falls back to a placeholder message via applyLabel() instead.
    const bool isBackgroundVideo = m_content.kind == SlideKind::Text
        && m_content.backgroundType == BackgroundType::Video
        && !m_content.backgroundPath.isEmpty();
    const bool isLocalMainVideo = m_content.kind == SlideKind::Video
        && !m_content.imagePath.isEmpty()
        && !m_content.imagePath.startsWith(QStringLiteral("http://"))
        && !m_content.imagePath.startsWith(QStringLiteral("https://"));

    const bool wantVideo = isBackgroundVideo || isLocalMainVideo;
    const QString path = isBackgroundVideo ? m_content.backgroundPath : m_content.imagePath;
    // Background video is always a muted, looping ambience behind text.
    // Main video defaults to audible/one-shot but can be looped as an
    // ambient background too ("Как фон (в цикле)"), in which case it's
    // muted the same way.
    const bool muted = isBackgroundVideo || (isLocalMainVideo && m_content.videoLoop);
    const bool loop = isBackgroundVideo || (isLocalMainVideo && m_content.videoLoop);

    if (wantVideo) {
        if (!m_mediaPlayer) {
            m_mediaPlayer = new QMediaPlayer(this);
            m_audioOutput = new QAudioOutput(this);
            m_mediaPlayer->setAudioOutput(m_audioOutput);

            m_videoWidget = new QVideoWidget(this);
            m_videoWidget->setAspectRatioMode(isBackgroundVideo ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio);
            m_mediaPlayer->setVideoOutput(m_videoWidget);
            m_videoWidget->lower();
            m_videoWidget->show();
        }

        m_videoWidget->setAspectRatioMode(isBackgroundVideo ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio);
        m_mediaPlayer->setLoops(loop ? QMediaPlayer::Infinite : 1);
        m_audioOutput->setMuted(muted);

        if (m_currentVideoPath != path || m_currentVideoMuted != muted) {
            m_currentVideoPath = path;
            m_currentVideoMuted = muted;
            m_mediaPlayer->setSource(QUrl::fromLocalFile(path));
            m_mediaPlayer->play();
        } else if (m_mediaPlayer->playbackState() != QMediaPlayer::PlayingState) {
            m_mediaPlayer->play();
        }
        m_videoWidget->show();
    } else if (m_mediaPlayer) {
        m_mediaPlayer->stop();
        m_currentVideoPath.clear();
        if (m_videoWidget)
            m_videoWidget->hide();
    }

    m_backgroundLabel->setVisible(!wantVideo && !m_backgroundPixmap.isNull());
    m_textShadow->setEnabled(isBackgroundVideo || !m_backgroundPixmap.isNull());

    layoutOverlayChildren();
    m_label->raise();
}

void SlideRenderWidget::applyLabel()
{
    m_label->setAlignment(m_content.kind == SlideKind::Text ? DisplaySettings::qtAlignment() : Qt::AlignCenter);

    switch (m_content.kind) {
    case SlideKind::Empty:
        m_label->setPixmap(QPixmap());
        m_label->setText(QStringLiteral("Gather"));
        m_label->setStyleSheet(QStringLiteral("color: #333c4d; font-weight: 700;"));
        m_label->setFont(QFont(QStringLiteral("Inter"), qBound(14, height() / 14, 40)));
        break;

    case SlideKind::Black:
        m_label->setPixmap(QPixmap());
        m_label->setText(QString());
        break;

    case SlideKind::Text: {
        m_label->setPixmap(QPixmap());
        m_label->setText(m_content.text);
        m_label->setStyleSheet(QStringLiteral("color: white; font-weight: 700; background: transparent;"));
        QFont font(DisplaySettings::fontFamily(m_content.sourceType));
        font.setPointSize(DisplaySettings::fontPointSize(m_content.sourceType, height()));
        font.setWeight(QFont::Bold);
        m_label->setFont(font);
        break;
    }

    case SlideKind::Photo:
        m_label->setText(QString());
        if (!m_photoPixmap.isNull())
            m_label->setPixmap(m_photoPixmap.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        break;

    case SlideKind::Video:
        m_label->setPixmap(QPixmap());
        if (m_content.imagePath.startsWith(QStringLiteral("http://"))
            || m_content.imagePath.startsWith(QStringLiteral("https://"))) {
            // QMediaPlayer can't decode a YouTube page directly; this kind of
            // source only plays through the OBS/browser output (DisplayServer
            // embeds it as an iframe there).
            m_label->setText(tr("Видео по ссылке доступно через вывод на OBS\n(см. веб-адрес в панели показа)"));
            m_label->setStyleSheet(QStringLiteral("color: #8b95a6; font-weight: 600; background: transparent;"));
            m_label->setFont(QFont(QStringLiteral("Inter"), qBound(11, height() / 26, 22)));
        } else {
            m_label->setText(QString());
        }
        break;
    }
}
