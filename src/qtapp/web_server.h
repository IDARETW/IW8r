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
//   GET  /api/assets?kind=materials|models|effects|sounds|prefabs&q=text&limit=N   catalog names (unbuildable ones
//                                    left out); prefabs: the editor's prefab library, each {name, id, note}, placed
//                                    with POST /api/place {kind: "prefabs", name: id} in front of the view
//   GET  /api/thumb?kind=..&name=..  a cached thumbnail image (404 when none yet)
//   POST /api/place    {kind, name, at:[x,y,z]}   models -> misc_model, effects -> fx_origin, sounds -> sound_emitter;
//                                    with view: {pos, yaw, pitch, fov, x, y, aspect} instead of at, on the surface
//                                    under that point of the browser's live view (400 when nothing is there)
//   POST /api/material {name}        apply to the selected brushes / faces
//   GET  /api/frame?pos=x,y,z&yaw=&pitch=&fov=&w=&h=&q=   the editor's own 3D render from that camera, as a JPEG
//                                    (w 64..2560, h 64..1440, q 30..95); the editor's modes apply (textured, lit,
//                                    Game Lighting, the stock reference)
//   POST /api/pick     {pos, yaw, pitch, fov, x, y, aspect, add}   select what lies under the point (x, y in 0..1
//                                    from the top left) of that view; nothing there deselects
//   POST /api/brush    {mins, maxs}  a box brush with the current material (one undo step)
//   POST /api/resize   {axis, factor, pivot:[x,y,z]}   scales the selection along axis 0..2 about the pivot
//   GET  /api/entityclasses          the entity classes: {name, kind, description, built}
//   GET  /api/entitydef?class=C&e=ID one class: description, kind, built, keys [{name, type, default, description,
//                                    required, examples}]; with e, "problems": what Check Map says about that entity
//   POST /api/entity   {classname, at:[x,y,z] | view:{...}}   a point entity of that class
//   GET  /api/commands               every editor command in menu order: {id, label, menu, shortcut, checkable,
//                                    checked, enabled, blocked} ("blocked": why the browser cannot run it, "" = it
//                                    can) and {sep: true, menu} between groups
//   POST /api/command  {id}          runs a command as the menu would (400 with the reason when it cannot)
//   GET  /api/forms                  the dialogs that have a web form: {id, title, command, menu}
//   GET  /api/form?id=ID             one form: {id, title, fields: [{key, label, type, value, min, max, step,
//                                    choices, help}]}; type number | text | choice | color (#rrggbb) | bool
//   POST /api/form     {id, values}  applies the changed fields (one undo step; 400 with the reason when a value
//                                    is not accepted)
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

// A camera a browser sends with its view requests (each browser has its own).
struct WebCamera {
    double pos[3] = {0, 0, 0};
    double yaw = 0, pitch = 0;  // degrees: yaw around +Z from +X, pitch up
    double fov = 90;            // horizontal degrees
};

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
        // The editor's own 3D render from a browser's camera (plan 3.1): JPEG bytes, empty when it cannot draw.
        std::function<QByteArray(const WebCamera& cam, int w, int h, int quality)> renderFrame;
        // Selects what lies under a point of that view (x, y in 0..1 from the top left); false when nothing is there.
        std::function<bool(const WebCamera& cam, double x, double y, double aspect, bool add)> pickFrame;
        // The same for one brush face (the editor's Ctrl + Shift + click): add toggles it in the face selection.
        std::function<bool(const WebCamera& cam, double x, double y, double aspect, bool add)> pickFace;
        // The point of the scene under a point of that view; false when the ray hits nothing.
        std::function<bool(const WebCamera& cam, double x, double y, double aspect, double point[3])> surfacePoint;
        // The editor's own camera, as {pos: [x, y, z], yaw, pitch, fov}: where a browser's view starts.
        std::function<QJsonObject()> camera;
        // The prefab library (the Prefabs dock): [{name, id, note}] (note: why it cannot be placed, "" = it can), and
        // placing one by id in front of the editor's camera or, with a camera, in front of that view.
        std::function<QJsonArray()> prefabs;
        std::function<bool(const QString& id, const WebCamera* cam, QString* why)> placePrefab;
        // The info text of a material, model or effect: {title, html}; empty = none.
        std::function<QJsonObject(const QString& kind, const QString& name)> assetInfo;
        // The web forms (MainWindow::webFormList / webForm / applyWebForm).
        std::function<QJsonArray()> forms;
        std::function<QJsonObject(const QString& id)> form;
        std::function<bool(const QString& id, const QJsonObject& values, QString* why)> applyForm;
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
