#include "DisplayServer.h"
#include "TextSlideWidget.h"
#include "core/AppSettings.h"
#include "ui/DisplaySettings.h"

#include <QDateTime>
#include <QBuffer>
#include <QImage>
#include <QPainter>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>
#include <QMimeDatabase>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <memory>
#include <utility>

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
    m_textImage.clear();
    ++m_version;
    releaseStateWaiters();
}

void DisplayServer::releaseStateWaiters()
{
    const QList<QTcpSocket *> waiters = std::exchange(m_stateWaiters, {});
    for (QTcpSocket *socket : waiters) {
        if (socket->state() == QAbstractSocket::ConnectedState)
            sendResponse(socket, 200, "text/plain; charset=utf-8", QByteArray::number(m_version));
    }
}

void DisplayServer::handleNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();

        // A request can arrive in pieces: wait for the end of its headers.
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            const QByteArray buffer = socket->property("request").toByteArray() + socket->readAll();
            if (!buffer.contains("\r\n\r\n") && buffer.size() < 16 * 1024) {
                socket->setProperty("request", buffer);
                return;
            }
            socket->setProperty("request", QVariant());
            if (!socket->property("handled").toBool()) {
                socket->setProperty("handled", true);
                handleRequest(socket, buffer);
            }
        });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            m_stateWaiters.removeAll(socket);
            socket->deleteLater();
        });
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
        // "/state?v=<version the page shows>": answered at once if that is
        // already old, otherwise held until the slide changes (or 20 s pass).
        const int query = rawPath.indexOf("v=");
        const QByteArray known = query >= 0 ? rawPath.mid(query + 2) : QByteArray();
        if (known != QByteArray::number(m_version)) {
            sendResponse(socket, 200, "text/plain; charset=utf-8", QByteArray::number(m_version));
            return;
        }
        m_stateWaiters.append(socket);
        QTimer::singleShot(20000, socket, [this, socket]() {
            if (m_stateWaiters.removeAll(socket) > 0)
                sendResponse(socket, 200, "text/plain; charset=utf-8", QByteArray::number(m_version));
        });
        return;
    }

    // The same renderer as the projector supplies a transparent text layer.
    // OBS therefore needs neither locally installed fonts nor a second layout
    // implementation for spacing, outlines and automatic text fitting.
    if (path == "/text-layer") {
        if (m_content.kind != SlideKind::Text
            || (m_content.sourceType != ContentType::Song && m_content.sourceType != ContentType::BibleVerse)) {
            sendResponse(socket, 404, "text/plain", "not found");
            return;
        }
        if (m_textImage.isEmpty()) {
            TextSlideWidget text;
            text.resize(1920, 1080);
            text.setText(m_content.text, m_content.reference, m_content.chords, DisplaySettings::textStyle(m_content.sourceType),
                         m_content.starsGapLines, m_content.fitGroup);
            QImage image(text.size(), QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            text.render(&image);
            QBuffer buffer(&m_textImage);
            buffer.open(QIODevice::WriteOnly);
            image.save(&buffer, "PNG", 1); // 1 = fastest compression, reduces lag
        }
        sendResponse(socket, 200, "image/png", m_textImage);
        return;
    }

    if (path == "/video") {
        if (m_content.kind != SlideKind::Video || m_content.imagePath.startsWith("http://")
            || m_content.imagePath.startsWith("https://") || !QFile::exists(m_content.imagePath)) {
            sendResponse(socket, 404, "text/plain", "not found");
            return;
        }

        sendFile(socket, m_content.imagePath, request);
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

    if (path == "/logo") {
        const QString logo = AppSettings::value(AppSettings::LogoPath).toString();
        sendFile(socket, logo.isEmpty() || !QFile::exists(logo) ? QStringLiteral(":/icons/app-256.png") : logo, request);
        return;
    }

    if (path == "/background") {
        const QString backgroundPath = backgroundFilePath();
        if (!backgroundPath.isEmpty() && QFile::exists(backgroundPath)) {
            sendFile(socket, backgroundPath, request);
            return;
        }
        sendResponse(socket, 404, "text/plain", "not found");
        return;
    }

    if (path == "/timer") {
        // The timer page asks for the current frame 4× a second, so OBS shows
        // exactly what TimerFormat::frame() computes for the projector.
        if (m_content.kind != SlideKind::Timer) {
            sendResponse(socket, 200, "application/json", QByteArray("{\"v\":") + QByteArray::number(m_version) + "}");
            return;
        }
        const TimerFrame frame = TimerFormat::frame(m_content.timer, QDateTime::currentMSecsSinceEpoch());
        const QJsonObject json{
            {QStringLiteral("v"), double(m_version)},
            {QStringLiteral("title"), frame.title},
            {QStringLiteral("main"), frame.primary},
            {QStringLiteral("below"), QJsonArray::fromStringList(frame.below)},
            {QStringLiteral("color"), frame.mainColor.isValid() ? frame.mainColor.name() : QString()},
            {QStringLiteral("progress"), frame.progress},
            {QStringLiteral("digital"), frame.showDigital},
            {QStringLiteral("analog"), frame.showAnalog},
            {QStringLiteral("message"), frame.isMessage},
            {QStringLiteral("h"), frame.clockTime.hour()},
            {QStringLiteral("m"), frame.clockTime.minute()},
            {QStringLiteral("s"), frame.clockTime.second()},
        };
        sendResponse(socket, 200, "application/json; charset=utf-8", QJsonDocument(json).toJson(QJsonDocument::Compact));
        return;
    }

    sendResponse(socket, 404, "text/plain", "not found");
}

void DisplayServer::sendFile(QTcpSocket *socket, const QString &path, const QByteArray &request)
{
    auto *file = new QFile(path, socket);
    if (!file->open(QIODevice::ReadOnly)) {
        delete file;
        sendResponse(socket, 404, "text/plain", "not found");
        return;
    }

    const qint64 fileSize = file->size();
    const QMimeDatabase db;
    const QByteArray mime = db.mimeTypeForFile(path, QMimeDatabase::MatchExtension).name().toUtf8();
    const QByteArray rangeHeader = extractHeader(request, "Range");

    qint64 start = 0;
    qint64 end = fileSize - 1;
    QByteArray header;
    // OBS/Chromium's <video> element relies on range requests to seek
    // and to stream progressively rather than waiting on the whole
    // file; without honoring Range, playback can stall or refuse to
    // start on anything but small files.
    if (rangeHeader.startsWith("bytes=")) {
        const QByteArray spec = rangeHeader.mid(6);
        const int dash = spec.indexOf('-');
        start = dash > 0 ? spec.left(dash).toLongLong() : 0;
        start = qBound<qint64>(0, start, fileSize > 0 ? fileSize - 1 : 0);
        bool hasEnd = false;
        if (dash >= 0 && dash + 1 < spec.size())
            end = spec.mid(dash + 1).toLongLong(&hasEnd);
        if (!hasEnd || end < start || end > fileSize - 1)
            end = fileSize - 1;

        header = "HTTP/1.1 206 Partial Content\r\n";
        header += "Content-Range: bytes " + QByteArray::number(start) + "-" + QByteArray::number(end)
            + "/" + QByteArray::number(fileSize) + "\r\n";
    } else {
        header = "HTTP/1.1 200 OK\r\n";
    }
    const qint64 length = qMax<qint64>(0, end - start + 1);
    header += "Content-Type: " + mime + "\r\n";
    header += "Accept-Ranges: bytes\r\n";
    header += "Content-Length: " + QByteArray::number(length) + "\r\n";
    header += "Cache-Control: no-store\r\n";
    header += "Connection: close\r\n\r\n";
    socket->write(header);
    file->seek(start);

    // Streamed a piece at a time as the socket drains. Reading the whole
    // range up front (up to 16 MB, or the whole file) froze the app and the
    // projector's video while OBS was playing a clip.
    auto remaining = std::make_shared<qint64>(length);
    const auto pump = [socket, file, remaining]() {
        constexpr qint64 piece = 256 * 1024;
        while (*remaining > 0 && socket->bytesToWrite() < 4 * piece) {
            const QByteArray chunk = file->read(qMin(piece, *remaining));
            if (chunk.isEmpty()) {
                *remaining = 0;
                break;
            }
            *remaining -= chunk.size();
            socket->write(chunk);
        }
        if (*remaining == 0 && socket->bytesToWrite() == 0)
            socket->disconnectFromHost();
    };
    connect(socket, &QTcpSocket::bytesWritten, file, pump);
    pump();
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

QString DisplayServer::backgroundFilePath() const
{
    if (m_content.kind == SlideKind::Text && m_content.backgroundType != BackgroundType::None)
        return m_content.backgroundPath;
    if (m_content.kind == SlideKind::Empty && DisplaySettings::defaultBackgroundType() == BackgroundType::Photo)
        return DisplaySettings::defaultBackgroundPath();
    if (m_content.kind == SlideKind::Timer) {
        const QString video = TimerFormat::backgroundVideoPath(m_content.timer.screen);
        return video.isEmpty() ? TimerFormat::backgroundImagePath(m_content.timer.screen) // may be a :/ resource
                               : video;
    }
    return QString();
}

QByteArray DisplayServer::buildTimerHtml() const
{
    // Static look (background, font, colours) is baked into the page; the
    // changing lines come from /timer (TimerFormat::frame), polled by the
    // page itself. The page reloads when the slide version moves.
    const TimerScreen &screen = m_content.timer.screen;
    const QString video = TimerFormat::backgroundVideoPath(screen);
    const QString image = TimerFormat::backgroundImagePath(screen);
    const bool photoBehind = !video.isEmpty() || (!image.isEmpty() && screen.background != TimerBackground::Dark);

    QString background;
    if (!video.isEmpty())
        background = QStringLiteral("<video id=\"bg\" src=\"/background?v=%1\" autoplay loop muted playsinline></video>").arg(m_version);
    else if (screen.background == TimerBackground::Gradient)
        background = QStringLiteral("<div id=\"bg\" style=\"background:linear-gradient(#7c6fd6,#2b2350)\"></div>");
    else if (photoBehind)
        background = QStringLiteral("<img id=\"bg\" src=\"/background?v=%1\">").arg(m_version);

    QColor track = TimerFormat::textColor(screen.textColor);
    track.setAlphaF(0.22);
    const QJsonObject config{
        {QStringLiteral("v"), double(m_version)},
        {QStringLiteral("color"), TimerFormat::textColor(screen.textColor).name()},
        {QStringLiteral("track"), QStringLiteral("rgba(%1,%2,%3,.22)").arg(track.red()).arg(track.green()).arg(track.blue())},
        {QStringLiteral("scale"), TimerFormat::fontScale(screen.fontSize)},
        {QStringLiteral("align"), static_cast<int>(screen.align)},
        {QStringLiteral("dim"), photoBehind ? QStringLiteral("rgba(0,0,0,%1)").arg(qBound(0, screen.dimPercent, 90) / 100.0)
                                            : QStringLiteral("transparent")},
    };
    const bool shadow = screen.textShadow && (photoBehind || screen.background == TimerBackground::Gradient);

    const QString html = QStringLiteral(R"HTML(<!DOCTYPE html><html><head><meta charset="utf-8"><style>
html,body{margin:0;padding:0;width:100%;height:100%;overflow:hidden;background:#12151c;}
body{font-family:%1,Inter,'Segoe UI',Arial,sans-serif;font-variant-numeric:tabular-nums;}
#bg{position:absolute;inset:0;width:100%;height:100%;object-fit:cover;}
#dim{position:absolute;inset:0;}
#box{position:absolute;left:6vw;right:6vw;top:8vh;bottom:10vh;display:flex;flex-direction:column;justify-content:center;}
#box div{white-space:pre-wrap;line-height:1.2;max-width:100%;}
.shadow div{text-shadow:0 .45vh 1.2vh rgba(0,0,0,.47);}
#track{position:absolute;left:5.5vw;width:89vw;bottom:7vh;height:1.2vh;min-height:2px;border-radius:1vh;display:none;}
#fill{height:100%;border-radius:1vh;width:0;}
</style></head><body>%2<div id="dim"></div><div id="box" class="%3"></div><div id="track"><div id="fill"></div></div><script>
var C=%4,box=document.getElementById('box'),track=document.getElementById('track'),fill=document.getElementById('fill');
document.getElementById('dim').style.background=C.dim;
var AL=['flex-start','center','flex-end'],TA=['left','center','right'];
function line(text,vh,weight,alpha,color){var d=document.createElement('div');d.textContent=text;d.style.fontSize=vh+'vh';
 d.style.fontWeight=weight;d.style.color=color||C.color;d.style.opacity=alpha;d.style.textAlign=TA[C.align];return d;}
function drawClock(cv,f){var D=cv.width,u=D/140,r=D/2,x=cv.getContext('2d');x.clearRect(0,0,D,D);
 x.globalAlpha=.11;x.fillStyle=C.color;x.beginPath();x.arc(r,r,r-u,0,7);x.fill();
 x.globalAlpha=.43;x.strokeStyle=C.color;x.lineWidth=2*u;x.stroke();
 x.globalAlpha=.8;x.fillStyle=C.color;x.font='600 '+(13*u)+'px Inter,Segoe UI,Arial';x.textAlign='center';x.textBaseline='middle';
 [[12,0],[3,90],[6,180],[9,270]].forEach(function(n){var a=n[1]*Math.PI/180,k=r-14*u;x.fillText(n[0],r+k*Math.sin(a),r-k*Math.cos(a));});
 x.globalAlpha=1;x.lineCap='round';
 function hand(deg,len,w,col){var a=deg*Math.PI/180;x.strokeStyle=col;x.lineWidth=w*u;x.beginPath();x.moveTo(r,r);x.lineTo(r+len*u*Math.sin(a),r-len*u*Math.cos(a));x.stroke();}
 var mi=f.m+f.s/60;hand(((f.h%12)+mi/60)*30,34,2.5,C.color);hand(mi*6,48,2,C.color);hand(f.s*6,52,1.5,'#2f6feb');
 x.fillStyle=C.color;x.beginPath();x.arc(r,r,4*u,0,7);x.fill();}
function render(f){var S=C.scale*100;box.innerHTML='';box.style.alignItems=AL[C.align];box.style.gap=(S*.12)+'vh';
 if(f.title)box.appendChild(line(f.title.toUpperCase(),S*.24,600,.8));
 if(f.analog){var cv=document.createElement('canvas'),dia=Math.min(box.clientWidth,innerHeight*(f.digital?.5:.72)*C.scale/.29);
  cv.width=cv.height=Math.max(1,Math.round(dia));box.appendChild(cv);drawClock(cv,f);}
 if(f.digital&&f.main){var size=(f.message||f.analog)?S*.5:S,el=line(f.main,size,700,1,f.color);box.appendChild(el);
  for(var i=0;i<12&&(el.scrollWidth>box.clientWidth||box.scrollHeight>box.clientHeight);i++){size*=.85;el.style.fontSize=size+'vh';}}
 f.below.forEach(function(t){box.appendChild(line(t,S*.2,500,.8));});
 if(f.progress>=0){track.style.display='block';track.style.background=C.track;fill.style.background=f.color||C.color;fill.style.width=(f.progress*100)+'%';}
 else track.style.display='none';}
function poll(){fetch('/timer').then(function(r){return r.json();}).then(function(f){if(f.v!==C.v){location.reload();return;}render(f);}).catch(function(){});}
poll();setInterval(poll,250);
</script></body></html>)HTML")
        .arg(QStringLiteral("'%1'").arg(TimerFormat::resolvedFontFamily(screen)), background,
             shadow ? QStringLiteral("shadow") : QString(),
             QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact)));
    return html.toUtf8();
}

QByteArray DisplayServer::buildDisplayHtml() const
{
    if (m_content.kind == SlideKind::Timer)
        return buildTimerHtml();

    QString bodyHtml;

    switch (m_content.kind) {
    case SlideKind::Photo:
        bodyHtml = QStringLiteral("<img src=\"/photo?t=%1\">").arg(QDateTime::currentMSecsSinceEpoch());
        break;
    case SlideKind::Text: {
        QString escaped = m_content.text.toHtmlEscaped().replace(QStringLiteral("\n"), QStringLiteral("<br>"));
        if (m_content.chords) {
            static const QRegularExpression chord(QStringLiteral(R"(\[([^\]\s]{1,8})\])"));
            escaped.replace(chord, QStringLiteral("<sup style=\"color:#ffd166;font-weight:600;font-size:0.55em\">\\1</sup>"));
        }
        const QString corner = m_content.cornerLabel.isEmpty() ? QString()
            : QStringLiteral("<div style=\"position:absolute;left:2.5vw;top:2vw;z-index:2;color:rgba(255,255,255,.75);font:700 3vw %1,Inter,Arial,sans-serif\">%2</div>")
                  .arg(DisplaySettings::fontFamily(m_content.sourceType), m_content.cornerLabel.toHtmlEscaped());
        QString background;
        if (m_content.backgroundType == BackgroundType::Photo && !m_content.backgroundPath.isEmpty()) {
            background = QStringLiteral("<img class=\"bg-photo\" src=\"/background?t=%1\">")
                .arg(QDateTime::currentMSecsSinceEpoch());
        } else if (m_content.backgroundType == BackgroundType::Video && !m_content.backgroundPath.isEmpty()) {
            background = QStringLiteral("<video class=\"bg-photo\" src=\"/background?v=%1\" autoplay loop muted playsinline></video>")
                .arg(m_version);
        }
        if (m_content.sourceType == ContentType::Song || m_content.sourceType == ContentType::BibleVerse) {
            const auto style = DisplaySettings::textStyle(m_content.sourceType);
            if (style.dim) background += QStringLiteral("<div class=\"bg-dim\" style=\"background:rgba(0,0,0,%1)\"></div>").arg(style.dimOpacity / 100.0);
            bodyHtml = background + corner + QStringLiteral("<img class=\"text-layer\" src=\"/text-layer?v=%1\">").arg(m_version);
        } else if (!m_content.heading.isEmpty()) {
            // Announcement: same proportions as SlideRenderWidget (heading
            // 12% and lines 7% of the slide height, 16:9), photo dimmed.
            if (!background.isEmpty())
                background += QStringLiteral("<div class=\"bg-dim\"></div>");
            const double scale = m_content.textScale * DisplaySettings::textScalePercent(m_content.sourceType) / 100.0;
            const QString align = m_content.align == TextAlign::Left ? QStringLiteral("left")
                : m_content.align == TextAlign::Right ? QStringLiteral("right") : QStringLiteral("center");
            QString inner = QStringLiteral("<div style=\"font-size:%1vw;font-weight:800;\">%2</div>")
                                .arg(QString::number(6.75 * scale, 'f', 2), m_content.heading.toHtmlEscaped());
            for (const QString &line : m_content.text.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
                inner += QStringLiteral("<div style=\"font-size:%1vw;font-weight:500;color:#f1f3f6;margin-top:0.6vw;\">%2</div>")
                             .arg(QString::number(3.94 * scale, 'f', 2), line.toHtmlEscaped());
            bodyHtml = background + QStringLiteral("<div class=\"slide-text\" style=\"text-align:%1;\">%2</div>").arg(align, inner);
        } else {
            bodyHtml = background + corner + QStringLiteral("<div class=\"slide-text\">%1</div>").arg(escaped);
        }
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
    case SlideKind::Timer:
        bodyHtml.clear();
        break;
    case SlideKind::Empty: {
        // "Экран между службами".
        const QString idle = AppSettings::value(AppSettings::IdleScreen).toString();
        if (idle == QLatin1String("logo")) {
            if (DisplaySettings::defaultBackgroundType() == BackgroundType::Photo)
                bodyHtml = QStringLiteral("<img class=\"bg-photo\" src=\"/background?t=%1\"><div class=\"bg-dim\"></div>")
                               .arg(QDateTime::currentMSecsSinceEpoch());
            if (AppSettings::value(AppSettings::ShowLogo).toBool())
                bodyHtml += QStringLiteral("<img class=\"idle-logo\" src=\"/logo?t=%1\">").arg(m_version);
        }
        break;
    }
    }

    // "Ничего": a transparent page, so the OBS scene underneath shows.
    const bool transparent = m_content.kind == SlideKind::Empty
        && AppSettings::value(AppSettings::IdleScreen).toString() == QLatin1String("none");
    const QString mode = DisplaySettings::transition();
    const QString enter = mode == QLatin1String("fade") ? QStringLiteral("body>*{animation:enter %1ms ease-out;}@keyframes enter{from{opacity:0}to{opacity:1}}")
                          : mode == QLatin1String("slide") ? QStringLiteral("body>*{animation:enter %1ms ease-out;}@keyframes enter{from{transform:translateX(100%)}to{transform:none}}")
                                                           : QString();
    const QString extraCss = (enter.isEmpty() ? QString() : enter.arg(DisplaySettings::transitionMs()))
        + (transparent ? QStringLiteral("html,body{background:transparent !important;}") : QString())
        + (DisplaySettings::textShadow() ? QString() : QStringLiteral(".slide-text{text-shadow:none !important;}"))
        + QStringLiteral(".idle-logo{position:relative;z-index:1;max-width:50vw;max-height:40vh;object-fit:contain;}");

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
        ".bg-dim{position:absolute;inset:0;background:rgba(0,0,0,0.45);z-index:0;}"
        ".video-frame{width:100%;height:100%;border:none;object-fit:contain;}"
        ".text-layer{position:absolute;inset:0;width:100%;height:100%;object-fit:contain;z-index:1;}"
        "%6</style></head><body>%1"
        "<script>(function(){var v=%5;function wait(){"
        "fetch('/state?v='+v).then(function(r){return r.text();}).then(function(t){"
        "if(t!==v)location.reload();else wait();}).catch(function(){setTimeout(wait,1000);});}wait();})();</script>"
        "</body></html>"
    ).arg(bodyHtml, DisplaySettings::fontFamily(m_content.sourceType),
          QString::number(DisplaySettings::cssFontSizeVw(m_content.sourceType)), DisplaySettings::cssTextAlign(),
          QStringLiteral("\"%1\"").arg(m_version), extraCss);

    return html.toUtf8();
}
