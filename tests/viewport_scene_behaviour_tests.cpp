// The camera's scene cache (src/viewport/scene.cpp) for scripted objects (docs/research/131 gaps 3, 6, 10, 12, 13, 15):
// what the viewport draws for trigger_radius, damage effects, door use areas, teleporters, vehicles, invisible brush
// models and exploder pieces, and where the Behaviour Preview puts movers. This test binary does not link
// iw8r_viewport, so scene.cpp (with the two viewport files it calls) is compiled into this translation unit, as
// texture_decode_tests.cpp does with texture_decode.cpp; the profiler hooks of render.cpp are stubbed below.
#include "../src/viewport/scene.cpp"
#include "../src/viewport/game_lightmap.cpp"
#include "../src/viewport/patch_covers.cpp"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "brush_ops.h"
#include "document.h"
#include "gamemode_templates.h"
#include "test_harness.h"

namespace iw8r::app {
ProfScope::ProfScope(const char* n) : name(n), t0(0) {}
ProfScope::~ProfScope() {}
bool perfOff(const char*) { return false; }
}  // namespace iw8r::app

using namespace iw8r;

namespace {

bool near(double a, double b, double eps = 0.05) { return std::fabs(a - b) <= eps; }

bool boxNear(const AABB& b, const Vec3& mn, const Vec3& mx, double eps = 0.05) {
    return b.valid() && near(b.mins.x, mn.x, eps) && near(b.mins.y, mn.y, eps) && near(b.mins.z, mn.z, eps) && near(b.maxs.x, mx.x, eps) &&
           near(b.maxs.y, mx.y, eps) && near(b.maxs.z, mx.z, eps);
}

Primitive boxPrim(const AABB& box, const std::string& material = "iw8r/test_grey") {
    auto p = makeBrushPrimitive(makeBox(box, " " + material + " 128 128 0 0 0 0"), "\n");
    CHECK(p.has_value());
    return p ? *p : Primitive{};
}

// A document with a worldspawn only; entities are added with addEntity (which selects them: the tests deselect).
void emptyDoc(Document& doc) {
    std::string err;
    CHECK(doc.loadFromText("iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n", "scene_behaviour_test.map", &err));
}

using KVs = std::vector<std::pair<std::string, std::string>>;

Id add(Document& doc, const KVs& keys, std::vector<Primitive> prims = {}) {
    const Id id = doc.addEntity(keys, std::move(prims));
    doc.deselectAll();
    return id;
}

// The bounds of every item of an entity (its brushes, or its point item).
AABB entityBounds(const app::SceneCache& sc, Id entity) {
    AABB b;
    for (const app::PrimGeom* g : sc.items())
        if (g->ref.entity == entity) b.add(g->bounds);
    return b;
}

const app::PrimGeom* pointItem(const app::SceneCache& sc, Id entity) {
    for (const app::PrimGeom* g : sc.items())
        if (g->ref.entity == entity && g->ref.item == kNoId) return g;
    return nullptr;
}

AABB sceneAt(const Document& doc, Id entity, double t) {
    app::SceneCache sc;
    sc.behaviourTime = t;
    sc.sync(doc);
    return entityBounds(sc, entity);
}

// A small closed mesh about the model origin (file coordinates; the scene turns them to map axes: x, -z, y).
std::shared_ptr<const ImportedModel> tetraModel() {
    auto m = std::make_shared<ImportedModel>();
    m->name = "tetra";
    ImportedMesh mesh;
    mesh.pos = {{-8, -8, -8}, {8, -8, -8}, {0, 8, 8}, {0, -8, 8}};
    mesh.idx = {0, 1, 2, 0, 1, 3, 0, 2, 3, 1, 2, 3};
    m->meshes.push_back(mesh);
    return m;
}

}  // namespace

namespace {

struct Camera {
    std::vector<app::GpuVertex> tris, lines, overlay;
    size_t casters = 0;
};

Camera cameraOf(const app::SceneCache& sc, const Document& doc) {
    Camera c;
    app::buildCameraGeometry(sc, doc, false, c.tris, c.lines, c.overlay);
    c.casters = sc.shadowCasterVertices;
    return c;
}

size_t countColour(const std::vector<app::GpuVertex>& v, uint32_t rgba) {
    size_t n = 0;
    for (const app::GpuVertex& g : v) n += g.rgba == rgba;
    return n;
}

Id firstBrushItem(const Document& doc, Id entity) {
    if (const Entity* e = doc.findEntity(entity))
        for (const auto& it : e->items)
            if (it.prim()) return it.id;
    return kNoId;
}

}  // namespace

// Gap 3: trigger_radius is drawn as the cylinder the build tests (radius / height from the origin up), as a trigger
// volume: a wire outline in the trigger colour, filled only when selected, never a shadow caster, picked by its bounds.
TEST(scene_trigger_radius_cylinder) {
    Document doc;
    emptyDoc(doc);
    const Id flag = add(doc, {{"classname", "trigger_radius"}, {"origin", "100 0 32"}, {"radius", "120"}, {"height", "128"},
                              {"targetname", "flag_primary"}});
    const Id deflt = add(doc, {{"classname", "trigger_radius"}, {"origin", "1000 0 0"}});
    const Id flat = add(doc, {{"classname", "trigger_radius"}, {"origin", "2000 0 0"}, {"radius", "0"}});
    app::SceneCache sc;
    sc.sync(doc);
    const app::PrimGeom* g = pointItem(sc, flag);
    CHECK(g && g->volume && g->prefab && !g->collides && g->polys.size() == 2 + 16);
    CHECK(g && boxNear(g->bounds, {-20, -120, 32}, {220, 120, 160}));
    CHECK(g && g->color == app::colorForClass("trigger_radius"));
    // Every ring corner lies on the circle.
    bool onCircle = g && !g->polys.empty();
    if (g)
        for (const Vec3& v : g->polys[0]) onCircle = onCircle && near(std::hypot(v.x - 100, v.y), 120, 1e-6);
    CHECK(onCircle);
    // The build's defaults: radius 64, height 72. A radius of 0 is no volume in the build: the plain marker cube.
    const app::PrimGeom* gd = pointItem(sc, deflt);
    CHECK(gd && gd->volume && boxNear(gd->bounds, {936, -64, 0}, {1064, 64, 72}));
    const app::PrimGeom* gf = pointItem(sc, flat);
    CHECK(gf && !gf->volume && boxNear(gf->bounds, {1992, -8, -8}, {2008, 8, 8}));
    // Unselected: outlines only (each ring 16 edges + a diameter, each side 4 edges + a diagonal) in the trigger colour.
    const uint32_t tc = g ? (g->color | 0xff000000u) : 0;
    Camera c = cameraOf(sc, doc);
    CHECK(countColour(c.lines, tc) == 2 * (2 * (16 * 2 + 2) + 16 * (4 * 2 + 2)));
    CHECK(c.casters == 0);  // markers and volumes only: nothing casts a sun shadow
    // Picked inside its bounds, not beside them.
    double t = 0;
    const auto hit = sc.pick(Ray{{100, 0, 1000}, {0, 0, -1}}, &t);
    CHECK(hit && hit->entity == flag && near(t, 1000 - 160));
    CHECK(!sc.pick(Ray{{100, 300, 1000}, {0, 0, -1}}));
    // Selected: filled with the selection tint (not a caster) and outlined on top.
    doc.select({flag, kNoId}, false);
    c = cameraOf(sc, doc);
    CHECK(c.casters == 0 && c.tris.size() > 0);
    CHECK(countColour(c.overlay, 0xff40e0ffu) >= 2 * (16 * 2 + 16 * 4));
}

// Gap 3: the damage cylinder of an fx_origin with iw8r_damage, always drawn (depth-tested) in red.
TEST(scene_fx_damage_cylinder) {
    Document doc;
    emptyDoc(doc);
    const Id fire = add(doc, {{"classname", "fx_origin"}, {"origin", "0 0 100"}, {"fx", "fire/fire_small"}, {"iw8r_damage", "10 64"}});
    add(doc, {{"classname", "fx_origin"}, {"origin", "500 0 0"}, {"fx", "fire/fire_small"}});
    add(doc, {{"classname", "fx_origin"}, {"origin", "900 0 0"}, {"iw8r_damage", "0 64"}});
    const Id dflt = add(doc, {{"classname", "fx_origin"}, {"origin", "-500 0 0"}, {"iw8r_damage", "5"}});
    app::SceneCache sc;
    sc.sync(doc);
    AABB fireBox, dfltBox;
    size_t fireLines = 0, other = 0;
    for (const auto& h : sc.helperLines()) {
        CHECK(h.always);
        if (h.owner == fire) {
            ++fireLines;
            fireBox.add(h.a), fireBox.add(h.b);
        } else if (h.owner == dflt) {
            dfltBox.add(h.a), dfltBox.add(h.b);
        } else {
            ++other;
        }
    }
    CHECK(fireLines == 16 * 2 + 4 && other == 0);
    // Radius 64 around the origin, from 16 below it to the radius above it (map_script.cpp readTriggerVolumes).
    CHECK(boxNear(fireBox, {-64, -64, 84}, {64, 64, 164}, 1e-6));
    CHECK(boxNear(dfltBox, {-548, -48, -16}, {-452, 48, 48}, 1e-6));  // the default radius 48
    // The effect's marker keeps its own small bounds (the cylinder does not make it pickable from afar).
    const app::PrimGeom* g = pointItem(sc, fire);
    CHECK(g && g->bounds.maxs.x < 10);
    const Camera c = cameraOf(sc, doc);
    CHECK(countColour(c.lines, app::packRGBA(0.9, 0.15, 0.15)) == 2 * (2 * (16 * 2 + 4)));
    // Probe capture: none.
    sc.captureMode = true;
    std::vector<app::GpuVertex> l, o;
    app::appendHelperLines(sc, doc, l, o);
    CHECK(l.empty() && o.empty());
}

// Gaps 3 and 15: the use area the build gives a door that no named trigger opens, and the teleporter's line to its
// destination, both on top while one of their entities is selected.
TEST(scene_door_use_area_and_teleport_line) {
    Document doc;
    emptyDoc(doc);
    const Id door = add(doc, {{"classname", "script_brushmodel"}, {"targetname", "d1"}, {"iw8r_door", "slide 0 0 100"}},
                        {boxPrim({{0, 0, 0}, {64, 8, 96}})});
    // Opened by a named trigger: no use area.
    const Id door2 = add(doc, {{"classname", "script_brushmodel"}, {"targetname", "d2"}, {"iw8r_door", "rotate 90"}},
                         {boxPrim({{0, 500, 0}, {64, 508, 96}})});
    add(doc, {{"classname", "trigger_use"}, {"targetname", "t2"}, {"target", "d2"}}, {boxPrim({{0, 450, 0}, {64, 470, 96}}, "trigger")});
    // A trigger without a targetname is not used by the build: the door keeps its own area.
    const Id door3 = add(doc, {{"classname", "script_brushmodel"}, {"targetname", "d3"}, {"iw8r_door", "rotate 90"}},
                         {boxPrim({{0, 1000, 0}, {64, 1008, 96}})});
    add(doc, {{"classname", "trigger_use"}, {"target", "d3"}}, {boxPrim({{0, 950, 0}, {64, 970, 96}}, "trigger")});
    const Id tp = add(doc, {{"classname", "trigger_multiple"}, {"targetname", "tp"}, {"iw8r_teleport", "dest"}},
                      {boxPrim({{-200, -200, 0}, {-136, -136, 64}}, "trigger")});
    const Id dest = add(doc, {{"classname", "script_origin"}, {"targetname", "dest"}, {"origin", "800 0 0"}});
    add(doc, {{"classname", "trigger_multiple"}, {"targetname", "tp2"}, {"iw8r_teleport", "nowhere"}},
        {boxPrim({{-400, -200, 0}, {-336, -136, 64}}, "trigger")});
    app::SceneCache sc;
    sc.sync(doc);
    std::map<Id, size_t> lines;
    AABB useBox;
    const app::SceneCache::HelperLine* tpLine = nullptr;
    for (const auto& h : sc.helperLines()) {
        ++lines[h.owner];
        if (h.owner == door) useBox.add(h.a), useBox.add(h.b);
        if (h.owner == tp) tpLine = &h;
        CHECK(!h.always);
    }
    CHECK(lines[door] == 12 && lines[door2] == 0 && lines[door3] == 12 && lines[tp] == 1 && sc.helperLines().size() == 25);
    CHECK(boxNear(useBox, {-40, -40, 0}, {104, 48, 96}, 1e-6));
    CHECK(tpLine && tpLine->other == dest && near(tpLine->a.x, -168) && near(tpLine->a.y, -168) && near(tpLine->b.x, 800));
    const uint32_t useColour = app::colorForClass("trigger_use"), tpColour = app::packRGBA(0.75, 0.4, 1.0);
    // Nothing selected: none drawn.
    Camera c = cameraOf(sc, doc);
    CHECK(countColour(c.overlay, useColour) == 0 && countColour(c.overlay, tpColour) == 0 && countColour(c.lines, tpColour) == 0);
    // The door selected (a brush entity is selected by its brushes): its area only.
    doc.select({door, firstBrushItem(doc, door)}, false);
    c = cameraOf(sc, doc);
    CHECK(countColour(c.overlay, useColour) == 24 && countColour(c.overlay, tpColour) == 0);
    // The destination selected: the teleporter's line.
    doc.select({dest, kNoId}, false);
    c = cameraOf(sc, doc);
    CHECK(countColour(c.overlay, useColour) == 0 && countColour(c.overlay, tpColour) == 2);
    doc.select({tp, firstBrushItem(doc, tp)}, false);
    CHECK(countColour(cameraOf(sc, doc).overlay, tpColour) == 2);
}

// Gap 6: a brush model spins about its pitch / roll axis as well as its yaw, about the centre of its brushes (the build
// rotates brush-model spinners about that centre), whatever the case of the axis word.
TEST(scene_behaviour_brush_spin_axes_about_centre) {
    const AABB box{{100, 200, 0}, {228, 232, 64}};  // centre (164, 216, 32); 128 x 32 x 64
    struct Case {
        const char* key;
        Vec3 mn, mx;
    };
    const Case cases[] = {
        // A quarter turn (1 s of a 4 s turn). Pitch turns about Y (x and z extents swap), roll about X (y and z swap).
        {"4 PITCH", {132, 200, -32}, {196, 232, 96}},
        {"4 Roll", {100, 184, 16}, {228, 248, 48}},
        {"4 YAW", {148, 152, 0}, {180, 280, 64}},
        {"4", {148, 152, 0}, {180, 280, 64}},
    };
    for (const Case& c : cases) {
        Document doc;
        emptyDoc(doc);
        const Id fan = add(doc, {{"classname", "script_brushmodel"}, {"targetname", "fan"}, {"iw8r_spin", c.key}}, {boxPrim(box)});
        const AABB rest = sceneAt(doc, fan, -1);
        CHECK(boxNear(rest, box.mins, box.maxs));
        const AABB at = sceneAt(doc, fan, 1.0);
        CHECK(boxNear(at, c.mn, c.mx));
        // Turning about the centre: the centre stays where it is (the old preview orbited the map origin).
        CHECK(near(at.center().x, 164) && near(at.center().y, 216) && near(at.center().z, 32));
    }
    // An axis word the build does not take (it warns and skips the spinner): not moved.
    Document doc;
    emptyDoc(doc);
    const Id odd = add(doc, {{"classname", "script_brushmodel"}, {"targetname", "odd"}, {"iw8r_spin", "4 sideways"}}, {boxPrim(box)});
    CHECK(boxNear(sceneAt(doc, odd, 1.0), box.mins, box.maxs));
}

// Gap 15: a hinged door without a time swings in 0.6 s, as the build writes it; a sliding door keeps 1 s.
TEST(scene_behaviour_hinged_door_default_time) {
    Document doc;
    emptyDoc(doc);
    // Along X, hinge at (0, 4, 0) (doorHingeOf: the near end, middle of the thickness); opening turns +90 about it.
    const Id door = add(doc, {{"classname", "script_brushmodel"}, {"targetname", "door"}, {"iw8r_door", "rotate 90"}},
                        {boxPrim({{0, 0, 0}, {64, 8, 96}})});
    CHECK(boxNear(sceneAt(doc, door, 0.5), {0, 0, 0}, {64, 8, 96}));  // the first second it rests
    // 1 s rest + 0.6 s move: open at 1.6 s (with a 1 s move it would be about two thirds of the way).
    CHECK(boxNear(sceneAt(doc, door, 1.6), {-4, 4, 0}, {4, 68, 96}));
    const Id hinge = add(doc, {{"classname", "script_brushmodel"}, {"targetname", "door2"}, {"iw8r_door", "hinge 90"}},
                         {boxPrim({{0, 500, 0}, {64, 508, 96}})});
    CHECK(boxNear(sceneAt(doc, hinge, 1.6), {-4, 504, 0}, {4, 568, 96}));
    // An explicit time is kept: half way (45 degrees) at 1 + 1 s of a 2 s move.
    const Id slow = add(doc, {{"classname", "script_brushmodel"}, {"targetname", "door3"}, {"iw8r_door", "rotate 90 2"}},
                        {boxPrim({{0, 1000, 0}, {64, 1008, 96}})});
    const AABB half = sceneAt(doc, slow, 2.0);
    CHECK(half.valid() && near(half.maxs.y, 1004 + 64 * std::sin(3.14159265358979 / 4) + 4 * std::cos(3.14159265358979 / 4), 0.1));
    // Sliding: 1 s by default, so at 1.6 s it is still on the way.
    const Id slide = add(doc, {{"classname", "script_brushmodel"}, {"targetname", "door4"}, {"iw8r_door", "slide 0 0 100"}},
                         {boxPrim({{0, 2000, 0}, {64, 2008, 96}})});
    const AABB s = sceneAt(doc, slide, 1.6);
    CHECK(s.valid() && s.mins.z > 1 && s.mins.z < 99);
    CHECK(boxNear(sceneAt(doc, slide, 2.0), {0, 2000, 100}, {64, 2008, 196}));
}

// Gap 6: iw8r_lift moves misc_model / script_model placements too (the build makes them movers); the spin axis is read
// in any case; other model classes are not moved (the build skips them).
TEST(scene_behaviour_model_lift_and_spin) {
    const std::shared_ptr<const ImportedModel> tetra = tetraModel();
    const auto run = [&](const KVs& keys, double t) {
        Document doc;
        emptyDoc(doc);
        const Id id = add(doc, keys);
        app::SceneCache sc;
        sc.modelSource = [&](const Entity& e) -> std::shared_ptr<const ImportedModel> {
            const std::string* m = e.value("model");
            return m && *m == "tetra" ? tetra : nullptr;
        };
        sc.behaviourTime = t;
        sc.sync(doc);
        return entityBounds(sc, id);
    };
    const KVs lift = {{"classname", "script_model"}, {"model", "tetra"}, {"origin", "0 0 100"}, {"iw8r_lift", "50 2 2"}};
    const AABB rest = run(lift, -1);
    CHECK(rest.valid() && near(rest.mins.z, 92) && near(rest.maxs.z, 108));
    CHECK(near(run(lift, 1.0).mins.z, 92));       // waits 2 s at the bottom
    CHECK(near(run(lift, 4.0).mins.z, 142));      // 2 s up: at the top
    CHECK(near(run(lift, 5.5).mins.z, 142));      // waits 2 s there
    CHECK(near(run(lift, 8.0).mins.z, 92));       // 2 s down
    KVs misc = lift;
    misc[0].second = "misc_model";
    CHECK(near(run(misc, 4.0).mins.z, 142));
    KVs prop = lift;
    prop[0].second = "dynamic_prop";
    CHECK(near(run(prop, 4.0).mins.z, 92));
    // Bob and lift add up.
    KVs both = lift;
    both.push_back({"iw8r_bob", "10 2"});
    CHECK(near(run(both, 4.0).mins.z, 142));  // the bob is down at whole periods
    CHECK(near(run(both, 5.0).mins.z, 152));  // and up half way through one, while the lift holds at the top
    // Spin with an upper-case axis word turns the pitch, as the build (which lower-cases it) does.
    Entity e;
    for (const auto& [k, v] : KVs{{"classname", "script_model"}, {"origin", "0 0 0"}, {"iw8r_spin", "4 PITCH"}}) {
        EntityItem it;
        it.v = makeKeyValue(k, v, "\n");
        e.items.push_back(std::move(it));
    }
    const Entity posed = app::behaviourPose(e, 1.0);
    CHECK(posed.value("angles") && *posed.value("angles") == "90 0 0");
}

// Gap 12: a script_brushmodel with iw8r_brush_visual 0 is not drawn by the game: a volume outline in its class colour,
// no textured faces, no shadow; still solid. Setting the key rebuilds the brushes (a key change moves no brush rev).
TEST(scene_brush_visual_off_is_an_outline) {
    Document doc;
    emptyDoc(doc);
    const Id off = add(doc, {{"classname", "script_brushmodel"}, {"targetname", "ghost"}, {"iw8r_brush_visual", "0"}},
                       {boxPrim({{0, 0, 0}, {64, 64, 64}})});
    app::SceneCache sc;
    sc.sync(doc);
    const app::PrimGeom* g = nullptr;
    for (const app::PrimGeom* it : sc.items())
        if (it->ref.entity == off) g = it;
    CHECK(g && g->volume && g->collides && g->kind == app::PrimGeom::Brush);
    const std::function<int(const std::string&)> tex = [](const std::string&) { return 1; };
    for (const bool gameLook : {false, true}) {
        sc.gameLook = gameLook;
        std::vector<app::GpuVertex> tris, lines, overlay;
        std::vector<app::TexturedBatch> textured;
        app::buildCameraGeometry(sc, doc, false, tris, lines, overlay, tex, textured);
        size_t texturedTris = 0;
        for (const auto& b : textured) texturedTris += b.tris.size();
        CHECK(texturedTris == 0 && sc.shadowCasterVertices == 0);
        CHECK(g && countColour(lines, g->color | 0xff000000u) == 6 * (4 * 2 + 2));
    }
    sc.gameLook = false;
    // Collision mode still shows it (the player collides with the brush model).
    sc.cameraMode = app::CameraMode::Collision;
    CHECK(cameraOf(sc, doc).tris.size() == 6 * 2 * 3);
    sc.cameraMode = app::CameraMode::Textured;
    // With the generated visual (the key set to 1): textured faces again.
    CHECK(doc.setKeyValue(off, "iw8r_brush_visual", "1"));
    sc.sync(doc);
    g = nullptr;
    for (const app::PrimGeom* it : sc.items())
        if (it->ref.entity == off) g = it;
    CHECK(g && !g->volume);
    std::vector<app::GpuVertex> tris, lines, overlay;
    std::vector<app::TexturedBatch> textured;
    app::buildCameraGeometry(sc, doc, false, tris, lines, overlay, tex, textured);
    size_t texturedTris = 0;
    for (const auto& b : textured) texturedTris += b.tris.size();
    CHECK(texturedTris == 6 * 2 * 3);
}

// Gap 13: the S&D template's destroyed crate (targetname exploder) is a small marker, not a model over the intact
// crate, unless showExploderPieces is set.
TEST(scene_exploder_piece_hidden_by_default) {
    Document doc;
    emptyDoc(doc);
    std::string err;
    const GameModeTemplate* sd = findGameModeTemplate("sd_sites");
    CHECK(sd != nullptr);
    if (!sd) return;
    CHECK(!insertGameModeTemplate(doc, *sd, Vec3{0, 0, 0}, &err).empty());
    doc.deselectAll();
    const std::shared_ptr<const ImportedModel> tetra = tetraModel();
    app::SceneCache sc;
    size_t asks = 0;
    sc.modelSource = [&](const Entity& e) -> std::shared_ptr<const ImportedModel> {
        ++asks;
        return e.value("model") ? tetra : nullptr;
    };
    sc.sync(doc);
    size_t intact = 0, destroyed = 0, destroyedMarkers = 0;
    for (const auto& e : doc.map().entities) {
        const std::string* m = e.value("model");
        if (!m) continue;
        const app::PrimGeom* g = pointItem(sc, e.id);
        CHECK(g != nullptr);
        if (!g) continue;
        if (*m == "sd_equipment_bombsite_crate") intact += g->mesh;
        if (*m == "sd_equipment_bombsite_crate_d") {
            ++destroyed;
            destroyedMarkers += !g->mesh && g->polys.size() == 6 && near(g->bounds.maxs.x - g->bounds.mins.x, 16);
        }
    }
    CHECK(intact == 2 && destroyed == 2 && destroyedMarkers == 2);
    // Shown on request: both crates as models.
    sc.showExploderPieces = true;
    sc.invalidate();
    sc.sync(doc);
    size_t meshes = 0;
    for (const auto& e : doc.map().entities)
        if (const std::string* m = e.value("model"); m && m->rfind("sd_equipment_bombsite_crate", 0) == 0)
            if (const app::PrimGeom* g = pointItem(sc, e.id)) meshes += g->mesh;
    CHECK(meshes == 4);
}

// Gap 10: a vehicle spawn struct is drawn as its vehicle's model when the scene resolves it, else as a box of the
// definition's size; it is not a collision object (the struct places no solid).
TEST(scene_vehicle_struct_model_or_box) {
    Document doc;
    emptyDoc(doc);
    const Id atv = add(doc, {{"classname", "script_struct_mp_atv"}, {"origin", "100 0 0"}, {"vehicletype", "atango_physics_mp"},
                             {"model", "tetra"}, {"targetname", "atv_spawn"}});
    const Id jeep = add(doc, {{"classname", "script_struct_mp_jeep"}, {"origin", "1000 0 0"}, {"vehicletype", "decho_physics_mp"},
                              {"model", "veh8_not_cached"}, {"targetname", "jeep_spawn"}});
    const Id other = add(doc, {{"classname", "script_struct_mp_rugby_jugg_crate"}, {"origin", "2000 0 0"}, {"model", "tetra"}});
    const std::shared_ptr<const ImportedModel> tetra = tetraModel();
    app::SceneCache sc;
    sc.modelSource = [&](const Entity& e) -> std::shared_ptr<const ImportedModel> {
        const std::string* m = e.value("model");
        return m && *m == "tetra" ? tetra : nullptr;
    };
    sc.sync(doc);
    const app::PrimGeom* ga = pointItem(sc, atv);
    CHECK(ga && ga->mesh && !ga->collides && ga->label == "model:tetra" && boxNear(ga->bounds, {92, -8, -8}, {108, 8, 8}));
    const app::PrimGeom* gj = pointItem(sc, jeep);
    CHECK(gj && !gj->mesh && !gj->collides && boxNear(gj->bounds, {952, -48, 0}, {1048, 48, 64}));
    // A struct without a vehicletype keeps the small marker.
    const app::PrimGeom* go = pointItem(sc, other);
    CHECK(go && !go->mesh && boxNear(go->bounds, {1992, -8, -8}, {2008, 8, 8}));
    // Without a model source at all: the box too.
    app::SceneCache bare;
    bare.sync(doc);
    const app::PrimGeom* gb = pointItem(bare, atv);
    CHECK(gb && boxNear(gb->bounds, {52, -48, 0}, {148, 48, 64}));
}

// The play preview's poses (core PlayBehaviours): a door is drawn where the play preview has moved it.
TEST(scene_play_pose_moves_brushes) {
    const AABB box{{0, -64, 0}, {8, 64, 112}};
    Document doc;
    emptyDoc(doc);
    const Id gate = add(doc, {{"classname", "script_brushmodel"}, {"targetname", "gate"}, {"iw8r_door", "slide 0 0 128"}}, {boxPrim(box)});
    app::SceneCache sc;
    sc.sync(doc);
    CHECK(boxNear(entityBounds(sc, gate), box.mins, box.maxs));
    app::SceneCache::PlayPose up;
    up.move = Vec3{0, 0, 100};
    sc.playPoses[gate] = up;
    sc.invalidate();
    sc.sync(doc);
    CHECK(boxNear(entityBounds(sc, gate), Vec3{0, -64, 100}, Vec3{8, 64, 212}));
    // A quarter yaw turn about the hinge at (0, -64): the door now runs along -x.
    app::SceneCache::PlayPose turn;
    turn.R[0][0] = 0, turn.R[0][1] = -1, turn.R[1][0] = 1, turn.R[1][1] = 0;
    turn.pivot = Vec3{0, -64, 0};
    sc.playPoses[gate] = turn;
    sc.invalidate();
    sc.sync(doc);
    CHECK(boxNear(entityBounds(sc, gate), Vec3{-128, -64, 0}, Vec3{0, -56, 112}));
    // Playing stops: the door is drawn at rest again.
    sc.playPoses.clear();
    sc.invalidate();
    sc.sync(doc);
    CHECK(boxNear(entityBounds(sc, gate), box.mins, box.maxs));
}
