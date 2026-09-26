#include "PresentationController.h"
#include "DisplayServer.h"
#include "DisplayWindow.h"
#include "SlideContentBuilder.h"
#include "SlideRenderWidget.h"

#include <QGuiApplication>
#include <QTimer>
#include <QWidget>
#include <climits>

namespace {
constexpr quint16 ObsServerPort = 8090;
}

PresentationController::PresentationController(QObject *parent)
    : QObject(parent)
{
    m_server = new DisplayServer(this);
    m_server->start(ObsServerPort);

    connect(qGuiApp, &QGuiApplication::screenAdded, this, &PresentationController::onScreensChanged);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &PresentationController::onScreensChanged);
}

void PresentationController::onScreensChanged()
{
    // Windows reshuffles the desktop for a moment after a monitor comes or
    // goes; place the window once that has settled. Without this, a projector
    // switched on after launch was never used, and one that dropped out left
    // the fullscreen show window covering the operator's screen.
    QTimer::singleShot(800, this, [this]() {
        if (m_window && m_window->isVisible())
            m_window->showOnBestScreen(m_controlWindow ? m_controlWindow->screen() : nullptr);
    });
}

PresentationController::~PresentationController()
{
    if (m_window)
        m_window->deleteLater();
}

void PresentationController::goLive(const ContentItem &item, int slideIndex, bool loopVideo)
{
    stopPlaylist();
    showItem(item, slideIndex, loopVideo);
}

void PresentationController::goLivePlaylist(const QList<ContentItem> &items, int index, int playlistId)
{
    if (items.isEmpty())
        return;
    m_livePlaylist = items;
    m_livePlaylistId = playlistId;
    m_livePlaylistIndex = qBound(0, index, int(items.size()) - 1);
    showItem(m_livePlaylist.at(m_livePlaylistIndex), 0, false);
    emit playlistPositionChanged();
}

void PresentationController::stopPlaylist()
{
    if (m_livePlaylist.isEmpty())
        return;
    m_livePlaylist.clear();
    m_livePlaylistIndex = -1;
    m_livePlaylistId = -1;
    emit playlistPositionChanged();
    if (m_hidden) {
        m_hidden = false;
        applyCurrent();
    }
}

void PresentationController::showItem(const ContentItem &item, int slideIndex, bool loopVideo)
{
    m_liveItem = item;
    m_liveTimer.reset();
    m_livePhotos.clear();
    m_liveLoopVideo = loopVideo;
    if (m_hidden) m_hidden = false;

    switch (item.type) {
    case ContentType::Song:
    case ContentType::BibleVerse:
        m_liveSlides = item.presentationSlides();
        break;
    case ContentType::Announcement: {
        QString combined = item.title;
        if (!item.text.trimmed().isEmpty())
            combined += QStringLiteral("\n\n") + item.text;
        m_liveSlides = QStringList{combined};
        break;
    }
    case ContentType::Photo:
    case ContentType::Video:
        m_liveSlides = QStringList{QString()};
        break;
    }

    if (m_liveSlides.isEmpty())
        m_liveSlides << QString();

    m_liveSlideIndex = qBound(0, slideIndex, m_liveSlides.size() - 1);

    ensureDisplayWindow();
    applyCurrent();
    emit liveSlideChanged(m_liveSlideIndex, m_liveSlides.size());
}

void PresentationController::goLiveTimer(const TimerSlide &slide)
{
    stopPlaylist();
    m_liveItem.reset();
    m_livePhotos.clear();
    m_liveTimer = slide;
    m_liveSlides = QStringList{QString()};
    m_liveSlideIndex = 0;

    ensureDisplayWindow();
    applyCurrent();
    emit liveSlideChanged(m_liveSlideIndex, m_liveSlides.size());
}

void PresentationController::goLivePhotos(const QList<ContentItem> &photos, int startIndex)
{
    if (photos.isEmpty())
        return;
    if (photos.size() == 1) {
        goLive(photos.first());
        return;
    }
    stopPlaylist();
    m_livePhotos = photos;
    m_liveTimer.reset();
    m_liveItem = photos.value(startIndex);
    m_liveSlides.clear();
    for (int i = 0; i < photos.size(); ++i)
        m_liveSlides << QString();
    m_liveSlideIndex = qBound(0, startIndex, int(photos.size()) - 1);

    ensureDisplayWindow();
    applyCurrent();
    emit liveSlideChanged(m_liveSlideIndex, m_liveSlides.size());
}

void PresentationController::updateLiveTimer(const TimerSlide &slide)
{
    if (!m_liveTimer.has_value())
        return;
    m_liveTimer = slide;
    applyCurrent();
}

void PresentationController::stepForward()
{
    if (!m_liveItem.has_value())
        return;
        
    if (m_hidden) {
        m_hidden = false;
        applyCurrent();
        emit liveSlideChanged(m_liveSlideIndex, m_liveSlides.size());
        return;
    }
        
    if (m_liveSlideIndex >= m_liveSlides.size() - 1) {
        if (!m_livePlaylist.isEmpty() && m_livePlaylistIndex + 1 < m_livePlaylist.size()) {
            ++m_livePlaylistIndex;
            showItem(m_livePlaylist.at(m_livePlaylistIndex), 0, false);
            emit playlistPositionChanged();
        }
        return;
    }
    ++m_liveSlideIndex;
    applyCurrent();
    emit liveSlideChanged(m_liveSlideIndex, m_liveSlides.size());
}

void PresentationController::stepBack()
{
    if (!m_liveItem.has_value())
        return;
        
    if (m_hidden) {
        m_hidden = false;
        applyCurrent();
        emit liveSlideChanged(m_liveSlideIndex, m_liveSlides.size());
        return;
    }
        
    if (m_liveSlideIndex <= 0) {
        if (!m_livePlaylist.isEmpty() && m_livePlaylistIndex > 0) {
            --m_livePlaylistIndex;
            // Back into the previous item at its last slide.
            showItem(m_livePlaylist.at(m_livePlaylistIndex), INT_MAX, false);
            emit playlistPositionChanged();
        }
        return;
    }
    --m_liveSlideIndex;
    applyCurrent();
    emit liveSlideChanged(m_liveSlideIndex, m_liveSlides.size());
}



void PresentationController::toggleFrozen()
{
    m_frozen = !m_frozen;
    if (!m_frozen)
        applyCurrent();
    emit frozenChanged(m_frozen);
}

void PresentationController::refreshDisplayStyle()
{
    const SlideContent content = computeContent();
    if (m_window)
        m_window->setContent(content);
    if (m_server)
        m_server->setContent(content);
    emit contentChanged(content);
}

void PresentationController::ensureDisplayWindow()
{
    if (m_window) {
        // Closed with Esc earlier: bring it back for the new slide.
        if (!m_window->isVisible()) {
            m_window->showOnBestScreen(m_controlWindow ? m_controlWindow->screen() : nullptr);
            emit displayWindowVisibilityChanged(true);
        }
        return;
    }

    m_window = new DisplayWindow();
    connect(m_window, &DisplayWindow::closed, this, [this]() {
        emit displayWindowVisibilityChanged(false);
    });
    m_window->showOnBestScreen(m_controlWindow ? m_controlWindow->screen() : nullptr);
    emit displayWindowVisibilityChanged(true);
}

void PresentationController::openDisplayWindow()
{
    ensureDisplayWindow();
    if (!m_window->isVisible())
        m_window->showOnBestScreen(m_controlWindow ? m_controlWindow->screen() : nullptr);
    m_window->setContent(computeContent());
    emit displayWindowVisibilityChanged(true);
}

void PresentationController::endShow()
{
    if (m_frozen) {
        m_frozen = false;
        emit frozenChanged(false);
    }
    
    if (m_hidden) return;
    
    m_hidden = true;
    applyCurrent();
    emit liveSlideChanged(0, 0); // Hide preview selection
}

void PresentationController::applyDisplaySettings(bool placementChanged)
{
    if (m_liveItem && (m_liveItem->type == ContentType::Song || m_liveItem->type == ContentType::BibleVerse)) {
        m_liveSlides = m_liveItem->presentationSlides();
        if (m_liveSlides.isEmpty()) m_liveSlides << QString();
        m_liveSlideIndex = qBound(0, m_liveSlideIndex, int(m_liveSlides.size()) - 1);
        emit liveSlideChanged(m_liveSlideIndex, m_liveSlides.size());
    }
    if (placementChanged && m_window && m_window->isVisible()) {
        m_window->showOnBestScreen(m_controlWindow ? m_controlWindow->screen() : nullptr);
        m_window->relayout();
    }
    refreshDisplayStyle();
}

bool PresentationController::isDisplayWindowVisible() const
{
    return m_window && m_window->isVisible();
}

void PresentationController::raiseDisplayWindow()
{
    if (!m_window)
        return;
    m_window->raise();
}

QMediaPlayer *PresentationController::liveVideoPlayer() const
{
    return m_window ? m_window->renderWidget()->mediaPlayer() : nullptr;
}

QAudioOutput *PresentationController::liveAudioOutput() const
{
    return m_window ? m_window->renderWidget()->audioOutput() : nullptr;
}

void PresentationController::toggleDisplayFullScreen()
{
    if (!m_window || !m_window->isVisible())
        return;
    if (m_window->isFullScreen())
        m_window->showNormal();
    else
        m_window->showFullScreen();
    m_window->raise();
}

QString PresentationController::activeScreenName() const
{
    return m_window ? m_window->activeScreenName() : QString();
}

SlideContent PresentationController::computeContent() const
{
    SlideContent content;

    if (m_hidden) {
        content.kind = SlideKind::Empty;
        return content;
    }

    if (m_liveTimer.has_value()) {
        content.kind = SlideKind::Timer;
        content.timer = *m_liveTimer;
        return content;
    }

    if (!m_livePhotos.isEmpty()) {
        const ContentItem &photo = m_livePhotos.at(qBound(0, m_liveSlideIndex, int(m_livePhotos.size()) - 1));
        return buildSlideContent(photo, QStringList{QString()}, 0, false);
    }

    if (!m_liveItem.has_value()) {
        content.kind = SlideKind::Empty;
        return content;
    }

    return buildSlideContent(*m_liveItem, m_liveSlides, m_liveSlideIndex, m_liveLoopVideo);
}

void PresentationController::applyCurrent()
{
    if (m_frozen)
        return;

    const SlideContent content = computeContent();
    if (m_window)
        m_window->setContent(content);
    if (m_server)
        m_server->setContent(content);
    emit contentChanged(content);
}
