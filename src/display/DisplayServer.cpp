#include "DisplayServer.h"
#include "ui/DisplaySettings.h"

#include <QDateTime>
#include <QFile>
#include <QMimeDatabase>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>

namespace {

QByteArray extractHeader(const QByteArray &request, const char *name)
{
    const QByteArray needle = QByteArray(name) + ": ";
    const int idx = request.indexOf(needle);
    if (idx < 0)
        return {};
    int end = request.indexOf("\r\n", idx);
    if (end < 0)
        end = request.size();
    return request.mid(idx + needle.size(), end - idx - needle.size()).trimmed();
}

} // namespace

DisplayServer::DisplayServer(QObject *parent)
    : QObject(parent)
{
}

bool DisplayServer::start(quint16 port)
{
    if (m_server)
        stop();

    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &DisplayServer::handleNewConnection);

    if (!m_server->listen(QHostAddress::AnyIPv4, port)) {
        delete m_server;
        m_server = nullptr;
        return false;
    }

    m_port = m_server->serverPort();
    return true;
}

void DisplayServer::stop()
{
    if (!m_server)
        return;
    m_server->close();
    m_server->deleteLater();
    m_server = nullptr;
    m_port = 0;
}

bool DisplayServer::isRunning() const
{
    return m_server && m_server->isListening();
}

QString DisplayServer::firstLanIPv4()
{
    for (const QHostAddress &address : QNetworkInterface::allAddresses()) {
        if (address.protocol() != QAbstractSocket::IPv4Protocol)
            continue;
        if (address.isLoopback())
            continue;
        return address.toString();
    }
    return QStringLiteral("127.0.0.1");
}

QString DisplayServer::displayUrl() const
{
    return QStringLiteral("http://%1:%2/display").arg(firstLanIPv4()).arg(m_port);
}

void DisplayServer::setContent(const SlideContent &content)
{
    m_content = content;
    ++m_version;
}

void DisplayServer::handleNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();

        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            handleRequest(socket, socket->readAll());
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    }
}

void DisplayServer::handleRequest(QTcpSocket *socket, const QByteArray &request)
{
    const QByteArray firstLine = request.left(request.indexOf("\r\n"));
    const QList<QByteArray> parts = firstLine.split(' ');
    const QByteArray rawPath = parts.size() >= 2 ? parts.at(1) : QByteArray("/");
    const QByteArray path = rawPath.left(rawPath.indexOf('?') >= 0 ? rawPath.indexOf('?') : rawPath.size());

    if (path == "/" || path == "/display") {
        sendResponse(socket, 200, "text/html; charset=utf-8", buildDisplayHtml());
        return;
    }

    if (path == "/state") {
        sendResponse(socket, 200, "text/plain; charset=utf-8", QByteArray::number(m_version));
        return;
    }

    if (path == "/video") {
        if (m_content.kind != SlideKind::Video || m_content.imagePath.startsWith("http://")
            || m_content.imagePath.startsWith("https://") || !QFile::exists(m_content.imagePath)) {
            sendResponse(socket, 404, "text/plain", "not found");
            return;
        }

        QFile file(m_content.imagePath);
        if (!file.open(QIODevice::ReadOnly)) {
            sendResponse(socket, 404, "text/plain", "not found");
            return;
        }

        const qint64 fileSize = file.size();
        const QMimeDatabase db;
        const QByteArray mime = db.mimeTypeForFile(m_content.imagePath).name().toUtf8();
        const QByteArray rangeHeader = extractHeader(request, "Range");

        // OBS/Chromium's <video> element relies on range requests to seek
        // and to stream progressively rather than waiting on the whole
        // file; without honoring Range, playback can stall or refuse to
        // start on anything but small files.
        if (rangeHeader.startsWith("bytes=")) {
            const QByteArray spec = rangeHeader.mid(6);
            const int dash = spec.indexOf('-');
            qint64 start = dash > 0 ? spec.left(dash).toLongLong() : 0;
            start = qBound<qint64>(0, start, fileSize > 0 ? fileSize - 1 : 0);

            qint64 end = fileSize - 1;
            bool hasEnd = false;
            if (dash >= 0 && dash + 1 < spec.size())
                end = spec.mid(dash + 1).toLongLong(&hasEnd);
            if (!hasEnd || end < start || end > fileSize - 1)
                end = fileSize - 1;

            constexpr qint64 maxChunk = 16 * 1024 * 1024;
            if (end - start + 1 > maxChunk)
                end = start + maxChunk - 1;

            file.seek(start);
            const QByteArray body = file.read(end - start + 1);

            QByteArray header = "HTTP/1.1 206 Partial Content\r\n";
            header += "Content-Type: " + mime + "\r\n";
            header += "Accept-Ranges: bytes\r\n";
            header += "Content-Range: bytes " + QByteArray::number(start) + "-" + QByteArray::number(end)
                + "/" + QByteArray::number(fileSize) + "\r\n";
            header += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
            header += "Cache-Control: no-store\r\n";
            header += "Connection: close\r\n\r\n";
            socket->write(header + body);
            socket->flush();
            socket->disconnectFromHost();
            return;
        }

        QByteArray header = "HTTP/1.1 200 OK\r\n";
        header += "Content-Type: " + mime + "\r\n";
        header += "Accept-Ranges: bytes\r\n";
        header += "Content-Length: " + QByteArray::number(fileSize) + "\r\n";
        header += "Cache-Control: no-store\r\n";
        header += "Connection: close\r\n\r\n";
        socket->write(header + file.readAll());
        socket->flush();
        socket->disconnectFromHost();
        return;
    }

    if (path == "/photo") {
        if (m_content.kind == SlideKind::Photo && QFile::exists(m_content.imagePath)) {
            QFile file(m_content.imagePath);
            if (file.open(QIODevice::ReadOnly)) {
                const QByteArray bytes = file.readAll();
                const QMimeDatabase db;
                const QByteArray mime = db.mimeTypeForFile(m_content.imagePath).name().toUtf8();
                sendResponse(socket, 200, mime, bytes);
                return;
            }
        }
        sendResponse(socket, 404, "text/plain", "not found");
        return;
    }

    if (path == "/background") {
        if (m_content.kind == SlideKind::Text && m_content.backgroundType == BackgroundType::Photo
            && QFile::exists(m_content.backgroundPath)) {
            QFile file(m_content.backgroundPath);
            if (file.open(QIODevice::ReadOnly)) {
                const QByteArray bytes = file.readAll();
                const QMimeDatabase db;
                const QByteArray mime = db.mimeTypeForFile(m_content.backgroundPath).name().toUtf8();
                sendResponse(socket, 200, mime, bytes);
                return;
            }
        }
        sendResponse(socket, 404, "text/plain", "not found");
        return;
    }

    sendResponse(socket, 404, "text/plain", "not found");
}

void DisplayServer::sendResponse(QTcpSocket *socket, int statusCode, const QByteArray &contentType, const QByteArray &body)
{
    const QByteArray statusText = statusCode == 200 ? "OK" : "Not Found";
    QByteArray header = "HTTP/1.1 " + QByteArray::number(statusCode) + " " + statusText + "\r\n";
    header += "Content-Type: " + contentType + "\r\n";
    header += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    header += "Cache-Control: no-store\r\n";
    header += "Connection: close\r\n\r\n";

    socket->write(header + body);
    socket->flush();
    socket->disconnectFromHost();
}

QString DisplayServer::youtubeEmbedUrl(const QString &url)
{
    static const QRegularExpression pattern(
        QStringLiteral(R"((?:youtu\.be/|youtube\.com/(?:watch\?v=|embed/|shorts/))([A-Za-z0-9_-]{6,}))"));
    const QRegularExpressionMatch match = pattern.match(url);
    const QString videoId = match.hasMatch() ? match.captured(1) : QString();
    if (videoId.isEmpty())
        return QString();
    return QStringLiteral("https://www.youtube.com/embed/%1?autoplay=1&mute=0&playsinline=1").arg(videoId);
}

QByteArray DisplayServer::buildDisplayHtml() const
{
    QString bodyHtml;

    switch (m_content.kind) {
    case SlideKind::Photo:
        bodyHtml = QStringLiteral("<img src=\"/photo?t=%1\">").arg(QDateTime::currentMSecsSinceEpoch());
        break;
    case SlideKind::Text: {
        const QString escaped = m_content.text.toHtmlEscaped().replace(QStringLiteral("\n"), QStringLiteral("<br>"));
        QString background;
        if (m_content.backgroundType == BackgroundType::Photo && !m_content.backgroundPath.isEmpty()) {
            background = QStringLiteral("<img class=\"bg-photo\" src=\"/background?t=%1\">")
                .arg(QDateTime::currentMSecsSinceEpoch());
        }
        bodyHtml = background + QStringLiteral("<div class=\"slide-text\">%1</div>").arg(escaped);
        break;
    }
    case SlideKind::Video: {
        const QString embedUrl = youtubeEmbedUrl(m_content.imagePath);
        if (!embedUrl.isEmpty()) {
            bodyHtml = QStringLiteral(
                "<iframe class=\"video-frame\" src=\"%1\" frameborder=\"0\" "
                "allow=\"autoplay; encrypted-media\" allowfullscreen></iframe>").arg(embedUrl);
        } else {
            bodyHtml = QStringLiteral(
                "<video class=\"video-frame\" src=\"/video?v=%1\" autoplay %2 playsinline></video>")
                .arg(m_version).arg(m_content.videoLoop ? QStringLiteral("loop muted") : QStringLiteral(""));
        }
        break;
    }
    case SlideKind::Black:
        bodyHtml.clear();
        break;
    case SlideKind::Empty:
        bodyHtml = QStringLiteral("<div class=\"logo\">Gather</div>");
        break;
    }

    // A blanket "reload every second" used to sit here (meta refresh). That
    // broke video playback (restarting it every second) and flickered
    // photos/text even when nothing changed. Poll a cheap /state endpoint
    // instead and only reload the page when the version actually moves.
    const QString html = QStringLiteral(
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<style>"
        "html,body{margin:0;padding:0;width:100%;height:100%;background:#0b0e14;overflow:hidden;}"
        "body{position:relative;display:flex;align-items:center;justify-content:center;}"
        ".slide-text{position:relative;z-index:1;color:#fff;font-family:'%2',Inter,Arial,sans-serif;font-weight:700;"
        "font-size:%3vw;text-align:%4;padding:5vw;line-height:1.3;width:100%;text-shadow:0 2px 12px rgba(0,0,0,0.85);}"
        ".logo{color:#333c4d;font-family:Inter,Arial,sans-serif;font-weight:700;font-size:4vw;}"
        "img{max-width:100%;max-height:100%;object-fit:contain;}"
        ".bg-photo{position:absolute;inset:0;width:100%;height:100%;object-fit:cover;z-index:0;}"
        ".video-frame{width:100%;height:100%;border:none;object-fit:contain;}"
        "</style></head><body>%1"
        "<script>(function(){var v=%5;setInterval(function(){"
        "fetch('/state').then(function(r){return r.text();}).then(function(t){"
        "if(t!==v)location.reload();}).catch(function(){});},1000);})();</script>"
        "</body></html>"
    ).arg(bodyHtml, DisplaySettings::fontFamily(m_content.sourceType),
          QString::number(DisplaySettings::cssFontSizeVw(m_content.sourceType)), DisplaySettings::cssTextAlign(),
          QStringLiteral("\"%1\"").arg(m_version));

    return html.toUtf8();
}
