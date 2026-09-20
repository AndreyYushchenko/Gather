#pragma once

#include "SlideContent.h"

#include <QObject>

class QTcpServer;
class QTcpSocket;

// Minimal local HTTP server that lets OBS (or any browser) pick up the
// current slide as a Browser Source, per the spec's "output over LAN"
// requirement. No websockets yet: the served page just polls itself via a
// meta refresh, which is enough for a slide that changes a few times a
// minute and keeps the implementation simple.
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
    void sendResponse(QTcpSocket *socket, int statusCode, const QByteArray &contentType, const QByteArray &body);
    QByteArray buildDisplayHtml() const;
    static QString firstLanIPv4();
    static QString youtubeEmbedUrl(const QString &url);

    QTcpServer *m_server = nullptr;
    quint16 m_port = 0;
    SlideContent m_content;
    // Bumped on every setContent() call; the served page polls /state and
    // only reloads itself when this changes, instead of the old blanket
    // "reload every second" (which visibly restarted video playback and
    // flickered photos/text even when nothing had actually changed).
    quint64 m_version = 0;
};
