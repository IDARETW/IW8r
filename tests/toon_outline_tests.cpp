// Ink outlines for toon maps (core toon_outline, plan M6).
#include "toon_outline.h"
#include "iw8_contents.h"

#include <cmath>
#include "test_harness.h"

using namespace iw8r;

TEST(toon_outline_box_edges) {
    AABB box;
    box.add(Vec3{0, 0, 0});
    box.add(Vec3{128, 64, 32});
    const ConvexBrush b = makeBox(box, " twc/plaster_tan_02 64 64 0 0 0 0");
    const std::vector<InkEdge> edges = brushInkEdges(b);
    CHECK(edges.size() == 12);  // a box: every edge is a 90-degree crease
    // The bisector points away from the box (between the two outward normals).
    for (const InkEdge& e : edges) {
        const Vec3 mid = (e.a + e.b) * 0.5;
        CHECK(dot(e.bisector, mid - Vec3{64, 32, 16}) > 0);
    }
    InkOptions longOnly;
    longOnly.minLength = 40;
    CHECK(brushInkEdges(b, longOnly).size() == 8);  // the four 32-unit verticals are dropped
}

TEST(toon_outline_shared_edges_and_strips) {
    AABB a, c;
    a.add(Vec3{0, 0, 0}), a.add(Vec3{64, 64, 64});
    c.add(Vec3{64, 0, 0}), c.add(Vec3{128, 64, 64});  // touching a along x = 64
    const std::vector<ConvexBrush> brushes{makeBox(a, " twc/plaster_tan_02 64 64 0 0 0 0"), makeBox(c, " twc/plaster_tan_02 64 64 0 0 0 0")};
    // 24 edges, of which the four along the shared face appear in both brushes.
    InkOptions keepAll;
    keepAll.skipBuried = false;
    CHECK(uniqueInkEdges(brushes, keepAll).size() == 20);
    // Only the creases of the 128 x 64 x 64 slab they make: its 4 short edges at each end and its 4 long edges in two
    // pieces; the seam around the shared face is no crease.
    CHECK(uniqueInkEdges(brushes).size() == 16);
    // A crate on a floor: the floor's 12 edges and the crate's 4 top and 4 upright edges, not its 4 on the floor.
    AABB floor, crate;
    floor.add(Vec3{-256, -256, -16}), floor.add(Vec3{256, 256, 0});
    crate.add(Vec3{0, 0, 0}), crate.add(Vec3{32, 32, 32});
    CHECK(uniqueInkEdges({makeBox(floor, " twc/plaster_tan_02 64 64 0 0 0 0"), makeBox(crate, " twc/plaster_tan_02 64 64 0 0 0 0")}).size() == 20);
    InkOptions opt;
    opt.width = 2;
    const InkEdge e{Vec3{0, 0, 0}, Vec3{64, 0, 0}, normalize(Vec3{0, -1, -1})};
    const ConvexBrush strip = inkStripBrush(e, opt, " iw8r_ink 64 64 0 0 0 0");
    CHECK(strip.size() == 6 && isValidBrush(strip));
    const AABB sb = brushBounds(strip);
    CHECK(std::fabs(sb.mins.x - -1) < 1e-6 && std::fabs(sb.maxs.x - 65) < 1e-6);  // the edge plus half the width at both ends
    CHECK(std::fabs(brushVolume(strip) - 66.0 * 2 * 2) < 1e-3);
    CHECK(inkStripBrush(InkEdge{Vec3{1, 1, 1}, Vec3{1, 1, 1}, Vec3{0, 0, 1}}, opt, " x").empty());
}

TEST(toon_outline_hidden_faces_and_text) {
    AABB a;
    a.add(Vec3{0, 0, 0}), a.add(Vec3{64, 64, 64});
    // All caulk: nothing is seen, so nothing is inked; one drawn face inks its four edges.
    ConvexBrush b = makeBox(a, " caulk 64 64 0 0 0 0");
    CHECK(brushInkEdges(b).empty());
    b[0].suffix = " twc/plaster_tan_02 64 64 0 0 0 0";
    CHECK(brushInkEdges(b).size() == 4);
    InkOptions all;
    all.skipHiddenFaces = false;
    CHECK(brushInkEdges(b, all).size() == 12);
    CHECK(inkFaceMaterial(" iw8r_ink 64 64 0 0 0 0") == "iw8r_ink");
    // The strip as map text: a valid brush that does not collide.
    const InkEdge e{Vec3{0, 0, 0}, Vec3{0, 0, 96}, normalize(Vec3{-1, -1, 0})};
    const auto p = inkStripPrimitive(e, InkOptions{}, " iw8r_ink 64 64 0 0 0 0", "\n");
    CHECK(p.has_value() && p->faces.size() == 6);
    CHECK(p && !brushContents(*p).collides);
    CHECK(p && p->text().find("contents nonColliding;") != std::string::npos);
}

TEST(toon_outline_apply_to_document) {
    // A drawn box in the worldspawn and one in a trigger (not outlined): one ink func_group, replaced on a second run.
    AABB bx;
    bx.add(Vec3{0, 0, 0}), bx.add(Vec3{64, 64, 64});
    const auto prim = makeBrushPrimitive(makeBox(bx, " twc/plaster_tan_02 64 64 0 0 0 0"), "\n");
    CHECK(prim.has_value());
    if (!prim) return;
    const std::string box1 = prim->text();
    const std::string text = "{\n\"classname\" \"worldspawn\"\n" + box1 + "}\n{\n\"classname\" \"trigger_multiple\"\n" + box1 + "}\n";
    Document doc;
    CHECK(doc.loadFromText(text, "ink_test.map"));
    InkOptions opt;
    const InkResult r = applyInkOutlines(doc, opt);
    CHECK(r.brushes == 1 && r.strips == 12 && !r.fromSelection && !r.replaced);
    size_t inkGroups = 0, inkBrushes = 0;
    for (const auto& e : doc.map().entities) {
        const std::string* note = e.value("iw8r_note");
        if (!note || *note != kInkNote) continue;
        ++inkGroups;
        CHECK(e.classname() == "func_group");
        for (const auto& it : e.items)
            if (const Primitive* p = it.prim(); p && p->kind == PrimKind::Brush) {
                ++inkBrushes;
                CHECK(!brushContents(*p).collides);
            }
    }
    CHECK(inkGroups == 1 && inkBrushes == 12);
    const InkResult again = applyInkOutlines(doc, opt);
    CHECK(again.replaced && again.strips == 12 && again.brushes == 1);  // the old ink is neither outlined nor kept
    inkGroups = 0;
    for (const auto& e : doc.map().entities)
        if (const std::string* note = e.value("iw8r_note"); note && *note == kInkNote) ++inkGroups;
    CHECK(inkGroups == 1);
    CHECK(doc.undo());
    CHECK(doc.undo());
    inkGroups = 0;
    for (const auto& e : doc.map().entities)
        if (const std::string* note = e.value("iw8r_note"); note && *note == kInkNote) ++inkGroups;
    CHECK(inkGroups == 0);
}
