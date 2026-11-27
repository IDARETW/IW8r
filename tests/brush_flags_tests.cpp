// Brush body flags (src/core/brush_flags.*): contents detail / nonColliding / weaponClip lines.
#include <string>
#include <vector>

#include "brush_flags.h"
#include "brush_ops.h"
#include "iw8_contents.h"
#include "map_text.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

const AABB kFlagBox{{0, 0, 0}, {64, 64, 64}};

// Brush primitive of a box with `mat` on every face, with `eol` line endings.
Primitive boxBrush(const std::string& mat, const std::string& eol = "\n") {
    auto prim = makeBrushPrimitive(makeBox(kFlagBox, " " + mat + " 128 128 0 0 0 0"), eol);
    if (!prim) return {};
    return *prim;
}

// Parses a one-entity map holding `brush` and returns its first primitive.
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

// Line texts of a primitive, without terminators.
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

}  // namespace

TEST(brush_flags_plain_brush_has_no_flags) {
    const Primitive p = boxBrush("tm2co/me_brick_01");
    const BrushFlags f = readBrushFlags(p);
    CHECK(!f.detail);
    CHECK(!f.nonColliding);
    CHECK(!f.weaponClip);
    // Writing the same flags changes nothing: the same primitive comes back byte for byte.
    auto same = withBrushFlags(p, BrushFlags{}, "\n");
    CHECK(same.has_value());
    if (same) CHECK(same->text() == p.text());
}

TEST(brush_flags_detail_round_trip) {
    const Primitive plain = boxBrush("tm2co/me_brick_01");
    auto detail = withBrushFlags(plain, BrushFlags{.detail = true}, "\n");
    CHECK(detail.has_value());
    if (!detail) return;
    CHECK(readBrushFlags(*detail).detail);
    CHECK(!readBrushFlags(*detail).nonColliding);
    CHECK(countLines(*detail, " contents detail;") == 1);
    CHECK(brushIsDetail(*detail));
    // Clearing it again restores the original text exactly.
    auto cleared = withBrushFlags(*detail, BrushFlags{}, "\n");
    CHECK(cleared.has_value());
    if (cleared) {
        CHECK(!readBrushFlags(*cleared).detail);
        CHECK(!brushIsDetail(*cleared));
        CHECK(cleared->text() == plain.text());
    }
}

TEST(brush_flags_set_and_clear_detail_on_parsed_text) {
    const Primitive base = parsedBrush("// brush 1\n{\n ( 0 0 0 ) ( 0 0 1 ) ( 0 1 0 ) tm2co/me_brick_01 0 0 0 0 0 0\n}\n");
    CHECK(base.kind == PrimKind::Brush);
    CHECK(base.faces.size() == 1);
    auto set = withBrushFlags(base, BrushFlags{.detail = true}, "\n");
    CHECK(set.has_value());
    if (!set) return;
    // Inserted directly after the opening brace, face lines untouched.
    CHECK(texts(*set) == std::vector<std::string>({"// brush 1", "{", " contents detail;",
                                                   " ( 0 0 0 ) ( 0 0 1 ) ( 0 1 0 ) tm2co/me_brick_01 0 0 0 0 0 0", "}"}));
    CHECK(set->faces.size() == 1);
    CHECK(set->faces[0].line == 3);  // reparsed: the face now sits one line lower
    CHECK(set->dirty);
    auto clear = withBrushFlags(*set, BrushFlags{}, "\n");
    CHECK(clear.has_value());
    if (clear) CHECK(clear->text() == base.text());
}

TEST(brush_flags_set_non_colliding) {
    const Primitive plain = boxBrush("tm2co/me_brick_01");
    CHECK(brushContents(plain).collides);
    auto nc = withBrushFlags(plain, BrushFlags{.nonColliding = true}, "\n");
    CHECK(nc.has_value());
    if (!nc) return;
    CHECK(readBrushFlags(*nc).nonColliding);
    CHECK(!readBrushFlags(*nc).detail);
    CHECK(countLines(*nc, " contents nonColliding;") == 1);
    const BrushContents bc = brushContents(*nc);
    CHECK(!bc.collides);
    CHECK(bc.contents == 0);
    // Toggling back restores collision and the original text.
    auto back = withBrushFlags(*nc, BrushFlags{}, "\n");
    CHECK(back.has_value());
    if (back) {
        CHECK(brushContents(*back).collides);
        CHECK(back->text() == plain.text());
    }
}

TEST(brush_flags_weapon_clip_contents_bit) {
    const Primitive plain = boxBrush("tm2co/me_brick_01");
    auto wc = withBrushFlags(plain, BrushFlags{.weaponClip = true}, "\n");
    CHECK(wc.has_value());
    if (!wc) return;
    CHECK(readBrushFlags(*wc).weaponClip);
    CHECK(countLines(*wc, " contents weaponClip;") == 1);
    CHECK((brushContents(*wc).contents & kIw3ContentsClipShot) != 0);
    CHECK(brushContents(*wc).collides);
}

TEST(brush_flags_unknown_lines_preserved) {
    const Primitive base = parsedBrush(
        "// brush 7\n{\n contents foo;\n toolFlags splitGeo;\n contents detail;\n"
        " ( 0 0 0 ) ( 0 0 1 ) ( 0 1 0 ) tm2co/me_brick_01 0 0 0 0 0 0 lightmap_gray 16384\n}\n");
    CHECK(base.kind == PrimKind::Brush);
    // Drop detail, add nonColliding: the unknown lines keep their text, order and terminators.
    auto out = withBrushFlags(base, BrushFlags{.nonColliding = true}, "\n");
    CHECK(out.has_value());
    if (!out) return;
    CHECK(readBrushFlags(*out).nonColliding);
    CHECK(!readBrushFlags(*out).detail);
    CHECK(texts(*out) == std::vector<std::string>({"// brush 7", "{", " contents nonColliding;", " contents foo;",
                                                   " toolFlags splitGeo;",
                                                   " ( 0 0 0 ) ( 0 0 1 ) ( 0 1 0 ) tm2co/me_brick_01 0 0 0 0 0 0 lightmap_gray 16384",
                                                   "}"}));
    CHECK(out->faces.size() == 1);
    CHECK(out->faces[0].material() == "tm2co/me_brick_01");
    CHECK(out->faces[0].line == 5);
    // Both flags: detail and nonColliding stay together with the unknown lines in place.
    auto both = withBrushFlags(base, BrushFlags{.detail = true, .nonColliding = true}, "\n");
    CHECK(both.has_value());
    if (both) {
        CHECK(readBrushFlags(*both).detail && readBrushFlags(*both).nonColliding);
        CHECK(countLines(*both, " contents foo;") == 1);
        CHECK(countLines(*both, " toolFlags splitGeo;") == 1);
        CHECK(!brushContents(*both).collides);
    }
}

TEST(brush_flags_crlf_eol_used_for_new_line) {
    const Primitive plain = boxBrush("tm2co/me_brick_01", "\r\n");
    auto det = withBrushFlags(plain, BrushFlags{.detail = true}, "\r\n");
    CHECK(det.has_value());
    if (!det) return;
    bool found = false;
    for (const TextLine& l : det->lines)
        if (l.text == " contents detail;") found = l.eol == "\r\n";
    CHECK(found);
    auto cleared = withBrushFlags(*det, BrushFlags{}, "\r\n");
    CHECK(cleared.has_value());
    if (cleared) CHECK(cleared->text() == plain.text());
}

TEST(brush_flags_patch_is_rejected) {
    Primitive patch;
    patch.kind = PrimKind::Patch;
    CHECK(!withBrushFlags(patch, BrushFlags{.detail = true}, "\n").has_value());
}

TEST(brush_flags_directive_words_case_insensitive) {
    const Primitive p = parsedBrush("{\n CONTENTS NONCOLLIDING ;\n ( 0 0 0 ) ( 0 0 1 ) ( 0 1 0 ) tm2co/me_brick_01 0 0 0 0 0 0\n}\n");
    CHECK(readBrushFlags(p).nonColliding);
    CHECK(!brushContents(p).collides);
    // Clearing removes the mixed-case line too.
    auto cleared = withBrushFlags(p, BrushFlags{}, "\n");
    CHECK(cleared.has_value());
    if (cleared) {
        CHECK(!readBrushFlags(*cleared).nonColliding);
        CHECK(brushContents(*cleared).collides);
    }
}
