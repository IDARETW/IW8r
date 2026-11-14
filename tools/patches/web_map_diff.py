import io

# Plan 3.2, part 1: the browser's map data is lean and incremental.
#  - A model (an entity item with many faces) is sent as its box, not as every triangle of the model: a 205-item map
#    was 64 MB of JSON, Cinder Rail 248 MB.
#  - GET /api/map?since=<mapRev>&epoch=<epoch> answers with only the items and entities that changed since then, and
#    the ones that went away. Without `since` (or after an editor restart) the whole map comes, as before.

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b, n=1):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    write(p, s.replace(a, b))


def between(p, start, end, new):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    assert s.count(start) == 1 and s.count(end) == 1, (p, s.count(start), s.count(end))
    i, j = s.index(start), s.index(end)
    assert i < j
    write(p, s[:i] + new.replace('\n', nl) + s[j:])


H = 'src/qtapp/web_server.h'
patch(H, '''//   GET  /api/map                    every drawable item: {e, i, kind, color, label, polys:[[[x,y,z],...],...]} and
//                                    entities {id, classname, origin, keys}
''', '''//   GET  /api/map                    every drawable item: {e, i, kind, color, label, polys:[[[x,y,z],...],...]} and
//                                    entities {id, classname, origin, keys}; with mapRev and epoch. A model (an
//                                    entity item with more than 48 faces) comes as its box: {box: true, faces: N}.
//   GET  /api/map?since=R&epoch=E    only what changed after mapRev R: {full: false, items, entities, removed:
//                                    [[e, i], ...], removedEntities: [id, ...]}; the whole map (full: true) when E
//                                    is not this editor's epoch
''')
patch(H, '''    QByteArray mapJson() const;
''', '''    QByteArray mapJson(qint64 since = -1, const QString& epoch = {}) const;
    void syncWebMap() const;
''')
patch(H, '''    QSet<QByteArray> acceptedAuth_;              // Authorization headers already verified this session
''', '''    QSet<QByteArray> acceptedAuth_;              // Authorization headers already verified this session
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
''')
s = read(H)
for inc in ('#include <QJsonObject>', '#include <QStringList>'):
    if inc not in s:
        s = s.replace('#include <QJsonArray>\n', '#include <QJsonArray>\n' + inc + '\n').replace('#include <QJsonArray>\r\n', '#include <QJsonArray>\r\n' + inc + '\r\n')
write(H, s)

S = 'src/qtapp/web_server.cpp'
patch(S, '''        if (path == "/api/map") {
            Response r;
            r.body = mapJson();
            return r;
        }''', '''        if (path == "/api/map") {
            Response r;
            r.body = q.hasQueryItem("since") ? mapJson(q.queryItemValue("since").toLongLong(), q.queryItemValue("epoch")) : mapJson();
            return r;
        }''')
between(S, 'QByteArray WebServer::mapJson() const {', 'WebServer::Response WebServer::handle(', '''// One scene item as the browser draws it. A model is thousands of triangles the 2D views do not need: an entity item
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

''')
s = read(S)
# The epoch: one random text per server object (an editor that was started again answers with the whole map).
a = 'WebServer::WebServer(EditorContext* ctx, Hooks hooks, QObject* parent)'
assert s.count(a) == 1
i = s.index(a)
j = s.index('{', i) + 1
nl = '\r\n' if '\r\n' in s else '\n'
s = s[:j] + nl + '    epoch_ = QString::number(QRandomGenerator::global()->generate64(), 36);' + s[j:]
for inc in ('#include <QRandomGenerator>', '#include <QSet>'):
    if inc not in s:
        k = s.index('#include <Q')
        s = s[:k] + inc + nl + s[k:]
write(S, s)

# --- the page: apply the difference ---------------------------------------------------------------------------------
patch('data/web/app.js', '''async function refresh() {
  const [m, s] = await Promise.all([get("/api/map"), get("/api/state")]);
  map = m; state = s;
''', '''// The map comes whole once, then as differences (GET /api/map?since=): changed items and entities replace their
// old ones, the ones in `removed` go.
let mapRev = -1, mapEpoch = "";
function applyMap(m) {
  if (m.full || mapRev < 0) {
    map = { items: m.items || [], entities: m.entities || [] };
  } else {
    const gone = new Set((m.removed || []).map(([e, i]) => selKey(e, i)));
    const fresh = new Map((m.items || []).map((it) => [selKey(it.e, it.i), it]));
    const items = [];
    for (const it of map.items) {
      const k = selKey(it.e, it.i);
      if (gone.has(k)) continue;
      if (fresh.has(k)) { items.push(fresh.get(k)); fresh.delete(k); } else items.push(it);
    }
    for (const it of fresh.values()) items.push(it);
    const goneEnts = new Set(m.removedEntities || []);
    const freshEnts = new Map((m.entities || []).map((e) => [e.id, e]));
    const ents = [];
    for (const e of map.entities) {
      if (goneEnts.has(e.id)) continue;
      if (freshEnts.has(e.id)) { ents.push(freshEnts.get(e.id)); freshEnts.delete(e.id); } else ents.push(e);
    }
    for (const e of freshEnts.values()) ents.push(e);
    map = { items, entities: ents };
  }
  mapRev = m.mapRev; mapEpoch = m.epoch || "";
}
async function refresh() {
  const [m, s] = await Promise.all([get(mapRev < 0 ? "/api/map" : `/api/map?since=${mapRev}&epoch=${encodeURIComponent(mapEpoch)}`), get("/api/state")]);
  applyMap(m); state = s;
''')

# --- GUI test -------------------------------------------------------------------------------------------------------
T = 'src/qtapp/qt_gui_test.cpp'
patch(T, '''        // Edits go through the shared document (one undo history).
        const std::string before = ctx.doc.text();''', '''        {
            // Plan 3.2: the map goes to the browser whole once, then as differences; a model goes as its box.
            const QJsonObject full = QJsonDocument::fromJson(web.handle("GET", "/api/map", {}, true, {}).body).object();
            const qint64 rev0 = qint64(full.value("mapRev").toDouble());
            const QString epoch = full.value("epoch").toString();
            const auto diffOf = [&](qint64 since, const QString& ep) {
                return QJsonDocument::fromJson(web.handle("GET", QString("/api/map?since=%1&epoch=%2").arg(since).arg(ep).toUtf8(), {}, true, {}).body).object();
            };
            const QJsonObject same = diffOf(rev0, epoch);
            r.check(full.value("full").toBool() && rev0 > 0 && !epoch.isEmpty() && !same.value("full").toBool() &&
                        same.value("items").toArray().isEmpty() && same.value("entities").toArray().isEmpty() &&
                        same.value("removed").toArray().isEmpty() && qint64(same.value("mapRev").toDouble()) == rev0,
                    QString("GET /api/map?since= of an unchanged map is empty (the whole map: %1 items)").arg(full.value("items").toArray().size()));
            const Id added = ctx.doc.addEntity({{"classname", "script_origin"}, {"origin", "64 64 0"}}, {}, "web diff");
            ctx.documentEdited();
            const QJsonObject plus = diffOf(rev0, epoch);
            const qint64 rev1 = qint64(plus.value("mapRev").toDouble());
            bool entListed = false;
            for (const QJsonValue& v : plus.value("entities").toArray()) entListed = entListed || Id(v.toObject().value("id").toDouble()) == added;
            const int itemsPlus = plus.value("items").toArray().size();
            ctx.doc.setKeyValue(added, "origin", "128 64 0");
            ctx.documentEdited();
            const QJsonObject moved = diffOf(rev1, epoch);
            const qint64 rev2 = qint64(moved.value("mapRev").toDouble());
            ctx.doc.undo();
            ctx.doc.undo();
            ctx.documentEdited();
            const QJsonObject minus = diffOf(rev2, epoch);
            bool entGone = false, itemGone = false;
            for (const QJsonValue& v : minus.value("removedEntities").toArray()) entGone = entGone || Id(v.toDouble()) == added;
            for (const QJsonValue& v : minus.value("removed").toArray()) itemGone = itemGone || Id(v.toArray().at(0).toDouble()) == added;
            r.check(!plus.value("full").toBool() && rev1 > rev0 && entListed && itemsPlus >= 1 && itemsPlus <= 2 && rev2 > rev1 &&
                        moved.value("entities").toArray().size() == 1 && moved.value("items").toArray().size() == 1 && entGone && itemGone,
                    QString("an added, moved and removed entity each come as a small difference (%1 item(s), then 1, then removed)").arg(itemsPlus));
            const QJsonObject other = diffOf(rev2, "another-editor");
            r.check(other.value("full").toBool() && other.value("items").toArray().size() == full.value("items").toArray().size(),
                    "a browser that last talked to another editor run gets the whole map again");
            int boxes = 0, maxFaces = 0;
            for (const QJsonValue& v : full.value("items").toArray()) {
                const QJsonObject o = v.toObject();
                if (o.value("box").toBool()) ++boxes, r.check(o.value("polys").toArray().size() == 6 && o.value("faces").toInt() > 48, "a model is sent as the six sides of its box");
                if (o.value("kind").toString() == "entity") maxFaces = std::max(maxFaces, int(o.value("polys").toArray().size()));
            }
            r.check(maxFaces <= 48, QString("no entity item sends more than 48 faces (%1 model box(es), largest entity item %2 faces)").arg(boxes).arg(maxFaces));
            auto jsNow = web.handle("GET", "/app.js", {}, true, {});
            r.check(jsNow.body.contains("applyMap") && jsNow.body.contains("/api/map?since="), "the browser page asks for differences");
        }
        // Edits go through the shared document (one undo history).
        const std::string before = ctx.doc.text();''')
print('ok')
