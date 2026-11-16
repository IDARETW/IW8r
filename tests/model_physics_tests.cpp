// Placed model collision by the stock models' own collision (core model_physics.h): a stock model without
// a physics asset (most bushes) is walked through, one with a physics asset blocks with that asset's convex pieces, and
// the rest keep the render mesh's hull. The same rule in the build (compile_iw8), the walk preview (play_world) and a
// stock map's props in the walk preview (addStockPropCollision).
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "brush_ops.h"
#include "collision_world.h"
#include "compile_iw8.h"
#include "convex_hull.h"
#include "glb_mesh.h"
#include "json_lite.h"
#include "map_text.h"
#include "model_physics.h"
#include "play_world.h"
#include "stock_smodels.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

constexpr const char* kId = "mp_phys_t";

// Synthetic catalogue: a bush without physics, a bush whose physics is a box (x 0..60, y -20..20, z 0..50: not
// centred, so a wrong rotation shows), a model whose physics is not read, and nothing about the crate.
const char* kCatalogue =
    "# test catalogue\n"
    "M\ttm/t_bush_cards\t-\t-\n"
    "M\ttm/t_bush_solid\tt_bush_solid\t-\n"
    "M\ttm/t_mesh_phys\tt_mesh_phys\tt_mesh_phys\n"
    "M\t,tm/t_comma\t-\t-\n"
    "P\tt_bush_solid\tb180\t60,20,50 0,20,50 60,-20,50 0,-20,50 60,20,0 0,20,0 60,-20,0 0,-20,0\t-\n"
    "P\tt_mesh_phys\t2180\tmesh\tshape type 602\n";

void writeText(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << text;
}

std::string readText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string boxBrush(const AABB& b, const char* material) {
    const auto prim = makeBrushPrimitive(makeBox(b, std::string(" ") + material + " 128 128 0 0 0 0"), "\n");
    return prim ? prim->text() : std::string();
}

std::string worldText(const std::string& entities) {
    std::string t = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n";
    t += boxBrush({{-256, -256, -16}, {512, 512, 0}}, "world/a");
    t += "}\n";
    t += "{\n\"classname\" \"mp_tdm_spawn\"\n\"origin\" \"-128 -128 16\"\n\"angles\" \"0 0 0\"\n}\n";
    t += "{\n\"classname\" \"mp_tdm_spawn\"\n\"origin\" \"400 400 16\"\n\"angles\" \"0 180 0\"\n}\n";
    t += entities;
    return t;
}

void addQuad(ImportedMesh& m, const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d) {
    const uint32_t base = uint32_t(m.pos.size());
    for (const Vec3& p : {a, b, c, d}) {
        m.pos.push_back(p);
        m.normal.push_back(Vec3{0, 1, 0});
        m.uv.push_back({0, 0});
    }
    for (const uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u}) m.idx.push_back(base + k);
}

// Two crossed cards, 128 wide and 96 tall, in file (Y-up) space: a foliage bush's render mesh (no hull: each card is
// flat, so the old build gave the bush a solid box of its bounds).
ImportedModel cards() {
    ImportedModel m;
    ImportedMesh mesh;
    mesh.material = "tmco/t_leaves";
    addQuad(mesh, {-64, 0, 0}, {64, 0, 0}, {64, 96, 0}, {-64, 96, 0});
    addQuad(mesh, {0, 0, -64}, {0, 0, 64}, {0, 96, 64}, {0, 96, -64});
    m.meshes.push_back(mesh);
    for (const Vec3& p : mesh.pos) m.bounds.add(p);
    return m;
}

// A closed 32 box, file space 0..32 on every axis (map: x 0..32, y -32..0, z 0..32).
ImportedModel crate() {
    ImportedModel m;
    ImportedMesh mesh;
    mesh.material = "tmo/t_crate";
    const auto P = [](int c) { return Vec3{double(c & 1) * 32, double((c >> 1) & 1) * 32, double((c >> 2) & 1) * 32}; };
    const int quads[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
    for (const auto& q : quads) addQuad(mesh, P(q[0]), P(q[1]), P(q[2]), P(q[3]));
    m.meshes.push_back(mesh);
    for (const Vec3& p : mesh.pos) m.bounds.add(p);
    return m;
}

bool inside(const std::vector<Plane>& planes, const Vec3& p) {
    if (planes.empty()) return false;
    for (const Plane& pl : planes)
        if (dot(pl.n, p) > pl.d + 0.01) return false;
    return true;
}

std::vector<std::vector<Plane>> brushesOf(const json::Value& col) {
    std::vector<std::vector<Plane>> out;
    const json::Value* brushes = col.get("brushes");
    if (!brushes) return out;
    for (const auto& b : brushes->arr) {
        std::vector<Plane> pl;
        if (const json::Value* ps = b.get("planes"))
            for (const auto& p : ps->arr)
                if (p.arr.size() == 4)
                    pl.push_back(Plane{Vec3{p.arr[0].number().value_or(0.0), p.arr[1].number().value_or(0.0), p.arr[2].number().value_or(0.0)},
                                       p.arr[3].number().value_or(0.0)});
        out.push_back(pl);
    }
    return out;
}

size_t containing(const std::vector<std::vector<Plane>>& brushes, const Vec3& p) {
    size_t n = 0;
    for (const auto& b : brushes) n += inside(b, p);
    return n;
}

bool hasWarning(const CompileReport& rep, const std::string& needle) {
    for (const auto& w : rep.warnings)
        if (w.find(needle) != std::string::npos) return true;
    return false;
}

bool blocked(const CollisionWorld& w, const Vec3& from, const Vec3& to) {
    return w.traceBox(from, to, Vec3{-15, -15, 0}, Vec3{15, 15, 60}).fraction < 0.999;
}

// The four placements: the walk-through bush at (0, 0), the physics bush at (192, 64) turned 90 degrees (its box lands
// at x 172..212, y 64..124, z 0..50), the crate at (0, 192), the unread-physics model at (192, 256).
const char* kEntities =
    "{\n\"classname\" \"misc_model\"\n\"model\" \"tm/t_bush_cards\"\n\"origin\" \"0 0 0\"\n}\n"
    "{\n\"classname\" \"misc_model\"\n\"model\" \"tm/t_bush_solid\"\n\"origin\" \"192 64 0\"\n\"angles\" \"0 90 0\"\n}\n"
    "{\n\"classname\" \"misc_model\"\n\"model\" \"tm/t_crate\"\n\"origin\" \"0 192 0\"\n}\n"
    "{\n\"classname\" \"misc_model\"\n\"model\" \"tm/t_mesh_phys\"\n\"origin\" \"192 256 0\"\n}\n";

}  // namespace

TEST(model_physics_catalogue_and_rule) {
    ModelPhysicsCatalog cat;
    cat.loadText(kCatalogue);
    CHECK(cat.size() == 4 && cat.shapeCount() == 2);
    const ModelPhysicsInfo* walk = cat.find("tm/t_bush_cards");
    CHECK(walk && walk->physicsAsset.empty());
    const ModelPhysicsInfo* solid = cat.find("tm/t_bush_solid");
    CHECK(solid && solid->physicsAsset == "t_bush_solid" && solid->shape && solid->shape->pieces.size() == 1 &&
          solid->shape->pieces[0].size() == 8 && solid->shape->contents == 0xB180);
    // Without the folder (the stock collision lists' spelling), and with the shared-name comma.
    CHECK(cat.find("t_bush_solid") == solid);
    CHECK(cat.find(",tm/t_bush_cards") == walk);
    CHECK(cat.find("t_comma") != nullptr);
    const ModelPhysicsInfo* mesh = cat.find("tm/t_mesh_phys");
    CHECK(mesh && mesh->shape && mesh->shape->pieces.empty() && mesh->detailCollision == "t_mesh_phys");
    CHECK(cat.find("tm/t_crate") == nullptr);

    // The rule.
    CHECK(chooseModelCollision("tm/t_bush_cards", nullptr, false, &cat).source == ModelCollisionSource::None);
    const ModelCollisionChoice s = chooseModelCollision("tm/t_bush_solid", nullptr, false, &cat);
    CHECK(s.source == ModelCollisionSource::PhysicsShape && s.shape == solid->shape);
    CHECK(chooseModelCollision("tm/t_mesh_phys", nullptr, false, &cat).source == ModelCollisionSource::RenderMesh);
    CHECK(chooseModelCollision("tm/t_crate", nullptr, false, &cat).source == ModelCollisionSource::RenderMesh);
    // The mapper's own model, and no catalogue: the render mesh, whatever the name.
    CHECK(chooseModelCollision("tm/t_bush_cards", nullptr, true, &cat).source == ModelCollisionSource::RenderMesh);
    CHECK(chooseModelCollision("tm/t_bush_cards", nullptr, false, nullptr).source == ModelCollisionSource::RenderMesh);
    // Placement keys come first.
    Id next = 1;
    ParseResult pr = parseMap(worldText("{\n\"classname\" \"misc_model\"\n\"model\" \"tm/t_bush_cards\"\n\"iw8r_collision\" \"1\"\n}\n"
                                        "{\n\"classname\" \"misc_model\"\n\"model\" \"tm/t_bush_solid\"\n\"iw8r_collision\" \"0\"\n}\n"
                                        "{\n\"classname\" \"misc_model\"\n\"model\" \"tm/t_crate\"\n\"iw8r_no_collision\" \"1\"\n}\n"),
                              next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const auto& ents = pr.map->entities;
    CHECK(ents.size() == 6);
    if (ents.size() != 6) return;
    CHECK(chooseModelCollision("tm/t_bush_cards", &ents[3], false, &cat).source == ModelCollisionSource::RenderMesh);
    CHECK(chooseModelCollision("tm/t_bush_solid", &ents[4], false, &cat).source == ModelCollisionSource::None);
    CHECK(chooseModelCollision("tm/t_crate", &ents[5], false, &cat).source == ModelCollisionSource::None);

    // The physics box as planes: six, holding its corners and not the space beside it.
    const auto planes = physicsShapePlanes(*solid->shape, 24);
    CHECK(planes.size() == 1 && planes[0].size() == 6);
    if (planes.size() == 1) {
        CHECK(inside(planes[0], Vec3{30, 0, 25}) && inside(planes[0], Vec3{60, 20, 50}) && !inside(planes[0], Vec3{-5, 0, 25}));
        // Placed turned 90 degrees about z (model x -> world y) at (192, 64, 0).
        const Vec3 axis[3] = {Vec3{0, 1, 0}, Vec3{-1, 0, 0}, Vec3{0, 0, 1}};
        const auto placed = placePlanes(planes[0], Vec3{192, 64, 0}, axis, 1.0);
        CHECK(inside(placed, Vec3{192, 100, 25}) && !inside(placed, Vec3{192, 40, 25}) && !inside(placed, Vec3{230, 100, 25}));
    }
    // A malformed shape line reads as not decoded.
    ModelPhysicsCatalog bad;
    bad.loadText("M\tx\tpx\t-\nP\tpx\t1\t1,2 3,4,5\t-\n");
    CHECK(bad.find("x") && bad.find("x")->shape && bad.find("x")->shape->pieces.empty());
}

TEST(model_physics_compile_follows_stock_collision) {
    ModelPhysicsCatalog cat;
    cat.loadText(kCatalogue);
    std::map<std::string, ImportedModel> stock{{"tm/t_bush_cards", cards()},
                                               {"tm/t_bush_solid", cards()},
                                               {"tm/t_crate", crate()},
                                               {"tm/t_mesh_phys", crate()}};
    Id next = 1;
    ParseResult pr = parseMap(worldText(kEntities), next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const auto compile = [&](const ModelPhysicsCatalog* c, const std::string& name, CompileReport& rep,
                             std::vector<std::vector<Plane>>& brushes) {
        CompileOptions opt;
        opt.mapId = kId;
        opt.stockModels = &stock;
        opt.modelPhysics = c;
        const fs::path out = fs::path(test::tmpDir()) / "model_physics" / name;
        std::error_code ec;
        fs::remove_all(out, ec);
        std::string err;
        if (!compileIw8Extraction(*pr.map, out.string(), opt, rep, &err)) {
            std::printf("  compile error: %s\n", err.c_str());
            return false;
        }
        std::string jerr;
        const auto col = json::parse(readText(out / "extracted" / "maps" / "mp" / (std::string(kId) + ".d3dbsp.replay-collision.json")), &jerr);
        if (!col) return false;
        brushes = brushesOf(*col);
        return true;
    };
    // Without the catalogue (the build before the rule): every model collides by its render mesh; each flat card is a
    // thin slab (convexPieces), so both bushes block along their cards.
    CompileReport before;
    std::vector<std::vector<Plane>> old;
    CHECK(compile(nullptr, "before", before, old));
    CHECK(old.size() == 7);  // the floor + two slabs per bush + the two boxes
    CHECK(hasWarning(before, "placed model collision: 4 model(s) as 6 convex hull brush(es)"));
    CHECK(containing(old, Vec3{20, 0, 48}) == 1);    // on the walk-through bush's first card
    CHECK(containing(old, Vec3{150, 64, 48}) == 1);  // on the physics bush's second card (turned: y = 64, x 128..256)
    // With the catalogue: the walk-through bush has nothing, the physics bush only its physics box, the crate and the
    // unread-physics model keep their render hull.
    CompileReport rep;
    std::vector<std::vector<Plane>> now;
    CHECK(compile(&cat, "after", rep, now));
    CHECK(now.size() == 4);
    CHECK(containing(now, Vec3{20, 0, 48}) == 0 && containing(now, Vec3{0, 0, 10}) == 0);
    CHECK(containing(now, Vec3{192, 100, 25}) == 1);  // in the physics box
    CHECK(containing(now, Vec3{192, 40, 25}) == 0);   // beside it (inside the render cards' box)
    CHECK(containing(now, Vec3{150, 64, 48}) == 0);
    CHECK(containing(now, Vec3{16, 176, 16}) == 1);   // the crate: x 0..32, y 160..192
    CHECK(containing(now, Vec3{208, 240, 16}) == 1);  // the unread-physics model's render hull
    CHECK(hasWarning(rep, "1 model(s) as 1 brush(es) from their physics asset, 1 walk-through"));
    CHECK(hasWarning(rep, "render mesh for 1 not in the catalogue and 1 whose physics shape is not read"));
    CHECK(hasWarning(rep, "placed model collision: 2 model(s) as 2 convex hull brush(es), 0 as a solid box"));
    CHECK(!hasWarning(before, "from their physics asset"));
}

TEST(model_physics_walk_preview_follows_stock_collision) {
    ModelPhysicsCatalog cat;
    cat.loadText(kCatalogue);
    const auto bush = std::make_shared<ImportedModel>(cards());
    const auto box = std::make_shared<ImportedModel>(crate());
    Id next = 1;
    ParseResult pr = parseMap(worldText(kEntities), next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    PlayWorldOptions opt;
    opt.physics = &cat;
    opt.modelOf = [&](const Entity& e) -> std::shared_ptr<const ImportedModel> {
        const std::string* m = e.value("model");
        if (!m) return nullptr;
        return m->find("bush") != std::string::npos ? bush : box;
    };
    CollisionWorld world;
    PlayWorldStats st;
    buildPlayWorld(*pr.map, opt, world, &st);
    CHECK(st.walkThrough == 1 && st.physicsModels == 1 && st.models == 3);
    // Through the walk-through bush (x -64..64 at y 0).
    CHECK(!blocked(world, Vec3{-120, 0, 1}, Vec3{120, 0, 1}));
    // Into the physics bush's box (x 172..212, y 64..124) along y = 100; and past it at y = 30 (inside the cards).
    CHECK(blocked(world, Vec3{120, 100, 1}, Vec3{260, 100, 1}));
    CHECK(!blocked(world, Vec3{150, 30, 1}, Vec3{230, 30, 1}));
    // The crate blocks.
    CHECK(blocked(world, Vec3{-60, 176, 1}, Vec3{60, 176, 1}));
    // Without the rule both bushes are boxes again: the walk through the first is stopped.
    PlayWorldOptions oldRule = opt;
    oldRule.noPhysicsRule = true;
    CollisionWorld oldWorld;
    buildPlayWorld(*pr.map, oldRule, oldWorld, &st);
    CHECK(st.walkThrough == 0 && st.physicsModels == 0 && st.models == 4);
    CHECK(blocked(oldWorld, Vec3{-120, 0, 1}, Vec3{120, 0, 1}));
}

TEST(model_physics_stock_props_in_walk_preview) {
    ModelPhysicsCatalog cat;
    cat.loadText(kCatalogue);
    std::vector<StockModelInstance> props(4);
    props[0].model = "tm/t_bush_cards";
    props[0].origin = Vec3{0, 0, 0};
    props[1].model = "tm/t_bush_solid";
    props[1].origin = Vec3{192, 64, 0};
    props[1].axis = {Vec3{0, 1, 0}, Vec3{-1, 0, 0}, Vec3{0, 0, 1}};
    props[2].model = "tm/t_crate";
    props[2].origin = Vec3{0, 192, 0};
    props[3].model = "tm/t_bush_solid";
    props[3].origin = Vec3{5000, 0, 0};
    // The crate's render pieces (Z-up model space): its box, ready; the unread model's: not ready.
    const std::vector<std::vector<Plane>> crateBox = {{{Vec3{1, 0, 0}, 32}, {Vec3{-1, 0, 0}, 0}, {Vec3{0, 1, 0}, 0}, {Vec3{0, -1, 0}, 32},
                                                       {Vec3{0, 0, 1}, 32}, {Vec3{0, 0, -1}, 0}}};
    const auto render = [&](const std::string& m) -> const std::vector<std::vector<Plane>>* {
        return m == "tm/t_crate" ? &crateBox : nullptr;
    };
    CollisionWorld world;
    const StockPropCollisionStats st = addStockPropCollision(props, world, Vec3{0, 0, 0}, 1000, cat, 24, render);
    CHECK(st.considered == 3 && st.physics == 1 && st.renderMesh == 1 && st.walkThrough == 1 && st.pending == 0 && st.solids == 2);
    CHECK(!blocked(world, Vec3{-120, 0, 1}, Vec3{120, 0, 1}));
    CHECK(blocked(world, Vec3{120, 100, 1}, Vec3{260, 100, 1}));
    CHECK(!blocked(world, Vec3{150, 30, 1}, Vec3{230, 30, 1}));
    CHECK(!blocked(world, Vec3{-60, -16, 1}, Vec3{60, -16, 1}));
    CHECK(blocked(world, Vec3{-60, 176, 1}, Vec3{60, 176, 1}));  // the crate: model y -32..0 at y 192 -> y 160..192
    // Everything (radius 0), the crate's pieces not ready yet: counted as pending.
    CollisionWorld all;
    const StockPropCollisionStats st2 = addStockPropCollision(props, all, Vec3{}, 0, cat, 24, {});
    CHECK(st2.considered == 4 && st2.physics == 2 && st2.pending == 1 && st2.solids == 2);
    // A hidden prop (scale 0) gives nothing.
    props[1].scale = 0;
    props[3].scale = 0;
    CollisionWorld hidden;
    CHECK(addStockPropCollision(props, hidden, Vec3{}, 0, cat, 24, render).physics == 0);
}

// The shipped catalogue (tools/build_model_physics_catalog.py) decides the stock bushes as the game does.
TEST(model_physics_shipped_catalogue_bushes) {
    const ModelPhysicsCatalog& cat = ModelPhysicsCatalog::shared(fs::current_path().string());
    if (cat.size() == 0) {
        test::skipf("  skip: data/asset_catalog/iw8_model_physics.tsv not found\n");
        return;
    }
    CHECK(cat.size() > 5000);
    // tm/foliage_alder_bush_large_01 (the dev showcase's bush) has no physics asset: walk-through in game.
    const ModelPhysicsInfo* alder = cat.find("tm/foliage_alder_bush_large_01");
    CHECK(alder && alder->physicsAsset.empty());
    CHECK(chooseModelCollision("tm/foliage_alder_bush_large_01", nullptr, false, &cat).source == ModelCollisionSource::None);
    // The hedge has one: a 128 x 48 x 36 box.
    const ModelCollisionChoice hedge = chooseModelCollision("tm/ee_foliage_bush_hedge_128x60", nullptr, false, &cat);
    CHECK(hedge.source == ModelCollisionSource::PhysicsShape);
    if (hedge.shape) {
        AABB b;
        for (const auto& p : hedge.shape->pieces)
            for (const Vec3& v : p) b.add(v);
        CHECK(std::fabs(b.maxs.x - b.mins.x - 128) < 2 && std::fabs(b.maxs.y - b.mins.y - 48) < 2 && std::fabs(b.maxs.z - 36) < 1);
    }
    // A tree collides by its trunk (about 120 tall), not by its canopy (to 264).
    const ModelCollisionChoice tree = chooseModelCollision("tm/uk_trees_large_magnolia_tree_01", nullptr, false, &cat);
    CHECK(tree.source == ModelCollisionSource::PhysicsShape);
    if (tree.shape) {
        AABB b;
        for (const auto& p : tree.shape->pieces)
            for (const Vec3& v : p) b.add(v);
        CHECK(b.maxs.z < 130 && b.maxs.x - b.mins.x < 20);
    }
    // Compressed mesh physics (hknpCompressedMeshShape) is read too: the powerline's 36 triangles, and the crane's ten
    // convex custom primitives.
    const ModelCollisionChoice line = chooseModelCollision("city_prop_powerline_01", nullptr, false, &cat);
    CHECK(line.source == ModelCollisionSource::PhysicsShape && line.shape && line.shape->meshTris.size() == 36);
    if (line.shape) CHECK(!physicsShapePlanes(*line.shape, 24).empty());
    const ModelCollisionChoice crane = chooseModelCollision("crane_hangar_large_01", nullptr, false, &cat);
    CHECK(crane.source == ModelCollisionSource::PhysicsShape && crane.shape && crane.shape->pieces.size() == 10);
}

// Physics that is a triangle mesh: the build and the own-map preview make convex pieces of it, a stock map's walk
// preview adds its triangles as facets.
TEST(model_physics_mesh_shapes) {
    // A box 40 x 20 x 30 (x 0..40, y 0..20, z 0..30) as 12 triangles, outward winding.
    const char* text =
        "M\ttm/t_mesh_only\tt_mesh_only\t-\n"
        "P\tt_mesh_only\t2180\t-\t0,0,0 40,0,0 40,20,0 0,20,0 0,0,30 40,0,30 40,20,30 0,20,30|"
        "0,2,1 0,3,2 4,5,6 4,6,7 0,1,5 0,5,4 1,2,6 1,6,5 2,3,7 2,7,6 3,0,4 3,4,7\t-\n"
        "M\ttm/t_bad_mesh\tt_bad_mesh\t-\n"
        "P\tt_bad_mesh\t2180\t-\t0,0,0 1,0,0 0,1,0 0,0,1|0,1,9\t-\n";
    ModelPhysicsCatalog cat;
    cat.loadText(text);
    const ModelPhysicsInfo* info = cat.find("tm/t_mesh_only");
    CHECK(info && info->shape && info->shape->pieces.empty() && info->shape->meshVerts.size() == 8 && info->shape->meshTris.size() == 12);
    if (!info || !info->shape) return;
    CHECK(info->shape->readable());
    CHECK(chooseModelCollision("tm/t_mesh_only", nullptr, false, &cat).source == ModelCollisionSource::PhysicsShape);
    // An index past the vertices: the triangles are dropped, so the render mesh stands in.
    const ModelPhysicsInfo* bad = cat.find("tm/t_bad_mesh");
    CHECK(bad && bad->shape && bad->shape->meshTris.empty() && !bad->shape->readable());
    CHECK(chooseModelCollision("tm/t_bad_mesh", nullptr, false, &cat).source == ModelCollisionSource::RenderMesh);
    // Build / own-map pieces: closed, holding the box's middle.
    const auto pieces = physicsShapePlanes(*info->shape, 24);
    CHECK(!pieces.empty());
    size_t holding = 0;
    for (const auto& pc : pieces) holding += inside(pc, Vec3{20, 10, 15});
    CHECK(holding >= 1);
    CHECK(physicsShapePlanes(*info->shape, 24, 4, false).empty());  // without mesh pieces: nothing (no convex pieces)
    // A stock map's prop: 12 facets that stop a walk into the box, none beside it.
    std::vector<StockModelInstance> props(1);
    props[0].model = "tm/t_mesh_only";
    props[0].origin = Vec3{100, 0, 0};
    CollisionWorld w;
    const StockPropCollisionStats st = addStockPropCollision(props, w, Vec3{}, 0, cat, 24);
    CHECK(st.physics == 1 && st.facets == 12 && st.solids == 0);
    CHECK(blocked(w, Vec3{40, 10, 1}, Vec3{200, 10, 1}));
    CHECK(!blocked(w, Vec3{40, 80, 1}, Vec3{200, 80, 1}));
}

// An open collision piece (planes without end caps) is a slab through the whole map: the play preview's player started
// every trace inside it (Hideout, 2026-11-15: 150 barbed wire brackets whose render-mesh hull had two planes). Every
// piece the build and the preview make is closed and stays near its points.
TEST(model_physics_open_pieces_never_reach_the_world) {
    // The checks themselves: a slab and a capless prism are open, a box is closed.
    AABB b;
    CHECK(!convexSolidBounds({{Vec3{0, 0, 1}, 1}, {Vec3{0, 0, -1}, 1}}, b));
    CHECK(!convexSolidBounds({{Vec3{1, 0, 0}, 1}, {Vec3{-1, 0, 0}, 1}, {Vec3{0, 1, 0}, 1}, {Vec3{0, -1, 0}, 1}}, b));
    const std::vector<Plane> box = {{Vec3{1, 0, 0}, 2}, {Vec3{-1, 0, 0}, 0}, {Vec3{0, 1, 0}, 3}, {Vec3{0, -1, 0}, 0},
                                    {Vec3{0, 0, 1}, 4}, {Vec3{0, 0, -1}, 0}};
    CHECK(convexSolidBounds(box, b) && std::fabs(b.maxs.x - 2) < 1e-6 && std::fabs(b.maxs.y - 3) < 1e-6 && std::fabs(b.maxs.z - 4) < 1e-6);
    CHECK(convexSolidWithin(box, AABB{{0, 0, 0}, {2, 3, 4}}));
    CHECK(!convexSolidWithin(box, AABB{{0, 0, 0}, {0.1, 0.1, 0.1}}));
    // A placement whose axes lose a direction gives no planes (not planes with a zero normal, which constrain nothing).
    const Vec3 flatAxis[3] = {Vec3{1, 0, 0}, Vec3{0, 0, 0}, Vec3{0, 0, 1}};
    CHECK(placePlanes(box, Vec3{}, flatAxis, 1.0).empty());
    // Axes with a uniform scale in them (k R): the solid scales with them and the normals stay unit.
    const Vec3 twice[3] = {Vec3{2, 0, 0}, Vec3{0, 2, 0}, Vec3{0, 0, 2}};
    const auto big = placePlanes(box, Vec3{}, twice, 1.0);
    CHECK(convexSolidBounds(big, b) && std::fabs(b.maxs.x - 4) < 1e-6 && std::fabs(length(big[0].n) - 1) < 1e-9);

    // Point sets that defeat a hull: whatever comes out is closed and near the points (or nothing).
    const auto closedOrEmpty = [](const std::vector<Vec3>& pts) {
        const std::vector<Plane> pl = convexHullPlanes(pts);
        AABB pb, sb;
        for (const Vec3& p : pts) pb.add(p);
        return pl.empty() || (convexSolidBounds(pl, sb) && convexSolidWithin(pl, pb));
    };
    std::vector<Vec3> needle, sliver, wire;
    for (int i = 0; i <= 40; ++i) {
        const double t = i / 40.0;
        needle.push_back(Vec3{t * 100, 1e-5 * std::sin(i * 1.7), 1e-5 * std::cos(i * 2.3)});
        sliver.push_back(Vec3{t * 64, std::fmod(i * 7.0, 13.0), 1e-6 * double(i % 3)});
        // A bent rod: two long thin segments at an angle (a bracket), radius 0.05.
        const double a = i * 0.9;
        wire.push_back(Vec3{t * 48, 0.05 * std::cos(a), 0.05 * std::sin(a)});
        wire.push_back(Vec3{48 + 0.05 * std::cos(a), 0.05 * std::sin(a), t * 30});
    }
    CHECK(closedOrEmpty(needle));
    CHECK(closedOrEmpty(sliver));
    CHECK(closedOrEmpty(wire));
    // A stock physics piece that is flat (no hull) collides as a thin closed box, not as nothing or a slab.
    ModelPhysicsShape flat;
    flat.pieces = {{Vec3{0, 0, 10}, Vec3{40, 0, 10}, Vec3{40, 30, 10}, Vec3{0, 30, 10}, Vec3{20, 15, 10}}};
    const auto fp = physicsShapePlanes(flat, 24);
    CHECK(fp.size() == 1 && fp[0].size() == 6);
    CHECK(!fp.empty() && convexSolidBounds(fp[0], b) && b.maxs.z - b.mins.z < 1 && std::fabs(b.maxs.x - 40) < 1e-6);
    // The render-mesh pieces of a mesh of thin rods (a barbed wire bracket's shape): every piece closed and near.
    ImportedModel rods;
    {
        ImportedMesh mesh;
        mesh.material = "tmo/t_wire";
        const auto rod = [&](const Vec3& a, const Vec3& c) {
            const uint32_t base = uint32_t(mesh.pos.size());
            for (int k = 0; k < 6; ++k) {
                const double ang = k * 3.14159265358979 / 3.0;
                const Vec3 off{0, 0.04 * std::cos(ang), 0.04 * std::sin(ang)};
                mesh.pos.push_back(a + off);
                mesh.pos.push_back(c + off);
                mesh.normal.push_back(Vec3{0, 1, 0});
                mesh.normal.push_back(Vec3{0, 1, 0});
                mesh.uv.push_back({0, 0});
                mesh.uv.push_back({0, 0});
            }
            for (uint32_t k = 0; k < 6; ++k) {
                const uint32_t a0 = base + 2 * k, a1 = base + 2 * ((k + 1) % 6);
                for (const uint32_t v : {a0, a0 + 1, a1 + 1, a0, a1 + 1, a1}) mesh.idx.push_back(v);
            }
        };
        rod(Vec3{0, 0, 0}, Vec3{40, 0, 0});
        rod(Vec3{0, 10, 0}, Vec3{40, 10.02, 0});
        rod(Vec3{0, 20, 0}, Vec3{40, 20, 0.01});
        rods.meshes.push_back(mesh);
        for (const Vec3& p : mesh.pos) rods.bounds.add(p);
    }
    for (const auto& piece : convexPieces(rods, 4, 24)) {
        CHECK(convexSolidWithin(piece, AABB{rods.bounds.mins, rods.bounds.maxs}));
        CollisionWorld w;
        CHECK(w.addConvex(piece));
    }
    // The real bracket (a machine fixture: the model cache's GLB), when it is there.
    const char* la = std::getenv("LOCALAPPDATA");
    const fs::path glb = la ? fs::path(la) / "IW8 Radiant" / "IW8 Radiant" / "model_cache" / "glb" / "tm_decor_barbed_wire_bracket_01.glb"
                            : fs::path();
    ImportedModel bracket;
    GlbReadOptions o;
    o.scale = 1.0 / 0.0254;
    std::string err;
    if (!la || !fs::exists(glb) || !readGlb(glb.string(), bracket, &err, o)) {
        test::skipf("  skip: model cache GLB tm/decor_barbed_wire_bracket_01 not on this machine\n");
        return;
    }
    AABB all;
    for (const auto& m : bracket.meshes)
        for (const Vec3& p : m.pos) all.add(p);
    const auto pieces = convexPieces(bracket, 4, 24);
    CHECK(!pieces.empty());
    for (const auto& piece : pieces) {
        CHECK(convexSolidWithin(piece, all));
        AABB wb;
        for (const Winding& w : buildBrushWindings(piece))
            for (const Vec3& v : w) wb.add(v);
        CHECK(wb.valid() && wb.maxs.x - wb.mins.x < 1000 && wb.maxs.y - wb.mins.y < 1000 && wb.maxs.z - wb.mins.z < 1000);
    }
}
