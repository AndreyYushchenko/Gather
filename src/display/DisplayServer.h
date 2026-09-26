#pragma once

#include "SlideContent.h"

#include <QList>
#include <QObject>

class QTcpServer;
class QTcpSocket;

// Minimal local HTTP server that lets OBS (or any browser) pick up the
// current slide as a Browser Source, per the spec's "output over LAN"
// requirement. The served page waits on /state (a long poll answered when
// the slide changes) and reloads itself then. Runs on the GUI thread, so
// nothing here may block: files are streamed in small pieces.
class DisplayServer : public QObject {
    Q_OBJECT
public:
    explicit DisplayServer(QObject *parent = nullptr);

    bool start(quint16 port);
    void stop();
    bool isRunning() const;
    quint16 port() const { return m_port; }

    // Best-guess LAN address for the operator to type into OBS.
    QString displayUrl() const;

public slots:
    void setContent(const SlideContent &content);

private slots:
    void handleNewConnection();

private:
    void handleRequest(QTcpSocket *socket, const QByteArray &request);
    // Answers every /state request that is waiting for a new version.
    void releaseStateWaiters();
    void sendResponse(QTcpSocket *socket, int statusCode, const QByteArray &contentType, const QByteArray &body);
    // Serves a file (incl. :/ resources), honouring HTTP Range for video.
    void sendFile(QTcpSocket *socket, const QString &path, const QByteArray &request);
    QByteArray buildDisplayHtml() const;
    QByteArray buildTimerHtml() const;
    QString backgroundFilePath() const;
    static QString firstLanIPv4();
    static QString youtubeEmbedUrl(const QString &url);

    QTcpServer *m_server = nullptr;
    quint16 m_port = 0;
    SlideContent m_content;
    QByteArray m_textImage;
    // Bumped on every setContent() call; the served page polls /state and
    // only reloads itself when this changes, instead of the old blanket
    // "reload every second" (which visibly restarted video playback and
    // flickered photos/text even when nothing had actually changed).
    quint64 m_version = 0;
    QList<QTcpSocket *> m_stateWaiters;
};
