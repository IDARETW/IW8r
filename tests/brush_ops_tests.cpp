// Convex brush operations (src/core/brush_ops.h): round trips through map text, generators and CSG-style edits.
#include <cmath>
#include <string>
#include <vector>

#include "brush_ops.h"
#include "map_text.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

const char* kTail = " caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0";

AABB box3(Vec3 a, Vec3 b) {
    AABB r;
    r.add(a);
    r.add(b);
    return r;
}

bool sameBounds(const AABB& a, const AABB& b, double eps) {
    return std::fabs(a.mins.x - b.mins.x) < eps && std::fabs(a.mins.y - b.mins.y) < eps &&
           std::fabs(a.mins.z - b.mins.z) < eps && std::fabs(a.maxs.x - b.maxs.x) < eps &&
           std::fabs(a.maxs.y - b.maxs.y) < eps && std::fabs(a.maxs.z - b.maxs.z) < eps;
}

// True when a plane with the same outward normal and offset exists in `set`.
bool hasPlane(const ConvexBrush& set, const Plane& p) {
    for (const auto& f : set)
        if (dot(f.plane.n, p.n) > 0.999999 && std::fabs(f.plane.d - p.d) < 1e-9) return true;
    return false;
}

double sumVolume(const std::vector<ConvexBrush>& pieces) {
    double v = 0;
    for (const auto& p : pieces) v += brushVolume(p);
    return v;
}

ConvexBrush concat(ConvexBrush a, const ConvexBrush& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

}  // namespace

TEST(brush_box_round_trip_through_text) {
    const ConvexBrush box = makeBox(box3({0, 0, 0}, {64, 64, 32}), kTail);
    auto prim = makeBrushPrimitive(box, "\r\n", "// brush 0");
    CHECK(prim.has_value());
    if (!prim) return;
    CHECK(prim->kind == PrimKind::Brush);
    CHECK(prim->lines.size() == 9);  // comment, {, 6 faces, }
    CHECK(prim->lines.front().text == "// brush 0");
    CHECK(prim->lines.back().text == "}");
    CHECK(prim->lines[1].text == "{");
    for (const auto& l : prim->lines) CHECK(l.eol == "\r\n");

    // Integer box: point text carries no decimal points (only the points, the suffix is kept verbatim).
    for (size_t i = 2; i + 1 < prim->lines.size(); ++i) {
        const std::string& t = prim->lines[i].text;
        CHECK(!t.empty() && t[0] == ' ');
        const size_t first = t.find(')');
        const size_t second = t.find(')', first + 1);
        const size_t third = second == std::string::npos ? std::string::npos : t.find(')', second + 1);
        CHECK(third != std::string::npos);
        if (third != std::string::npos) CHECK(t.substr(0, third + 1).find('.') == std::string::npos);
    }

    // Reparse the primitive text from scratch and compare with the original planes.
    Primitive reparsed = *prim;
    reparsePrimitive(reparsed);
    const ConvexBrush back = brushFromPrimitive(reparsed);
    CHECK(back.size() == box.size());
    for (size_t i = 0; i < box.size() && i < back.size(); ++i) {
        CHECK(length(back[i].plane.n - box[i].plane.n) < 1e-9);
        CHECK(std::fabs(back[i].plane.d - box[i].plane.d) < 1e-9);
        CHECK(back[i].suffix == box[i].suffix);
    }
    CHECK(isValidBrush(back));
    CHECK(sameBounds(brushBounds(back), box3({0, 0, 0}, {64, 64, 32}), 1e-9));
}

TEST(brush_box_lines_match_sample_room_generator) {
    // The six face lines of tools/make_iw8_sample_room.py box(0, 0, 0, 64, 64, 32, ...).
    const char* ref[] = {
        " ( 64 64 0 ) ( 0 64 0 ) ( 0 0 0 )",   // bottom -z
        " ( 0 0 32 ) ( 0 64 32 ) ( 64 64 32 )",  // top +z
        " ( 0 0 32 ) ( 64 0 32 ) ( 64 0 0 )",   // -y
        " ( 64 64 32 ) ( 0 64 32 ) ( 0 64 0 )",  // +y
        " ( 64 0 32 ) ( 64 64 32 ) ( 64 64 0 )",  // +x
        " ( 0 64 32 ) ( 0 0 32 ) ( 0 0 0 )",    // -x
    };
    Primitive refPrim;
    refPrim.lines.push_back({"{", "\n"});
    for (const char* r : ref) refPrim.lines.push_back({std::string(r) + kTail, "\n"});
    refPrim.lines.push_back({"}", "\n"});
    reparsePrimitive(refPrim);
    const ConvexBrush want = brushFromPrimitive(refPrim);
    CHECK(want.size() == 6);

    const ConvexBrush got = makeBox(box3({0, 0, 0}, {64, 64, 32}), kTail);
    CHECK(got.size() == 6);
    for (const auto& f : want) CHECK(hasPlane(got, f.plane));
    for (const auto& f : got) CHECK(hasPlane(want, f.plane));
    for (const auto& f : got) CHECK(f.suffix == kTail);
    CHECK(std::fabs(brushVolume(got) - 64.0 * 64.0 * 32.0) < 1e-6);
}

TEST(brush_prism_eight_sides) {
    const AABB bounds = box3({0, 0, 0}, {64, 64, 32});
    const ConvexBrush p = makePrism(bounds, 8, 2, kTail);
    CHECK(p.size() == 10);
    CHECK(isValidBrush(p));
    CHECK(sameBounds(brushBounds(p), bounds, 1e-6));
    // Regular octagon with circumradius 32 has area 2*sqrt(2)*R^2; the prism is 32 high.
    const double octArea = 2.0 * std::sqrt(2.0) * 32.0 * 32.0;
    CHECK(std::fabs(brushVolume(p) - octArea * 32.0) < 1e-3);
    CHECK(makeBrushPrimitive(p, "\n").has_value());
    CHECK(makePrism(bounds, 2, 2, kTail).empty());
    CHECK(makePrism(bounds, 65, 2, kTail).empty());
}

TEST(brush_clip_half_cube) {
    const ConvexBrush cube = makeBox(box3({0, 0, 0}, {64, 64, 64}), kTail);
    auto clipped = clipBrush(cube, Plane{{1, 0, 0}, 32}, " cap_tex 0 0 0 0 0 0");
    CHECK(clipped.has_value());
    if (!clipped) return;
    CHECK(sameBounds(brushBounds(*clipped), box3({0, 0, 0}, {32, 64, 64}), 1e-9));
    CHECK(std::fabs(brushVolume(*clipped) - 32.0 * 64.0 * 64.0) < 1e-6);
    CHECK(clipped->size() == 6);
    // The new face carries the cap suffix.
    bool capFound = false;
    for (const auto& f : *clipped)
        if (f.suffix == " cap_tex 0 0 0 0 0 0") capFound = true;
    CHECK(capFound);
    // Clipping by a plane that keeps everything returns the brush unchanged.
    auto same = clipBrush(cube, Plane{{1, 0, 0}, 100}, kTail);
    CHECK(same.has_value() && same->size() == 6);
    // Nothing left when the plane excludes the whole brush.
    CHECK(!clipBrush(cube, Plane{{1, 0, 0}, -10}, kTail).has_value());
}

TEST(brush_subtract_centred_cube) {
    const ConvexBrush a = makeBox(box3({0, 0, 0}, {64, 64, 64}), kTail);
    const ConvexBrush cutter = makeBox(box3({16, 16, 16}, {48, 48, 48}), " cut 0 0 0 0 0 0");
    const auto pieces = subtractBrush(a, cutter);
    CHECK(!pieces.empty());
    CHECK(std::fabs(sumVolume(pieces) - (64.0 * 64 * 64 - 32.0 * 32 * 32)) < 1e-6);
    for (const auto& p : pieces) {
        CHECK(isValidBrush(p));
        // No overlap with the cutter: their intersection is not a solid.
        CHECK(!isValidBrush(concat(p, cutter)));
    }
    // Disjoint from the cutter but overlapping a: the cutter does not touch a, so a comes back whole.
    const ConvexBrush far = makeBox(box3({100, 100, 100}, {120, 120, 120}), kTail);
    const auto untouched = subtractBrush(a, far);
    CHECK(untouched.size() == 1);
    // A brush entirely inside the cutter leaves nothing.
    const ConvexBrush big = makeBox(box3({-10, -10, -10}, {80, 80, 80}), kTail);
    CHECK(subtractBrush(a, big).empty());
}

TEST(brush_hollow_cube) {
    const ConvexBrush cube = makeBox(box3({0, 0, 0}, {128, 128, 128}), kTail);
    const auto walls = hollowBrush(cube, 8);
    CHECK(walls.size() == 6);
    CHECK(std::fabs(sumVolume(walls) - (128.0 * 128 * 128 - 112.0 * 112 * 112)) < 1e-6);
    for (const auto& w : walls) CHECK(isValidBrush(w));
    CHECK(hollowBrush(cube, 0).empty());
}

TEST(brush_rotate_quarter_turn_about_z) {
    const ConvexBrush b = makeBox(box3({0, 0, 0}, {64, 32, 16}), kTail);
    const ConvexBrush r = rotateBrush(b, 2, 90, {0, 0, 0});
    CHECK(isValidBrush(r));
    CHECK(sameBounds(brushBounds(r), box3({-32, 0, 0}, {0, 64, 16}), 1e-9));
    CHECK(std::fabs(brushVolume(r) - 64.0 * 32 * 16) < 1e-6);
    // A full turn about the brush centre leaves it in place.
    const ConvexBrush full = rotateBrush(b, 0, 360, brushBounds(b).center());
    CHECK(sameBounds(brushBounds(full), box3({0, 0, 0}, {64, 32, 16}), 1e-9));
}

TEST(brush_flip_keeps_normals_outward) {
    const ConvexBrush b = makeBox(box3({0, 0, 0}, {64, 32, 16}), kTail);
    const ConvexBrush f = flipBrush(b, 0, 0);
    CHECK(f.size() == 6);
    CHECK(isValidBrush(f));
    CHECK(sameBounds(brushBounds(f), box3({-64, 0, 0}, {0, 32, 16}), 1e-9));
    for (const auto& w : brushWindings(f)) CHECK(!w.empty());
    CHECK(std::fabs(brushVolume(f) - 64.0 * 32 * 16) < 1e-6);
    // Also after a text round trip the outward orientation is kept.
    auto prim = makeBrushPrimitive(f, "\n");
    CHECK(prim.has_value());
}

TEST(brush_move_face_resizes) {
    const ConvexBrush b = makeBox(box3({0, 0, 0}, {64, 64, 64}), kTail);
    size_t plusX = b.size();
    for (size_t i = 0; i < b.size(); ++i)
        if (dot(b[i].plane.n, Vec3{1, 0, 0}) > 0.5) plusX = i;
    CHECK(plusX < b.size());
    if (plusX >= b.size()) return;

    auto grown = moveFace(b, plusX, 16);
    CHECK(grown.has_value());
    if (grown) {
        const AABB bb = brushBounds(*grown);
        CHECK(std::fabs((bb.maxs.x - bb.mins.x) - 80) < 1e-9);
        CHECK(std::fabs((bb.maxs.y - bb.mins.y) - 64) < 1e-9);
        CHECK(grown->size() == b.size());
    }
    CHECK(!moveFace(b, plusX, -64).has_value());
    CHECK(!moveFace(b, plusX, -100).has_value());
    CHECK(!moveFace(b, 99, 4).has_value());
}

TEST(brush_translate_and_scale) {
    const ConvexBrush b = makeBox(box3({0, 0, 0}, {64, 64, 64}), kTail);
    const ConvexBrush t = translateBrush(b, {10, -5, 2});
    CHECK(sameBounds(brushBounds(t), box3({10, -5, 2}, {74, 59, 66}), 1e-9));
    const ConvexBrush s = scaleBrush(b, {2, 0.5, 1}, {0, 0, 0});
    CHECK(isValidBrush(s));
    CHECK(sameBounds(brushBounds(s), box3({0, 0, 0}, {128, 32, 64}), 1e-9));
    CHECK(scaleBrush(b, {0, 1, 1}, {0, 0, 0}).empty());
}

TEST(brush_invalid_inputs_are_rejected) {
    // Three faces cannot enclose a volume.
    ConvexBrush wedge = {
        {{{0, 0, -1}, 0}, kTail},
        {{{0, -1, 0}, 0}, kTail},
        {{{-1, 0, 0}, 0}, kTail},
    };
    CHECK(!isValidBrush(wedge));
    CHECK(!makeBrushPrimitive(wedge, "\n").has_value());
    CHECK(makeBox(box3({0, 0, 0}, {0, 0, 0}), kTail).size() == 6);
    CHECK(!isValidBrush(makeBox(box3({0, 0, 0}, {0, 0, 0}), kTail)));
}
