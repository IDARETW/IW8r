// Regions: itemsOutside (hidden set), mapTextSubset / mapTextInRegion (Save Selected, Save Region) and the
// round trip through parseMap.
#include <string>
#include <vector>

#include "geometry.h"
#include "map_text.h"
#include "region.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

// One axis-aligned 6-face brush in the prefab test's face format (outward normals).
std::string boxBrush(double a, double b, double c, double A, double B, double C) {
    auto p = [](double x, double y, double z) {
        return "( " + formatNumber(x) + " " + formatNumber(y) + " " + formatNumber(z) + " )";
    };
    const std::string tail = " caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\r\n";
    std::string s = "// brush 0\r\n{\r\n";
    s += " " + p(A, B, c) + " " + p(a, B, c) + " " + p(a, b, c) + tail;
    s += " " + p(a, b, C) + " " + p(a, B, C) + " " + p(A, B, C) + tail;
    s += " " + p(a, b, C) + " " + p(A, b, C) + " " + p(A, b, c) + tail;
    s += " " + p(A, B, C) + " " + p(a, B, C) + " " + p(a, B, c) + tail;
    s += " " + p(A, b, C) + " " + p(A, B, C) + " " + p(A, B, c) + tail;
    s += " " + p(a, B, C) + " " + p(a, b, C) + " " + p(a, b, c) + tail;
    s += "}\r\n";
    return s;
}

// Map: worldspawn with brush A (0..64) and brush B (1000..1064); light L1 at (10 10 10) inside the test region,
// light L2 at (2000 0 0) outside it, and a trigger entity with one brush C (1100..1164) outside.
std::string sampleMap() {
    std::string s =
        "iwmap 4\r\n"
        "\"000_Global\" flags active\r\n"
        "// entity 0\r\n"
        "{\r\n"
        "\"classname\" \"worldspawn\"\r\n"
        "\"_color\" \"0.5 0.5 0.5\"\r\n";
    s += boxBrush(0, 0, 0, 64, 64, 64);
    s += boxBrush(1000, 1000, 1000, 1064, 1064, 1064);
    s += "}\r\n";
    s += "// entity 1\r\n{\r\n\"classname\" \"light\"\r\n\"origin\" \"10 10 10\"\r\n}\r\n";
    s += "// entity 2\r\n{\r\n\"classname\" \"light\"\r\n\"origin\" \"2000 0 0\"\r\n}\r\n";
    s += "// entity 3\r\n{\r\n\"classname\" \"trigger_multiple\"\r\n";
    s += boxBrush(1100, 1100, 1100, 1164, 1164, 1164);
    s += "}\r\n";
    return s;
}

MapFile parseOrFail(const std::string& text, bool& ok) {
    Id next = 1;
    ParseResult r = parseMap(text, next);
    ok = r.map.has_value() && !r.error.has_value();
    return r.map ? *r.map : MapFile{};
}

// Ids of the primitives of an entity classname, in file order.
std::vector<Id> primIds(const MapFile& m, const std::string& classname) {
    std::vector<Id> ids;
    for (const auto& e : m.entities)
        if (e.classname() == classname)
            for (const auto& it : e.items)
                if (it.isPrimitive()) ids.push_back(it.id);
    return ids;
}

Id entityId(const MapFile& m, const std::string& classname, size_t nth = 0) {
    size_t seen = 0;
    for (const auto& e : m.entities)
        if (e.classname() == classname && seen++ == nth) return e.id;
    return kNoId;
}

size_t countPrims(const MapFile& m) {
    size_t n = 0;
    for (const auto& e : m.entities)
        for (const auto& it : e.items)
            if (it.isPrimitive()) ++n;
    return n;
}

AABB box(double a, double b, double c, double A, double B, double C) {
    AABB r;
    r.mins = {a, b, c};
    r.maxs = {A, B, C};
    return r;
}

}  // namespace

TEST(region_items_outside) {
    bool ok = false;
    const MapFile m = parseOrFail(sampleMap(), ok);
    CHECK(ok);
    const std::vector<Id> brushes = primIds(m, "worldspawn");
    CHECK(brushes.size() == 2);
    const std::vector<Id> trigger = primIds(m, "trigger_multiple");
    CHECK(trigger.size() == 1);

    // Region around the origin: brush A and light L1 inside, brush B, trigger C and light L2 outside.
    const std::set<Id> out = itemsOutside(m, box(-100, -100, -100, 100, 100, 100));
    CHECK(out.count(brushes[0]) == 0);
    CHECK(out.count(brushes[1]) == 1);
    CHECK(out.count(trigger[0]) == 1);
    CHECK(out.count(entityId(m, "light", 0)) == 0);
    CHECK(out.count(entityId(m, "light", 1)) == 1);
    CHECK(out.size() == 3);

    // Brush A touches the region face at x = 64: overlapping counts as inside.
    const std::set<Id> touch = itemsOutside(m, box(64, -100, -100, 200, 100, 100));
    CHECK(touch.count(brushes[0]) == 0);

    // An invalid (empty) region hides nothing.
    CHECK(itemsOutside(m, AABB{}).empty());
}

TEST(region_save_region_keeps_inside_only) {
    bool ok = false;
    const MapFile m = parseOrFail(sampleMap(), ok);
    CHECK(ok);
    const std::string text = mapTextInRegion(m, box(-100, -100, -100, 100, 100, 100));
    bool ok2 = false;
    const MapFile out = parseOrFail(text, ok2);
    CHECK(ok2);
    CHECK(out.header == m.header);
    CHECK(out.entities.size() == 2);  // worldspawn + light L1
    CHECK(countPrims(out) == 1);      // brush A only
    CHECK(out.entities[0].isWorldspawn());
    CHECK(out.entities[0].value("_color") && *out.entities[0].value("_color") == "0.5 0.5 0.5");
    CHECK(out.entities[1].classname() == "light");
    CHECK(out.entities[1].origin() && out.entities[1].origin()->x == 10);
    // The kept brush is byte-identical to the source brush text.
    const Primitive* src = nullptr;
    for (const auto& it : m.entities[0].items)
        if (it.isPrimitive()) { src = it.prim(); break; }
    const Primitive* kept = nullptr;
    for (const auto& it : out.entities[0].items)
        if (it.isPrimitive()) { kept = it.prim(); break; }
    CHECK(src && kept && src->text() == kept->text());
}

TEST(region_save_selected_subset) {
    bool ok = false;
    const MapFile m = parseOrFail(sampleMap(), ok);
    CHECK(ok);
    const std::vector<Id> brushes = primIds(m, "worldspawn");
    const Id light2 = entityId(m, "light", 1);

    // Only brush B and light L2 are selected: worldspawn keeps its keys and brush B, the light is written, the
    // trigger brush (not selected) and light L1 are not.
    const std::string text = mapTextSubset(m, [&](Id entity, Id item) {
        if (item == kNoId) return entity == light2;
        return item == brushes[1];
    });
    bool ok2 = false;
    const MapFile out = parseOrFail(text, ok2);
    CHECK(ok2);
    CHECK(out.entities.size() == 2);
    CHECK(countPrims(out) == 1);
    CHECK(out.entities[0].isWorldspawn());
    CHECK(out.entities[1].classname() == "light");
    CHECK(out.entities[1].origin() && out.entities[1].origin()->x == 2000);
}

TEST(region_save_selected_brush_entity) {
    bool ok = false;
    const MapFile m = parseOrFail(sampleMap(), ok);
    CHECK(ok);
    const Id trig = entityId(m, "trigger_multiple");
    // Selecting the trigger entity as a unit writes its brush; a single worldspawn brush is one primitive.
    const std::string text = mapTextSubset(m, [&](Id entity, Id) { return entity == trig; });
    bool ok2 = false;
    const MapFile out = parseOrFail(text, ok2);
    CHECK(ok2);
    CHECK(out.entities.size() == 2);  // worldspawn (keys only) + trigger
    CHECK(countPrims(out) == 1);
    CHECK(out.entities[0].isWorldspawn());
    CHECK(out.entities[0].items.size() == 2);  // classname and _color; no brushes
    CHECK(out.entities[1].classname() == "trigger_multiple");
}

TEST(region_subset_nothing_selected_is_keys_only) {
    bool ok = false;
    const MapFile m = parseOrFail(sampleMap(), ok);
    CHECK(ok);
    const std::string text = mapTextSubset(m, [](Id, Id) { return false; });
    bool ok2 = false;
    const MapFile out = parseOrFail(text, ok2);
    CHECK(ok2);
    CHECK(out.entities.size() == 1);
    CHECK(out.entities[0].isWorldspawn());
    CHECK(countPrims(out) == 0);
}

TEST(region_subset_round_trip_reparses) {
    bool ok = false;
    const MapFile m = parseOrFail(sampleMap(), ok);
    CHECK(ok);
    // The whole map through the subset writer reproduces the same primitives and entities.
    const std::string all = mapTextSubset(m, [](Id, Id) { return true; });
    bool ok2 = false;
    const MapFile out = parseOrFail(all, ok2);
    CHECK(ok2);
    CHECK(out.entities.size() == m.entities.size());
    CHECK(countPrims(out) == countPrims(m));
    // And the region writer with a region covering everything is the same map.
    const std::string everything = mapTextInRegion(m, box(-1e6, -1e6, -1e6, 1e6, 1e6, 1e6));
    CHECK(everything == mapTextSubset(m, [](Id, Id) { return true; }));
}
