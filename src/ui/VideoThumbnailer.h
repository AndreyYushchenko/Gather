#pragma once

#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QStringList>

class QMediaPlayer;
class QTimer;
class QVideoSink;

// Poster frames and durations for the Video screen's tiles. Each local
// video is opened once (muted, off-screen) with QMediaPlayer, a frame
// about a second in is grabbed, and the result is cached in memory and on
// disk (dataDir/thumbs + a duration entry in QSettings), so later starts
// are instant. Videos are processed one at a time in the background.
class VideoThumbnailer : public QObject {
    Q_OBJECT
public:
    static VideoThumbnailer *instance();

    // Null while not ready yet (or for remote links).
    QPixmap poster(const QString &path);
    // Milliseconds, or -1 when unknown yet.
    qint64 duration(const QString &path);

signals:
    void ready(const QString &path);

private:
    explicit VideoThumbnailer(QObject *parent = nullptr);
    void request(const QString &path);
    void startNext();
    void finish(const QImage &frame);

    QHash<QString, QPixmap> m_posters;
    QHash<QString, qint64> m_durations;
    QStringList m_queue;
    QString m_current;
    bool m_seeked = false;
    QMediaPlayer *m_player = nullptr;
    QVideoSink *m_sink = nullptr;
    QTimer *m_timeout = nullptr;
};
