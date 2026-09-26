#pragma once

#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QSize>

// Grid thumbnails for the Photos screen. Decoding a folder of 4K photos
// on the UI thread would freeze scrolling, so thumbnails are made on the
// global thread pool (QImageReader with a reduced decode size), cached in
// memory and on disk (dataDir/thumbs), and announced via ready().
class ThumbnailCache : public QObject {
    Q_OBJECT
public:
    static ThumbnailCache *instance();

    // The cached thumbnail, or a null pixmap while it's being made.
    QPixmap thumbnail(const QString &path);
    // Original pixel size (read from the file header, cheap), cached.
    QSize imageSize(const QString &path);

signals:
    void ready(const QString &path);

private:
    explicit ThumbnailCache(QObject *parent = nullptr);
    // Called (queued) from the worker threads.
    Q_INVOKABLE void deliver(const QString &path, const QImage &image);

    QHash<QString, QPixmap> m_pixmaps;
    QHash<QString, QSize> m_sizes;
    QSet<QString> m_pending;
};
