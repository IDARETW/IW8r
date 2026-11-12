// Bevel tools (roadmap 2.23): chamfer edges, inset and recess a face, selection helpers with one undo step.
#include <cmath>
#include <cstdint>
#include <string>

#include "bevel_ops.h"
#include "document.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

const std::string kMat = " tm2co/me_curb_stone_01 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0";

bool near(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol; }
bool samePoint(const Vec3& a, const Vec3& b) { return length(a - b) < 1e-6; }

// Axis aligned cube [0, size]^3 with faces in makeBox order (+x, -x, +y, -y, +z, -z).
ConvexBrush cubeBrush(double size, const std::string& suffix = kMat) {
    AABB box;
    box.add(Vec3(0, 0, 0));
    box.add(Vec3(size, size, size));
    return makeBox(box, suffix);
}

// Index of the face whose outward normal points most along n.
size_t faceWithNormal(const ConvexBrush& b, const Vec3& n) {
    size_t best = b.size();
    double bestDot = -2;
    for (size_t i = 0; i < b.size(); ++i) {
        const double d = dot(normalize(b[i].plane.n), normalize(n));
        if (d > bestDot) {
            bestDot = d;
            best = i;
        }
    }
    return best;
}

// The edge of `b` joining p and q (either order).
bool findEdge(const ConvexBrush& b, const Vec3& p, const Vec3& q, BrushEdge& out) {
    for (const BrushEdge& e : brushEdges(b)) {
        if ((samePoint(e.a, p) && samePoint(e.b, q)) || (samePoint(e.a, q) && samePoint(e.b, p))) {
            out = e;
            return true;
        }
    }
    return false;
}

bool insideBrush(const ConvexBrush& b, const Vec3& p) {
    for (const BrushFaceSpec& f : b)
        if (dot(f.plane.n, p) > f.plane.d + 1e-9) return false;
    return true;
}

bool insideAny(const std::vector<ConvexBrush>& pieces, const Vec3& p) {
    for (const ConvexBrush& b : pieces)
        if (insideBrush(b, p)) return true;
    return false;
}

// Map text with one brush (worldspawn) built from `b`.
std::string mapWithBrush(const ConvexBrush& b) {
    auto prim = makeBrushPrimitive(b, "\n");
    if (!prim) return "";
    return "iwmap 4\n{\n\"classname\" \"worldspawn\"\n" + prim->text() + "}\n";
}

SelRef firstBrushRef(const Document& doc) {
    for (const Entity& e : doc.map().entities)
        for (const EntityItem& it : e.items)
            if (it.isPrimitive()) return {e.id, it.id};
    return {};
}

const Primitive* primOf(const Document& doc, const SelRef& r) {
    const EntityItem* it = doc.findItem(r.entity, r.item);
    return it ? it->prim() : nullptr;
}

}  // namespace

TEST(bevel_chamfer_one_edge_of_cube) {
    const ConvexBrush cube = cubeBrush(64);
    BrushEdge edge;
    CHECK(findEdge(cube, Vec3(64, 0, 64), Vec3(64, 64, 64), edge));

    const auto r = chamferEdge(cube, edge, 8);
    CHECK(r.has_value());
    if (!r) return;
    CHECK(r->size() == 7);
    CHECK(isValidBrush(*r));
    CHECK(makeBrushPrimitive(*r, "\n").has_value());

    // Bounds unchanged: the cut only removes the edge itself. Volume = 64^3 - 64 * (8^2 / 2).
    const AABB bb = brushBounds(*r);
    CHECK(near(bb.mins.x, 0) && near(bb.mins.y, 0) && near(bb.mins.z, 0));
    CHECK(near(bb.maxs.x, 64) && near(bb.maxs.y, 64) && near(bb.maxs.z, 64));
    CHECK(near(brushVolume(*r), 262144.0 - 2048.0, 1e-3));

    // The new face is the cut with normal (1,0,1)/sqrt(2).
    const size_t cut = faceWithNormal(*r, Vec3(1, 0, 1));
    CHECK(near(dot(normalize((*r)[cut].plane.n), normalize(Vec3(1, 0, 1))), 1.0, 1e-9));

    // Too large: a 64 cut removes the top and side faces entirely. Non-edges and bad sizes fail too.
    CHECK(chamferEdge(cube, edge, 63).has_value());
    CHECK(!chamferEdge(cube, edge, 64));
    CHECK(!chamferEdge(cube, edge, 0));
    BrushEdge diagonal{Vec3(0, 0, 0), Vec3(64, 64, 0)};
    CHECK(!chamferEdge(cube, diagonal, 8));
}

TEST(bevel_chamfer_suffix_from_lower_index_face) {
    ConvexBrush cube = cubeBrush(64);
    cube[0].suffix = " mat_x 64 64 0 0 0 0";  // +x, index 0
    cube[4].suffix = " mat_z 64 64 0 0 0 0";  // +z, index 4
    BrushEdge edge;
    CHECK(findEdge(cube, Vec3(64, 0, 64), Vec3(64, 64, 64), edge));
    const auto r = chamferEdge(cube, edge, 8);
    CHECK(r.has_value());
    if (!r) return;
    const size_t cut = faceWithNormal(*r, Vec3(1, 0, 1));
    CHECK((*r)[cut].suffix == cube[0].suffix);
}

TEST(bevel_chamfer_all_edges_of_cube) {
    const ConvexBrush cube = cubeBrush(64);
    const auto r = chamferAllEdges(cube, 8);
    CHECK(r.has_value());
    if (r) {
        CHECK(r->size() == 18);  // 6 faces + 12 chamfers
        CHECK(isValidBrush(*r));
        CHECK(makeBrushPrimitive(*r, "\n").has_value());
        const AABB bb = brushBounds(*r);
        CHECK(near(bb.mins.x, 0) && near(bb.maxs.x, 64) && near(bb.maxs.z, 64));
        // Removed volume: the 12 edge prisms (12 * 2048), minus the pairwise corner overlaps (24 * 8^3/3), plus the
        // triple corner overlaps (8 * 8^3/4) = 21504. Remaining 262144 - 21504.
        CHECK(near(brushVolume(*r), 240640.0, 1e-3));
    }
    CHECK(!chamferAllEdges(cube, 32));  // top and bottom faces vanish
    CHECK(!chamferAllEdges(cube, 40));
    CHECK(!chamferAllEdges(cube, 0));
}

TEST(bevel_inset_cube_top_face_recess) {
    const ConvexBrush cube = cubeBrush(64);
    const size_t top = faceWithNormal(cube, Vec3(0, 0, 1));
    const auto pieces = insetFace(cube, top, 8, 8);
    CHECK(pieces.size() == 5);  // main brush + one ring piece per edge
    if (pieces.size() != 5) return;

    double sum = 0;
    AABB all;
    for (const ConvexBrush& p : pieces) {
        CHECK(isValidBrush(p));
        CHECK(makeBrushPrimitive(p, "\n").has_value());
        sum += brushVolume(p);
        all.add(brushBounds(p));
    }
    // Cube minus the recessed centre: a 48 x 48 x 8 pit.
    CHECK(near(sum, 262144.0 - 48.0 * 48.0 * 8.0, 1e-3));
    CHECK(near(all.mins.x, 0) && near(all.mins.y, 0) && near(all.mins.z, 0));
    CHECK(near(all.maxs.x, 64) && near(all.maxs.y, 64) && near(all.maxs.z, 64));

    CHECK(!insideAny(pieces, Vec3(32, 32, 60)));  // inside the pit
    CHECK(insideAny(pieces, Vec3(4, 32, 60)));    // rim
    CHECK(insideAny(pieces, Vec3(32, 32, 30)));   // body below the pit

    CHECK(insetFace(cube, top, 40, 8).empty());   // inset past the face's middle
    CHECK(insetFace(cube, top, 8, 0).empty());    // recess only
    CHECK(insetFace(cube, top, 8, 64).empty());   // would remove the whole brush
}

TEST(bevel_inset_hex_prism_top) {
    AABB box;
    box.add(Vec3(-32, -32, 0));
    box.add(Vec3(32, 32, 64));
    const ConvexBrush hex = makePrism(box, 6, 2, kMat);
    CHECK(hex.size() == 8);
    const size_t top = faceWithNormal(hex, Vec3(0, 0, 1));
    const auto pieces = insetFace(hex, top, 4, 8);
    CHECK(pieces.size() == 7);
    if (pieces.size() != 7) return;

    // Regular hexagon, circumradius 32: the inset outline is a regular hexagon with apothem 32*sqrt(3)/2 - 4.
    const double apothem = 32.0 * std::sqrt(3.0) / 2.0 - 4.0;
    const double innerArea = 2.0 * std::sqrt(3.0) * apothem * apothem;
    double sum = 0;
    for (const ConvexBrush& p : pieces) {
        CHECK(isValidBrush(p));
        sum += brushVolume(p);
    }
    CHECK(near(sum, brushVolume(hex) - 8.0 * innerArea, 1e-3));
}

TEST(bevel_selection_chamfer_one_undo_step) {
    Document doc;
    std::string err;
    const std::string text = mapWithBrush(cubeBrush(64));
    CHECK(!text.empty());
    CHECK(doc.loadFromText(text, "", &err));
    const std::string before = doc.text();
    const size_t p0 = doc.undoPosition();

    doc.select(firstBrushRef(doc), false);
    CHECK(chamferSelection(doc, 8) == 1);
    CHECK(doc.undoPosition() == p0 + 1);
    CHECK(doc.text() != before);
    const Primitive* p = primOf(doc, firstBrushRef(doc));
    CHECK(p != nullptr && p->faces.size() == 18);

    CHECK(doc.undo());
    CHECK(doc.text() == before);
    CHECK(doc.undoPosition() == p0);

    doc.deselectAll();
    CHECK(chamferSelection(doc, 8) == 0);  // nothing selected: no change
    CHECK(doc.undoPosition() == p0);
}

TEST(bevel_selection_inset_one_undo_step) {
    Document doc;
    std::string err;
    const std::string text = mapWithBrush(cubeBrush(64));
    CHECK(!text.empty());
    CHECK(doc.loadFromText(text, "", &err));
    const std::string before = doc.text();
    const size_t p0 = doc.undoPosition();

    const SelRef ref = firstBrushRef(doc);
    const Primitive* p = primOf(doc, ref);
    CHECK(p != nullptr);
    if (!p) return;
    size_t top = p->faces.size();
    for (size_t i = 0; i < p->faces.size(); ++i) {
        const auto pl = facePlane(*p, i);
        if (pl && dot(pl->n, Vec3(0, 0, 1)) > 0.99) top = i;
    }
    CHECK(top < p->faces.size());

    CHECK(insetSelectedFace(doc, FaceRef{ref.entity, ref.item, static_cast<uint32_t>(top)}, 8, 8));
    CHECK(doc.undoPosition() == p0 + 1);
    CHECK(doc.selection().size() == 5);
    CHECK(doc.text() != before);

    CHECK(doc.undo());
    CHECK(doc.text() == before);
    CHECK(doc.undoPosition() == p0);
}

// Bridge two faces: two cubes 64 apart along x; bridging the +x face of the first and the -x face of the second gives
// the 64 x 64 x 64 box between them, with the first face's material.
TEST(bevel_bridge_two_faces) {
    AABB a, b;
    a.add(Vec3(0, 0, 0));
    a.add(Vec3(64, 64, 64));
    b.add(Vec3(128, 0, 0));
    b.add(Vec3(192, 64, 64));
    const auto pa = makeBrushPrimitive(makeBox(a, kMat), "\n");
    const auto pb = makeBrushPrimitive(makeBox(b, " tm2co/other 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0"), "\n");
    CHECK(pa.has_value() && pb.has_value());
    if (!pa || !pb) return;
    // Primitive face indices of +x on a and -x on b.
    const auto faceAlong = [](const Primitive& p, const Vec3& n) {
        for (size_t i = 0; i < p.faces.size(); ++i)
            if (const auto pl = facePlane(p, i); pl && dot(pl->n, n) > 0.99) return i;
        return p.faces.size();
    };
    const size_t fa = faceAlong(*pa, Vec3(1, 0, 0)), fb = faceAlong(*pb, Vec3(-1, 0, 0));
    const auto bridge = bridgeFaces(*pa, fa, *pb, fb);
    CHECK(bridge.has_value());
    if (!bridge) return;
    const AABB bb = brushBounds(*bridge);
    CHECK(bb.mins == Vec3(64, 0, 0) && bb.maxs == Vec3(128, 64, 64) && bridge->size() == 6);
    for (const BrushFaceSpec& f : *bridge) CHECK(f.suffix == kMat);
    // Faces in one plane (the +z faces of both cubes) have nothing to span.
    const size_t ta = faceAlong(*pa, Vec3(0, 0, 1)), tb = faceAlong(*pb, Vec3(0, 0, 1));
    CHECK(!bridgeFaces(*pa, ta, *pb, tb).has_value());
    // A sloped bridge: the +x face of a to the -x face of a raised cube is a convex ramp-like beam.
    AABB c;
    c.add(Vec3(128, 0, 64));
    c.add(Vec3(192, 64, 128));
    const auto pc = makeBrushPrimitive(makeBox(c, kMat), "\n");
    CHECK(pc.has_value());
    if (!pc) return;
    const auto ramp = bridgeFaces(*pa, fa, *pc, faceAlong(*pc, Vec3(-1, 0, 0)));
    CHECK(ramp.has_value() && isValidBrush(*ramp));
    if (ramp) {
        const AABB rb = brushBounds(*ramp);
        CHECK(rb.mins == Vec3(64, 0, 0) && rb.maxs == Vec3(128, 64, 128));
    }

    // In a document: the two selected faces give one new brush in one undo step.
    Document doc;
    Id next = 1;
    std::string text = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n" + pa->text() + pb->text() + "}\n";
    std::string err;
    CHECK(doc.loadFromText(text, "", &err));
    const Entity& world = doc.map().entities[0];
    std::vector<Id> items;
    for (const auto& it : world.items)
        if (it.prim()) items.push_back(it.id);
    CHECK(items.size() == 2);
    if (items.size() != 2) return;
    doc.selectFace({world.id, items[0], uint32_t(fa)}, false);
    doc.selectFace({world.id, items[1], uint32_t(fb)}, true);
    const size_t pos = doc.undoPosition();
    CHECK(bridgeSelectedFaces(doc));
    size_t prims = 0;
    for (const auto& it : doc.map().entities[0].items) prims += it.prim() ? 1 : 0;
    CHECK(prims == 3 && doc.undoPosition() == pos + 1);
    (void)next;
}
