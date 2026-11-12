// Batch material replace (roadmap 12.9): planning over brushes, patches and material keys.
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "map_text.h"
#include "material_replace.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

// One brush face line with the given material.
std::string faceLine(const std::string& mat) { return " ( 0 0 0 ) ( 0 0 1 ) ( 0 1 0 ) " + mat + " 0 0 0 1 1\n"; }

// A brush block with one face per material.
std::string brush(const std::vector<std::string>& mats) {
    std::string s = "{\n";
    for (const std::string& m : mats) s += faceLine(m);
    return s + "}\n";
}

// A one-patch primitive using `mat`.
std::string patch(const std::string& mat) {
    return "{\n mesh\n {\n  " + mat +
           "\n  lightmap_gray\n  2 2 16 8\n  (\n\tv 0 0 0 t 0 0 0 0\n\tv 0 0 64 t 0 -64 0 4\n  )\n"
           "  (\n\tv 64 0 0 t 64 0 4 0\n\tv 64 0 64 t 64 -64 4 4\n  )\n }\n}\n";
}

// A key/value line.
std::string kv(const std::string& key, const std::string& value) { return "\"" + key + "\" \"" + value + "\"\n"; }

// An entity block holding the given lines and primitives.
std::string entity(const std::string& body) { return "{\n" + body + "}\n"; }

MapFile parse(const std::string& text) {
    Id next = 1;
    ParseResult pr = parseMap("iwmap 4\n" + text, next);
    CHECK(pr.map.has_value());
    return pr.map ? std::move(*pr.map) : MapFile{};
}

// Materials of every brush face in a primitive, in order.
std::vector<std::string> faceMaterials(const Primitive& p) {
    std::vector<std::string> out;
    for (const BrushFace& f : p.faces) out.push_back(f.material());
    return out;
}

const Primitive* firstPrim(const Entity& e) {
    for (const EntityItem& it : e.items)
        if (const Primitive* p = it.prim()) return p;
    return nullptr;
}

std::string lowerText(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

TEST(material_replace_brush_changes_only_matching_faces) {
    const MapFile map = parse(entity(kv("classname", "worldspawn") + brush({"tm/a", "tm/b", "tm/a"})));
    const std::string before = map.text();

    const MaterialReplaceResult r = planMaterialReplace(map, "tm/a", "tm/c");
    CHECK(r.edits.size() == 1);
    CHECK(r.faces == 2);
    CHECK(r.patches == 0);
    CHECK(r.keys.empty());
    if (r.edits.size() != 1) return;

    const Primitive& p = r.edits[0].prim;
    CHECK(p.kind == PrimKind::Brush);
    CHECK((faceMaterials(p) == std::vector<std::string>{"tm/c", "tm/b", "tm/c"}));
    // The edit's text has the new material and no longer the old one.
    CHECK(p.text().find("tm/c") != std::string::npos);
    CHECK(p.text().find("tm/a") == std::string::npos);
    CHECK(p.text().find("tm/b") != std::string::npos);
    // The map itself is untouched.
    CHECK(map.text() == before);
    CHECK(firstPrim(map.entities[0]) != nullptr && faceMaterials(*firstPrim(map.entities[0])) ==
                                                       (std::vector<std::string>{"tm/a", "tm/b", "tm/a"}));
}

TEST(material_replace_patch_changes) {
    const MapFile map = parse(entity(kv("classname", "worldspawn") + patch("tm/a") + brush({"tm/b"})));

    const MaterialReplaceResult r = planMaterialReplace(map, "tm/a", "tm/c");
    CHECK(r.edits.size() == 1);
    CHECK(r.patches == 1);
    CHECK(r.faces == 0);
    if (r.edits.size() != 1) return;

    const Primitive& p = r.edits[0].prim;
    CHECK(p.kind == PrimKind::Patch);
    CHECK(p.patchMaterial == "tm/c");
    CHECK(p.text().find("tm/c") != std::string::npos);
    CHECK(p.text().find("tm/a") == std::string::npos);
}

TEST(material_replace_is_case_insensitive) {
    const MapFile map = parse(entity(kv("classname", "worldspawn") + brush({"TM/A", "tm/a", "tm/b"})));

    const MaterialReplaceResult r = planMaterialReplace(map, "Tm/A", "tm/c");
    CHECK(r.faces == 2);
    CHECK(r.patches == 0);
    CHECK(r.edits.size() == 1);
    if (r.edits.size() != 1) return;
    CHECK((faceMaterials(r.edits[0].prim) == std::vector<std::string>{"tm/c", "tm/c", "tm/b"}));
}

TEST(material_replace_same_or_empty_gives_nothing) {
    const MapFile map = parse(entity(kv("classname", "worldspawn") + brush({"tm/a"}) + patch("tm/a") +
                                     entity(kv("classname", "misc_model") + kv("iw8r_material", "tm/a"))));

    const MaterialReplaceResult same = planMaterialReplace(map, "tm/a", "tm/a");
    CHECK(same.edits.empty());
    CHECK(same.faces == 0 && same.patches == 0 && same.keys.empty());

    const MaterialReplaceResult noFrom = planMaterialReplace(map, "", "tm/c");
    CHECK(noFrom.edits.empty() && noFrom.faces == 0 && noFrom.patches == 0 && noFrom.keys.empty());

    const MaterialReplaceResult noTo = planMaterialReplace(map, "tm/a", "");
    CHECK(noTo.edits.empty() && noTo.faces == 0 && noTo.patches == 0 && noTo.keys.empty());
}

TEST(material_replace_unknown_material_gives_nothing) {
    const MapFile map = parse(entity(kv("classname", "worldspawn") + brush({"tm/a"}) + patch("tm/a") +
                                     entity(kv("classname", "misc_model") + kv("iw8r_material", "tm/a"))));

    const MaterialReplaceResult r = planMaterialReplace(map, "tm/missing", "tm/c");
    CHECK(r.edits.empty());
    CHECK(r.faces == 0);
    CHECK(r.patches == 0);
    CHECK(r.keys.empty());
}

TEST(material_replace_counts_across_primitives) {
    const MapFile map = parse(entity(kv("classname", "worldspawn") + brush({"tm/a", "tm/a", "tm/b"}) + brush({"tm/b"}) +
                                     patch("tm/a")));

    const MaterialReplaceResult r = planMaterialReplace(map, "tm/a", "tm/c");
    CHECK(r.edits.size() == 2);  // the first brush and the patch; the second brush has no tm/a
    CHECK(r.faces == 2);
    CHECK(r.patches == 1);
}

TEST(material_replace_lists_entity_keys) {
    const MapFile map = parse(entity(kv("classname", "worldspawn")) +
                              entity(kv("classname", "misc_model") + kv("model", "tm/a") + kv("iw8r_material", "tm/a") +
                                     kv("iw8r_mat:0", "TM/a") + kv("iw8r_mat:1", "tm/b") + kv("other", "tm/a")));
    const Id modelId = map.entities[1].id;

    const MaterialReplaceResult r = planMaterialReplace(map, "tm/a", "tm/c");
    CHECK(r.edits.empty());  // keys are listed, not edited
    CHECK(r.faces == 0 && r.patches == 0);
    CHECK(r.keys.size() == 2);
    if (r.keys.size() != 2) return;
    CHECK(r.keys[0].first == modelId && r.keys[0].second == "iw8r_material");
    CHECK(r.keys[1].first == modelId && r.keys[1].second == "iw8r_mat:0");
}

TEST(material_replace_sample_room_counts) {
    std::string text;
    CHECK(readFile("tests/data/iw8_sample_room.map", text));
    Id next = 1;
    const ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const MapFile& map = *pr.map;

    const std::vector<std::string> mats = materialsInMap(map);
    CHECK(!mats.empty());
    if (mats.empty()) return;
    const std::string from = mats.front();

    // Independent count of faces and patches that use `from`.
    size_t wantFaces = 0, wantPatches = 0;
    for (const Entity& e : map.entities)
        for (const EntityItem& it : e.items) {
            const Primitive* p = it.prim();
            if (!p) continue;
            if (p->kind == PrimKind::Brush)
                for (const BrushFace& f : p->faces) wantFaces += lowerText(f.material()) == lowerText(from);
            else if (p->kind == PrimKind::Patch)
                wantPatches += lowerText(p->patchMaterial) == lowerText(from);
        }

    const MaterialReplaceResult r = planMaterialReplace(map, from, "iw8r_test_replacement");
    CHECK(r.faces == wantFaces);
    CHECK(r.patches == wantPatches);
    CHECK(wantFaces + wantPatches > 0);
    for (const MaterialReplaceEdit& ed : r.edits) {
        CHECK(ed.prim.dirty);
        if (ed.prim.kind == PrimKind::Brush)
            for (const BrushFace& f : ed.prim.faces) CHECK(lowerText(f.material()) != lowerText(from));
        else
            CHECK(ed.prim.patchMaterial == "iw8r_test_replacement");
    }
}

TEST(material_replace_materials_in_map_is_sorted_unique) {
    const MapFile map = parse(entity(kv("classname", "worldspawn") + brush({"TM/B", "tm/a", "tm/b"}) + patch("tm/c")) +
                              entity(kv("classname", "misc_model") + kv("iw8r_material", "Tm/D")));

    const std::vector<std::string> mats = materialsInMap(map);
    const std::vector<std::string> want = {"tm/a", "tm/b", "tm/c", "tm/d"};
    CHECK(mats == want);
    CHECK(std::is_sorted(mats.begin(), mats.end()));
}
