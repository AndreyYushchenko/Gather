#include "ThumbnailCache.h"
#include "core/Database.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QMetaObject>
#include <QRunnable>
#include <QThreadPool>

namespace {

constexpr int ThumbLongSide = 520;

QString diskCachePath(const QString &path)
{
    const QFileInfo info(path);
    const QByteArray key = (path + QString::number(info.lastModified().toMSecsSinceEpoch())).toUtf8();
    const QString dir = Database::dataDir() + QStringLiteral("/thumbs");
    QDir().mkpath(dir);
    return dir + QLatin1Char('/') + QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Md5).toHex())
        + QStringLiteral(".jpg");
}

class ThumbnailJob : public QRunnable {
public:
    ThumbnailJob(QString path, ThumbnailCache *cache)
        : m_path(std::move(path))
        , m_cache(cache)
    {
    }

    void run() override
    {
        const QString cached = diskCachePath(m_path);
        QImage image;
        if (QFileInfo::exists(cached))
            image.load(cached);
        if (image.isNull()) {
            QImageReader reader(m_path);
            reader.setAutoTransform(true);
            const QSize full = reader.size();
            if (full.isValid() && qMax(full.width(), full.height()) > ThumbLongSide)
                reader.setScaledSize(full.scaled(ThumbLongSide, ThumbLongSide, Qt::KeepAspectRatio));
            image = reader.read();
            if (!image.isNull())
                image.save(cached, "JPG", 85);
        }
        QMetaObject::invokeMethod(m_cache, "deliver", Qt::QueuedConnection, Q_ARG(QString, m_path), Q_ARG(QImage, image));
    }

private:
    QString m_path;
    ThumbnailCache *m_cache;
};

} // namespace

ThumbnailCache *ThumbnailCache::instance()
{
    static ThumbnailCache *cache = new ThumbnailCache;
    return cache;
}

ThumbnailCache::ThumbnailCache(QObject *parent)
    : QObject(parent)
{
}

QPixmap ThumbnailCache::thumbnail(const QString &path)
{
    const auto it = m_pixmaps.constFind(path);
    if (it != m_pixmaps.constEnd())
        return it.value();
    if (!path.isEmpty() && !m_pending.contains(path)) {
        m_pending.insert(path);
        QThreadPool::globalInstance()->start(new ThumbnailJob(path, this));
    }
    return QPixmap();
}

QSize ThumbnailCache::imageSize(const QString &path)
{
    const auto it = m_sizes.constFind(path);
    if (it != m_sizes.constEnd())
        return it.value();
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QSize size = reader.size();
    // EXIF-rotated photos report their stored (unrotated) size.
    if (size.isValid() && reader.transformation() & QImageIOHandler::TransformationRotate90)
        size.transpose();
    m_sizes.insert(path, size);
    return size;
}

void ThumbnailCache::deliver(const QString &path, const QImage &image)
{
    m_pending.remove(path);
    m_pixmaps.insert(path, QPixmap::fromImage(image));
    emit ready(path);
}
