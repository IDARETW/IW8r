// Prefabs: misc_prefab parsing, path resolution, instancing (brush, patch, point entity), explode with undo,
// and save-selection-as-prefab round trip.
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

#include "document.h"
#include "prefab.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

// Prefab: one 64 x 64 x 32 box at the origin (the document_ops room brush) and one mp_tdm_spawn.
const char* kPrefabText =
    "iwmap 4\r\n"
    "\"000_Global\" flags active\r\n"
    "// entity 0\r\n"
    "{\r\n"
    "\"classname\" \"worldspawn\"\r\n"
    "// brush 0\r\n"
    "{\r\n"
    " ( 64 64 0 ) ( 0 64 0 ) ( 0 0 0 ) caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\r\n"
    " ( 0 0 32 ) ( 0 64 32 ) ( 64 64 32 ) floor 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\r\n"
    " ( 0 0 32 ) ( 64 0 32 ) ( 64 0 0 ) caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\r\n"
    " ( 64 64 32 ) ( 0 64 32 ) ( 0 64 0 ) caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\r\n"
    " ( 64 0 32 ) ( 64 64 32 ) ( 64 64 0 ) caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\r\n"
    " ( 0 64 32 ) ( 0 0 32 ) ( 0 0 0 ) caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\r\n"
    "}\r\n"
    "}\r\n"
    "// entity 1\r\n"
    "{\r\n"
    "\"classname\" \"mp_tdm_spawn\"\r\n"
    "\"origin\" \"32 0 0\"\r\n"
    "\"angles\" \"0 45 0\"\r\n"
    "}\r\n";

// Host map: worldspawn, then a misc_prefab at (100 0 0) yawed 90 degrees.
const char* kHostText =
    "iwmap 4\r\n"
    "\"000_Global\" flags active\r\n"
    "// entity 0\r\n"
    "{\r\n"
    "\"classname\" \"worldspawn\"\r\n"
    "}\r\n"
    "// entity 1\r\n"
    "{\r\n"
    "\"classname\" \"misc_prefab\"\r\n"
    "\"model\" \"prefabs/box.map\"\r\n"
    "\"origin\" \"100 0 0\"\r\n"
    "\"angles\" \"0 90 0\"\r\n"
    "}\r\n";

std::string samplePath() {
    fs::path p = fs::current_path();
    for (int up = 0; up < 6; ++up, p = p.parent_path()) {
        const fs::path cand = p / "tests" / "data" / "iw8_sample_room.map";
        if (fs::exists(cand)) return cand.string();
    }
    return {};
}

MapFile parseOrFail(const std::string& text) {
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    if (!pr.map) std::printf("  parse error at line %zu: %s\n", pr.error->line, pr.error->message.c_str());
    CHECK(pr.map.has_value());
    return pr.map ? *pr.map : MapFile{};
}

bool near(const Vec3& a, const Vec3& b, double eps = 1e-6) { return length(a - b) < eps; }
bool boundsNear(const AABB& a, const AABB& b) { return near(a.mins, b.mins) && near(a.maxs, b.maxs); }

const std::string* kv(const std::vector<std::pair<std::string, std::string>>& kvs, const std::string& key) {
    for (const auto& [k, v] : kvs)
        if (k == key) return &v;
    return nullptr;
}

size_t primCount(const Entity& e) {
    size_t n = 0;
    for (const auto& it : e.items) n += it.isPrimitive();
    return n;
}

}  // namespace

TEST(prefab_instance_parse) {
    const MapFile m = parseOrFail(
        "iwmap 4\n"
        "// entity 0\n{\n\"classname\" \"worldspawn\"\n}\n"
        "// entity 1\n{\n\"classname\" \"misc_prefab\"\n\"model\" \"prefabs/crate.map\"\n"
        "\"origin\" \"100 -8 4\"\n\"angles\" \"0 90 0\"\n\"modelscale\" \"2\"\n}\n"
        "// entity 2\n{\n\"classname\" \"misc_prefab\"\n\"model\" \"prefabs/bare.map\"\n}\n"
        "// entity 3\n{\n\"classname\" \"misc_prefab\"\n\"origin\" \"0 0 0\"\n}\n"
        "// entity 4\n{\n\"classname\" \"script_origin\"\n\"model\" \"prefabs/crate.map\"\n}\n");
    const auto a = prefabInstance(m.entities[1]);
    CHECK(a.has_value());
    if (a) {
        CHECK(a->model == "prefabs/crate.map");
        CHECK(near(a->origin, Vec3{100, -8, 4}));
        CHECK(near(a->angles, Vec3{0, 90, 0}));
        CHECK(a->scale == 2.0);
    }
    const auto b = prefabInstance(m.entities[2]);  // defaults: origin 0, angles 0, scale 1
    CHECK(b.has_value() && b->scale == 1.0 && near(b->origin, Vec3{}) && near(b->angles, Vec3{}));
    CHECK(!prefabInstance(m.entities[3]));  // no model
    CHECK(!prefabInstance(m.entities[4]));  // not misc_prefab
    CHECK(!prefabInstance(m.entities[0]));  // worldspawn
}

TEST(prefab_instance_overrides_and_material_swaps) {
    // 8.8: keys on the misc_prefab instance change what the build gets from this one instance.
    const MapFile host = parseOrFail(
        "iwmap 4\r\n"
        "// entity 0\r\n"
        "{\r\n"
        "\"classname\" \"worldspawn\"\r\n"
        "}\r\n"
        "// entity 1\r\n"
        "{\r\n"
        "\"classname\" \"misc_prefab\"\r\n"
        "\"model\" \"prefabs/box.map\"\r\n"
        "\"origin\" \"0 0 0\"\r\n"
        "\"iw8r_ovr:class=mp_tdm_spawn:script_noteworthy\" \"blue_side\"\r\n"
        "\"iw8r_ovr:*:angles\" \"0 0 0\"\r\n"
        "\"iw8r_ovr:nosuch:targetname\" \"x\"\r\n"
        "\"iw8r_mat:floor\" \"tm/wood_planks\"\r\n"
        "}\r\n");
    const auto inst = prefabInstance(host.entities[1]);
    CHECK(inst.has_value());
    if (!inst) return;
    CHECK(inst->overrides.size() == 2);  // angles is placement: ignored
    CHECK(inst->materialSwaps.size() == 1);
    const PrefabContents c = instantiatePrefab(parseOrFail(kPrefabText), *inst, "\r\n");
    CHECK(c.worldPrims.size() == 1 && c.entities.size() == 1);
    if (c.worldPrims.size() == 1) {
        CHECK(c.worldPrims[0].faces[1].material() == "tm/wood_planks");
        CHECK(c.worldPrims[0].faces[0].material() == "caulk");
    }
    if (c.entities.size() == 1) {
        const auto& kvs = c.entities[0].first;
        CHECK(kv(kvs, "script_noteworthy") && *kv(kvs, "script_noteworthy") == "blue_side");
        CHECK(kv(kvs, "angles") && *kv(kvs, "angles") == "0 45 0");
        CHECK(!kv(kvs, "targetname"));
    }
}

TEST(prefab_angles_matrix_convention) {
    // Yaw 90 turns +X into +Y (Quake/CoD AngleVectors convention), exactly.
    const Vec3 fwd = anglesToMatrix({0, 90, 0}) * Vec3{1, 0, 0};
    CHECK(fwd == Vec3(0, 1, 0));
    // Pitch 90 nose-down: +X goes to -Z. Roll 90 turns +Y into +Z.
    CHECK(near(anglesToMatrix({90, 0, 0}) * Vec3{1, 0, 0}, Vec3{0, 0, -1}));
    CHECK(near(anglesToMatrix({0, 0, 90}) * Vec3{0, 1, 0}, Vec3{0, 0, 1}));
    // Identity.
    CHECK(near(anglesToMatrix({0, 0, 0}) * Vec3{3, -2, 5}, Vec3{3, -2, 5}));
}

TEST(prefab_instantiate_brush_and_spawn) {
    const MapFile prefab = parseOrFail(kPrefabText);
    PrefabInstance inst;
    inst.model = "prefabs/box.map";
    inst.origin = {100, 0, 0};
    inst.angles = {0, 90, 0};
    const PrefabContents c = instantiatePrefab(prefab, inst, "\r\n");
    CHECK(c.worldPrims.size() == 1);
    CHECK(c.entities.size() == 1);
    if (c.worldPrims.size() == 1) {
        // Box x 0..64, y 0..64, z 0..32 yawed 90 about the origin, then moved to x = 100: x 36..100.
        const AABB b = c.worldPrims[0].bounds();
        CHECK(near(b.mins, Vec3{36, 0, 0}));
        CHECK(near(b.maxs, Vec3{100, 64, 32}));
        // Face suffixes (material and texture fields) are untouched.
        const Primitive* orig = nullptr;
        for (const auto& it : prefab.entities[0].items)
            if (!orig && it.prim()) orig = it.prim();
        CHECK(orig != nullptr);
        if (!orig) return;
        CHECK(c.worldPrims[0].faces.size() == orig->faces.size());
        for (size_t i = 0; i < orig->faces.size() && i < c.worldPrims[0].faces.size(); ++i)
            CHECK(c.worldPrims[0].faces[i].suffix == orig->faces[i].suffix);
        CHECK(c.worldPrims[0].faces[1].material() == "floor");
    }
    if (c.entities.size() == 1) {
        const auto& [kvs, prims] = c.entities[0];
        CHECK(prims.empty());
        CHECK(kv(kvs, "classname") && *kv(kvs, "classname") == "mp_tdm_spawn");
        // Spawn at (32 0 0) rotated to (0 32 0), then moved to (100 32 0). Yaw 45 + 90 = 135.
        CHECK(kv(kvs, "origin") && *kv(kvs, "origin") == "100 32 0");
        CHECK(kv(kvs, "angles") && *kv(kvs, "angles") == "0 135 0");
    }
}

TEST(prefab_explode_then_undo_restores_text) {
    Document d;
    CHECK(d.loadFromText(kHostText, "host.map", nullptr));
    const std::string original = d.text();
    const Id prefabId = d.map().entities[1].id;
    const MapFile prefab = parseOrFail(kPrefabText);
    std::string err;
    CHECK(explodePrefab(d, prefabId, prefab, &err));
    CHECK(err.empty());
    CHECK(d.findEntity(prefabId) == nullptr);
    CHECK(d.map().entities.size() == 2);
    CHECK(primCount(d.map().entities[0]) == 1);
    CHECK(d.map().entities[1].classname() == "mp_tdm_spawn");
    CHECK(d.map().entities[1].origin() && near(*d.map().entities[1].origin(), Vec3{100, 32, 0}));
    // The whole explode (brush edit, spawn addEntity, prefab removal) is one undo step (Document::beginGroup).
    CHECK(d.history().size() == 1 && d.history()[0].name == "Explode Prefab");
    CHECK(!d.selection().empty());  // exploded contents are selected
    const std::string exploded = d.text();
    CHECK(d.undo() && !d.canUndo());
    CHECK(d.text() == original);
    CHECK(d.redo() && !d.canRedo());
    CHECK(d.text() == exploded);
    CHECK(d.undo() && d.text() == original);
    CHECK(d.redo() && d.text() == exploded);

    // Not a prefab: worldspawn, and a stale id.
    CHECK(!explodePrefab(d, d.map().entities[0].id, prefab, &err));
    CHECK(!err.empty());
    CHECK(!explodePrefab(d, 99999, prefab, &err));
}

TEST(prefab_selection_to_prefab_round_trip) {
    std::string text;
    const std::string sample = samplePath();
    CHECK(!sample.empty() && readFile(sample, text));
    Document d;
    CHECK(d.loadFromText(text, "sample.map", nullptr));
    const Entity& world = d.map().entities[0];
    CHECK(world.isWorldspawn());
    AABB original;
    size_t prims = 0;
    for (const auto& it : world.items)
        if (const Primitive* p = it.prim()) {
            d.select({world.id, it.id}, true);
            original.add(p->bounds());
            ++prims;
        }
    Id spawn = kNoId;
    for (const auto& e : d.map().entities)
        if (e.classname() == "mp_tdm_spawn") spawn = e.id;
    CHECK(spawn != kNoId);
    d.select({spawn, kNoId}, true);
    CHECK(prims > 0);

    const Vec3 origin{-192, 192, 16};
    const std::string out = selectionToPrefabText(d, origin);
    CHECK(out.rfind("iwmap 4", 0) == 0);
    const MapFile pm = parseOrFail(out);
    CHECK(pm.entities.size() == 2);
    if (pm.entities.size() != 2) return;
    CHECK(pm.entities[0].isWorldspawn());
    CHECK(primCount(pm.entities[0]) == prims);
    CHECK(pm.entities[1].origin() && near(*pm.entities[1].origin(), Vec3{}));

    // Re-instantiating at the original origin reproduces the selected geometry.
    PrefabInstance inst;
    inst.model = "prefabs/selection.map";
    inst.origin = origin;
    const PrefabContents c = instantiatePrefab(pm, inst, "\r\n");
    CHECK(c.worldPrims.size() == prims);
    AABB back;
    for (const auto& p : c.worldPrims) back.add(p.bounds());
    CHECK(boundsNear(back, original));
    CHECK(c.entities.size() == 1);
    if (c.entities.size() == 1) CHECK(kv(c.entities[0].first, "origin") && *kv(c.entities[0].first, "origin") == "-192 192 16");
}

TEST(prefab_instantiate_sample_rotation_bounds) {
    std::string text;
    const std::string sample = samplePath();
    CHECK(!sample.empty() && readFile(sample, text));
    const MapFile m = parseOrFail(text);
    PrefabInstance inst;
    inst.origin = {0, 0, 0};
    inst.angles = {0, 90, 0};
    const PrefabContents c = instantiatePrefab(m, inst, "\r\n");
    AABB src, rot;
    size_t srcPrims = 0;
    for (const auto& it : m.entities[0].items)
        if (const Primitive* p = it.prim()) {
            src.add(p->bounds());
            ++srcPrims;
        }
    for (const auto& p : c.worldPrims) rot.add(p.bounds());
    CHECK(c.worldPrims.size() == srcPrims);
    // Yaw 90 maps (x, y, z) to (-y, x, z).
    const Vec3 mins{-src.maxs.y, src.mins.x, src.mins.z};
    const Vec3 maxs{-src.mins.y, src.maxs.x, src.maxs.z};
    CHECK(boundsNear(rot, AABB{mins, maxs}));
}

TEST(prefab_resolve_path_search_order) {
    const fs::path root = fs::path(test::tmpDir()) / "prefab_paths";
    std::error_code ec;
    fs::remove_all(root, ec);
    auto touch = [](const fs::path& p) {
        fs::create_directories(p.parent_path());
        std::ofstream(p) << "iwmap 4\n";
    };
    touch(root / "map_source" / "prefabs" / "crate.map");
    touch(root / "map_source" / "maps" / "level.map");
    touch(root / "other" / "prefabs" / "barrel.map");
    touch(root / "loose" / "maps" / "room.map");

    auto same = [](const std::string& got, const fs::path& want) {
        std::error_code e;
        return !got.empty() && fs::exists(want) && fs::equivalent(got, want, e);
    };
    // Map under map_source: the ancestor map_source folder is searched.
    CHECK(same(resolvePrefabPath((root / "map_source" / "maps" / "level.map").string(), "prefabs/crate.map", {}),
               root / "map_source" / "prefabs" / "crate.map"));
    // Search roots (with and without map_source) are tried after the map's own folders.
    CHECK(same(resolvePrefabPath((root / "loose" / "maps" / "room.map").string(), "prefabs/barrel.map",
                                 {(root / "other").string()}),
               root / "other" / "prefabs" / "barrel.map"));
    CHECK(same(resolvePrefabPath((root / "loose" / "maps" / "room.map").string(), "prefabs/crate.map",
                                 {(root / "map_source").string()}),
               root / "map_source" / "prefabs" / "crate.map"));
    // Nothing found.
    CHECK(resolvePrefabPath((root / "loose" / "maps" / "room.map").string(), "prefabs/missing.map", {}).empty());
    CHECK(resolvePrefabPath((root / "loose" / "maps" / "room.map").string(), "", {}).empty());
}

TEST(prefab_text_round_trip_with_patch_rotation) {
    // A patch-only prefab: the control points of the mesh move with the instance.
    const MapFile m = parseOrFail(
        "iwmap 4\n"
        "// entity 0\n{\n\"classname\" \"worldspawn\"\n"
        "// brush 0\n{\n mesh\n {\n  toolFlags splitGeo;\n  caulk\n  lightmap_gray\n  2 2 16 8\n  (\n"
        "\tv 0 0 0 t 0 0 0 0\n\tv 0 64 0 t 0 0 0 0\n  )\n  (\n"
        "\tv 64 0 0 t 0 0 0 0\n\tv 64 64 0 t 0 0 0 0\n  )\n }\n}\n}\n");
    CHECK(m.entities.size() == 1);
    PrefabInstance inst;
    inst.origin = {10, 0, 0};
    inst.angles = {0, 90, 0};
    const PrefabContents c = instantiatePrefab(m, inst, "\n");
    CHECK(c.worldPrims.size() == 1);
    if (c.worldPrims.size() != 1) return;
    const Primitive& p = c.worldPrims[0];
    CHECK(p.kind == PrimKind::Patch);
    CHECK(p.verts.size() == 4);
    // (x, y, 0) -> (-y + 10, x, 0): (64, 64, 0) becomes (-54, 64, 0).
    bool found = false;
    for (const auto& v : p.verts) found = found || near(v.pos, Vec3{-54, 64, 0});
    CHECK(found);
    CHECK(p.patchMaterial == "caulk");
    // Written text contains the new coordinates in the same vertex syntax.
    CHECK(p.text().find("v -54 64 0 t") != std::string::npos);
}
