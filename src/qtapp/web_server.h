// IW8 Radiant Suite - browser access (roadmap L10): a small HTTP/1.1 server inside the editor.
//
// The browser client (data/web/index.html + app.js) drives the same live document as the desktop window, so both share
// one undo history. Off by default (Tools > Web Server); it listens on 127.0.0.1 unless "web/lan" is set. Requests from
// another machine must authenticate: with credentials set (Tools > Web Server Settings; stored as a salted PBKDF2-SHA256
// hash, settings web/user, web/salt, web/hash) by HTTP Basic auth, otherwise with the session token (?token= or the
// X-IW8R-Token header). Eight failed logins from one address lock it out for ten minutes. "web/upnp" asks the router
// to forward the port (upnp.h) for access from outside the LAN.
//
// API (JSON; all edits are ordinary undoable document edits):
//   GET  /api/state                  revision, map name, grid, selection, status line, compile state
//   GET  /api/map                    every drawable item: {e, i, kind, color, label, polys:[[[x,y,z],...],...]} and
//                                    entities {id, classname, origin, keys}; with mapRev and epoch. A model (an
//                                    entity item with more than 48 faces) comes as its box: {box: true, faces: N}.
//   GET  /api/map?since=R&epoch=E    only what changed after mapRev R: {full: false, items, entities, removed:
//                                    [[e, i], ...], removedEntities: [id, ...]}; the whole map (full: true) when E
//                                    is not this editor's epoch
//   GET  /api/log?since=N            log lines after N
//   GET  /api/events                 server-sent events: "revision" (document changed), "status", "log"
//   POST /api/select   {e, i, add}   select an item (i = -1: the entity); add = keep the selection
//   POST /api/deselect
//   POST /api/key      {e, key, value}   set a key (value null: remove it)
//   POST /api/move     {d:[x,y,z]}   move the selection (one undo step)
//   POST /api/undo | /api/redo | /api/save | /api/compile | /api/deploy | /api/build (F5) | /api/load
//   GET  /api/assets?kind=materials|models|effects|sounds&q=text&limit=N   catalog names (unbuildable ones left out)
//   GET  /api/thumb?kind=..&name=..  a cached thumbnail image (404 when none yet)
//   POST /api/place    {kind, name, at:[x,y,z]}   models -> misc_model, effects -> fx_origin, sounds -> sound_emitter
//   POST /api/material {name}        apply to the selected brushes / faces
//   GET  /api/commands               every editor command in menu order: {id, label, menu, shortcut, checkable,
//                                    checked, enabled, blocked} ("blocked": why the browser cannot run it, "" = it
//                                    can) and {sep: true, menu} between groups
//   POST /api/command  {id}          runs a command as the menu would (400 with the reason when it cannot)
#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QStringList>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUrlQuery>

#include <functional>

class QTcpServer;
class QTcpSocket;

namespace iw8r::qt {

class EditorContext;

class WebServer : public QObject {
    Q_OBJECT
public:
    struct Hooks {
        std::function<bool()> save;           // File > Save
        std::function<void()> compile;        // F7
        std::function<QString()> mapName;     // window title name
        std::function<bool()> compileBusy;
        std::function<void()> deploy;          // Compile > Compile Map and Deploy (Ctrl+F7)
        std::function<void()> buildDeployLoad; // F5
        std::function<bool(QString*)> loadInGame;  // Live Link: load the compiled map in the running game
        std::function<QByteArray(const QString& kind, const QString& name)> thumbnail;  // JPEG bytes, empty = none yet
        // Every command of the editor's menus, in menu order (MainWindow::webCommandList).
        std::function<QJsonArray()> commands;
        // Runs one by id; false with *why when it is unknown, not available now, or not for the browser.
        std::function<bool(const QString& id, QString* why)> runCommand;
    };
    WebServer(EditorContext* ctx, Hooks hooks, QObject* parent = nullptr);
    ~WebServer() override;

    // Starts listening; false with *err when the port is taken. lan: bind every interface (token required off-host).
    // With certFile/keyFile (PEM) the server speaks HTTPS only (TLS 1.2 or later); false with *err when they cannot be
    // read. tools/make_web_cert.ps1 makes a self-signed pair.
    bool start(quint16 port, bool lan, QString* err = nullptr, const QString& certFile = {}, const QString& keyFile = {});
    bool tls() const { return tls_; }
    QString lastTlsError() const { return tlsError_; }
    void stop();
    bool running() const;
    quint16 port() const;
    QString token() const { return token_; }
    QString url() const;

    // Handles one request (also used by --gui-test without sockets). Returns status, content type and body.
    struct Response {
        int status = 200;
        QByteArray type = "application/json";
        QByteArray body;
        QByteArray extraHeaders;  // "Name: value\r\n" lines
    };
    Response handle(const QByteArray& method, const QByteArray& target, const QByteArray& body, bool local, const QByteArray& tokenHeader,
                    const QByteArray& authorization = {}, const QString& peer = {});

    // Credentials for remote access (empty user clears them). The password is kept only as a salted hash.
    static void setCredentials(const QString& user, const QString& password);
    static bool hasCredentials();
    static QString credentialUser();

private:
    struct Conn {
        QByteArray buf;
        bool sse = false;
    };
    void onNewConnection();
    void onReadyRead(QTcpSocket* s);
    void broadcast(const QByteArray& event, const QByteArray& data);
    QByteArray mapJson(qint64 since = -1, const QString& epoch = {}) const;
    void syncWebMap() const;
    QByteArray stateJson() const;
    Response staticFile(const QString& path) const;

    EditorContext* ctx_;
    Hooks hooks_;
    QTcpServer* server_ = nullptr;
    QHash<QTcpSocket*, Conn> conns_;
    QString token_;
    bool lan_ = false;
    bool tls_ = false;
    QString tlsError_;
    bool authorized(bool local, const QUrlQuery& q, const QByteArray& tokenHeader, const QByteArray& authorization, const QString& peer,
                    Response* deny);
    QSet<QByteArray> acceptedAuth_;              // Authorization headers already verified this session
    // The map as the browser has it (plan 3.2): per item and per entity the JSON last sent and the map revision
    // (mapRev_, counted here) at which it last changed; what went away, with the revision of its going.
    struct WebEntry {
        QJsonObject json;
        QByteArray bytes;
        qint64 rev = 0;
    };
    mutable QHash<QString, WebEntry> webItems_, webEnts_;
    mutable QHash<QString, qint64> webGoneItems_, webGoneEnts_;
    mutable QStringList webItemOrder_, webEntOrder_;  // scene order, for the full answer
    mutable qint64 mapRev_ = 0;
    mutable quint64 webSyncedAt_ = ~0ull;  // document revision x scene generation of the last sync
    QString epoch_;
    QHash<QString, QPair<int, qint64>> failures_;  // peer -> (failed logins, first failure ms)
};

}  // namespace iw8r::qt
