#include "ObsClient.h"
#include "core/AppSettings.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QTimer>

ObsClient::ObsClient(QObject *parent)
    : QObject(parent)
{
    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    m_timeout->setInterval(5000);
    connect(m_timeout, &QTimer::timeout, this, [this]() { fail(tr("OBS не отвечает (проверьте адрес и порт)")); });
}

void ObsClient::test(const QString &host, int port, const QString &password)
{
    close();
    m_testing = true;
    open(host, port, password);
    whenIdentified([this]() {
        request(QStringLiteral("GetSceneList"), {}, [this](const QJsonObject &data) {
            QStringList scenes;
            for (const QJsonValue &scene : data.value(QStringLiteral("scenes")).toArray())
                scenes.prepend(scene.toObject().value(QStringLiteral("sceneName")).toString()); // OBS lists bottom-up
            m_testing = false;
            emit testFinished(true, tr("Подключено"), scenes);
        });
    });
}

void ObsClient::switchToShowScene()
{
    if (!AppSettings::value(AppSettings::ObsEnabled).toBool())
        return;
    const QString scene = AppSettings::value(AppSettings::ObsScene).toString();
    if (scene.isEmpty())
        return;
    const QString host = AppSettings::value(AppSettings::ObsHost).toString();
    const int port = AppSettings::value(AppSettings::ObsPort).toInt();
    const QString password = AppSettings::value(AppSettings::ObsPassword).toString();
    if (!m_identified || host != m_host || port != m_port || password != m_password) {
        close();
        open(host, port, password);
    }
    whenIdentified([this, scene]() {
        request(QStringLiteral("GetCurrentProgramScene"), {}, [this, scene](const QJsonObject &data) {
            const QString current = data.value(QStringLiteral("currentProgramSceneName")).toString();
            if (current != scene) {
                m_previousScene = current;
                request(QStringLiteral("SetCurrentProgramScene"), {{QStringLiteral("sceneName"), scene}}, [](const QJsonObject &) {});
            }
        });
    });
}

void ObsClient::switchToPreviousScene()
{
    if (!AppSettings::value(AppSettings::ObsEnabled).toBool() || m_previousScene.isEmpty())
        return;
    whenIdentified([this]() {
        request(QStringLiteral("SetCurrentProgramScene"), {{QStringLiteral("sceneName"), m_previousScene}}, [](const QJsonObject &) {});
        m_previousScene.clear();
    });
}

void ObsClient::whenIdentified(const std::function<void()> &action)
{
    if (m_identified)
        action();
    else
        m_pending << action;
}

void ObsClient::open(const QString &host, int port, const QString &password)
{
    m_host = host.trimmed().isEmpty() ? QStringLiteral("localhost") : host.trimmed();
    m_port = port;
    m_password = password;
    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::connected, this, [this]() {
        QByteArray nonce(16, Qt::Uninitialized);
        for (char &c : nonce)
            c = char(QRandomGenerator::global()->bounded(256));
        m_key = nonce.toBase64();
        const QByteArray request = "GET / HTTP/1.1\r\nHost: " + m_host.toUtf8() + ':' + QByteArray::number(m_port)
            + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + m_key
            + "\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: obswebsocket.json\r\n\r\n";
        m_socket->write(request);
    });
    connect(m_socket, &QTcpSocket::readyRead, this, &ObsClient::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred, this, [this]() {
        fail(tr("Нет подключения: %1").arg(m_socket ? m_socket->errorString() : QString()));
    });
    connect(m_socket, &QTcpSocket::disconnected, this, [this]() {
        m_identified = false;
        m_handshakeDone = false;
    });
    m_timeout->start();
    // "localhost" would try IPv6 first and wait; OBS listens on IPv4.
    m_socket->connectToHost(m_host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0 ? QStringLiteral("127.0.0.1") : m_host,
                            quint16(m_port));
}

void ObsClient::close()
{
    m_timeout->stop();
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_buffer.clear();
    m_handshakeDone = false;
    m_identified = false;
    m_pending.clear();
    m_callbacks.clear();
}

void ObsClient::fail(const QString &message)
{
    const bool testing = m_testing;
    m_testing = false;
    close();
    if (testing)
        emit testFinished(false, message, {});
}

void ObsClient::onReadyRead()
{
    m_buffer += m_socket->readAll();
    if (!m_handshakeDone) {
        const int end = m_buffer.indexOf("\r\n\r\n");
        if (end < 0)
            return;
        const QByteArray head = m_buffer.left(end);
        m_buffer.remove(0, end + 4);
        if (!head.startsWith("HTTP/1.1 101")) {
            fail(tr("Это не obs-websocket (в OBS: Сервис → Настройки WebSocket-сервера)"));
            return;
        }
        m_handshakeDone = true;
    }
    // Server frames are unmasked; obs-websocket only sends text frames.
    while (m_buffer.size() >= 2) {
        const quint8 b0 = quint8(m_buffer.at(0));
        const quint8 b1 = quint8(m_buffer.at(1));
        const int opcode = b0 & 0x0f;
        qint64 length = b1 & 0x7f;
        int header = 2;
        if (length == 126) {
            if (m_buffer.size() < 4)
                return;
            length = (quint8(m_buffer.at(2)) << 8) | quint8(m_buffer.at(3));
            header = 4;
        } else if (length == 127) {
            if (m_buffer.size() < 10)
                return;
            length = 0;
            for (int i = 2; i < 10; ++i)
                length = (length << 8) | quint8(m_buffer.at(i));
            header = 10;
        }
        if (m_buffer.size() < header + length)
            return;
        const QByteArray payload = m_buffer.mid(header, int(length));
        m_buffer.remove(0, header + int(length));
        if (opcode == 0x1)
            handleMessage(QJsonDocument::fromJson(payload).object());
        else if (opcode == 0x8)
            fail(tr("OBS закрыл подключение (неверный пароль?)"));
    }
}

void ObsClient::handleMessage(const QJsonObject &message)
{
    const int op = message.value(QStringLiteral("op")).toInt(-1);
    const QJsonObject data = message.value(QStringLiteral("d")).toObject();
    if (op == 0) { // Hello
        QJsonObject identify{{QStringLiteral("rpcVersion"), 1}};
        const QJsonObject auth = data.value(QStringLiteral("authentication")).toObject();
        if (!auth.isEmpty()) {
            if (m_password.isEmpty()) {
                fail(tr("OBS требует пароль"));
                return;
            }
            const QByteArray secret = QCryptographicHash::hash(m_password.toUtf8() + auth.value(QStringLiteral("salt")).toString().toUtf8(),
                                                               QCryptographicHash::Sha256).toBase64();
            const QByteArray response = QCryptographicHash::hash(secret + auth.value(QStringLiteral("challenge")).toString().toUtf8(),
                                                                 QCryptographicHash::Sha256).toBase64();
            identify.insert(QStringLiteral("authentication"), QString::fromLatin1(response));
        }
        sendJson({{QStringLiteral("op"), 1}, {QStringLiteral("d"), identify}});
    } else if (op == 2) { // Identified
        m_timeout->stop();
        m_identified = true;
        const auto pending = m_pending;
        m_pending.clear();
        for (const auto &action : pending)
            action();
    } else if (op == 7) { // RequestResponse
        const QString id = data.value(QStringLiteral("requestId")).toString();
        const auto callback = m_callbacks.take(id);
        const QJsonObject status = data.value(QStringLiteral("requestStatus")).toObject();
        if (!status.value(QStringLiteral("result")).toBool()) {
            if (m_testing)
                fail(tr("Ошибка OBS: %1").arg(status.value(QStringLiteral("comment")).toString()));
            return;
        }
        if (callback)
            callback(data.value(QStringLiteral("responseData")).toObject());
    }
}

void ObsClient::request(const QString &type, const QJsonObject &data, const std::function<void(const QJsonObject &)> &done)
{
    const QString id = QString::number(m_nextRequestId++);
    m_callbacks.insert(id, done);
    QJsonObject d{{QStringLiteral("requestType"), type}, {QStringLiteral("requestId"), id}};
    if (!data.isEmpty())
        d.insert(QStringLiteral("requestData"), data);
    sendJson({{QStringLiteral("op"), 6}, {QStringLiteral("d"), d}});
}

void ObsClient::sendJson(const QJsonObject &message)
{
    if (!m_socket)
        return;
    // Client frames must be masked.
    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    QByteArray frame;
    frame.append(char(0x81));
    if (payload.size() < 126) {
        frame.append(char(0x80 | payload.size()));
    } else if (payload.size() < 65536) {
        frame.append(char(0x80 | 126));
        frame.append(char((payload.size() >> 8) & 0xff));
        frame.append(char(payload.size() & 0xff));
    } else {
        frame.append(char(0x80 | 127));
        for (int i = 7; i >= 0; --i)
            frame.append(char((qint64(payload.size()) >> (8 * i)) & 0xff));
    }
    char mask[4];
    for (char &c : mask)
        c = char(QRandomGenerator::global()->bounded(256));
    frame.append(mask, 4);
    for (int i = 0; i < payload.size(); ++i)
        frame.append(char(payload.at(i) ^ mask[i % 4]));
    m_socket->write(frame);
}
