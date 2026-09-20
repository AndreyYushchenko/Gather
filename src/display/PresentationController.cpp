#include "PresentationController.h"
#include "DisplayServer.h"
#include "DisplayWindow.h"
#include "SlideContentBuilder.h"

namespace {
constexpr quint16 ObsServerPort = 8090;
}

PresentationController::PresentationController(QObject *parent)
    : QObject(parent)
{
    m_server = new DisplayServer(this);
    m_server->start(ObsServerPort);
}

PresentationController::~PresentationController()
{
    if (m_window)
        m_window->deleteLater();
}

void PresentationController::goLive(const ContentItem &item, int slideIndex, bool loopVideo)
{
    m_liveItem = item;
    m_liveLoopVideo = loopVideo;

    switch (item.type) {
    case ContentType::Song:
    case ContentType::BibleVerse:
        m_liveSlides = item.slides();
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

void PresentationController::stepForward()
{
    if (!m_liveItem.has_value() || m_liveSlideIndex >= m_liveSlides.size() - 1)
        return;
    ++m_liveSlideIndex;
    applyCurrent();
    emit liveSlideChanged(m_liveSlideIndex, m_liveSlides.size());
}

void PresentationController::stepBack()
{
    if (!m_liveItem.has_value() || m_liveSlideIndex <= 0)
        return;
    --m_liveSlideIndex;
    applyCurrent();
    emit liveSlideChanged(m_liveSlideIndex, m_liveSlides.size());
}

void PresentationController::setBlack(bool black)
{
    if (m_black == black)
        return;
    m_black = black;
    applyCurrent();
    emit blackChanged(m_black);
}

void PresentationController::toggleBlack()
{
    setBlack(!m_black);
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
    if (m_window)
        return;

    m_window = new DisplayWindow();
    connect(m_window, &DisplayWindow::closed, this, [this]() {
        emit displayWindowVisibilityChanged(false);
    });
    m_window->showOnBestScreen();
    emit displayWindowVisibilityChanged(true);
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
    m_window->activateWindow();
}

QString PresentationController::activeScreenName() const
{
    return m_window ? m_window->activeScreenName() : QString();
}

SlideContent PresentationController::computeContent() const
{
    SlideContent content;

    if (m_black) {
        content.kind = SlideKind::Black;
        return content;
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
