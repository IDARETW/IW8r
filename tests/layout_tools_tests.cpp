// Layout tools (roadmap 2.25): array / radial duplicate, mirror clone, align and distribute. Every operation must be one
// undo step, and redo must restore the same text and selection.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#include "brush_ops.h"
#include "document.h"
#include "geometry.h"
#include "layout_tools.h"
#include "map_text.h"
#include "patch_ops.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

// A box brush in iwmap face-line text (caulk on every face).
std::string box(double x0, double y0, double z0, double x1, double y1, double z1) {
    auto p = [](double x, double y, double z) { return formatNumber(x) + " " + formatNumber(y) + " " + formatNumber(z); };
    auto f = [](const std::string& a, const std::string& b, const std::string& c) {
        return " ( " + a + " ) ( " + b + " ) ( " + c + " ) caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n";
    };
    std::string t = "{\n";
    t += f(p(x1, y1, z0), p(x0, y1, z0), p(x0, y0, z0));  // bottom
    t += f(p(x0, y0, z1), p(x0, y1, z1), p(x1, y1, z1));  // top
    t += f(p(x0, y0, z0), p(x0, y0, z1), p(x1, y0, z1));  // -y
    t += f(p(x1, y0, z0), p(x1, y0, z1), p(x1, y1, z1));  // +x
    t += f(p(x1, y1, z0), p(x1, y1, z1), p(x0, y1, z1));  // +y
    t += f(p(x0, y1, z0), p(x0, y1, z1), p(x0, y0, z1));  // -x
    return t + "}\n";
}

// A map with a worldspawn holding `worldBody`, followed by the entities in `extra`.
std::string mapWith(const std::string& worldBody, const std::string& extra = "") {
    return "iwmap 4\n{\n\"classname\" \"worldspawn\"\n" + worldBody + "}\n" + extra;
}

std::string pointEntity(const std::string& cls, const std::string& origin, const std::string& angles = "") {
    std::string t = "{\n\"classname\" \"" + cls + "\"\n\"origin\" \"" + origin + "\"\n";
    if (!angles.empty()) t += "\"angles\" \"" + angles + "\"\n";
    return t + "}\n";
}

void load(Document& doc, const std::string& text) {
    std::string err;
    CHECK(doc.loadFromText(text, "", &err));
}

bool close(double a, double b) { return std::fabs(a - b) < 1e-4; }
bool closeV(const Vec3& a, const Vec3& b) { return close(a.x, b.x) && close(a.y, b.y) && close(a.z, b.z); }

// Worldspawn primitives in file order.
std::vector<const Primitive*> worldPrims(const Document& doc) {
    std::vector<const Primitive*> out;
    for (const auto& e : doc.map().entities)
        if (e.isWorldspawn())
            for (const auto& it : e.items)
                if (const Primitive* p = it.prim()) out.push_back(p);
    return out;
}

std::vector<SelRef> worldRefs(const Document& doc) {
    std::vector<SelRef> out;
    for (const auto& e : doc.map().entities)
        if (e.isWorldspawn())
            for (const auto& it : e.items)
                if (it.isPrimitive()) out.push_back({e.id, it.id});
    return out;
}

std::vector<const Entity*> entitiesNamed(const Document& doc, const std::string& cls) {
    std::vector<const Entity*> out;
    for (const auto& e : doc.map().entities)
        if (e.classname() == cls) out.push_back(&e);
    return out;
}

const Entity* entityAt(const Document& doc, const std::string& cls, const Vec3& at) {
    for (const Entity* e : entitiesNamed(doc, cls))
        if (auto o = e->origin(); o && closeV(*o, at)) return e;
    return nullptr;
}

Vec3 anglesOf(const Entity& e) {
    Vec3 a;
    if (const std::string* s = e.value("angles")) std::sscanf(s->c_str(), "%lf %lf %lf", &a.x, &a.y, &a.z);
    return a;
}

// Map text stays the same and one undo restores it; redo brings back the same text and selection.
void expectOneStep(Document& doc, const std::string& before, size_t posBefore) {
    const std::string after = doc.text();
    const std::set<SelRef> sel = doc.selection();
    CHECK(doc.undoPosition() == posBefore + 1);
    CHECK(doc.undo());
    CHECK(doc.text() == before);
    CHECK(doc.undoPosition() == posBefore);
    CHECK(doc.redo());
    CHECK(doc.text() == after);
    CHECK(doc.selection() == sel);
}

}  // namespace

TEST(layout_array_duplicate_offsets_and_selects_copies) {
    Document doc;
    load(doc, mapWith(box(0, 0, 0, 64, 64, 64)));
    const std::string before = doc.text();
    const size_t pos = doc.undoPosition();
    for (const SelRef& r : worldRefs(doc)) doc.select(r, true);
    CHECK(arrayDuplicate(doc, Vec3{100.0, 0.0, 0.0}, 3) == 3);

    std::vector<double> xs;
    for (const Primitive* p : worldPrims(doc)) xs.push_back(p->bounds().mins.x);
    std::sort(xs.begin(), xs.end());
    CHECK(xs.size() == 4);
    if (xs.size() == 4) {
        CHECK(close(xs[0], 0.0));
        CHECK(close(xs[1], 100.0));
        CHECK(close(xs[2], 200.0));
        CHECK(close(xs[3], 300.0));
    }
    // The three copies are selected, the original is not.
    CHECK(doc.selection().size() == 3);
    size_t copiesSelected = 0;
    for (const SelRef& r : doc.selection()) {
        const EntityItem* it = doc.findItem(r.entity, r.item);
        if (it && it->prim() && it->prim()->bounds().mins.x > 50) ++copiesSelected;
    }
    CHECK(copiesSelected == 3);
    expectOneStep(doc, before, pos);
}

TEST(layout_array_duplicate_moves_point_entities) {
    Document doc;
    load(doc, mapWith("", pointEntity("light", "100 0 0")));
    const std::string before = doc.text();
    const size_t pos = doc.undoPosition();
    const Id light = entitiesNamed(doc, "light")[0]->id;
    doc.select({light, kNoId}, false);
    CHECK(arrayDuplicate(doc, Vec3{0.0, 50.0, 0.0}, 2) == 2);
    CHECK(entitiesNamed(doc, "light").size() == 3);
    CHECK(entityAt(doc, "light", Vec3{100.0, 0.0, 0.0}) != nullptr);
    CHECK(entityAt(doc, "light", Vec3{100.0, 50.0, 0.0}) != nullptr);
    CHECK(entityAt(doc, "light", Vec3{100.0, 100.0, 0.0}) != nullptr);
    CHECK(doc.selection().size() == 2);
    expectOneStep(doc, before, pos);
}

TEST(layout_array_duplicate_guards) {
    Document doc;
    load(doc, mapWith(box(0, 0, 0, 64, 64, 64)));
    const std::string before = doc.text();
    const size_t pos = doc.undoPosition();
    CHECK(arrayDuplicate(doc, Vec3{100.0, 0.0, 0.0}, 3) == 0);  // nothing selected
    for (const SelRef& r : worldRefs(doc)) doc.select(r, true);
    CHECK(arrayDuplicate(doc, Vec3{100.0, 0.0, 0.0}, 0) == 0);
    CHECK(arrayDuplicate(doc, Vec3{100.0, 0.0, 0.0}, -2) == 0);
    CHECK(doc.text() == before);
    CHECK(doc.undoPosition() == pos);
}

TEST(layout_radial_full_circle_rotates_brushes_and_yaw) {
    Document doc;
    load(doc, mapWith(box(100, 0, 0, 164, 64, 64), pointEntity("light", "100 0 0", "0 30 0")));
    const std::string before = doc.text();
    const size_t pos = doc.undoPosition();
    for (const SelRef& r : worldRefs(doc)) doc.select(r, true);
    doc.select({entitiesNamed(doc, "light")[0]->id, kNoId}, true);
    // Count 3 copies on a full circle: 4 slots, so 90 degrees apart.
    CHECK(radialDuplicate(doc, Vec3{0.0, 0.0, 0.0}, 2, 3) == 3);
    CHECK(worldPrims(doc).size() == 4);

    // Rotations by 90, 180 and 270 degrees (right-handed about Z) of the box x 100..164, y 0..64.
    const double expectX[] = {-64.0, -164.0, 0.0};
    const double expectY[] = {100.0, -64.0, -164.0};
    for (int k = 0; k < 3; ++k) {
        bool found = false;
        for (const Primitive* p : worldPrims(doc)) {
            const AABB b = p->bounds();
            found |= close(b.mins.x, expectX[k]) && close(b.mins.y, expectY[k]);
        }
        CHECK(found);
    }

    // The light: origin on the circle and yaw advanced by the same angle (30 + 90, 180, 270).
    const Vec3 origins[] = {{0.0, 100.0, 0.0}, {-100.0, 0.0, 0.0}, {0.0, -100.0, 0.0}};
    const double yaws[] = {120.0, 210.0, 300.0};
    for (int k = 0; k < 3; ++k) {
        const Entity* e = entityAt(doc, "light", origins[k]);
        CHECK(e != nullptr);
        if (e) CHECK(close(anglesOf(*e).y, yaws[k]));
    }
    const Entity* original = entityAt(doc, "light", Vec3{100.0, 0.0, 0.0});
    CHECK(original != nullptr);
    if (original) CHECK(close(anglesOf(*original).y, 30.0));
    CHECK(doc.selection().size() == 6);  // three brush copies and three light copies
    expectOneStep(doc, before, pos);
}

TEST(layout_radial_partial_arc_and_angle_key) {
    // A light with only a yaw-only `angle` key (45).
    Document doc;
    load(doc, mapWith("", "{\n\"classname\" \"light\"\n\"origin\" \"100 0 0\"\n\"angle\" \"45\"\n}\n"));
    const std::string before = doc.text();
    const size_t pos = doc.undoPosition();
    doc.select({entitiesNamed(doc, "light")[0]->id, kNoId}, false);
    // Count 2 over a 180 degree arc: copies at 90 and 180 degrees, both ends included.
    CHECK(radialDuplicate(doc, Vec3{0.0, 0.0, 0.0}, 2, 2, 180.0) == 2);
    const Entity* a = entityAt(doc, "light", Vec3{0.0, 100.0, 0.0});
    const Entity* b = entityAt(doc, "light", Vec3{-100.0, 0.0, 0.0});
    CHECK(a != nullptr);
    CHECK(b != nullptr);
    if (a) {
        const std::string* yaw = a->value("angle");
        CHECK(yaw != nullptr && close(std::atof(yaw->c_str()), 135.0));
        CHECK(a->value("angles") == nullptr);  // a yaw-only entity keeps its angle key
    }
    if (b) {
        const std::string* yaw = b->value("angle");
        CHECK(yaw != nullptr && close(std::atof(yaw->c_str()), 225.0));
    }
    expectOneStep(doc, before, pos);
}

TEST(layout_radial_guards) {
    Document doc;
    load(doc, mapWith(box(100, 0, 0, 164, 64, 64)));
    const std::string before = doc.text();
    const size_t pos = doc.undoPosition();
    CHECK(radialDuplicate(doc, Vec3{0.0, 0.0, 0.0}, 2, 3) == 0);  // nothing selected
    for (const SelRef& r : worldRefs(doc)) doc.select(r, true);
    CHECK(radialDuplicate(doc, Vec3{0.0, 0.0, 0.0}, 2, 0) == 0);
    CHECK(radialDuplicate(doc, Vec3{0.0, 0.0, 0.0}, 3, 3) == 0);   // bad axis
    CHECK(radialDuplicate(doc, Vec3{0.0, 0.0, 0.0}, 2, 3, 0.0) == 0);
    CHECK(doc.text() == before);
    CHECK(doc.undoPosition() == pos);
}

TEST(layout_mirror_clone_brush_and_point_yaw) {
    Document doc;
    load(doc, mapWith(box(100, 0, 0, 164, 64, 64), pointEntity("light", "100 0 0", "0 30 0")));
    const std::string before = doc.text();
    const size_t pos = doc.undoPosition();
    for (const SelRef& r : worldRefs(doc)) doc.select(r, true);
    doc.select({entitiesNamed(doc, "light")[0]->id, kNoId}, true);
    CHECK(mirrorClone(doc, 0, 0.0));
    CHECK(worldPrims(doc).size() == 2);

    const Primitive* copy = nullptr;
    const Primitive* original = nullptr;
    for (const Primitive* p : worldPrims(doc)) (p->bounds().mins.x < 0 ? copy : original) = p;
    CHECK(copy != nullptr);
    CHECK(original != nullptr);
    if (copy && original) {
        const AABB b = copy->bounds();
        CHECK(close(b.mins.x, -164.0));
        CHECK(close(b.maxs.x, -100.0));
        CHECK(close(b.mins.y, 0.0));
        CHECK(close(b.maxs.y, 64.0));
        CHECK(copy->faces.size() == 6);
        const ConvexBrush cb = brushFromPrimitive(*copy);
        CHECK(isValidBrush(cb));
        CHECK(close(brushVolume(cb), 64.0 * 64.0 * 64.0));
        CHECK(close(original->bounds().mins.x, 100.0));  // the original stays put
    }
    const Entity* light = entityAt(doc, "light", Vec3{-100.0, 0.0, 0.0});
    CHECK(light != nullptr);
    if (light) {
        CHECK(close(anglesOf(*light).y, 150.0));  // yaw 30 mirrored across the YZ plane is 180 - 30
        CHECK(doc.selection().size() == 2);       // one brush copy and one light copy
    }
    expectOneStep(doc, before, pos);
}

TEST(layout_mirror_patch_reverses_facing) {
    // A flat patch facing +X at x = 132. Mirrored across x = 0 it sits at x = -132 and must face -X.
    AABB bounds;
    bounds.add(Vec3{100.0, 0.0, 0.0});
    bounds.add(Vec3{164.0, 64.0, 64.0});
    const PatchGrid g = makeFlatPatch(bounds, 0, 3, 3, "caulk");
    const Primitive patch = makePatchPrimitive(g, "\n", "");
    Document doc;
    load(doc, mapWith(patch.text()));
    CHECK(worldPrims(doc).size() == 1);
    const std::string before = doc.text();
    const size_t pos = doc.undoPosition();
    for (const SelRef& r : worldRefs(doc)) doc.select(r, true);
    CHECK(mirrorClone(doc, 0, 0.0));

    auto normalOf = [](const PatchGrid& pg) {
        // Front normal: cross(column direction, row direction) at the first control point (row-major, cols per row).
        const Vec3 colDir = pg.pos[1] - pg.pos[0];
        const Vec3 rowDir = pg.pos[static_cast<size_t>(pg.cols)] - pg.pos[0];
        return cross(colDir, rowDir);
    };
    const Primitive* copy = nullptr;
    const Primitive* original = nullptr;
    for (const Primitive* p : worldPrims(doc)) (p->bounds().mins.x < 0 ? copy : original) = p;
    CHECK(copy != nullptr);
    CHECK(original != nullptr);
    if (copy && original) {
        auto gc = patchGrid(*copy);
        auto go = patchGrid(*original);
        CHECK(gc.has_value());
        CHECK(go.has_value());
        if (gc && go) {
            for (const Vec3& v : gc->pos) CHECK(close(v.x, -132.0));
            const Vec3 n0 = normalOf(*go);
            const Vec3 n1 = normalOf(*gc);
            CHECK(std::fabs(n0.x) > 0.5);
            CHECK(close(n1.x, -n0.x));  // reflected facing
            CHECK(close(n1.y, n0.y));
            CHECK(close(n1.z, n0.z));
        }
    }
    expectOneStep(doc, before, pos);
}

TEST(layout_mirror_guards) {
    Document doc;
    load(doc, mapWith(box(100, 0, 0, 164, 64, 64)));
    const std::string before = doc.text();
    const size_t pos = doc.undoPosition();
    CHECK(!mirrorClone(doc, 0, 0.0));  // nothing selected
    for (const SelRef& r : worldRefs(doc)) doc.select(r, true);
    CHECK(!mirrorClone(doc, 5, 0.0));  // bad axis
    CHECK(doc.text() == before);
    CHECK(doc.undoPosition() == pos);
}

TEST(layout_align_min_max_center) {
    // A: x 0..64, y 0..64. B: x 200..264, y 0..128. Both 64 units tall in z.
    const std::string text = mapWith(box(0, 0, 0, 64, 64, 64) + box(200, 0, 0, 264, 128, 64));

    Document maxY;
    load(maxY, text);
    const std::string before = maxY.text();
    size_t pos = maxY.undoPosition();
    for (const SelRef& r : worldRefs(maxY)) maxY.select(r, true);
    CHECK(alignSelection(maxY, 1, AlignMode::Max));  // A moves up so its top meets B's top at y 128
    auto prims = worldPrims(maxY);
    CHECK(prims.size() == 2);
    if (prims.size() == 2) {
        CHECK(close(prims[0]->bounds().mins.y, 64.0));
        CHECK(close(prims[0]->bounds().maxs.y, 128.0));
        CHECK(close(prims[1]->bounds().mins.y, 0.0));  // B does not move
    }
    CHECK(maxY.selection().size() == 2);
    expectOneStep(maxY, before, pos);

    Document centre;
    load(centre, text);
    pos = centre.undoPosition();
    for (const SelRef& r : worldRefs(centre)) centre.select(r, true);
    // Selection spans x 0..264, centre 132. A (centre 32) moves +100 and B (centre 232) moves -100: both at 100..164.
    CHECK(alignSelection(centre, 0, AlignMode::Center));
    prims = worldPrims(centre);
    CHECK(prims.size() == 2);
    if (prims.size() == 2) {
        CHECK(close(prims[0]->bounds().mins.x, 100.0));
        CHECK(close(prims[1]->bounds().mins.x, 100.0));
        CHECK(close(prims[1]->bounds().maxs.x, 164.0));
    }

    Document minX;
    load(minX, text);
    pos = minX.undoPosition();
    for (const SelRef& r : worldRefs(minX)) minX.select(r, true);
    CHECK(alignSelection(minX, 0, AlignMode::Min));  // B moves down to x 0
    prims = worldPrims(minX);
    CHECK(prims.size() == 2);
    if (prims.size() == 2) {
        CHECK(close(prims[1]->bounds().mins.x, 0.0));
        CHECK(close(prims[1]->bounds().maxs.x, 64.0));
        CHECK(close(prims[0]->bounds().mins.x, 0.0));
    }
    expectOneStep(minX, text, pos);
}

TEST(layout_align_brush_entity_is_one_object) {
    // Worldspawn box at x 200..264. A brush entity made of two boxes at x 0..64 and 64..128.
    const std::string entity = "{\n\"classname\" \"func_test\"\n" + box(0, 0, 0, 64, 64, 64) + box(64, 0, 0, 128, 64, 64) + "}\n";
    Document doc;
    load(doc, mapWith(box(200, 0, 0, 264, 64, 64), entity));
    const std::string before = doc.text();
    const size_t pos = doc.undoPosition();
    const Entity* func = entitiesNamed(doc, "func_test")[0];
    Id firstBrush = kNoId;
    for (const auto& it : func->items)
        if (it.isPrimitive() && firstBrush == kNoId) firstBrush = it.id;
    doc.select({func->id, firstBrush}, false);  // selecting one brush selects the whole entity
    for (const SelRef& r : worldRefs(doc)) doc.select(r, true);
    CHECK(alignSelection(doc, 0, AlignMode::Max));  // the entity's extent 0..128 moves to end at 264
    std::vector<double> mins;
    for (const auto& it : entitiesNamed(doc, "func_test")[0]->items)
        if (const Primitive* p = it.prim()) mins.push_back(p->bounds().mins.x);
    CHECK(mins.size() == 2);
    if (mins.size() == 2) {
        CHECK(close(mins[0], 136.0));
        CHECK(close(mins[1], 200.0));  // the two brushes keep their spacing
    }
    expectOneStep(doc, before, pos);
}

TEST(layout_align_guards) {
    Document doc;
    load(doc, mapWith(box(0, 0, 0, 64, 64, 64)));
    const std::string before = doc.text();
    const size_t pos = doc.undoPosition();
    CHECK(!alignSelection(doc, 0, AlignMode::Min));  // nothing selected
    for (const SelRef& r : worldRefs(doc)) doc.select(r, true);
    CHECK(!alignSelection(doc, 0, AlignMode::Max));  // one object aligned to itself
    CHECK(!alignSelection(doc, 4, AlignMode::Min));  // bad axis
    CHECK(doc.text() == before);
    CHECK(doc.undoPosition() == pos);
}

TEST(layout_distribute_equal_gaps) {
    // Four 10-unit boxes on X: 0, 20, 100 and 300. Middle two move so all three gaps are 90.
    Document doc;
    load(doc, mapWith(box(0, 0, 0, 10, 10, 10) + box(20, 0, 0, 30, 10, 10) + box(100, 0, 0, 110, 10, 10) + box(300, 0, 0, 310, 10, 10)));
    const std::string before = doc.text();
    const size_t pos = doc.undoPosition();
    for (const SelRef& r : worldRefs(doc)) doc.select(r, true);
    CHECK(distributeSelection(doc, 0));
    auto prims = worldPrims(doc);
    CHECK(prims.size() == 4);
    if (prims.size() == 4) {
        const AABB a = prims[0]->bounds(), b = prims[1]->bounds(), c = prims[2]->bounds(), d = prims[3]->bounds();
        CHECK(close(a.mins.x, 0.0));  // first and last stay
        CHECK(close(d.mins.x, 300.0));
        CHECK(close(b.mins.x - a.maxs.x, 90.0));
        CHECK(close(c.mins.x - b.maxs.x, 90.0));
        CHECK(close(d.mins.x - c.maxs.x, 90.0));
    }
    expectOneStep(doc, before, pos);
}

TEST(layout_distribute_guards) {
    Document doc;
    load(doc, mapWith(box(0, 0, 0, 10, 10, 10) + box(100, 0, 0, 110, 10, 10)));
    const std::string before = doc.text();
    const size_t pos = doc.undoPosition();
    CHECK(!distributeSelection(doc, 0));  // nothing selected
    for (const SelRef& r : worldRefs(doc)) doc.select(r, true);
    CHECK(!distributeSelection(doc, 0));  // two objects
    CHECK(doc.text() == before);
    CHECK(doc.undoPosition() == pos);
}
