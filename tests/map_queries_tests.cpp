// Map queries: select by material / class / key, material usage, find & replace (one undo step, exact text),
// and the Check map problem list. Uses the IW8 sample room.
#include <algorithm>
#include <filesystem>
#include <string>

#include "document.h"
#include "map_queries.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

std::string samplePath() {
    fs::path p = fs::current_path();
    for (int up = 0; up < 6; ++up, p = p.parent_path()) {
        const fs::path cand = p / "tests" / "data" / "iw8_sample_room.map";
        if (fs::exists(cand)) return cand.string();
    }
    return {};
}

MapFile loadSample(std::string& textOut) {
    Id next = 1;
    const std::string path = samplePath();
    if (path.empty() || !readFile(path, textOut)) return {};
    ParseResult pr = parseMap(textOut, next);
    return pr.map ? *pr.map : MapFile{};
}

std::string replaceAllPlain(std::string s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

Id entityIdOf(const MapFile& m, const std::string& classname) {
    for (const auto& e : m.entities)
        if (e.classname() == classname) return e.id;
    return kNoId;
}

size_t primCountOf(const MapFile& m, Id entity) {
    for (const auto& e : m.entities)
        if (e.id == entity) {
            size_t n = 0;
            for (const auto& it : e.items) n += it.isPrimitive();
            return n;
        }
    return 0;
}

bool anyContains(const std::vector<std::string>& v, const std::string& needle) {
    for (const auto& s : v)
        if (s.find(needle) != std::string::npos) return true;
    return false;
}

const char* kFence = "tm2co/me_plaster_barrier_fence_wall";

}  // namespace

TEST(query_find_by_material_class_key_on_sample) {
    std::string text;
    const MapFile m = loadSample(text);
    CHECK(!m.entities.empty());
    if (m.entities.empty()) return;

    // Exact material: 23 brush faces in the sample, one SelRef per primitive.
    const auto exact = findByMaterial(m, kFence, false);
    const auto usage = materialUsage(m);
    size_t fenceFaces = 0, fencePrims = 0;
    for (const auto& u : usage)
        if (u.material == kFence) {
            fenceFaces = u.faces;
            fencePrims = u.primitives;
        }
    CHECK(fenceFaces == 23);
    CHECK(exact.size() == fencePrims);
    CHECK(!exact.empty());
    for (const auto& r : exact) CHECK(r.item != kNoId);

    // Substring is a superset; matching is ASCII case-insensitive.
    const auto sub = findByMaterial(m, "fence", true);
    CHECK(sub.size() >= exact.size());
    for (const auto& r : exact) CHECK(std::find(sub.begin(), sub.end(), r) != sub.end());
    CHECK(findByMaterial(m, "TM2CO/ME_PLASTER_BARRIER_FENCE_WALL", false).size() == exact.size());
    CHECK(findByMaterial(m, "no_such_material", true).empty());
    CHECK(findByMaterial(m, "", true).empty());

    // Class: the spawn is a point entity (item 0); trigger_multiple is a brush entity (one SelRef per brush).
    const Id spawn = entityIdOf(m, "mp_tdm_spawn");
    const auto spawns = findByClass(m, "mp_tdm_spawn");
    CHECK(spawn != kNoId && spawns.size() == 1);
    if (spawns.size() == 1) CHECK((spawns[0] == SelRef{spawn, kNoId}));
    const Id trig = entityIdOf(m, "trigger_multiple");
    const auto triggers = findByClass(m, "trigger_multiple");
    CHECK(trig != kNoId && triggers.size() == primCountOf(m, trig) && primCountOf(m, trig) > 0);
    for (const auto& r : triggers) CHECK(r.entity == trig);
    CHECK(findByClass(m, "no_such_class").empty());

    // Key: a key with a given value; key present; substring.
    const Id probe = entityIdOf(m, "script_origin");
    const auto byTarget = findByKeyValue(m, "targetname", "room_center", false);
    CHECK((byTarget.size() == 1 && byTarget[0] == SelRef{probe, kNoId}));
    // ramp (point), room_center (point), room_trigger (brush entity: one SelRef per brush).
    const auto present = findByKeyValue(m, "targetname", "", false);
    CHECK(present.size() == 2 + primCountOf(m, trig));
    const auto sub2 = findByKeyValue(m, "targetname", "room", true);
    CHECK(sub2.size() == 1 + primCountOf(m, trig));
    CHECK(findByKeyValue(m, "targetname", "ROOM_CENTER", false).empty());  // keys and values are case-sensitive
    const auto probes = findByKeyValue(m, "origin", "0 0 64", false);
    CHECK(probes.size() == 1 && probes[0].item == kNoId);
    CHECK(findByKeyValue(m, "", "x", false).empty());
}

TEST(query_material_usage_sorted) {
    std::string text;
    const MapFile m = loadSample(text);
    const auto usage = materialUsage(m);
    CHECK(!usage.empty());
    for (size_t i = 1; i < usage.size(); ++i) CHECK(usage[i - 1].faces >= usage[i].faces);
    size_t total = 0;
    for (const auto& u : usage) {
        CHECK(!u.material.empty());
        CHECK(u.faces >= u.primitives && u.primitives >= 1);
        total += u.faces;
    }
    // Every brush face and patch is counted once: the sample has 6 + 6 + 6 + ... faces in total (no empty materials).
    size_t brushFaces = 0;
    for (const auto& e : m.entities)
        for (const auto& it : e.items)
            if (const Primitive* p = it.prim()) brushFaces += p->kind == PrimKind::Brush ? p->faces.size() : 1;
    CHECK(total == brushFaces);
}

TEST(query_replace_material_one_undo_exact_text) {
    std::string text;
    const MapFile m = loadSample(text);
    if (m.entities.empty()) {
        CHECK(false);
        return;
    }
    Document d;
    CHECK(d.loadFromText(text, "sample.map", nullptr));

    size_t expected = 0;
    for (const auto& u : materialUsage(m))
        if (u.material == kFence) expected = u.faces;
    CHECK(expected == 23);

    // Exact replace across the whole map.
    const size_t n = replaceMaterial(d, kFence, "tm2co/me_plaster_test", false, false);
    CHECK(n == expected);
    CHECK(d.history().size() == 1);  // one undo step
    // Only the material tokens differ: the same text with the name swapped.
    CHECK(d.text() == replaceAllPlain(text, kFence, "tm2co/me_plaster_test"));
    CHECK(d.undo());
    CHECK(d.text() == text);
    CHECK(d.redo());
    CHECK(d.text() == replaceAllPlain(text, kFence, "tm2co/me_plaster_test"));
    CHECK(d.undo());

    // Substring replace: every occurrence of the part inside a material name.
    const size_t n2 = replaceMaterial(d, "me_curb", "me_curb_new", true, false);
    size_t curbFaces = 0;
    for (const auto& u : materialUsage(m))
        if (u.material.find("me_curb") != std::string::npos) curbFaces += u.faces;
    CHECK(n2 == curbFaces && curbFaces > 0);
    CHECK(d.text() == replaceAllPlain(text, "tm2co/me_curb_stone_01", "tm2co/me_curb_new_stone_01"));
    CHECK(d.undo());
    CHECK(d.text() == text);

    // Invalid or empty names and empty find change nothing and create no undo step.
    CHECK(replaceMaterial(d, kFence, "bad name", false, false) == 0);
    CHECK(replaceMaterial(d, kFence, "", false, false) == 0);
    CHECK(replaceMaterial(d, "", "x", false, false) == 0);
    CHECK(replaceMaterial(d, "no_such_material", "x", false, false) == 0);
    CHECK(d.text() == text);
    // The substring step replaced the redo branch: one transaction, now undone.
    CHECK(d.history().size() == 1 && d.undoPosition() == 0);
}

TEST(query_replace_material_selection_only) {
    std::string text;
    const MapFile m = loadSample(text);
    Document d;
    CHECK(d.loadFromText(text, "sample.map", nullptr));

    // Select one brush that uses the fence material.
    SelRef chosen{kNoId, kNoId};
    size_t facesInChosen = 0;
    for (const auto& e : m.entities)
        for (const auto& it : e.items)
            if (const Primitive* p = it.prim(); p && chosen.entity == kNoId && p->kind == PrimKind::Brush) {
                size_t k = 0;
                for (const auto& f : p->faces) k += f.material() == kFence;
                if (k > 0) {
                    chosen = {e.id, it.id};
                    facesInChosen = k;
                }
            }
    CHECK(chosen.entity != kNoId && facesInChosen > 0);
    d.select(chosen, false);
    const size_t n = replaceMaterial(d, kFence, "tm2co/me_plaster_test", false, true);
    CHECK(n == facesInChosen);
    const std::string edited = d.text();
    CHECK(edited != text);
    // The other fence faces are still there, and the edit touched only the chosen brush.
    size_t remaining = 0;
    for (size_t pos = 0; (pos = edited.find(kFence, pos)) != std::string::npos; pos += 1) ++remaining;
    CHECK(remaining == 23 - facesInChosen);
    CHECK(d.undo());
    CHECK(d.text() == text);
}

TEST(query_map_problems_flags_broken_map) {
    const char* kBroken =
        "iwmap 4\n"
        "// entity 0\n{\n\"classname\" \"worldspawn\"\n"
        "// brush 0\n{\n"
        " ( 0 0 0 ) ( 64 0 0 ) ( 0 64 0 ) caulk 64 64 0 0 0 0\n"
        " ( 0 0 0 ) ( 0 64 0 ) ( 0 0 64 ) caulk 64 64 0 0 0 0\n"
        " ( 0 0 0 ) ( 0 0 64 ) ( 64 0 0 ) caulk 64 64 0 0 0 0\n"
        "}\n}\n"
        // Targets a name nobody has, and shares its targetname with entity 2.
        "// entity 1\n{\n\"classname\" \"trigger_multiple\"\n\"targetname\" \"dup\"\n\"target\" \"nowhere\"\n\"origin\" \"0 0 0\"\n}\n"
        "// entity 2\n{\n\"classname\" \"script_origin\"\n\"targetname\" \"dup\"\n\"origin\" \"8 8 8\"\n}\n"
        // No classname.
        "// entity 3\n{\n\"targetname\" \"lonely\"\n\"origin\" \"0 0 0\"\n}\n"
        // Point entity without an origin.
        "// entity 4\n{\n\"classname\" \"mp_tdm_spawn\"\n}\n"
        // Degenerate patch (every control point at the same place).
        "// entity 5\n{\n\"classname\" \"script_brushmodel\"\n"
        "// brush 0\n{\n mesh\n {\n  toolFlags splitGeo;\n  caulk\n  lightmap_gray\n  2 2 16 8\n  (\n"
        "\tv 0 0 0 t 0 0 0 0\n\tv 0 0 0 t 0 0 0 0\n  )\n  (\n"
        "\tv 0 0 0 t 0 0 0 0\n\tv 0 0 0 t 0 0 0 0\n  )\n }\n}\n}\n";
    Id next = 1;
    ParseResult pr = parseMap(kBroken, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const auto problems = mapProblems(*pr.map);
    for (const auto& p : problems) std::printf("    problem: %s\n", p.c_str());
    CHECK(problems.size() == 6);
    CHECK(anyContains(problems, "duplicate targetname \"dup\""));
    CHECK(anyContains(problems, "targets \"nowhere\""));
    CHECK(anyContains(problems, "entity 0 (worldspawn) brush 0 has 3 valid planes"));
    CHECK(anyContains(problems, "entity 3 has no classname"));
    CHECK(anyContains(problems, "entity 4 (mp_tdm_spawn) is a point entity without a valid origin"));
    CHECK(anyContains(problems, "patch 0 is degenerate"));
}

TEST(query_map_problems_sample_is_clean) {
    std::string text;
    const MapFile m = loadSample(text);
    const auto problems = mapProblems(m);
    for (const auto& p : problems) std::printf("    problem: %s\n", p.c_str());
    CHECK(problems.empty());
}
