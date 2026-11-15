// The camera's kept geometry (src/viewport/camera_geometry.cpp, CameraGeometry) draws the scene's behaviour helpers
// (SceneCache::helperLines, research 131 gaps 3 and 15) as buildCameraGeometry does. camera_geometry.cpp is compiled
// into this translation unit (the test binary does not link iw8r_viewport); scene.cpp and the profiler stubs come from
// viewport_scene_behaviour_tests.cpp.
#include "../src/viewport/camera_geometry.cpp"

#include <string>
#include <vector>

#include "brush_ops.h"
#include "document.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

size_t countRgba(const std::vector<app::GpuVertex>& v, uint32_t rgba) {
    size_t n = 0;
    for (const app::GpuVertex& g : v) n += g.rgba == rgba;
    return n;
}

}  // namespace

TEST(camera_geometry_draws_behaviour_helpers) {
    Document doc;
    std::string err;
    CHECK(doc.loadFromText("iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n", "camera_helpers_test.map", &err));
    doc.addEntity({{"classname", "fx_origin"}, {"origin", "0 0 0"}, {"iw8r_damage", "10 64"}}, {});
    auto prim = makeBrushPrimitive(makeBox(AABB{{0, 200, 0}, {64, 208, 96}}, " iw8r/test_grey 128 128 0 0 0 0"), "\n");
    CHECK(prim.has_value());
    const Id door = doc.addEntity({{"classname", "script_brushmodel"}, {"targetname", "d1"}, {"iw8r_door", "rotate 90"}},
                                  {prim ? *prim : Primitive{}});
    doc.deselectAll();
    app::SceneCache sc;
    sc.sync(doc);
    CHECK(sc.helperLines().size() == 36 + 12);
    app::CameraGeometry cg;
    cg.update(sc, doc, false, nullptr, true);
    std::vector<app::GpuVertex> tris, lines, overlay;
    std::vector<app::TexturedBatch> textured;
    cg.flatten(tris, lines, overlay, textured);
    const uint32_t red = 0xff2626e6u;  // packRGBA(0.9, 0.15, 0.15)
    const uint32_t use = app::colorForClass("trigger_use");
    CHECK(countRgba(lines, red) == 72 && countRgba(overlay, use) == 0);
    // The same as the immediate path.
    std::vector<app::GpuVertex> t2, l2, o2;
    app::buildCameraGeometry(sc, doc, false, t2, l2, o2);
    CHECK(countRgba(l2, red) == 72);
    // The door selected: its use area on top (the misc lines are made again on every update).
    Id brush = kNoId;
    for (const auto& it : doc.findEntity(door)->items)
        if (it.prim()) brush = it.id;
    doc.select({door, brush}, false);
    cg.update(sc, doc, false, nullptr, false);
    cg.flatten(tris, lines, overlay, textured);
    CHECK(countRgba(overlay, use) == 24 && countRgba(lines, red) == 72);
}
