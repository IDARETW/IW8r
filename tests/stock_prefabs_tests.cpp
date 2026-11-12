// Stock prefab harvester (src/core/stock_prefabs.h): the category table, the family key, the cluster relative transform
// (anchor at the origin, anchor yaw removed), the tolerance-based dedup, the status table reader, and a written prefab
// that parses with the editor's map parser and carries the expected misc_model entities.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "map_text.h"
#include "stock_prefabs.h"
#include "stock_smodels.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

namespace fs = std::filesystem;

constexpr const char* kDoor = "tm2co/me_door_metal_01";
constexpr const char* kFrame = "tm2co/me_frame_wood_01";
constexpr const char* kBarrel = "tm2co/prop_barrel_01";

StockModelInstance inst(const std::string& model, Vec3 origin, double yawDeg) {
    StockModelInstance m;
    m.model = model;
    m.origin = origin;
    m.axis = anglesToAxis(Vec3{0, yawDeg, 0});
    m.scale = 1;
    return m;
}

bool closeTo(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol; }

}  // namespace

TEST(stock_prefab_category_table) {
    CHECK(!stockPrefabRules().empty());
    for (const auto& r : stockPrefabRules()) CHECK(r.radius <= 128.0);
    CHECK(stockPrefabCategory(kDoor) == "door");
    CHECK(stockPrefabCategory("tm/me_window_frame_01") == "window");
    CHECK(stockPrefabCategory(kFrame) == "frame");
    CHECK(stockPrefabCategory(kBarrel) == "barrel");
    CHECK(stockPrefabCategory("tm/foliage_tree_oak_01") == "foliage");
    CHECK(stockPrefabCategory("tm/me_stair_concrete_01") == "stair");
    CHECK(stockPrefabCategory("tm/me_fence_chain_link_02") == "fence");
    CHECK(stockPrefabCategory("tm/me_sandbags_stack_01") == "sandbag");
    CHECK(stockPrefabCategory("tm/unknown_widget_77") == "");
    // A door handle is primarily a door, but its handle category is still visible to the attachment test.
    CHECK(stockPrefabCategory("tm/me_door_handle_01") == "door");
    const auto cats = stockPrefabCategories("tm/me_door_handle_01");
    CHECK(cats.size() == 2 && cats[0] == "door" && cats[1] == "handle");
    // "trailer" must not match the railing keyword "rail".
    CHECK(stockPrefabCategory("tm/vehicle_trailer_01") != "railing");
}

TEST(stock_prefab_tokens_and_family) {
    const auto t = stockModelTokens(kDoor);
    CHECK((t == std::vector<std::string>{"me", "door", "metal"}));
    CHECK(stockModelFamily(kDoor) == "me_door");
    CHECK(stockModelFamily(kFrame) == "me_frame");
    CHECK(stockPrefabSafeName(kDoor) == "tm2co_me_door_metal_01");
}

TEST(stock_prefab_cluster_relative_transform) {
    // Door anchor at (100,0,0) facing +Y (yaw 90). Its frame is 30 units ahead of it along the world X axis.
    // Relative to the anchor (anchor yaw removed) the frame lies at (0,-30,0) and faces +X (yaw 0).
    std::vector<StockModelInstance> ins = {
        inst(kDoor, {100, 0, 0}, 90),
        inst(kFrame, {130, 0, 0}, 90),
    };
    const auto rel = stockClusterRelative(ins, 0, {0, 1});
    CHECK(rel.size() == 2);
    const StockModelInstance* door = nullptr;
    const StockModelInstance* frame = nullptr;
    for (const auto& m : rel) (m.model == kDoor ? door : frame) = &m;
    CHECK(door && frame);
    if (door && frame) {
        CHECK(closeTo(door->origin.x, 0) && closeTo(door->origin.y, 0) && closeTo(door->origin.z, 0));
        CHECK(closeTo(frame->origin.x, 0) && closeTo(frame->origin.y, -30) && closeTo(frame->origin.z, 0));
        CHECK(closeTo(axisToAngles(door->axis).y, 0, 1e-6));
        CHECK(closeTo(axisToAngles(frame->axis).y, 0, 1e-6));
    }

    // The same cluster after a rotation and a translation of the whole group gives the same relative placement.
    const double s = std::sqrt(0.5);
    std::vector<StockModelInstance> ins2 = {
        inst(kDoor, {1000, 500, 0}, 45),
        inst(kFrame, {1000 + 30 * s, 500 - 30 * s, 0}, 45),
    };
    CHECK(sameStockPrefab(stockClusterRelative(ins, 0, {0, 1}), stockClusterRelative(ins2, 0, {0, 1})));
}

TEST(stock_prefab_attachment_excludes_clutter) {
    // Frame (attached) and a barrel inside the radius (floor clutter, not attached), and a barrel far outside it.
    std::vector<StockModelInstance> ins = {
        inst(kDoor, {100, 0, 0}, 90),
        inst(kFrame, {130, 0, 0}, 90),
        inst(kBarrel, {110, 0, 0}, 0),
        inst(kBarrel, {500, 0, 0}, 0),
    };
    StockPrefabOptions opt;
    StockZone z;
    z.name = "zone_a";
    z.instances = ins;
    const auto lib = harvestStockPrefabs({z}, opt);
    bool found = false;
    for (const auto& p : lib.prefabs) {
        if (p.kind == "cluster" && p.anchorModel == kDoor) {
            found = true;
            CHECK(p.members.size() == 2);
            CHECK(p.count == 1);
        }
    }
    CHECK(found);
    CHECK(lib.instancesSeen == 4);
}

TEST(stock_prefab_dedup_repeated_cluster) {
    const double s = std::sqrt(0.5);
    StockZone z;
    z.name = "zone_b";
    // Two copies that differ only by placement (translated, rotated 45 degrees), and a third that is off by 5 units.
    z.instances = {
        inst(kDoor, {100, 0, 0}, 90),
        inst(kFrame, {130, 0, 0}, 90),
        inst(kDoor, {1000, 500, 0}, 45),
        inst(kFrame, {1000 + 30 * s, 500 - 30 * s, 0}, 45),
        inst(kDoor, {2000, 0, 0}, 0),
        inst(kFrame, {2030, 5, 0}, 0),
    };
    StockPrefabOptions opt;
    const auto lib = harvestStockPrefabs({z}, opt);
    size_t doorClusters = 0, twice = 0, once = 0;
    for (const auto& p : lib.prefabs) {
        if (p.kind != "cluster" || p.anchorModel != kDoor) continue;
        ++doorClusters;
        if (p.count == 2) {
            ++twice;
            CHECK(p.zones.count("zone_b") == 1 && p.zones.at("zone_b") == 2);
        }
        if (p.count == 1) ++once;
    }
    CHECK(doorClusters == 2);
    CHECK(twice == 1);
    CHECK(once == 1);
}

TEST(stock_prefab_status_filter_and_singles) {
    StockZone z;
    z.name = "zone_c";
    z.instances = {inst(kDoor, {0, 0, 0}, 0), inst("tm2co/hidden_model_01", {50, 0, 0}, 0)};
    StockPrefabOptions opt;
    opt.okModels = {kDoor};  // the other model is not ok and must not be harvested
    const auto lib = harvestStockPrefabs({z}, opt);
    CHECK(lib.instancesSkipped == 1);
    bool single = false;
    for (const auto& p : lib.prefabs) {
        CHECK(p.anchorModel != "tm2co/hidden_model_01");
        if (p.kind == "single" && p.anchorModel == kDoor) {
            single = true;
            CHECK(p.members.size() == 1 && p.members[0].origin == Vec3(0, 0, 0));
        }
    }
    CHECK(single);
}

TEST(stock_prefab_sameness_tolerance) {
    auto a = std::vector<StockModelInstance>{inst(kFrame, {0, 0, 0}, 0)};
    auto near_ = std::vector<StockModelInstance>{inst(kFrame, {0.5, 0, 0}, 0.5)};
    auto far_ = std::vector<StockModelInstance>{inst(kFrame, {2, 0, 0}, 0)};
    auto angled = std::vector<StockModelInstance>{inst(kFrame, {0, 0, 0}, 2)};
    CHECK(sameStockPrefab(a, near_));
    CHECK(!sameStockPrefab(a, far_));
    CHECK(!sameStockPrefab(a, angled));
}

TEST(stock_prefab_status_table_reader) {
    const fs::path dir = fs::path(iw8r::test::tmpDir()) / "stock_prefabs_status";
    fs::create_directories(dir);
    const std::string p = (dir / "status.tsv").string();
    {
        std::ofstream o(p, std::ios::binary);
        o << "# header\n" << kDoor << "\tok\t\n" << "tm2co/grey_thing_01\tgrey\tno LOD0\n" << kFrame << "\tok\t\r\n";
    }
    std::set<std::string> ok;
    CHECK(loadStockModelStatus(p, ok));
    CHECK(ok.size() == 2 && ok.count(kDoor) == 1 && ok.count(kFrame) == 1);
    CHECK(ok.count("tm2co/grey_thing_01") == 0);
}

TEST(stock_prefab_written_map_parses) {
    StockZone z;
    z.name = "zone_d";
    z.instances = {inst(kDoor, {100, 0, 0}, 90), inst(kFrame, {130, 0, 0}, 90)};
    StockPrefabOptions opt;
    const auto lib = harvestStockPrefabs({z}, opt);
    CHECK(!lib.prefabs.empty());

    // Text of one cluster: worldspawn plus two misc_model entities, parsed by the editor's map parser.
    const auto rel = stockClusterRelative(z.instances, 0, {0, 1});
    const std::string text = stockPrefabMapText(rel);
    Id nextId = 1;
    const auto parsed = parseMap(text, nextId);
    CHECK(parsed.map.has_value());
    if (parsed.map) {
        const auto& ents = parsed.map->entities;
        CHECK(ents.size() == 3);
        if (ents.size() == 3) {
            CHECK(ents[0].isWorldspawn());
            CHECK(ents[1].classname() == "misc_model");
            CHECK(ents[2].classname() == "misc_model");
            const std::string* model = ents[1].value("model");
            CHECK(model != nullptr && (*model == kDoor || *model == kFrame));
            const auto o = ents[2].origin();
            CHECK(o.has_value());
            if (o) CHECK(closeTo(o->x, 0, 1e-3) && closeTo(o->y, -30, 1e-3));
        }
    }

    // Whole library written to a folder: index.tsv, README.md and a parsable .map per prefab.
    const fs::path out = fs::path(iw8r::test::tmpDir()) / "stock_prefabs_out";
    std::error_code ec;
    fs::remove_all(out, ec);
    StockWriteStats st;
    std::string err;
    CHECK(writeStockPrefabLibrary(out.string(), lib, &err, &st));
    CHECK(fs::exists(out / "index.tsv"));
    CHECK(fs::exists(out / "README.md"));
    CHECK(st.files == lib.prefabs.size());
    for (const auto& p : lib.prefabs) {
        std::string body;
        CHECK(readFile((out / p.relPath).string(), body));
        Id id = 1;
        const auto r = parseMap(body, id);
        CHECK(r.map.has_value());
        if (r.map) CHECK(r.map->entities.size() == p.members.size() + 1);
    }
}
