// Entity change report and JSON patch (roadmap 11.7 / 2.28): diffMapEntities, formatMapDiff, mapPatchJson, applyMapPatch.
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "document.h"
#include "map_diff.h"
#include "test_harness.h"

using namespace iw8r;

namespace {
using KV = std::vector<std::pair<std::string, std::string>>;

// Axis-aligned box as Radiant brush text (same layout as the auto-caulk tests).
std::string box(int x0, int y0, int z0, int x1, int y1, int z1, const std::string& mat) {
    auto f = [&](const std::string& a, const std::string& b, const std::string& c) {
        return " ( " + a + " ) ( " + b + " ) ( " + c + " ) " + mat + " 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n";
    };
    const std::string X0 = std::to_string(x0), Y0 = std::to_string(y0), Z0 = std::to_string(z0), X1 = std::to_string(x1),
                      Y1 = std::to_string(y1), Z1 = std::to_string(z1);
    std::string t = "{\n";
    t += f(X1 + " " + Y1 + " " + Z0, X0 + " " + Y1 + " " + Z0, X0 + " " + Y0 + " " + Z0);
    t += f(X0 + " " + Y0 + " " + Z1, X0 + " " + Y1 + " " + Z1, X1 + " " + Y1 + " " + Z1);
    t += f(X0 + " " + Y0 + " " + Z0, X0 + " " + Y0 + " " + Z1, X1 + " " + Y0 + " " + Z1);
    t += f(X1 + " " + Y0 + " " + Z0, X1 + " " + Y0 + " " + Z1, X1 + " " + Y1 + " " + Z1);
    t += f(X1 + " " + Y1 + " " + Z0, X1 + " " + Y1 + " " + Z1, X0 + " " + Y1 + " " + Z1);
    t += f(X0 + " " + Y1 + " " + Z0, X0 + " " + Y1 + " " + Z1, X0 + " " + Y0 + " " + Z1);
    return t + "}\n";
}

// Same brush with every space tripled: must compare equal to the original after whitespace normalization.
std::string spaced(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == ' ') o += "   ";
        else o += c;
    }
    return o;
}

// One entity block in the editor's layout, followed by its brush text.
std::string ent(const KV& kv, const std::string& body = "") {
    std::string t = "{\n";
    for (const auto& [k, v] : kv) t += "\"" + k + "\" \"" + v + "\"\n";
    return t + body + "}\n";
}

std::string worldspawn(const std::string& brushes) { return ent({{"classname", "worldspawn"}}, brushes); }

MapFile parseText(const std::string& text) {
    Id next = 1;
    ParseResult r = parseMap(text, next);
    CHECK(r.map.has_value());
    return r.map ? *r.map : MapFile{};
}
}  // namespace

TEST(map_diff_added_and_removed) {
    const std::string world = worldspawn(box(0, 0, 0, 64, 64, 64, "tm/brick"));
    const std::string l1 = ent({{"classname", "light"}, {"targetname", "l1"}, {"origin", "0 0 0"}});
    const MapFile a = parseText("iwmap 4\n" + world + l1);
    const MapFile b = parseText("iwmap 4\n" + world + l1 + ent({{"classname", "light"}, {"origin", "200 0 0"}}));

    const auto added = diffMapEntities(a, b);
    CHECK(added.size() == 1);
    if (added.size() == 1) {
        CHECK(added[0].kind == EntityChange::Added);
        CHECK(added[0].classname == "light");
        CHECK(added[0].to == Vec3(200, 0, 0));
    }
    const auto removed = diffMapEntities(b, a);
    CHECK(removed.size() == 1);
    if (removed.size() == 1) {
        CHECK(removed[0].kind == EntityChange::Removed);
        CHECK(removed[0].from == Vec3(200, 0, 0));
    }
}

TEST(map_diff_moved_by_targetname) {
    const std::string world = worldspawn(box(0, 0, 0, 64, 64, 64, "tm/brick"));
    const MapFile a = parseText("iwmap 4\n" + world + ent({{"classname", "light"}, {"targetname", "l1"}, {"origin", "0 0 0"}}));
    const MapFile b = parseText("iwmap 4\n" + world + ent({{"classname", "light"}, {"targetname", "l1"}, {"origin", "64 0 0"}}));

    const auto changes = diffMapEntities(a, b);
    CHECK(changes.size() == 1);
    if (changes.size() == 1) {
        CHECK(changes[0].kind == EntityChange::Moved);
        CHECK(changes[0].label == "l1");
        CHECK(changes[0].from == Vec3(0, 0, 0));
        CHECK(changes[0].to == Vec3(64, 0, 0));
    }
}

TEST(map_diff_move_without_targetname_needs_small_distance) {
    const std::string world = worldspawn(box(0, 0, 0, 64, 64, 64, "tm/brick"));
    const MapFile a = parseText("iwmap 4\n" + world + ent({{"classname", "light"}, {"origin", "0 0 0"}}));
    // Within 1 unit: still the same entity, reported as a move.
    const MapFile near = parseText("iwmap 4\n" + world + ent({{"classname", "light"}, {"origin", "0.5 0 0"}}));
    const auto moved = diffMapEntities(a, near);
    CHECK(moved.size() == 1);
    if (moved.size() == 1) CHECK(moved[0].kind == EntityChange::Moved);
    // Beyond 1 unit with no targetname: the rule gives Removed + Added.
    const MapFile far = parseText("iwmap 4\n" + world + ent({{"classname", "light"}, {"origin", "5 0 0"}}));
    CHECK(diffMapEntities(a, far).size() == 2);
}

TEST(map_diff_keys_changed) {
    const std::string world = worldspawn(box(0, 0, 0, 64, 64, 64, "tm/brick"));
    const MapFile a = parseText("iwmap 4\n" + world + ent({{"classname", "light"}, {"targetname", "l1"}, {"intensity", "1"}}));
    const MapFile b = parseText("iwmap 4\n" + world +
                                ent({{"classname", "light"}, {"targetname", "l1"}, {"intensity", "2"}, {"color", "1 1 1"}}));

    const auto changes = diffMapEntities(a, b);
    CHECK(changes.size() == 1);
    if (changes.size() == 1) {
        CHECK(changes[0].kind == EntityChange::KeysChanged);
        CHECK(changes[0].keyChanges.size() == 2);
        if (changes[0].keyChanges.size() == 2) {
            CHECK(changes[0].keyChanges[0] == "intensity: 1 -> 2");
            CHECK(changes[0].keyChanges[1] == "color: (absent) -> 1 1 1");
        }
    }
    CHECK(formatMapDiff(changes) == "Keys light l1: intensity: 1 -> 2; color: (absent) -> 1 1 1\n");
}

TEST(map_diff_worldspawn_brushes) {
    const std::string b1 = box(0, 0, 0, 64, 64, 64, "tm/brick");
    const std::string b2 = box(200, 0, 0, 216, 16, 16, "tm/brick");
    const MapFile a = parseText("iwmap 4\n" + worldspawn(b1));
    const MapFile more = parseText("iwmap 4\n" + worldspawn(b1 + b2));

    const auto changes = diffMapEntities(a, more);
    CHECK(changes.size() == 1);
    if (changes.size() == 1) {
        CHECK(changes[0].kind == EntityChange::BrushesChanged);
        CHECK(changes[0].classname == "worldspawn");
        CHECK(changes[0].brushesAdded == 1);
        CHECK(changes[0].brushesRemoved == 0);
    }
    CHECK(formatMapDiff(changes) == "Brushes worldspawn entity 0: +1\n");

    // Same brush with different whitespace is not a change.
    const MapFile respaced = parseText("iwmap 4\n" + worldspawn(spaced(b1)));
    CHECK(diffMapEntities(a, respaced).empty());
}

TEST(map_diff_identical_maps_are_empty) {
    const std::string text = "iwmap 4\n" + worldspawn(box(0, 0, 0, 64, 64, 64, "tm/brick")) +
                             ent({{"classname", "light"}, {"targetname", "l1"}, {"origin", "0 0 0"}});
    const MapFile a = parseText(text);
    const MapFile b = parseText(text);
    CHECK(diffMapEntities(a, b).empty());
    CHECK(formatMapDiff(diffMapEntities(a, b)).empty());
    const std::string patch = mapPatchJson(a, b);
    CHECK(patch.find("\"changed\": []") != std::string::npos);
    CHECK(patch.find("\"removed\": []") != std::string::npos);
    CHECK(patch.find("\"added\": []") != std::string::npos);
}

TEST(map_diff_patch_round_trip) {
    // A -> B covers every change kind: a moved light, a light removed, a light added, a key changed, a brush entity
    // gaining a brush and a key, and a worldspawn brush added.
    const std::string brickA = box(0, 0, 0, 64, 64, 64, "tm/brick");
    const std::string doorBrush1 = box(100, 0, 0, 116, 16, 16, "tm/door");
    const std::string doorBrush2 = box(120, 0, 0, 136, 16, 16, "tm/door");
    const std::string textA = "iwmap 4\n" + worldspawn(brickA) +
                              ent({{"classname", "light"}, {"targetname", "l1"}, {"origin", "0 0 0"}, {"intensity", "1"}}) +
                              ent({{"classname", "light"}, {"origin", "300 0 0"}, {"intensity", "5"}}) +
                              ent({{"classname", "func_door"}, {"targetname", "door1"}}, doorBrush1);
    const std::string textB =
        "iwmap 4\n" + worldspawn(brickA + box(200, 0, 0, 216, 16, 16, "tm/brick")) +
        ent({{"classname", "light"}, {"targetname", "l1"}, {"origin", "64 0 0"}, {"intensity", "2"}}) +
        ent({{"classname", "light"}, {"origin", "500 0 0"}, {"color", "1 1 1"}}) +
        ent({{"classname", "func_door"}, {"targetname", "door1"}, {"speed", "10"}}, doorBrush1 + doorBrush2);

    const MapFile a = parseText(textA);
    const MapFile b = parseText(textB);
    const auto changes = diffMapEntities(a, b);
    CHECK(changes.size() == 7);

    Document doc;
    std::string err;
    CHECK(doc.loadFromText(textA, "", &err));
    const std::string before = doc.text();
    const size_t historyBefore = doc.history().size();

    const std::string patch = mapPatchJson(a, b);
    CHECK(applyMapPatch(doc, patch, &err));
    if (!err.empty()) std::printf("  apply error: %s\n", err.c_str());
    CHECK(diffMapEntities(doc.map(), b).empty());
    CHECK(formatMapDiff(diffMapEntities(doc.map(), b)).empty());
    CHECK(doc.history().size() == historyBefore + 1);  // one undo step for the whole patch

    doc.undo();
    CHECK(doc.text() == before);
}

TEST(map_diff_patch_fails_without_touching_document) {
    const std::string brick = box(0, 0, 0, 64, 64, 64, "tm/brick");
    const std::string light1 = ent({{"classname", "light"}, {"targetname", "l1"}, {"origin", "0 0 0"}});
    const std::string light2 = ent({{"classname", "light"}, {"origin", "300 0 0"}});
    const std::string textA = "iwmap 4\n" + worldspawn(brick) + light1 + light2;
    const std::string textB = "iwmap 4\n" + worldspawn(brick + box(200, 0, 0, 216, 16, 16, "tm/brick")) +
                              ent({{"classname", "light"}, {"targetname", "l1"}, {"origin", "64 0 0"}}) + light2;
    const std::string patch = mapPatchJson(parseText(textA), parseText(textB));

    // The target document has no l1, so the patch cannot resolve its "changed" entry.
    Document doc;
    std::string err;
    CHECK(doc.loadFromText("iwmap 4\n" + worldspawn(brick) + light2, "", &err));
    const std::string before = doc.text();
    const size_t historyBefore = doc.history().size();
    CHECK(!applyMapPatch(doc, patch, &err));
    CHECK(!err.empty());
    CHECK(doc.text() == before);
    CHECK(doc.history().size() == historyBefore);

    // Garbage input is rejected too.
    CHECK(!applyMapPatch(doc, "{\"format\": \"something-else\"}", &err));
    CHECK(!applyMapPatch(doc, "not json", &err));
}
