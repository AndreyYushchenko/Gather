#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QStringList>
#include <functional>

class QTcpSocket;
class QTimer;

// A small obs-websocket (v5, OBS 28+) client for Настройки → OBS и сеть:
// connects with the password, lists scenes, and switches the program
// scene when Sermon puts something on screen. Qt's WebSockets module isn't
// part of this build, so the WebSocket framing is done here over a plain
// QTcpSocket (text frames only, which is all obs-websocket uses).
class ObsClient : public QObject {
    Q_OBJECT
public:
    explicit ObsClient(QObject *parent = nullptr);

    // "Проверить подключение": connects with these values (not the saved
    // ones) and reports back through testFinished().
    void test(const QString &host, int port, const QString &password);
    // Switches OBS to "Сцена показа" if the integration is on.
    void switchToShowScene();
    void switchToPreviousScene();

signals:
    void testFinished(bool ok, const QString &message, const QStringList &scenes);

private:
    void open(const QString &host, int port, const QString &password);
    void close();
    void onReadyRead();
    void handleMessage(const QJsonObject &message);
    void sendJson(const QJsonObject &message);
    void request(const QString &type, const QJsonObject &data, const std::function<void(const QJsonObject &)> &done);
    void fail(const QString &message);
    void whenIdentified(const std::function<void()> &action);

    QTcpSocket *m_socket = nullptr;
    QTimer *m_timeout = nullptr;
    QByteArray m_buffer;
    QByteArray m_key;
    bool m_handshakeDone = false;
    bool m_identified = false;
    bool m_testing = false;
    QString m_password;
    QString m_host;
    QString m_previousScene;
    int m_port = 0;
    int m_nextRequestId = 1;
    QList<std::function<void()>> m_pending;
    QHash<QString, std::function<void(const QJsonObject &)>> m_callbacks;
};
