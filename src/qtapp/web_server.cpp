#include "web_server.h"

#include <cmath>
#include <QSet>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslServer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

#include <QCryptographicHash>
#include <QDateTime>
#include <QPasswordDigestor>

#include "brush_tools.h"
#include "editor_context.h"
#include "editor_log.h"
#include <QTextStream>
#include "settings.h"

namespace iw8r::qt {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

QJsonArray vec(const Vec3& v) { return {v.x, v.y, v.z}; }

QByteArray json(const QJsonObject& o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); }

WebServer::Response error(int status, const QString& message) {
    WebServer::Response r;
    r.status = status;
    r.body = json({{"error", message}});
    return r;
}

const char* reason(int status) {
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        default: return "Error";
    }
}

// The browser client lives in data/web beside the editor (repo or package).
QString webRoot() {
    QDir d(QCoreApplication::applicationDirPath());
    for (int up = 0; up < 4; ++up, d.cdUp())
        if (QFileInfo::exists(d.filePath("data/web/index.html"))) return d.filePath("data/web");
    return {};
}

// Catalog names per kind (data/asset_catalog), cached; models and materials drop what the availability audit lists as
// missing, models also the streaming-zone weapons the Models dock leaves out.
const QStringList& catalog(const QString& kind) {
    static QHash<QString, QStringList> cache;
    if (auto it = cache.find(kind); it != cache.end()) return *it;
    QStringList names;
    QDir d(QCoreApplication::applicationDirPath());
    for (int up = 0; up < 4 && !QFileInfo::exists(d.filePath("data/asset_catalog")); ++up) d.cdUp();
    const QDir cat(d.filePath("data/asset_catalog"));
    const QString file = kind == "materials" ? "iw8_materials.tsv" : kind == "models" ? "iw8_models.tsv"
                         : kind == "effects" ? "iw8_common_vfx.tsv" : kind == "sounds" ? "iw8_common_sounds.tsv" : QString();
    QSet<QString> missing;
    if (kind == "materials" || kind == "models")
        for (const QString& fn : cat.entryList({"iw8_" + kind + "_status*.tsv"}, QDir::Files)) {
            QFile sf(cat.filePath(fn));
            if (!sf.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
            QTextStream in(&sf);
            while (!in.atEnd()) {
                const QStringList f = in.readLine().split('\t');
                if (f.size() >= 2 && f[1] == "missing") missing.insert(f[0]);
            }
        }
    QFile f(cat.filePath(file));
    if (!file.isEmpty() && f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&f);
        in.readLine();
        while (!in.atEnd()) {
            const QString line = in.readLine();
            if (line.startsWith('#')) continue;
            const QStringList cols = line.split('\t');
            if (cols.size() < 2 || missing.contains(cols[0])) continue;
            if (kind == "models") {
                bool streamOnly = true;
                for (const QString& z : cols.last().split(',', Qt::SkipEmptyParts))
                    streamOnly &= z.trimmed() == "global_stream_mp" || z.trimmed() == "techsets_global_stream_mp";
                if (streamOnly) continue;
            }
            names << (kind == "sounds" ? cols[0] + (cols.value(1) == "1" ? "\tloop" : "") : cols[0]);
        }
    }
    return cache.insert(kind, names).value();
}

// Recent log lines with a running sequence number (EditorLog's own buffer drops its oldest entries).
struct LogTail {
    qint64 next = 0;
    QList<QPair<qint64, LogEntry>> lines;
};
LogTail& logTail() {
    static LogTail t;
    return t;
}

}  // namespace

namespace {
constexpr int kPbkdf2Rounds = 120000;
QByteArray derive(const QString& password, const QByteArray& salt) {
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256, password.toUtf8(), salt, kPbkdf2Rounds, 32).toHex();
}
}  // namespace

void WebServer::setCredentials(const QString& user, const QString& password) {
    auto& s = Settings::instance();
    if (user.trimmed().isEmpty()) {
        s.setValue("web/user", QString());
        s.setValue("web/salt", QString());
        s.setValue("web/hash", QString());
        return;
    }
    QByteArray salt(16, 0);
    for (char& c : salt) c = char(QRandomGenerator::system()->bounded(256));
    s.setValue("web/user", user.trimmed());
    s.setValue("web/salt", QString::fromLatin1(salt.toHex()));
    s.setValue("web/hash", QString::fromLatin1(derive(password, salt)));
}

bool WebServer::hasCredentials() {
    return !Settings::instance().value("web/user").toString().isEmpty() && !Settings::instance().value("web/hash").toString().isEmpty();
}

QString WebServer::credentialUser() { return Settings::instance().value("web/user").toString(); }

bool WebServer::authorized(bool local, const QUrlQuery& q, const QByteArray& tokenHeader, const QByteArray& authorization,
                           const QString& peer, Response* deny) {
    if (local) return true;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    auto f = failures_.find(peer);
    if (f != failures_.end() && now - f->second > 10 * 60 * 1000) failures_.erase(f), f = failures_.end();
    if (f != failures_.end() && f->first >= 8) {
        *deny = error(403, "too many failed logins; try again later");
        return false;
    }
    if (!hasCredentials()) {
        if (q.queryItemValue("token") == token_ || tokenHeader == token_.toUtf8()) return true;
        *deny = error(403, "token required");
        return false;
    }
    if (acceptedAuth_.contains(authorization)) return true;
    bool ok = false;
    if (authorization.startsWith("Basic ")) {
        const QByteArray plain = QByteArray::fromBase64(authorization.mid(6).trimmed());
        const int colon = plain.indexOf(':');
        if (colon > 0) {
            const QString user = QString::fromUtf8(plain.left(colon)), pass = QString::fromUtf8(plain.mid(colon + 1));
            const auto& s = Settings::instance();
            ok = user == s.value("web/user").toString() &&
                 derive(pass, QByteArray::fromHex(s.value("web/salt").toString().toLatin1())) == s.value("web/hash").toString().toLatin1();
        }
    }
    if (ok) {
        acceptedAuth_.insert(authorization);
        failures_.remove(peer);
        return true;
    }
    if (!authorization.isEmpty()) {
        auto& slot = failures_[peer];
        if (slot.first == 0) slot.second = now;
        ++slot.first;
    }
    *deny = error(401, "login required");
    deny->extraHeaders = "WWW-Authenticate: Basic realm=\"IW8 Radiant\", charset=\"UTF-8\"\r\n";
    return false;
}

WebServer::WebServer(EditorContext* ctx, Hooks hooks, QObject* parent) : QObject(parent), ctx_(ctx), hooks_(std::move(hooks)) {
    epoch_ = QString::number(QRandomGenerator::global()->generate64(), 36);
    // A per-session token for non-local clients.
    token_ = QString::number(QRandomGenerator::system()->generate64(), 36);
    connect(ctx_, &EditorContext::documentChanged, this, [this] {
        broadcast("revision", QByteArray::number(qulonglong(ctx_->doc.revision())));
    });
    connect(ctx_, &EditorContext::statusMessage, this, [this](const QString& s) { broadcast("status", json({{"text", s}})); });
    connect(&EditorLog::instance(), &EditorLog::added, this, [this](const LogEntry& e) {
        LogTail& t = logTail();
        t.lines.append({t.next++, e});
        while (t.lines.size() > 2000) t.lines.removeFirst();
        broadcast("log", json({{"seq", double(t.next - 1)}, {"source", e.source}, {"text", e.text}}));
    });
}

WebServer::~WebServer() { stop(); }

bool WebServer::start(quint16 port, bool lan, QString* err, const QString& certFile, const QString& keyFile) {
    stop();
    lan_ = lan;
    tls_ = !certFile.isEmpty();
    if (tls_) {
        QFile cf(certFile), kf(keyFile);
        if (!cf.open(QIODevice::ReadOnly) || !kf.open(QIODevice::ReadOnly)) {
            if (err) *err = "cannot read the TLS certificate or key (" + certFile + ", " + keyFile + ")";
            return false;
        }
        const QList<QSslCertificate> chain = QSslCertificate::fromData(cf.readAll(), QSsl::Pem);
        const QByteArray keyPem = kf.readAll();
        QSslKey key(keyPem, QSsl::Rsa, QSsl::Pem);
        if (key.isNull()) key = QSslKey(keyPem, QSsl::Ec, QSsl::Pem);
        if (chain.isEmpty() || key.isNull()) {
            if (err) *err = "the TLS certificate or key is not valid PEM";
            return false;
        }
        QSslConfiguration conf = QSslConfiguration::defaultConfiguration();
        conf.setLocalCertificateChain(chain);
        conf.setPrivateKey(key);
        conf.setProtocol(QSsl::TlsV1_2OrLater);
        conf.setPeerVerifyMode(QSslSocket::VerifyNone);
        auto* ssl = new QSslServer(this);
        ssl->setSslConfiguration(conf);
        tlsError_.clear();
        connect(ssl, &QSslServer::errorOccurred, this, [this](QSslSocket* s, QAbstractSocket::SocketError) { tlsError_ = s->errorString(); });
        connect(ssl, &QSslServer::handshakeInterruptedOnError, this, [this](QSslSocket*, const QSslError& e) { tlsError_ = e.errorString(); });
        connect(ssl, &QSslServer::sslErrors, this, [this](QSslSocket* s, const QList<QSslError>& errs) {
            if (!errs.isEmpty()) tlsError_ = errs.first().errorString();
            s->ignoreSslErrors();
        });
        server_ = ssl;
    } else {
        server_ = new QTcpServer(this);
    }
    if (!server_->listen(lan ? QHostAddress::Any : QHostAddress::LocalHost, port)) {
        if (err) *err = server_->errorString();
        delete server_;
        server_ = nullptr;
        return false;
    }
    // QSslServer queues a connection only after its TLS handshake.
    if (auto* ssl = qobject_cast<QSslServer*>(server_)) connect(ssl, &QSslServer::pendingConnectionAvailable, this, &WebServer::onNewConnection);
    else connect(server_, &QTcpServer::newConnection, this, &WebServer::onNewConnection);
    return true;
}

void WebServer::stop() {
    for (QTcpSocket* s : conns_.keys()) s->disconnectFromHost();
    conns_.clear();
    if (server_) {
        server_->close();
        server_->deleteLater();
        server_ = nullptr;
    }
}

bool WebServer::running() const { return server_ && server_->isListening(); }
quint16 WebServer::port() const { return server_ ? server_->serverPort() : 0; }

QString WebServer::url() const {
    if (!running()) return {};
    QString host = "127.0.0.1";
    if (lan_)
        for (const QHostAddress& a : QNetworkInterface::allAddresses())
            if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback()) {
                host = a.toString();
                break;
            }
    return QString("%1://%2:%3/").arg(tls_ ? "https" : "http").arg(host).arg(port()) + (lan_ && !hasCredentials() ? "?token=" + token_ : QString());
}

void WebServer::onNewConnection() {
    while (QTcpSocket* s = server_->nextPendingConnection()) {
        conns_.insert(s, Conn{});
        connect(s, &QTcpSocket::readyRead, this, [this, s] { onReadyRead(s); });
        connect(s, &QTcpSocket::disconnected, this, [this, s] {
            conns_.remove(s);
            s->deleteLater();
        });
    }
}

void WebServer::onReadyRead(QTcpSocket* s) {
    auto it = conns_.find(s);
    if (it == conns_.end()) return;
    it->buf += s->readAll();
    if (it->buf.size() > 8 * 1024 * 1024) return s->disconnectFromHost();
    const int headEnd = it->buf.indexOf("\r\n\r\n");
    if (headEnd < 0) return;
    const QList<QByteArray> lines = it->buf.left(headEnd).split('\n');
    const QList<QByteArray> request = lines.value(0).trimmed().split(' ');
    if (request.size() < 2) return s->disconnectFromHost();
    qint64 length = 0;
    QByteArray tokenHeader, authorization;
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray l = lines[i].trimmed();
        const int colon = l.indexOf(':');
        if (colon < 0) continue;
        const QByteArray name = l.left(colon).trimmed().toLower(), value = l.mid(colon + 1).trimmed();
        if (name == "content-length") length = value.toLongLong();
        else if (name == "x-iw8r-token") tokenHeader = value;
        else if (name == "authorization") authorization = value;
    }
    if (it->buf.size() < headEnd + 4 + length) return;  // wait for the body
    const QByteArray body = it->buf.mid(headEnd + 4, int(length));
    it->buf.remove(0, headEnd + 4 + int(length));
    const bool local = s->peerAddress().isLoopback();
    const QString peer = s->peerAddress().toString();
    const QByteArray method = request[0], target = request[1];

    if (method == "GET" && QUrl(QString::fromUtf8(target)).path() == "/api/events") {
        // Server-sent events: keep the socket open; broadcast() writes to it.
        const QUrlQuery q(QUrl(QString::fromUtf8(target)));
        Response deny;
        if (!authorized(local, q, tokenHeader, authorization, peer, &deny)) {
            s->write("HTTP/1.1 " + QByteArray::number(deny.status) + " " + reason(deny.status) + "\r\n" + deny.extraHeaders +
                     "Content-Length: 0\r\nConnection: close\r\n\r\n");
            return s->disconnectFromHost();
        }
        it->sse = true;
        s->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-cache\r\nConnection: keep-alive\r\n\r\n");
        s->write("event: revision\ndata: " + QByteArray::number(qulonglong(ctx_->doc.revision())) + "\n\n");
        return;
    }
    const Response r = handle(method, target, body, local, tokenHeader, authorization, peer);
    QByteArray head = "HTTP/1.1 " + QByteArray::number(r.status) + " " + reason(r.status) + "\r\n" + r.extraHeaders + "Content-Type: " + r.type +
                      "\r\nContent-Length: " + QByteArray::number(r.body.size()) + "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
    s->write(head + r.body);
    s->disconnectFromHost();
}

void WebServer::broadcast(const QByteArray& event, const QByteArray& data) {
    for (auto it = conns_.begin(); it != conns_.end(); ++it)
        if (it->sse) it.key()->write("event: " + event + "\ndata: " + data + "\n\n");
}

WebServer::Response WebServer::staticFile(const QString& path) const {
    const QString root = webRoot();
    QString rel = path == "/" ? QString("index.html") : path.mid(1);
    if (root.isEmpty() || rel.contains("..")) return error(404, "not found");
    QFile f(QDir(root).filePath(rel));
    if (!f.open(QIODevice::ReadOnly)) return error(404, "not found: " + rel);
    Response r;
    r.body = f.readAll();
    r.type = rel.endsWith(".html") ? "text/html; charset=utf-8" : rel.endsWith(".js") ? "text/javascript; charset=utf-8"
             : rel.endsWith(".css") ? "text/css; charset=utf-8" : "application/octet-stream";
    return r;
}

QByteArray WebServer::stateJson() const {
    QJsonArray sel;
    for (const SelRef& r : ctx_->doc.selection()) sel.append(QJsonArray{double(r.entity), r.item == kNoId ? -1.0 : double(r.item)});
    QJsonObject o{{"revision", double(ctx_->doc.revision())},
                  {"map", hooks_.mapName ? hooks_.mapName() : qs(ctx_->doc.path())},
                  {"grid", ctx_->doc.grid()},
                  {"selection", sel},
                  {"modified", ctx_->doc.modified()},
                  {"canUndo", ctx_->doc.undoPosition() > 0},
                  {"compiling", hooks_.compileBusy ? hooks_.compileBusy() : false},
                  {"logNext", double(logTail().next)}};
    if (hooks_.camera) o.insert("camera", hooks_.camera());
    return json(o);
}

// One scene item as the browser draws it. A model is thousands of triangles the 2D views do not need: an entity item
// with more than kWebModelFaces faces is sent as the six sides of its box.
namespace {
constexpr size_t kWebModelFaces = 48;
QJsonObject webItemJson(const app::PrimGeom& g) {
    const auto num = [](double v) { return std::round(v * 100) / 100; };
    QJsonArray polys, mats, uvs;
    const bool asBox = g.kind != app::PrimGeom::Brush && g.kind != app::PrimGeom::Patch && g.polys.size() > kWebModelFaces && g.bounds.valid();
    if (asBox) {
        const Vec3 a = g.bounds.mins, b = g.bounds.maxs;
        const Vec3 c[8] = {{a.x, a.y, a.z}, {b.x, a.y, a.z}, {b.x, b.y, a.z}, {a.x, b.y, a.z}, {a.x, a.y, b.z}, {b.x, a.y, b.z}, {b.x, b.y, b.z}, {a.x, b.y, b.z}};
        static const int quads[6][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}, {0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}};
        for (const auto& quad : quads) {
            QJsonArray poly;
            for (const int k : quad) poly.append(QJsonArray{num(c[k].x), num(c[k].y), num(c[k].z)});
            polys.append(poly);
        }
    } else {
        // 10.5: per poly material and texture coordinates (texture repeats) for the textured 3D view.
        const bool textured = !g.polyMaterial.empty() && g.polyMaterial.size() == g.polys.size() && g.polyUV.size() == g.polys.size();
        for (size_t pi = 0; pi < g.polys.size(); ++pi) {
            QJsonArray poly;
            for (const Vec3& p : g.polys[pi]) poly.append(QJsonArray{num(p.x), num(p.y), num(p.z)});
            polys.append(poly);
            if (textured) {
                mats.append(qs(g.polyMaterial[pi]));
                QJsonArray uv;
                for (const auto& t : g.polyUV[pi]) uv.append(QJsonArray{std::round(t[0] * 1000) / 1000, std::round(t[1] * 1000) / 1000});
                uvs.append(uv);
            }
        }
    }
    const char* kind = g.kind == app::PrimGeom::Brush ? "brush" : g.kind == app::PrimGeom::Patch ? "patch" : "entity";
    QJsonObject o{{"e", double(g.ref.entity)},
                  {"i", g.ref.item == kNoId ? -1.0 : double(g.ref.item)},
                  {"kind", kind},
                  {"color", double(g.color & 0xffffffu)},
                  {"label", qs(g.label)},
                  {"polys", polys},
                  {"mats", mats},
                  {"uvs", uvs}};
    if (asBox) o.insert("box", true), o.insert("faces", double(g.polys.size()));
    return o;
}
}  // namespace

// Brings the browser's copy of the map up to date: every item and entity whose JSON differs from the one last sent
// gets the next map revision; the ones that are gone are remembered with theirs.
void WebServer::syncWebMap() const {
    ctx_->scene.sync(ctx_->doc);
    const quint64 at = quint64(ctx_->doc.revision()) * 1000003ull ^ quint64(ctx_->scene.generation());
    if (at == webSyncedAt_) return;
    webSyncedAt_ = at;
    const qint64 next = mapRev_ + 1;
    bool changed = false;
    const auto update = [&](QHash<QString, WebEntry>& known, QHash<QString, qint64>& gone, QStringList& order,
                            const std::vector<std::pair<QString, QJsonObject>>& now) {
        QSet<QString> seen;
        order.clear();
        for (const auto& [key, obj] : now) {
            if (seen.contains(key)) continue;  // two scene items of one reference: the first one stands
            seen.insert(key);
            order.append(key);
            const QByteArray bytes = QJsonDocument(obj).toJson(QJsonDocument::Compact);
            auto it = known.find(key);
            if (it != known.end() && it->bytes == bytes) continue;
            known.insert(key, WebEntry{obj, bytes, next});
            gone.remove(key);
            changed = true;
        }
        for (auto it = known.begin(); it != known.end();) {
            if (seen.contains(it.key())) {
                ++it;
                continue;
            }
            gone.insert(it.key(), next);
            it = known.erase(it);
            changed = true;
        }
    };
    std::vector<std::pair<QString, QJsonObject>> items, ents;
    for (const auto* g : ctx_->scene.items())
        items.push_back({QString("%1:%2").arg(g->ref.entity).arg(g->ref.item == kNoId ? -1 : qint64(g->ref.item)), webItemJson(*g)});
    for (const auto& e : ctx_->doc.map().entities) {
        QJsonObject keys;
        for (const auto& it : e.items)
            if (const KeyValueLine* kv = it.kv()) keys.insert(qs(kv->name()), qs(kv->value));
        QJsonObject eo{{"id", double(e.id)}, {"classname", qs(e.classname())}, {"keys", keys}};
        if (const auto o = e.origin()) eo.insert("origin", vec(*o));
        ents.push_back({QString::number(e.id), eo});
    }
    update(webItems_, webGoneItems_, webItemOrder_, items);
    update(webEnts_, webGoneEnts_, webEntOrder_, ents);
    if (changed) mapRev_ = next;
}

QByteArray WebServer::mapJson(qint64 since, const QString& epoch) const {
    syncWebMap();
    const bool full = since < 0 || epoch != epoch_ || since > mapRev_;
    QJsonArray items, ents, removed, removedEnts;
    for (const QString& key : webItemOrder_)
        if (const auto it = webItems_.constFind(key); it != webItems_.constEnd() && (full || it->rev > since)) items.append(it->json);
    for (const QString& key : webEntOrder_)
        if (const auto it = webEnts_.constFind(key); it != webEnts_.constEnd() && (full || it->rev > since)) ents.append(it->json);
    if (!full) {
        for (auto it = webGoneItems_.constBegin(); it != webGoneItems_.constEnd(); ++it)
            if (it.value() > since) removed.append(QJsonArray{it.key().section(':', 0, 0).toDouble(), it.key().section(':', 1, 1).toDouble()});
        for (auto it = webGoneEnts_.constBegin(); it != webGoneEnts_.constEnd(); ++it)
            if (it.value() > since) removedEnts.append(it.key().toDouble());
    }
    return json({{"revision", double(ctx_->doc.revision())}, {"mapRev", double(mapRev_)}, {"epoch", epoch_}, {"full", full},
                 {"items", items}, {"entities", ents}, {"removed", removed}, {"removedEntities", removedEnts}});
}

WebServer::Response WebServer::handle(const QByteArray& method, const QByteArray& target, const QByteArray& body, bool local,
                                      const QByteArray& tokenHeader, const QByteArray& authorization, const QString& peer) {
    const QUrl url(QString::fromUtf8(target));
    const QString path = url.path();
    const QUrlQuery q(url);
    if (Response deny; !authorized(local, q, tokenHeader, authorization, peer, &deny)) return deny;
    if (!path.startsWith("/api/")) return method == "GET" ? staticFile(path) : error(404, "not found");
    const QJsonObject in = QJsonDocument::fromJson(body).object();
    Response ok;
    ok.body = stateJson();
    if (method == "GET") {
        if (path == "/api/state") return ok;
        if (path == "/api/map") {
            Response r;
            r.body = q.hasQueryItem("since") ? mapJson(q.queryItemValue("since").toLongLong(), q.queryItemValue("epoch")) : mapJson();
            return r;
        }
        if (path == "/api/assets") {
            const QString kind = q.queryItemValue("kind"), needle = q.queryItemValue("q").trimmed().toLower();
            const int limit = std::clamp(q.queryItemValue("limit").toInt() > 0 ? q.queryItemValue("limit").toInt() : 200, 1, 2000);
            QJsonArray names;
            int total = 0;
            for (const QString& n : catalog(kind)) {
                const QString name = n.section('\t', 0, 0);
                if (!needle.isEmpty() && !name.toLower().contains(needle)) continue;
                ++total;
                if (names.size() < limit) names.append(QJsonObject{{"name", name}, {"loop", n.endsWith("\tloop")}});
            }
            Response r;
            r.body = json({{"kind", kind}, {"total", total}, {"names", names}});
            return r;
        }
        if (path == "/api/thumb") {
            const QByteArray img = hooks_.thumbnail ? hooks_.thumbnail(q.queryItemValue("kind"), q.queryItemValue("name")) : QByteArray();
            if (img.isEmpty()) return error(404, "no thumbnail yet");
            Response r;
            r.type = "image/jpeg";
            r.body = img;
            return r;
        }
        if (path == "/api/frame") {
            WebCamera cam;
            const QStringList pos = q.queryItemValue("pos").split(',');
            bool ok = pos.size() == 3;
            for (int k = 0; k < 3 && ok; ++k) cam.pos[k] = pos[k].toDouble(&ok);
            cam.yaw = q.queryItemValue("yaw").toDouble();
            cam.pitch = std::clamp(q.queryItemValue("pitch").toDouble(), -89.0, 89.0);
            if (q.hasQueryItem("fov")) cam.fov = std::clamp(q.queryItemValue("fov").toDouble(), 10.0, 150.0);
            const int w = q.queryItemValue("w").toInt(), h = q.queryItemValue("h").toInt();
            if (!ok || w < 64 || h < 64 || w > 2560 || h > 1440 || !std::isfinite(cam.pos[0] + cam.pos[1] + cam.pos[2] + cam.yaw))
                return error(400, "need pos=x,y,z, yaw, pitch, w (64..2560) and h (64..1440)");
            const int quality = std::clamp(q.hasQueryItem("q") ? q.queryItemValue("q").toInt() : 75, 30, 95);
            const QByteArray jpeg = hooks_.renderFrame ? hooks_.renderFrame(cam, w, h, quality) : QByteArray();
            if (jpeg.isEmpty()) return error(503, "the editor cannot draw the view now");
            Response r;
            r.type = "image/jpeg";
            r.body = jpeg;
            r.extraHeaders = "Cache-Control: no-store\r\n";
            return r;
        }
        if (path == "/api/forms") {
            Response r;
            r.body = json({{"forms", hooks_.forms ? hooks_.forms() : QJsonArray()}});
            return r;
        }
        if (path == "/api/form") {
            const QJsonObject f = hooks_.form ? hooks_.form(q.queryItemValue("id")) : QJsonObject();
            if (f.isEmpty()) return error(404, "there is no form " + q.queryItemValue("id"));
            Response r;
            r.body = json(f);
            return r;
        }
        if (path == "/api/commands") {
            Response r;
            r.body = json({{"commands", hooks_.commands ? hooks_.commands() : QJsonArray()}});
            return r;
        }
        if (path == "/api/log") {
            const qint64 since = q.queryItemValue("since").toLongLong();
            QJsonArray lines;
            for (const auto& [seq, e] : logTail().lines)
                if (seq >= since) lines.append(QJsonObject{{"seq", double(seq)}, {"source", e.source}, {"text", e.text}});
            Response r;
            r.body = json({{"lines", lines}, {"next", double(logTail().next)}});
            return r;
        }
        return error(404, "unknown " + path);
    }
    if (method != "POST") return error(400, "GET or POST");
    if (path == "/api/select") {
        const Id e = Id(in.value("e").toDouble(-1));
        const double i = in.value("i").toDouble(-1);
        if (!ctx_->doc.findEntity(e)) return error(400, "no such entity");
        ctx_->doc.select({e, i < 0 ? kNoId : Id(i)}, in.value("add").toBool());
        ctx_->documentEdited("Selected from the browser");
    } else if (path == "/api/deselect") {
        ctx_->doc.deselectAll();
        ctx_->documentEdited();
    } else if (path == "/api/key") {
        const Id e = Id(in.value("e").toDouble(-1));
        const std::string key = in.value("key").toString().trimmed().toStdString();
        if (!ctx_->doc.findEntity(e) || key.empty() || key.find_first_of("\" \t\r\n") != std::string::npos) return error(400, "bad entity or key");
        const bool done = in.value("value").isNull() ? ctx_->doc.removeKey(e, key)
                                                     : ctx_->doc.setKeyValue(e, key, in.value("value").toString().toStdString());
        if (done) ctx_->documentEdited(QString("%1 set from the browser").arg(qs(key)));
    } else if (path == "/api/move") {
        const QJsonArray d = in.value("d").toArray();
        if (d.size() != 3 || ctx_->doc.selection().empty()) return error(400, "need d:[x,y,z] and a selection");
        ctx_->doc.beginDrag();
        ctx_->doc.updateDrag(Vec3{d[0].toDouble(), d[1].toDouble(), d[2].toDouble()});
        if (ctx_->doc.endDrag(true)) ctx_->documentEdited("Moved from the browser");
    } else if (path == "/api/undo") {
        if (ctx_->doc.undo()) ctx_->documentEdited("Undo (browser)");
    } else if (path == "/api/redo") {
        if (ctx_->doc.redo()) ctx_->documentEdited("Redo (browser)");
    } else if (path == "/api/save") {
        if (!hooks_.save || !hooks_.save()) return error(400, "save failed (unsaved maps need Save As in the editor)");
    } else if (path == "/api/compile") {
        if (!hooks_.compile) return error(400, "no compiler");
        hooks_.compile();
    } else if (path == "/api/deploy") {
        if (!hooks_.deploy) return error(400, "deploy is not available");
        hooks_.deploy();
    } else if (path == "/api/build") {
        if (!hooks_.buildDeployLoad) return error(400, "build is not available");
        hooks_.buildDeployLoad();
    } else if (path == "/api/load") {
        QString why;
        if (!hooks_.loadInGame || !hooks_.loadInGame(&why)) return error(400, why.isEmpty() ? QString("the game is not reachable") : why);
    } else if (path == "/api/pick") {
        WebCamera cam;
        const QJsonArray pos = in.value("pos").toArray();
        if (pos.size() != 3 || !hooks_.pickFrame) return error(400, "need pos:[x,y,z], yaw, pitch, x and y");
        for (int k = 0; k < 3; ++k) cam.pos[k] = pos[k].toDouble();
        cam.yaw = in.value("yaw").toDouble();
        cam.pitch = std::clamp(in.value("pitch").toDouble(), -89.0, 89.0);
        cam.fov = std::clamp(in.value("fov").toDouble(90), 10.0, 150.0);
        const double x = in.value("x").toDouble(-1), y = in.value("y").toDouble(-1);
        if (x < 0 || x > 1 || y < 0 || y > 1) return error(400, "x and y are 0..1 from the top left of the view");
        hooks_.pickFrame(cam, x, y, std::clamp(in.value("aspect").toDouble(16.0 / 9.0), 0.2, 5.0), in.value("add").toBool());
    } else if (path == "/api/form") {
        QString why;
        if (!hooks_.applyForm || !hooks_.applyForm(in.value("id").toString(), in.value("values").toObject(), &why))
            return error(400, why.isEmpty() ? QString("the form cannot be applied") : why);
    } else if (path == "/api/command") {
        const QString id = in.value("id").toString();
        QString why;
        if (!hooks_.runCommand || !hooks_.runCommand(id, &why)) return error(400, why.isEmpty() ? QString("the command cannot be run") : why);
    } else if (path == "/api/material") {
        const std::string name = in.value("name").toString().trimmed().toStdString();
        if (name.empty() || name.find_first_of(" \t\"") != std::string::npos) return error(400, "bad material name");
        ctx_->currentMaterial = name;
        if (applyMaterialToSelection(*ctx_, name)) ctx_->documentEdited(QString("Applied %1 from the browser").arg(QString::fromStdString(name)));
    } else if (path == "/api/place") {
        const QString kind = in.value("kind").toString();
        const std::string name = in.value("name").toString().trimmed().toStdString();
        const QJsonArray at = in.value("at").toArray();
        if (name.empty() || name.find_first_of(" \t\"") != std::string::npos || at.size() != 3) return error(400, "need kind, name and at");
        const std::string origin = formatNumber(at[0].toDouble()) + " " + formatNumber(at[1].toDouble()) + " " + formatNumber(at[2].toDouble());
        std::vector<std::pair<std::string, std::string>> kv;
        if (kind == "models") kv = {{"classname", "misc_model"}, {"model", name}, {"origin", origin}};
        else if (kind == "effects") kv = {{"classname", "fx_origin"}, {"fx", name}, {"origin", origin}};
        else if (kind == "sounds") {
            const bool loop = in.value("loop").toBool();
            kv = {{"classname", "sound_emitter"}, {"soundalias", name}, {"loop", loop ? "1" : "0"}, {"origin", origin}};
            if (!loop) kv.push_back({"delay", "5"});
        } else
            return error(400, "kind must be models, effects or sounds");
        const Id id = ctx_->doc.addEntity(kv, {}, "Place from the browser");
        ctx_->doc.select({id, kNoId}, false);
        ctx_->documentEdited(QString("Placed %1 from the browser").arg(QString::fromStdString(name)));
    } else {
        return error(404, "unknown " + path);
    }
    ok.body = stateJson();
    return ok;
}

}  // namespace iw8r::qt
