// Brush toolFlags lines (src/core/tool_flags.*): read, write, round trip, selection edit and undo.
#include <string>
#include <vector>

#include "brush_ops.h"
#include "document.h"
#include "map_text.h"
#include "test_harness.h"
#include "tool_flags.h"

using namespace iw8r;

namespace {

const AABB kFlagBox{{0, 0, 0}, {64, 64, 64}};

// One face line, without terminator.
const char* kFaceLine = " ( 0 0 0 ) ( 0 0 1 ) ( 0 1 0 ) tm2co/me_brick_01 0 0 0 0 0 0";

Primitive boxBrush(const std::string& eol = "\n") {
    auto prim = makeBrushPrimitive(makeBox(kFlagBox, " tm2co/me_brick_01 128 128 0 0 0 0"), eol);
    if (!prim) return {};
    return *prim;
}

// First primitive of worldspawn in a one-entity map holding `brush` (LF line endings).
Primitive parsedBrush(const std::string& brush) {
    const std::string text = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n" + brush + "}\n";
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return {};
    for (const EntityItem& item : pr.map->entities[0].items)
        if (const Primitive* p = item.prim()) return *p;
    CHECK(false);
    return {};
}

std::vector<std::string> texts(const Primitive& p) {
    std::vector<std::string> out;
    for (const TextLine& l : p.lines) out.push_back(l.text);
    return out;
}

int countLines(const Primitive& p, const std::string& text) {
    int n = 0;
    for (const TextLine& l : p.lines)
        if (l.text == text) ++n;
    return n;
}

// Index of the line whose text is `text`; npos when absent.
size_t indexOfLine(const Primitive& p, const std::string& text) {
    for (size_t i = 0; i < p.lines.size(); ++i)
        if (p.lines[i].text == text) return i;
    return std::string::npos;
}

const Primitive* primById(const Document& doc, Id item) {
    for (const Entity& e : doc.map().entities)
        for (const EntityItem& it : e.items)
            if (it.id == item) return it.prim();
    return nullptr;
}

std::vector<Id> brushIdsOf(const Document& doc) {
    std::vector<Id> out;
    for (const EntityItem& it : doc.map().entities[0].items)
        if (it.prim()) out.push_back(it.id);
    return out;
}

}  // namespace

TEST(tool_flags_read_names_in_written_order) {
    const Primitive p = parsedBrush(
        "// brush 3\n{\n contents detail;\n toolFlags splitGeo;\n TOOLFLAGS zeta;\n" + std::string(kFaceLine) + "\n}\n");
    CHECK(p.kind == PrimKind::Brush);
    CHECK(readToolFlags(p) == std::vector<std::string>({"splitGeo", "zeta"}));
    CHECK(readToolFlags(boxBrush()).empty());
}

TEST(tool_flags_round_trip_is_byte_exact_lf) {
    // Odd spacing, an unknown contents word and a layer line stay exactly as written.
    const Primitive p = parsedBrush(
        "// brush 4\n{\n layer \"high town\"\n contents foo;\n  toolFlags   splitGeo ;\n toolFlags customFoo;\n" +
        std::string(kFaceLine) + "\n}\n");
    CHECK(readToolFlags(p) == std::vector<std::string>({"splitGeo", "customFoo"}));
    auto same = withToolFlags(p, readToolFlags(p), "\n");
    CHECK(same.has_value());
    if (same) CHECK(same->text() == p.text());
}

TEST(tool_flags_round_trip_is_byte_exact_crlf) {
    const std::string text =
        "iwmap 4\r\n{\r\n\"classname\" \"worldspawn\"\r\n// brush 5\r\n{\r\n toolFlags splitGeo;\r\n" +
        std::string(kFaceLine) + "\r\n}\r\n}\r\n";
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const Primitive* p = nullptr;
    for (const EntityItem& item : pr.map->entities[0].items)
        if (!p) p = item.prim();
    CHECK(p != nullptr);
    if (!p) return;
    auto out = withToolFlags(*p, readToolFlags(*p), "\r\n");
    CHECK(out.has_value());
    if (out) CHECK(out->text() == p->text());
}

TEST(tool_flags_add_and_remove) {
    const Primitive plain = boxBrush();
    CHECK(readToolFlags(plain).empty());

    auto added = withToolFlags(plain, {"splitGeo"}, "\n");
    CHECK(added.has_value());
    if (!added) return;
    CHECK(readToolFlags(*added) == std::vector<std::string>({"splitGeo"}));
    CHECK(countLines(*added, " toolFlags splitGeo;") == 1);
    // Inserted directly after the opening brace.
    const size_t open = indexOfLine(*added, "{");
    CHECK(open != std::string::npos);
    if (open != std::string::npos) CHECK(added->lines[open + 1].text == " toolFlags splitGeo;");
    CHECK(added->dirty);

    auto removed = withToolFlags(*added, {}, "\n");
    CHECK(removed.has_value());
    if (removed) {
        CHECK(readToolFlags(*removed).empty());
        CHECK(removed->text() == plain.text());
    }
}

TEST(tool_flags_multiple_names_follow_request_order) {
    const Primitive plain = boxBrush();
    auto two = withToolFlags(plain, {"zeta", "splitGeo"}, "\n");
    CHECK(two.has_value());
    if (!two) return;
    CHECK(readToolFlags(*two) == std::vector<std::string>({"zeta", "splitGeo"}));

    // Reordering reuses the existing lines.
    auto swapped = withToolFlags(*two, {"splitGeo", "zeta"}, "\n");
    CHECK(swapped.has_value());
    if (swapped) CHECK(readToolFlags(*swapped) == std::vector<std::string>({"splitGeo", "zeta"}));
}

TEST(tool_flags_unknown_names_preserved) {
    const Primitive base = parsedBrush(
        "// brush 6\n{\n  toolFlags   customFoo ;\n toolFlags splitGeo;\n" + std::string(kFaceLine) + "\n}\n");
    // Drop splitGeo, keep the unknown name: its line keeps its original bytes.
    auto out = withToolFlags(base, {"customFoo"}, "\n");
    CHECK(out.has_value());
    if (!out) return;
    CHECK(readToolFlags(*out) == std::vector<std::string>({"customFoo"}));
    CHECK(texts(*out) == std::vector<std::string>({"// brush 6", "{", "  toolFlags   customFoo ;", kFaceLine, "}"}));

    // Add a known name next to it: the unknown line is still byte-identical.
    auto more = withToolFlags(*out, {"customFoo", "splitGeo"}, "\n");
    CHECK(more.has_value());
    if (more) {
        CHECK(readToolFlags(*more) == std::vector<std::string>({"customFoo", "splitGeo"}));
        CHECK(countLines(*more, "  toolFlags   customFoo ;") == 1);
        CHECK(countLines(*more, " toolFlags splitGeo;") == 1);
    }
}

TEST(tool_flags_block_goes_after_layer_line) {
    const Primitive base = parsedBrush("{\n layer \"high town\"\n" + std::string(kFaceLine) + "\n}\n");
    auto out = withToolFlags(base, {"splitGeo"}, "\n");
    CHECK(out.has_value());
    if (!out) return;
    CHECK(texts(*out) == std::vector<std::string>({"{", " layer \"high town\"", " toolFlags splitGeo;", kFaceLine, "}"}));
}

TEST(tool_flags_duplicates_collapse) {
    const Primitive base = parsedBrush("{\n toolFlags splitGeo;\n toolFlags splitGeo;\n" + std::string(kFaceLine) + "\n}\n");
    CHECK(readToolFlags(base).size() == 2);
    auto out = withToolFlags(base, {"splitGeo", "splitGeo"}, "\n");
    CHECK(out.has_value());
    if (out) {
        CHECK(countLines(*out, " toolFlags splitGeo;") == 1);
        CHECK(readToolFlags(*out) == std::vector<std::string>({"splitGeo"}));
    }
}

TEST(tool_flags_rejects_patch_and_bad_names) {
    Primitive patch;
    patch.kind = PrimKind::Patch;
    CHECK(!withToolFlags(patch, {"splitGeo"}, "\n").has_value());

    const Primitive plain = boxBrush();
    CHECK(!withToolFlags(plain, {"bad;name"}, "\n").has_value());
    CHECK(!withToolFlags(plain, {""}, "\n").has_value());
}

TEST(tool_flags_known_list_has_evidence) {
    const auto& defs = knownToolFlags();
    CHECK(!defs.empty());
    bool hasSplit = false;
    for (const ToolFlagDef& d : defs) {
        CHECK(!d.name.empty());
        CHECK(!d.help.empty());
        CHECK(!d.source.empty());
        if (d.name == "splitGeo") hasSplit = true;
    }
    CHECK(hasSplit);
}

TEST(tool_flags_selection_one_undo_step) {
    const std::string text =
        "iwmap 4\n{\n\"classname\" \"worldspawn\"\n"
        "// brush 0\n{\n" + std::string(kFaceLine) + "\n}\n"
        "// brush 1\n{\n" + std::string(kFaceLine) + "\n}\n}\n";
    Document doc;
    CHECK(doc.loadFromText(text, "tool_flags_test.map"));
    const Id worldId = doc.map().entities[0].id;
    const std::vector<Id> brushes = brushIdsOf(doc);
    CHECK(brushes.size() == 2);
    if (brushes.size() != 2) return;

    const std::string before = doc.text();
    const size_t undoBefore = doc.undoPosition();

    // Off on brushes without the flag changes nothing and adds no undo step.
    doc.select(SelRef{worldId, brushes[0]}, false);
    CHECK(!setToolFlagOnSelection(doc, "splitGeo", false));
    CHECK(doc.undoPosition() == undoBefore);

    // On for both selected brushes: one undo step, selection kept.
    doc.select(SelRef{worldId, brushes[1]}, true);
    CHECK(doc.selection().size() == 2);
    CHECK(setToolFlagOnSelection(doc, "splitGeo", true));
    CHECK(doc.undoPosition() == undoBefore + 1);
    CHECK(doc.selection().size() == 2);
    for (Id b : brushes) {
        const Primitive* p = primById(doc, b);
        CHECK(p != nullptr);
        if (p) CHECK(readToolFlags(*p) == std::vector<std::string>({"splitGeo"}));
    }
    CHECK(doc.text() != before);

    // One undo restores the text byte for byte.
    CHECK(doc.undo());
    CHECK(doc.undoPosition() == undoBefore);
    CHECK(doc.text() == before);
}

TEST(tool_flags_selection_off_removes_only_selected) {
    const std::string text =
        "iwmap 4\n{\n\"classname\" \"worldspawn\"\n"
        "// brush 0\n{\n toolFlags splitGeo;\n" + std::string(kFaceLine) + "\n}\n"
        "// brush 1\n{\n toolFlags splitGeo;\n" + std::string(kFaceLine) + "\n}\n}\n";
    Document doc;
    CHECK(doc.loadFromText(text, "tool_flags_test.map"));
    const Id worldId = doc.map().entities[0].id;
    const std::vector<Id> brushes = brushIdsOf(doc);
    CHECK(brushes.size() == 2);
    if (brushes.size() != 2) return;

    doc.select(SelRef{worldId, brushes[0]}, false);
    const std::string before = doc.text();
    CHECK(setToolFlagOnSelection(doc, "splitGeo", false));
    const Primitive* first = primById(doc, brushes[0]);
    const Primitive* second = primById(doc, brushes[1]);
    CHECK(first != nullptr && readToolFlags(*first).empty());
    CHECK(second != nullptr && readToolFlags(*second) == std::vector<std::string>({"splitGeo"}));
    CHECK(doc.undo());
    CHECK(doc.text() == before);
}
