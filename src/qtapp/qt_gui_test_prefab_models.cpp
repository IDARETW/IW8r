#include "qt_gui_test_prefab_models.h"

#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "brush_ops.h"
#include "camera_view.h"
#include "main_window.h"
#include "map_text.h"
#include "model_import.h"
#include "render.h"
#include "scene.h"

#ifdef _MSC_VER
#pragma optimize("", off)  // one long scripted function, as the other GUI check files
#endif

namespace iw8r::qt {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

std::string boxText(const AABB& b, const char* material) {
    const auto prim = makeBrushPrimitive(makeBox(b, std::string(" ") + material + " 128 128 0 0 0 0"), "\n");
    return prim ? prim->text() : std::string();
}

AABB box(double x0, double y0, double z0, double x1, double y1, double z1) {
    AABB b;
    b.add(Vec3{x0, y0, z0});
    b.add(Vec3{x1, y1, z1});
    return b;
}

// A box model in file coordinates (OBJ / glTF Y-up): half sizes hx along x, hy along y (world z), hz along z (world
// -y). 12 triangles, one material, texture coordinates per vertex.
std::shared_ptr<ImportedModel> boxModel(const std::string& name, double hx, double hy, double hz, const std::string& material) {
    auto m = std::make_shared<ImportedModel>();
    m->name = name;
    ImportedMesh mesh;
    mesh.material = material;
    for (int k = 0; k < 8; ++k) {
        mesh.pos.push_back(Vec3{k & 1 ? hx : -hx, k & 2 ? hy : -hy, k & 4 ? hz : -hz});
        mesh.uv.push_back({double(k & 1), double((k >> 1) & 1)});
    }
    static const uint32_t faces[6][4] = {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}};
    for (const auto& f : faces)
        for (const int t : {0, 1, 2, 0, 2, 3}) mesh.idx.push_back(f[t]);
    for (const Vec3& p : mesh.pos) m->bounds.add(p);
    m->meshes.push_back(std::move(mesh));
    return m;
}

// The box around polys [from, to) of an item.
AABB polyBounds(const app::PrimGeom& g, size_t from, size_t to) {
    AABB b;
    for (size_t i = from; i < to && i < g.polys.size(); ++i)
        for (const Vec3& v : g.polys[i]) b.add(v);
    return b;
}

bool near(const AABB& a, const AABB& b) {
    if (!a.valid() || !b.valid()) return false;
    const double d[6] = {a.mins.x - b.mins.x, a.mins.y - b.mins.y, a.mins.z - b.mins.z, a.maxs.x - b.maxs.x, a.maxs.y - b.maxs.y, a.maxs.z - b.maxs.z};
    for (double v : d)
        if (std::fabs(v) > 1e-3) return false;
    return true;
}

QString boxStr(const AABB& b) {
    if (!b.valid()) return "none";
    return QString("%1 %2 %3 .. %4 %5 %6").arg(b.mins.x).arg(b.mins.y).arg(b.mins.z).arg(b.maxs.x).arg(b.maxs.y).arg(b.maxs.z);
}

template <class V>
bool sameVerts(const std::vector<V>& a, const std::vector<V>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(V)) == 0);
}

const app::PrimGeom* itemOf(const app::SceneCache& sc, Id entity) {
    for (const app::PrimGeom* g : sc.items())
        if (g->ref.entity == entity && g->ref.item == kNoId) return g;
    return nullptr;
}

}  // namespace

void runPrefabModelChecks(std::function<void(const QString&)> section, std::function<void(bool, const QString&)> check, MainWindow& w,
                          const QString& outDir) {
    section("models inside placed prefabs (scene cache)");
    {
        // A scene of its own: prefab files and models come from the callbacks below, no file or cache is read.
        std::map<std::string, std::shared_ptr<const MapFile>> prefabs;
        const auto addPrefab = [&](const std::string& name, const std::string& text) {
            Id next = 1;
            ParseResult pr = parseMap(text, next);
            check(pr.map.has_value(), qs("the test prefab " + name + " parses"));
            if (pr.map) prefabs[name] = std::make_shared<const MapFile>(std::move(*pr.map));
        };
        // inner: one brush, a model 2x its size with modelscale 2, and a plain point entity (a marker box).
        addPrefab("inner.map", "iwmap 4\n{\n\"classname\" \"worldspawn\"\n" + boxText(box(-8, -8, 0, 8, 8, 16), "pfm_wall") + "}\n"
                               "{\n\"classname\" \"misc_model\"\n\"model\" \"pfm_box\"\n\"origin\" \"32 0 0\"\n\"modelscale\" \"2\"\n}\n"
                               "{\n\"classname\" \"script_origin\"\n\"origin\" \"0 0 32\"\n}\n");
        // outer: the inner prefab raised by 64, plus a dynamic_prop of the same model.
        addPrefab("outer.map", "iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n"
                               "{\n\"classname\" \"misc_prefab\"\n\"model\" \"inner.map\"\n\"origin\" \"0 0 64\"\n}\n"
                               "{\n\"classname\" \"dynamic_prop\"\n\"model\" \"pfm_box\"\n\"origin\" \"200 0 0\"\n}\n");
        // late: a model that is not loaded yet.
        addPrefab("late.map", "iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n"
                              "{\n\"classname\" \"script_model\"\n\"model\" \"pfm_late\"\n\"origin\" \"0 0 0\"\n}\n");
        const auto pfmBox = boxModel("pfm_box", 16, 2, 4, "iw8r_pfm_mat");
        const auto pfmLate = boxModel("pfm_late", 4, 4, 4, "iw8r_pfm_mat");
        bool lateArrived = false;
        size_t modelAsks = 0;
        app::SceneCache sc;
        sc.modelScale = 1.0;
        sc.prefabSource = [&](const Entity& e) -> std::shared_ptr<const MapFile> {
            const std::string* m = e.value("model");
            const auto it = m ? prefabs.find(*m) : prefabs.end();
            return it != prefabs.end() ? it->second : nullptr;
        };
        sc.modelSource = [&](const Entity& e) -> std::shared_ptr<const ImportedModel> {
            ++modelAsks;
            const std::string* m = e.value("model");
            if (!m) return nullptr;
            if (*m == "pfm_box") return pfmBox;
            if (*m == "pfm_late" && lateArrived) return pfmLate;
            return nullptr;
        };
        sc.scriptableModelFor = [](const std::string& d) { return d == "iw8r_pfm_def" ? std::string("pfm_box") : std::string(); };
        Document doc;
        std::string err;
        const bool loaded = doc.loadFromText(
            "iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n"
            "{\n\"classname\" \"misc_prefab\"\n\"model\" \"inner.map\"\n\"origin\" \"1000 0 0\"\n\"angles\" \"0 90 0\"\n}\n"
            "{\n\"classname\" \"misc_prefab\"\n\"model\" \"outer.map\"\n\"origin\" \"-1000 0 0\"\n}\n"
            "{\n\"classname\" \"misc_prefab\"\n\"model\" \"late.map\"\n\"origin\" \"0 1000 0\"\n}\n"
            "{\n\"classname\" \"scriptable\"\n\"scriptable\" \"iw8r_pfm_def\"\n\"origin\" \"0 -1000 0\"\n}\n"
            "{\n\"classname\" \"scriptable\"\n\"scriptable\" \"no_such_def\"\n\"origin\" \"0 -1200 0\"\n}\n",
            "prefab_models_test.map", &err);
        check(loaded, "the prefab model test map loads" + (err.empty() ? QString() : " (" + qs(err) + ")"));
        Id pInner = kNoId, pOuter = kNoId, pLate = kNoId, sDef = kNoId, sNone = kNoId;
        for (const auto& e : doc.map().entities) {
            const std::string* m = e.value("model");
            if (m && *m == "inner.map") pInner = e.id;
            if (m && *m == "outer.map") pOuter = e.id;
            if (m && *m == "late.map") pLate = e.id;
            if (const std::string* s = e.value("scriptable")) (*s == "iw8r_pfm_def" ? sDef : sNone) = e.id;
        }
        sc.sync(doc);
        using app::PrimGeom;
        const PrimGeom* gi = itemOf(sc, pInner);
        // 6 brush faces, 12 model triangles, 6 faces of the script_origin marker.
        check(gi && gi->prefab && gi->polys.size() == 24 && gi->meshFrom == 6 && gi->markerFrom == 18 && gi->hasMeshPolys(),
              QString("a prefab's misc_model is drawn as its mesh, not a box (%1 polys, models from %2, markers from %3)")
                  .arg(gi ? int(gi->polys.size()) : -1).arg(gi ? int(gi->meshFrom) : -1).arg(gi ? int(gi->markerFrom) : -1));
        if (gi) {
            // Model 32 x 8 x 4 at modelscale 2, its origin 32 0 0 in the prefab, the prefab at 1000 0 0 turned by yaw 90.
            const AABB want = box(992, 0, -4, 1008, 64, 4);
            const AABB got = polyBounds(*gi, gi->meshFrom, gi->markerFrom);
            check(near(got, want), "the prefab's model sits at the composed origin, angles and modelscale (" + boxStr(got) + ", want " + boxStr(want) + ")");
            check(gi->polyMaterial.size() == 18 && gi->polyMaterial[0] == "pfm_wall" && gi->polyUV[0].size() == 4 && gi->polyMaterial[6] == "iw8r_pfm_mat" &&
                      gi->polyUV.size() == 18 && gi->polyUV[6].size() == 3,
                  QString("the prefab's brush faces and its model keep their materials and texture coordinates (%1 materials)").arg(gi->polyMaterial.size()));
            check(!gi->meshPoly(0) && gi->meshPoly(6) && !gi->meshPoly(18) && gi->markerPoly(18) && !gi->markerPoly(17),
                  "brush faces, model triangles and markers are told apart per poly");
            // Picked as the one prefab: a ray down through the model hits the placement.
            double t = 0;
            const auto hit = sc.pick(Ray{Vec3{1000, 48, 500}, Vec3{0, 0, -1}}, &t);
            check(hit && hit->entity == pInner && hit->item == kNoId, "a click on the prefab's model picks the prefab placement");
        }
        const PrimGeom* go = itemOf(sc, pOuter);
        check(go && go->polys.size() == 36 && go->meshFrom == 6 && go->markerFrom == 30,
              QString("a prefab inside a prefab draws the inner prefab's brush and model, and the outer dynamic_prop (%1 polys)")
                  .arg(go ? int(go->polys.size()) : -1));
        if (go) {
            const AABB nestedModel = polyBounds(*go, 6, 18), prop = polyBounds(*go, 18, 30);
            check(near(nestedModel, box(-1000, -8, 60, -936, 8, 68)), "the nested prefab's model is placed through both prefabs (" + boxStr(nestedModel) + ")");
            check(near(prop, box(-816, -4, -2, -784, 4, 2)), "the outer prefab's dynamic_prop is its mesh (" + boxStr(prop) + ")");
        }
        const PrimGeom* gl = itemOf(sc, pLate);
        check(gl && gl->polys.size() == 6 && gl->meshFrom == PrimGeom::kNoPoly && gl->markerFrom == 0,
              QString("a prefab's model that is not loaded yet is a marker box (%1 polys)").arg(gl ? int(gl->polys.size()) : -1));
        const PrimGeom* gs = itemOf(sc, sDef);
        check(gs && gs->mesh && gs->label == "model:pfm_box" && gs->polys.size() == 12,
              QString("a scriptable is drawn with its definition's model (%1, %2 polys)").arg(gs ? qs(gs->label) : "none").arg(gs ? int(gs->polys.size()) : -1));
        const PrimGeom* gn = itemOf(sc, sNone);
        check(gn && !gn->mesh && gn->label == "scriptable" && gn->polys.size() == 6, "a scriptable without a catalogue model stays a box");

        // Kept geometry against a fresh build: not selected, the prefab selected, and in a probe capture.
        app::CameraGeometry geo;
        const auto keptEqualsFresh = [&](const QString& when) {
            geo.update(sc, doc, false, nullptr, false);
            std::vector<app::GpuVertex> t1, l1, o1, t2, l2, o2;
            std::vector<app::TexturedBatch> x1;
            geo.flatten(t1, l1, o1, x1);
            app::buildCameraGeometry(sc, doc, false, t2, l2, o2);
            check(sameVerts(t1, t2) && sameVerts(l1, l2) && sameVerts(o1, o2),
                  QString("kept camera geometry equals a fresh build with prefab models (%1: %2 / %3 triangle vertices)").arg(when).arg(t1.size()).arg(t2.size()));
        };
        keptEqualsFresh("nothing selected");
        doc.select(SelRef{pInner, kNoId}, false);
        keptEqualsFresh("the prefab selected");
        {
            std::vector<app::GpuVertex> t, l, o;
            app::buildCameraGeometry(sc, doc, false, t, l, o);
            size_t selLines = o.size();
            doc.deselectAll();
            app::buildCameraGeometry(sc, doc, false, t, l, o);
            check(selLines > o.size(), "a selected prefab draws its outlines, model triangles included, on top");
        }
        keptEqualsFresh("deselected again");
        {
            std::vector<app::GpuVertex> tn, ln, on, tc, lc, oc;
            app::buildCameraGeometry(sc, doc, false, tn, ln, on);
            const size_t noCastNormal = tn.size() - sc.shadowCasterVertices;
            sc.captureMode = true;
            app::buildCameraGeometry(sc, doc, false, tc, lc, oc);
            const size_t noCastCapture = tc.size() - sc.shadowCasterVertices;
            // Markers: the two script_origin boxes (inner prefab and nested), the late model's box and the plain
            // scriptable's box: 4 x 36 vertices in the normal view, none in the capture.
            check(noCastNormal == 4 * 36 && noCastCapture == 0 && tn.size() - tc.size() == 4 * 36,
                  QString("prefab inner markers cast no shadow and are left out of captures (%1 / %2 marker vertices, %3 -> %4)")
                      .arg(noCastNormal).arg(noCastCapture).arg(tn.size()).arg(tc.size()));
            geo.update(sc, doc, false, nullptr, true);
            keptEqualsFresh("probe capture");
            sc.captureMode = false;
            geo.update(sc, doc, false, nullptr, true);
        }

        // The model arrives: the scene is invalidated (as on ModelCache::modelReady) and only that prefab is made again.
        const uint64_t stampInner = gi ? gi->stamp : 0, stampLate = gl ? gl->stamp : 0;
        lateArrived = true;
        sc.invalidate();
        sc.sync(doc);
        gi = itemOf(sc, pInner);
        gl = itemOf(sc, pLate);
        check(gl && gl->polys.size() == 12 && gl->meshFrom == 0 && gl->markerFrom == PrimGeom::kNoPoly && gl->stamp != stampLate,
              QString("a prefab's model that finishes loading is drawn as its mesh after the refresh (%1 polys)").arg(gl ? int(gl->polys.size()) : -1));
        check(gi && gi->stamp == stampInner, "prefabs whose models did not change are not made again");
        keptEqualsFresh("after the model arrived");
        (void)modelAsks;
    }

    section("models inside placed prefabs (camera view)");
    {
        EditorContext& ctx = w.context();
        CameraView* cv = w.cameraView();
        const QString dir = QDir(outDir).filePath("prefab_models");
        QDir().mkpath(dir);
        const QStringList roots = w.customModelRoots();
        const QString objPath = roots.isEmpty() ? QString() : roots.front() + "/iw8r_pfm_gui_box.obj";
        bool wrote = false;
        if (!objPath.isEmpty()) {
            QDir().mkpath(QFileInfo(objPath).absolutePath());
            QFile f(objPath);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                f.write("v -16 -2 -4\nv 16 -2 -4\nv 16 2 -4\nv -16 2 -4\nv -16 -2 4\nv 16 -2 4\nv 16 2 4\nv -16 2 4\n"
                        "f 1 2 3 4\nf 5 8 7 6\nf 1 5 6 2\nf 2 6 7 3\nf 3 7 8 4\nf 4 8 5 1\n");
                wrote = true;
            }
        }
        check(wrote, "a custom OBJ model is written to the map's model folder");
        const QString prefabPath = QFileInfo(QDir(dir).filePath("pfm_gui_prefab.map")).absoluteFilePath();  // the model key names it
        {
            QFile f(prefabPath);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                f.write(QByteArray::fromStdString("iwmap 4\n{\n\"classname\" \"worldspawn\"\n" + boxText(box(-8, -8, 0, 8, 8, 16), "caulk") + "}\n"
                                                  "{\n\"classname\" \"misc_model\"\n\"model\" \"iw8r_pfm_gui_box\"\n\"origin\" \"40 0 8\"\n}\n"));
        }
        const std::string before = ctx.doc.text();
        const size_t pos = ctx.doc.undoPosition();
        const Id placed = ctx.doc.addEntity({{"classname", "misc_prefab"}, {"model", QDir::fromNativeSeparators(prefabPath).toStdString()},
                                             {"origin", "0 0 600"}, {"angles", "0 90 0"}},
                                            {}, "prefab with a model");
        ctx.documentEdited();
        ctx.scene.sync(ctx.doc);
        const app::PrimGeom* g = itemOf(ctx.scene, placed);
        const double s = ctx.scene.modelScale > 0 ? ctx.scene.modelScale : 1.0;
        check(g && g->polys.size() == 18 && g->meshFrom == 6, QString("a placed prefab draws its custom OBJ model as the mesh (%1 polys)").arg(g ? int(g->polys.size()) : -1));
        if (g && g->meshFrom == 6) {
            // OBJ x +-16, y +-2 (world z), z +-4 (world -y), at 40 0 8 in the prefab; the prefab at 0 0 600 turned by yaw 90.
            const AABB want = box(-4 * s, 40 - 16 * s, 608 - 2 * s, 4 * s, 40 + 16 * s, 608 + 2 * s);
            const AABB got = polyBounds(*g, 6, 18);
            check(near(got, want), "the OBJ model in the prefab is at the composed transform (" + boxStr(got) + ", want " + boxStr(want) + ")");
        }
        if (QAction* a = w.action("CameraModeTextured")) a->trigger();
        ctx.cam.pos = Vec3{-200, 40, 700};
        ctx.cam.yaw = 0;
        ctx.cam.pitch = -20;
        ctx.cameraMoved();
        cv->renderNow();
        QString diff = cv->geometrySelfCheck();
        check(diff.isEmpty(), "kept camera geometry equals a fresh build with a prefab model in the window" + (diff.isEmpty() ? QString() : " (" + diff + ")"));
        ctx.doc.select(SelRef{placed, kNoId}, false);
        ctx.documentEdited();
        cv->renderNow();
        diff = cv->geometrySelfCheck();
        check(diff.isEmpty() && ctx.doc.isSelected(SelRef{placed, kNoId}),
              "the selected prefab with a model keeps equal kept and fresh geometry" + (diff.isEmpty() ? QString() : " (" + diff + ")"));
        ctx.doc.deselectAll();
        for (int guard = 0; guard < 16 && ctx.doc.undoPosition() > pos; ++guard) ctx.doc.undo();
        ctx.documentEdited();
        check(ctx.doc.text() == before, "the prefab placement is undone");
        if (!objPath.isEmpty()) QFile::remove(objPath);
        ctx.scene.invalidate();
        ctx.cameraMoved();
    }

    section("stock reference editor markers in probe captures");
    {
        CameraView* cv = w.cameraView();
        EditorContext& ctx = w.context();
        if (QAction* a = w.action("CameraModeTextured")) a->trigger();
        const size_t had = cv->referenceInstances();
        check(had == 0, QString("no stock reference instances are loaded before the marker check (%1)").arg(had));
        if (had == 0) {
            const auto cube = [](uint32_t c) {
                std::vector<app::GpuVertex> v;
                for (int i = 0; i < 36; ++i) v.push_back({float(i % 3 == 0), float(i % 3 == 1), float(i % 3 == 2), c});
                return v;
            };
            const auto rowsAt = [](float x, float y, float z) {
                return std::vector<float>{24, 0, 0, x, 0, 24, 0, y, 0, 0, 24, z};
            };
            std::vector<app::InstancedMesh> meshes(2);
            meshes[0].tris = cube(0xff808080u);  // a prop's box: real geometry
            meshes[0].rows = rowsAt(0, 0, 0);
            meshes[1].tris = cube(0xff40c0ffu);  // the amber marker of a prop whose model is missing
            meshes[1].rows = rowsAt(64, 0, 0);
            meshes[1].editorMarker = true;
            cv->setReferenceInstances(std::move(meshes));
            ctx.cam.pos = Vec3{-300, 0, 100};
            ctx.cameraMoved();
            cv->renderNow();
            const size_t screen = cv->frameStats().referenceInstanceVertices;
            const std::vector<uint8_t> cubeFaces = cv->captureCube(Vec3{0, -200, 50}, 16);
            const size_t capture = cv->frameStats().referenceInstanceVertices;
            check(screen == 72 && capture == 36 && !cubeFaces.empty(),
                  QString("editor marker props are drawn on screen and left out of a probe capture (%1 / %2 vertices)").arg(screen).arg(capture));
            cv->setReferenceInstances({});
            cv->renderNow();
            check(cv->referenceInstances() == 0, "the test reference instances are cleared again");
        }
    }
}

}  // namespace iw8r::qt
