#include "VideoThumbnailer.h"
#include "core/Database.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QMediaPlayer>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>

namespace {

QString cacheKey(const QString &path)
{
    const QFileInfo info(path);
    const QByteArray key = (path + QString::number(info.lastModified().toMSecsSinceEpoch())).toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Md5).toHex());
}

QString posterFile(const QString &path)
{
    const QString dir = Database::dataDir() + QStringLiteral("/thumbs");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/video-") + cacheKey(path) + QStringLiteral(".jpg");
}

bool isRemote(const QString &path)
{
    return path.startsWith(QStringLiteral("http://")) || path.startsWith(QStringLiteral("https://"));
}

} // namespace

VideoThumbnailer *VideoThumbnailer::instance()
{
    static VideoThumbnailer *thumbnailer = new VideoThumbnailer;
    return thumbnailer;
}

VideoThumbnailer::VideoThumbnailer(QObject *parent)
    : QObject(parent)
{
    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    m_timeout->setInterval(8000);
    connect(m_timeout, &QTimer::timeout, this, [this]() { finish(QImage()); });
}

QPixmap VideoThumbnailer::poster(const QString &path)
{
    const auto it = m_posters.constFind(path);
    if (it != m_posters.constEnd())
        return it.value();
    request(path);
    return QPixmap();
}

qint64 VideoThumbnailer::duration(const QString &path)
{
    const auto it = m_durations.constFind(path);
    if (it != m_durations.constEnd())
        return it.value();
    request(path);
    return -1;
}

void VideoThumbnailer::request(const QString &path)
{
    if (path.isEmpty() || isRemote(path) || m_posters.contains(path) || m_queue.contains(path) || m_current == path)
        return;

    // Disk cache from an earlier run.
    const QString cached = posterFile(path);
    const qint64 cachedDuration = QSettings().value(QStringLiteral("videoMeta/") + cacheKey(path), -1).toLongLong();
    if (QFileInfo::exists(cached) && cachedDuration >= 0) {
        m_posters.insert(path, QPixmap(cached));
        m_durations.insert(path, cachedDuration);
        return;
    }

    m_queue << path;
    if (m_current.isEmpty())
        QTimer::singleShot(0, this, &VideoThumbnailer::startNext);
}

void VideoThumbnailer::startNext()
{
    if (!m_current.isEmpty() || m_queue.isEmpty())
        return;
    m_current = m_queue.takeFirst();
    m_seeked = false;

    if (!m_player) {
        // No QAudioOutput attached: decoding is silent.
        m_player = new QMediaPlayer(this);
        m_sink = new QVideoSink(this);
        m_player->setVideoSink(m_sink);
        connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
            if (m_current.isEmpty())
                return;
            if (status == QMediaPlayer::InvalidMedia) {
                finish(QImage());
            } else if (status == QMediaPlayer::LoadedMedia && !m_seeked) {
                m_durations.insert(m_current, m_player->duration());
                m_seeked = true;
                // A second in skips black fade-ins; very short clips use a third.
                m_player->setPosition(qMin<qint64>(1000, m_player->duration() / 3));
                m_player->play();
            }
        });
        connect(m_sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
            if (m_current.isEmpty() || !m_seeked || !frame.isValid())
                return;
            if (m_player->position() < qMin<qint64>(900, m_player->duration() / 3 - 100))
                return; // still the frame before the seek landed
            finish(frame.toImage());
        });
    }
    m_player->setSource(QUrl::fromLocalFile(m_current));
    m_timeout->start();
}

void VideoThumbnailer::finish(const QImage &frame)
{
    if (m_current.isEmpty())
        return;
    m_timeout->stop();
    m_player->stop();

    const QString path = m_current;
    m_current.clear();
    QPixmap poster;
    if (!frame.isNull()) {
        const QImage scaled = frame.scaled(480, 480, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        scaled.save(posterFile(path), "JPG", 85);
        poster = QPixmap::fromImage(scaled);
    }
    m_posters.insert(path, poster);
    const qint64 duration = m_durations.value(path, -1);
    m_durations.insert(path, duration);
    if (!poster.isNull() && duration >= 0)
        QSettings().setValue(QStringLiteral("videoMeta/") + cacheKey(path), duration);
    emit ready(path);

    // Release the file before moving on.
    m_player->setSource(QUrl());
    QTimer::singleShot(0, this, &VideoThumbnailer::startNext);
}
