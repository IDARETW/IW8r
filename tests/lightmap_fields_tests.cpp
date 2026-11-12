// Tests for src/core/lightmap_fields.h: lightmap face fields, patch control-point locks and the Surface panel edit.
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "../src/core/brush_ops.h"
#include "../src/core/document.h"
#include "../src/core/lightmap_fields.h"
#include "../src/core/map_text.h"
#include "../src/core/patch_ops.h"
#include "../src/core/tex_projection.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }

// <repo>/tests/data/iw8_sample_room.map, found by walking up from the working directory.
std::string samplePath() {
    fs::path p = fs::current_path();
    for (int up = 0; up < 6; ++up, p = p.parent_path()) {
        const fs::path cand = p / "tests" / "data" / "iw8_sample_room.map";
        if (fs::exists(cand)) return cand.string();
    }
    return {};
}

bool loadSample(std::string& text) { return !samplePath().empty() && readFile(samplePath(), text); }

bool sameLm(const LightmapParams& a, const LightmapParams& b) {
    return a.material == b.material && near(a.sx, b.sx) && near(a.sy, b.sy) && near(a.ox, b.ox) && near(a.oy, b.oy) &&
           near(a.rot, b.rot);
}

// Splits the suffix of a face into tokens (the same whitespace split the reader uses).
std::vector<std::string> tokensOf(const std::string& s) { return splitTokens(s); }

Primitive boxWithSuffix(const std::string& suffix) {
    return *makeBrushPrimitive(makeBox(AABB{{-64, -64, -64}, {64, 64, 64}}, suffix), "\n");
}

}  // namespace

TEST(lightmap_fields_parse_sample) {
    std::string text;
    CHECK(loadSample(text));
    if (text.empty()) return;
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;

    size_t brushFaces = 0, patches = 0;
    for (const Entity& e : pr.map->entities)
        for (const EntityItem& it : e.items) {
            const Primitive* p = it.prim();
            if (!p) continue;
            if (p->kind == PrimKind::Patch) {
                ++patches;
                CHECK(!lightmapParamsOfFace(*p, 0).has_value());  // patches carry no face lightmap fields
                continue;
            }
            for (size_t i = 0; i < p->faces.size(); ++i) {
                const auto lp = lightmapParamsOfFace(*p, i);
                CHECK(lp.has_value());
                if (!lp) continue;
                ++brushFaces;
                CHECK(lp->material == "lightmap_gray");
                CHECK(near(lp->sx, 16384) && near(lp->sy, 16384));
                CHECK(near(lp->ox, 0) && near(lp->oy, 0) && near(lp->rot, 0));
            }
        }
    CHECK(brushFaces == 42);  // the sample has 42 brush faces with `lightmap_gray 16384 16384 0 0 0 0`
    CHECK(patches >= 1);

    // First face of the first brush, against the literal line: colour texture and lightmap fields side by side.
    const Primitive* first = nullptr;
    for (const EntityItem& it : pr.map->entities[0].items)
        if (it.prim() && it.prim()->kind == PrimKind::Brush) {
            first = it.prim();
            break;
        }
    CHECK(first != nullptr);
    if (first) {
        const auto tp = texParamsOfFace(*first, 0);
        CHECK(tp.has_value());
        if (tp) CHECK(near(tp->width, 128) && near(tp->height, 128));
        CHECK(first->faces[0].material() == "tm2co/me_curb_stone_01");
        CHECK(first->faces[0].suffix == " tm2co/me_curb_stone_01 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0");
    }
}

TEST(lightmap_fields_round_trip) {
    std::string text;
    CHECK(loadSample(text));
    if (text.empty()) return;
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    if (!pr.map) return;
    size_t checked = 0;
    for (const Entity& e : pr.map->entities)
        for (const EntityItem& it : e.items) {
            const Primitive* p = it.prim();
            if (!p || p->kind != PrimKind::Brush) continue;
            for (size_t i = 0; i < p->faces.size(); ++i) {
                const auto lp = lightmapParamsOfFace(*p, i);
                if (!lp) continue;
                const auto out = withLightmapParams(*p, i, *lp);
                CHECK(out.has_value());
                if (!out) continue;
                CHECK(out->text() == p->text());  // writing back what was read changes nothing
                ++checked;
            }
        }
    CHECK(checked == 42);

    // A value written and read back keeps its text (16384.5 is in the corpus as well).
    Primitive p = boxWithSuffix(" caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0");
    LightmapParams lp;
    lp.sx = 16384.5;
    lp.sy = 16383.982;
    const auto w = withLightmapParams(p, 0, lp);
    CHECK(w.has_value());
    if (w) {
        const auto back = lightmapParamsOfFace(*w, 0);
        CHECK(back.has_value());
        if (back) CHECK(sameLm(*back, lp));
        CHECK(tokensOf(w->faces[0].suffix)[8] == "16384.5");
    }
}

TEST(lightmap_fields_change_only_lightmap_tokens) {
    std::string text;
    CHECK(loadSample(text));
    if (text.empty()) return;
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    if (!pr.map) return;
    const Primitive* first = nullptr;
    for (const EntityItem& it : pr.map->entities[0].items)
        if (it.prim() && it.prim()->kind == PrimKind::Brush) {
            first = it.prim();
            break;
        }
    CHECK(first != nullptr);
    if (!first) return;

    const BrushFace& oldFace = first->faces[0];
    const std::string oldLine = first->lines[oldFace.line].text;
    LightmapParams lp;
    lp.material = "lightmap_blue";
    lp.sx = 1024;
    lp.sy = 2048;
    lp.ox = 3;
    lp.oy = -4;
    lp.rot = 90;
    const auto out = withLightmapParams(*first, 0, lp);
    CHECK(out.has_value());
    if (!out) return;

    // Exact text: only the LMMAT and L1..L5 tokens changed, L6 (the trailing 0) and everything else is kept.
    const std::string& newFaceSuffix = out->faces[0].suffix;
    CHECK(oldFace.suffix == " tm2co/me_curb_stone_01 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0");
    CHECK(newFaceSuffix == " tm2co/me_curb_stone_01 128 128 0 0 0 0 lightmap_blue 1024 2048 3 -4 90 0");
    CHECK(out->lines[oldFace.line].text == oldLine.substr(0, oldLine.size() - oldFace.suffix.size()) + newFaceSuffix);
    CHECK(out->faces[0].p[0] == oldFace.p[0] && out->faces[0].p[2] == oldFace.p[2]);  // plane points untouched

    // Token diff: exactly indices 7..12 differ, with the same token count.
    const auto a = tokensOf(oldFace.suffix), b = tokensOf(newFaceSuffix);
    CHECK(a.size() == b.size());
    if (a.size() == b.size()) {
        std::vector<size_t> diff;
        for (size_t k = 0; k < a.size(); ++k)
            if (a[k] != b[k]) diff.push_back(k);
        CHECK((diff == std::vector<size_t>{7, 8, 9, 10, 11, 12}));
    }

    // The colour texture fields read the same before and after.
    const auto tpOld = texParamsOfFace(*first, 0), tpNew = texParamsOfFace(*out, 0);
    CHECK(tpOld.has_value() && tpNew.has_value());
    if (tpOld && tpNew) CHECK(near(tpOld->width, tpNew->width) && near(tpOld->rot, tpNew->rot) && near(tpOld->xoff, tpNew->xoff));

    // Other faces are untouched.
    CHECK(out->faces[1].suffix == first->faces[1].suffix);
}

TEST(lightmap_fields_keeps_unknown_tokens) {
    // L6 with an exponent and a smoothing tail: both survive a change to the other lightmap fields.
    Primitive p = boxWithSuffix(" caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 1.1444266e-005 smoothing smoothing_smooth");
    const auto before = lightmapParamsOfFace(p, 2);
    CHECK(before.has_value());
    LightmapParams lp;
    lp.sx = 512;
    const auto out = withLightmapParams(p, 2, lp);
    CHECK(out.has_value());
    if (!out) return;
    CHECK(out->faces[2].suffix == " caulk 64 64 0 0 0 0 lightmap_gray 512 16384 0 0 0 1.1444266e-005 smoothing smoothing_smooth");
    CHECK(out->faces[0].suffix == p.faces[0].suffix);
}

TEST(lightmap_fields_whole_brush_apply) {
    Primitive p = boxWithSuffix(" caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0");
    LightmapParams lp;
    lp.material = "lightmap_red";
    lp.sx = 1024;
    lp.sy = 1024;
    lp.rot = 45;
    const auto out = withLightmapParams(p, kAllFaces, lp);
    CHECK(out.has_value());
    if (!out) return;
    CHECK(out->faces.size() == p.faces.size());
    for (size_t i = 0; i < out->faces.size(); ++i) {
        const auto got = lightmapParamsOfFace(*out, i);
        CHECK(got.has_value());
        if (got) CHECK(sameLm(*got, lp));
        const auto tp = texParamsOfFace(*out, i);
        CHECK(tp.has_value() && near(tp->width, 64) && near(tp->height, 64));  // colour fields kept
    }

    // Per-face write leaves the other faces alone; bad indices and fieldless faces are refused.
    const auto one = withLightmapParams(p, 3, lp);
    CHECK(one.has_value());
    if (one) {
        CHECK(lightmapParamsOfFace(*one, 3).has_value() && sameLm(*lightmapParamsOfFace(*one, 3), lp));
        CHECK(one->faces[0].suffix == p.faces[0].suffix);
    }
    CHECK(!withLightmapParams(p, 99, lp).has_value());
    Primitive bare = boxWithSuffix(" caulk 64 64 0 0 0 0");
    CHECK(!lightmapParamsOfFace(bare, 0).has_value());
    CHECK(!withLightmapParams(bare, 0, lp).has_value());
    CHECK(!withLightmapParams(bare, kAllFaces, lp).has_value());
    LightmapParams noMat = lp;
    noMat.material = "";
    CHECK(!withLightmapParams(p, 0, noMat).has_value());
}

TEST(lightmap_fields_undo_selection) {
    std::string text;
    CHECK(loadSample(text));
    if (text.empty()) return;
    Document d;
    CHECK(d.loadFromText(text, "room.map", nullptr));

    // First brush of worldspawn with at least three faces.
    Id ent = kNoId, item = kNoId;
    for (const EntityItem& it : d.map().entities[0].items)
        if (it.prim() && it.prim()->kind == PrimKind::Brush && it.prim()->faces.size() >= 3) {
            ent = d.map().entities[0].id;
            item = it.id;
            break;
        }
    CHECK(item != kNoId);
    if (item == kNoId) return;

    LightmapParams lp;
    lp.material = "lightmap_green";
    lp.sx = 2048;
    lp.sy = 2048;

    CHECK(!setLightmapOnSelection(d, lp));  // nothing selected
    d.selectFace(FaceRef{ent, item, 0}, false);
    d.selectFace(FaceRef{ent, item, 2}, true);
    const std::string before = d.text();
    CHECK(setLightmapOnSelection(d, lp));

    const Primitive* prim = d.findItem(ent, item)->prim();
    CHECK(prim != nullptr);
    if (prim) {
        CHECK(sameLm(*lightmapParamsOfFace(*prim, 0), lp));
        CHECK(sameLm(*lightmapParamsOfFace(*prim, 2), lp));
        CHECK(near(lightmapParamsOfFace(*prim, 1)->sx, 16384));  // unselected face unchanged
    }
    CHECK(d.isFaceSelected(FaceRef{ent, item, 0}) && d.isFaceSelected(FaceRef{ent, item, 2}));  // selection kept
    CHECK(d.text() != before);

    // Applying the same values again changes nothing.
    CHECK(!setLightmapOnSelection(d, lp));

    // One undo step restores the whole edit.
    CHECK(d.undo());
    CHECK(d.text() == before);
    CHECK(d.redo());  // and redo brings the edit back
    CHECK(d.findItem(ent, item)->prim() != nullptr && sameLm(*lightmapParamsOfFace(*d.findItem(ent, item)->prim(), 2), lp));
}

TEST(patch_lock_border_keeps_locked_and_moves_rest) {
    const PatchGrid before = makeFlatPatch(AABB{{0, 0, 0}, {128, 128, 0}}, 2, 4, 4, "mat");
    CHECK(before.rows == 4 && before.cols == 4 && before.pos.size() == 16);
    if (before.pos.size() != 16) return;

    PatchGrid after = before;
    for (Vec3& p : after.pos) p.z += 10;  // lift every control point

    const std::vector<bool> locks = lockBorder(before);
    CHECK(locks.size() == 16);
    size_t nLocked = 0;
    for (bool b : locks) nLocked += b ? 1 : 0;
    CHECK(nLocked == 12);  // a 4x4 grid has 12 border points and 4 interior points

    const PatchGrid out = applyPatchEditWithLocks(before, after, locks);
    CHECK(out.pos.size() == 16);
    for (size_t i = 0; i < out.pos.size(); ++i) {
        if (locks[i]) CHECK(out.pos[i] == before.pos[i]);
        else CHECK(out.pos[i] == after.pos[i]);
    }
    // Interior points moved, border points kept.
    CHECK(near(out.pos[5].z, 10) && near(out.pos[0].z, 0));
    CHECK(out.material == before.material && out.rows == before.rows && out.cols == before.cols);

    // No locks: the edit goes through unchanged. Mismatched grids: `after` comes back.
    CHECK(applyPatchEditWithLocks(before, after, std::vector<bool>(16, false)).pos == after.pos);
    PatchGrid other = makeFlatPatch(AABB{{0, 0, 0}, {128, 128, 0}}, 2, 3, 3, "mat");
    CHECK(applyPatchEditWithLocks(before, other, locks).pos == other.pos);
    CHECK(lockBorder(PatchGrid{}).empty());
}

TEST(patch_lock_corners_only) {
    const PatchGrid before = makeFlatPatch(AABB{{0, 0, 0}, {128, 128, 0}}, 2, 3, 5, "mat");
    CHECK(before.pos.size() == 15);
    if (before.pos.size() != 15) return;
    PatchGrid after = before;
    for (Vec3& p : after.pos) p.x += 7;

    const std::vector<bool> locks = lockCorners(before);
    CHECK(locks.size() == 15);
    size_t nLocked = 0;
    for (bool b : locks) nLocked += b ? 1 : 0;
    CHECK(nLocked == 4);
    CHECK(locks[0] && locks[4] && locks[10] && locks[14]);  // (0,0), (0,4), (2,0), (2,4) in a 3x5 grid

    const PatchGrid out = applyPatchEditWithLocks(before, after, locks);
    for (size_t i = 0; i < out.pos.size(); ++i) {
        if (locks[i]) CHECK(out.pos[i] == before.pos[i]);
        else CHECK(near(out.pos[i].x, after.pos[i].x));
    }
    // The lock is positional: the points between the corners follow the edit.
    CHECK(near(out.pos[2].x, before.pos[2].x + 7));
}
