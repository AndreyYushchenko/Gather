#include "SlideRenderWidget.h"
#include "TimerRenderWidget.h"
#include "TextSlideWidget.h"
#include "core/AppSettings.h"
#include "ui/DisplaySettings.h"
#include "ui/Theme.h"
#include "ui/VideoThumbnailer.h"

#include <QAudioOutput>
#include <QColor>
#include <QFont>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QImageReader>
#include <QVariantAnimation>
#include <QLabel>
#include <QMediaPlayer>
#include <QPainter>
#include <QRegularExpression>
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

bool isRemoteVideo(const QString &path)
{
    return path.startsWith(QStringLiteral("http://")) || path.startsWith(QStringLiteral("https://"));
}

// Decodes an image no bigger than the slide can use: a 20-megapixel phone
// photo decoded at full size stalled the show for a moment on every photo.
QPixmap loadImage(const QString &path, bool preview)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize limit = preview ? QSize(1280, 720) : QSize(3840, 2160);
    const QSize size = reader.size();
    if (size.isValid() && (size.width() > limit.width() || size.height() > limit.height()))
        reader.setScaledSize(size.scaled(limit, Qt::KeepAspectRatioByExpanding).boundedTo(size));
    return QPixmap::fromImage(reader.read());
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

    m_backgroundShade = new QWidget(this);
    m_backgroundShade->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_backgroundShade->hide();
    m_styledText = new TextSlideWidget(this);
    m_styledText->hide();

    m_textShadow = new QGraphicsDropShadowEffect(m_label);
    m_textShadow->setColor(QColor(0, 0, 0, 220));
    m_textShadow->setBlurRadius(18);
    m_textShadow->setOffset(0, 2);
    m_textShadow->setEnabled(false);
    m_label->setGraphicsEffect(m_textShadow);

    layoutOverlayChildren();
    applyLabel();

    connect(VideoThumbnailer::instance(), &VideoThumbnailer::ready, this, [this](const QString &path) {
        const bool waiting = m_previewMode && m_backgroundKey.isEmpty() && m_hasContent
            && (m_content.backgroundPath == path || m_content.imagePath == path);
        if (waiting)
            setContent(m_content);
    });
}

namespace {

class TransitionOverlay : public QWidget {
public:
    TransitionOverlay(QWidget *parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    void setPixmap(const QPixmap &p) { m_pixmap = p; update(); }
    void setOpacity(double o) { m_opacity = o; update(); }

protected:
    void paintEvent(QPaintEvent *) override {
        if (m_pixmap.isNull() || m_opacity <= 0.0) return;
        QPainter p(this);
        p.setOpacity(m_opacity);
        p.drawPixmap(0, 0, m_pixmap);
    }

private:
    QPixmap m_pixmap;
    double m_opacity = 1.0;
};

bool sameSlide(const SlideContent &a, const SlideContent &b)
{
    return a.kind == b.kind && a.text == b.text && a.reference == b.reference && a.heading == b.heading && a.imagePath == b.imagePath
        && a.backgroundPath == b.backgroundPath && a.backgroundType == b.backgroundType;
}

} // namespace

void SlideRenderWidget::setContent(const SlideContent &content)
{
    // Timer screens redraw themselves; a restyle of the same slide isn't a
    // change of slide either.
    if (m_hasContent && !sameSlide(m_content, content) && !(m_content.kind == SlideKind::Timer && content.kind == SlideKind::Timer))
        startTransition();
    m_hasContent = true;
    m_content = content;

    if (content.kind == SlideKind::Photo) {
        if (content.imagePath != m_photoPath) {
            m_photoPath = content.imagePath;
            m_photoPixmap = loadImage(content.imagePath, m_previewMode);
            m_photoScaled = QPixmap();
        }
    } else {
        m_photoPath.clear();
        m_photoPixmap = QPixmap();
        m_photoScaled = QPixmap();
    }

    // Background behind text, or behind the idle logo.
    QString backgroundPath;
    bool dimBackground = false;
    if (content.kind == SlideKind::Text && content.backgroundType == BackgroundType::Photo
        && !content.backgroundPath.isEmpty()) {
        backgroundPath = content.backgroundPath;
        // design.pen "Preview Shade": announcements dim their photo (45%
        // black) so the white heading reads on any image.
        dimBackground = !content.heading.isEmpty();
    } else if (content.kind == SlideKind::Empty && !m_previewMode
               && AppSettings::value(AppSettings::IdleScreen).toString() == QLatin1String("logo")
               && DisplaySettings::defaultBackgroundType() == BackgroundType::Photo) {
        // "Логотип с фоном": the default background, dimmed, behind the logo.
        backgroundPath = DisplaySettings::defaultBackgroundPath();
        dimBackground = true;
    }
    // The mini preview shows a still of a video instead of decoding it a
    // second time next to the projector (both stuttered).
    QString posterPath;
    if (m_previewMode) {
        if (content.kind == SlideKind::Text && content.backgroundType == BackgroundType::Video)
            posterPath = content.backgroundPath;
        else if (content.kind == SlideKind::Video && !isRemoteVideo(content.imagePath))
            posterPath = content.imagePath;
    }
    const QString backgroundKey = !posterPath.isEmpty() ? QStringLiteral("poster|") + posterPath
        : backgroundPath.isEmpty() ? QString() : backgroundPath + (dimBackground ? QStringLiteral("|dim") : QString());
    if (backgroundKey != m_backgroundKey) {
        m_backgroundKey = backgroundKey;
        m_backgroundScaledFor = QSize();
        m_backgroundPixmap = !posterPath.isEmpty() ? VideoThumbnailer::instance()->poster(posterPath)
            : backgroundPath.isEmpty() ? QPixmap() : loadImage(backgroundPath, m_previewMode);
        if (!posterPath.isEmpty() && m_backgroundPixmap.isNull())
            m_backgroundKey.clear(); // not ready yet: retried on VideoThumbnailer::ready
        if (dimBackground && !m_backgroundPixmap.isNull()) {
            QPainter painter(&m_backgroundPixmap);
            painter.fillRect(m_backgroundPixmap.rect(), QColor(0, 0, 0, 115));
        }
    }

    if (content.kind == SlideKind::Timer) {
        if (!m_timerWidget) {
            m_timerWidget = new TimerRenderWidget(this);
            m_timerWidget->setGeometry(rect());
        }
        m_timerWidget->setSlide(content.timer);
        m_timerWidget->show();
        m_timerWidget->raise();
    } else if (m_timerWidget) {
        m_timerWidget->hide();
    }

    applyBackground();
    applyLabel();
}

void SlideRenderWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    layoutOverlayChildren();
    applyLabel();
}

void SlideRenderWidget::layoutOverlayChildren()
{
    m_backgroundLabel->setGeometry(rect());
    if (m_videoWidget)
        m_videoWidget->setGeometry(rect());
    m_label->setGeometry(rect());
    m_backgroundShade->setGeometry(rect());
    m_styledText->setGeometry(rect());
    if (m_timerWidget)
        m_timerWidget->setGeometry(rect());

    if (!m_backgroundPixmap.isNull() && m_backgroundScaledFor != size()) {
        m_backgroundScaledFor = size();
        m_backgroundLabel->setPixmap(scaledCover(m_backgroundPixmap, size()));
    }
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
        && !isRemoteVideo(m_content.imagePath);

    const bool wantVideo = (isBackgroundVideo || isLocalMainVideo) && !m_previewMode;
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
        m_audioOutput->setMuted(muted || m_alwaysMuted);

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
    m_textShadow->setEnabled(m_content.kind == SlideKind::Text && DisplaySettings::textShadow());

    layoutOverlayChildren();
    m_label->raise();
    if (m_timerWidget && m_timerWidget->isVisible())
        m_timerWidget->raise();
    if (m_transitionLabel && m_transitionLabel->isVisible())
        m_transitionLabel->raise();
}

void SlideRenderWidget::startTransition()
{
    if (m_previewMode)
        return;
    const QString mode = DisplaySettings::transition();
    if (mode == QLatin1String("none") || !isVisible() || width() < 20 || height() < 20)
        return;
    // grab() can't capture a playing video (it comes out black), so a
    // transition away from one flashed black: cut instead.
    if (m_videoWidget && m_videoWidget->isVisible())
        return;
    // A still of the outgoing slide on top, animated away over the new one.
    const QPixmap outgoing = grab();
    if (!m_transitionLabel) {
        m_transitionLabel = new TransitionOverlay(this);
        m_transitionAnimation = new QVariantAnimation(this);
        m_transitionAnimation->setStartValue(0.0);
        m_transitionAnimation->setEndValue(1.0);
        connect(m_transitionAnimation, &QVariantAnimation::finished, m_transitionLabel, &QWidget::hide);
    }
    m_transitionAnimation->stop();
    m_transitionAnimation->disconnect(this);
    static_cast<TransitionOverlay*>(m_transitionLabel)->setPixmap(outgoing);
    m_transitionLabel->setGeometry(rect());
    static_cast<TransitionOverlay*>(m_transitionLabel)->setOpacity(1.0);
    const bool slide = mode == QLatin1String("slide");
    if (slide) {
        connect(m_transitionAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            const double t = value.toDouble();
            const double eased = 1.0 - (1.0 - t) * (1.0 - t);
            m_transitionLabel->move(-qRound(width() * eased), 0);
        });
    } else {
        m_transitionLabel->move(0, 0);
        connect(m_transitionAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            static_cast<TransitionOverlay*>(m_transitionLabel)->setOpacity(1.0 - value.toDouble());
        });
    }
    m_transitionAnimation->setDuration(DisplaySettings::transitionMs());
    m_transitionLabel->show();
    m_transitionLabel->raise();
    m_transitionAnimation->start();
}

void SlideRenderWidget::applyLabel()
{
    const bool styled = m_content.kind == SlideKind::Text
        && (m_content.sourceType == ContentType::Song || m_content.sourceType == ContentType::BibleVerse);
    m_label->setVisible(!styled);
    m_styledText->setVisible(styled);
    m_backgroundShade->setVisible(styled);
    if (styled) {
        const auto style = DisplaySettings::textStyle(m_content.sourceType);
        m_backgroundShade->setStyleSheet(QStringLiteral("background: rgba(0,0,0,%1);").arg(style.dim ? qRound(style.dimOpacity * 2.55) : 0));
        m_backgroundShade->raise();
        m_styledText->setText(m_content.text, m_content.reference, m_content.chords, style, m_content.starsGapLines,
                               m_content.fitGroup);
        m_styledText->raise();
        m_textShadow->setEnabled(false);
    }
    // "Нумеровать куплеты": the verse number, small, top-left.
    if (!m_content.cornerLabel.isEmpty() && m_content.kind == SlideKind::Text) {
        if (!m_cornerLabel)
            m_cornerLabel = new QLabel(this);
        m_cornerLabel->setText(m_content.cornerLabel);
        m_cornerLabel->setStyleSheet(QStringLiteral("color: rgba(255,255,255,0.75); font-weight: 700; font-size: %1px; background: transparent;")
                                         .arg(qMax(9, height() / 18)));
        m_cornerLabel->adjustSize();
        m_cornerLabel->move(qMax(6, width() / 40), qMax(4, height() / 30));
        m_cornerLabel->show();
        m_cornerLabel->raise();
    } else if (m_cornerLabel) {
        m_cornerLabel->hide();
    }

    Qt::Alignment textAlignment = DisplaySettings::qtAlignment();
    if (m_content.hasOwnAlignment) {
        textAlignment = Qt::AlignVCenter
            | (m_content.align == TextAlign::Left ? Qt::AlignLeft : m_content.align == TextAlign::Right ? Qt::AlignRight : Qt::AlignHCenter);
    }
    m_label->setAlignment(m_content.kind == SlideKind::Text ? textAlignment : Qt::AlignCenter);
    m_label->setTextFormat(m_content.kind == SlideKind::Text && m_content.heading.isEmpty() ? Qt::PlainText : Qt::AutoText);

    switch (m_content.kind) {
    case SlideKind::Empty: {
        m_label->setPixmap(QPixmap());
        if (!m_previewMode) {
            // "Экран между службами": the church logo (or nothing / black).
            m_label->setText(QString());
            const bool logo = AppSettings::value(AppSettings::IdleScreen).toString() == QLatin1String("logo")
                && AppSettings::value(AppSettings::ShowLogo).toBool();
            if (logo) {
                const QString path = AppSettings::value(AppSettings::LogoPath).toString();
                QPixmap pixmap(path.isEmpty() ? QStringLiteral(":/icons/app-256.png") : path);
                if (!pixmap.isNull()) {
                    const QSize box(width() / 2, height() * 2 / 5);
                    QPixmap scaled = pixmap.scaled(box * devicePixelRatioF(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    scaled.setDevicePixelRatio(devicePixelRatioF());
                    m_label->setPixmap(scaled);
                }
            }
            break;
        }
        // Previously there was a "Sermon - Нет сигнала" watermark here,
        // but the user wants the preview to be completely clear when hidden.
        m_label->setPixmap(QPixmap());
        m_label->setText(QString());
        break;
    }

    case SlideKind::Timer: // drawn by m_timerWidget on top
        m_label->setPixmap(QPixmap());
        m_label->setText(QString());
        break;

    case SlideKind::Text: {
        if (styled) break;
        m_label->setPixmap(QPixmap());
        if (!m_content.heading.isEmpty()) {
            // design.pen "Preview Frame": heading 24/800 and lines 14/500 on
            // a 200px-tall slide — the same proportions at any size.
            const double scale = m_content.textScale * DisplaySettings::textScalePercent(m_content.sourceType) / 100.0;
            const int headingPx = qMax(8, qRound(height() * 0.12 * scale));
            const int linePx = qMax(6, qRound(height() * 0.07 * scale));
            const QString align = m_content.align == TextAlign::Left ? QStringLiteral("left")
                : m_content.align == TextAlign::Right ? QStringLiteral("right") : QStringLiteral("center");
            QString html = QStringLiteral("<div align='%1' style='font-size:%2px; font-weight:800;'>%3</div>")
                               .arg(align).arg(headingPx).arg(m_content.heading.toHtmlEscaped());
            for (const QString &line : m_content.text.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
                html += QStringLiteral("<div align='%1' style='font-size:%2px; font-weight:500; color:#f1f3f6; margin-top:%3px;'>%4</div>")
                            .arg(align).arg(linePx).arg(qMax(2, linePx / 3)).arg(line.toHtmlEscaped());
            m_label->setText(html);
            m_label->setStyleSheet(QStringLiteral("color: white; background: transparent; padding: 0 %1px;").arg(qMax(8, width() / 16)));
            m_label->setFont(QFont(DisplaySettings::fontFamily(m_content.sourceType)));
            break;
        }
        if (m_content.chords) {
            // Chords as small gold superscripts right before the syllable.
            QString html = m_content.text.toHtmlEscaped();
            static const QRegularExpression chord(QStringLiteral(R"(\[([^\]\s]{1,8})\])"));
            html.replace(chord, QStringLiteral("<sup style='color:#ffd166; font-weight:600;'>\\1</sup>"));
            html.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
            m_label->setTextFormat(Qt::RichText);
            m_label->setText(html);
        } else {
            m_label->setText(m_content.text);
        }
        m_label->setStyleSheet(QStringLiteral("color: white; font-weight: 700; background: transparent;"));
        QFont font(DisplaySettings::fontFamily(m_content.sourceType));
        font.setPointSize(DisplaySettings::fontPointSize(m_content.sourceType, height()));
        font.setWeight(QFont::Bold);
        m_label->setFont(font);
        break;
    }

    case SlideKind::Photo:
        m_label->setText(QString());
        if (!m_photoPixmap.isNull()) {
            if (m_photoScaled.isNull() || m_photoScaledFor != size()) {
                m_photoScaledFor = size();
                m_photoScaled = m_photoPixmap.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
            m_label->setPixmap(m_photoScaled);
        }
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
    if (m_transitionLabel && m_transitionLabel->isVisible())
        m_transitionLabel->raise();
}
