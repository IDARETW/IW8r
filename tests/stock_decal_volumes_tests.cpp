// Stock decal volumes: record decoding, table parsing, the box projector (src/core/stock_decal_volumes.h,
// docs/research/99_stock_decal_volumes.md).
#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "stock_decal_volumes.h"
#include "stock_world.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }

// A floor quad (two triangles, counter-clockwise from +Z) at height z, from (-s, -s) to (s, s), normal `up`.
ImportedMesh quad(double s, double z, bool up = true) {
    ImportedMesh m;
    m.material = "floor";
    m.pos = {{-s, -s, z}, {s, -s, z}, {s, s, z}, {-s, s, z}};
    const Vec3 n{0, 0, up ? 1.0 : -1.0};
    m.normal = {n, n, n, n};
    m.uv = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    m.idx = up ? std::vector<uint32_t>{0, 1, 2, 0, 2, 3} : std::vector<uint32_t>{0, 2, 1, 0, 3, 2};
    return m;
}

// A volume from explicit axes (unit, right handed) and half sizes.
StockDecalVolume box(const Vec3& c, const Vec3& a0, const Vec3& a1, const Vec3& h) {
    StockDecalVolume v;
    v.center = c;
    v.axis = {a0, a1, cross(a0, a1)};
    v.halfSize = h;
    return v;
}

double batchArea(const StockDecalBatch& b) {
    double a = 0;
    for (size_t i = 0; i + 2 < b.idx.size(); i += 3)
        a += 0.5 * length(cross(b.verts[b.idx[i + 1]].pos - b.verts[b.idx[i]].pos, b.verts[b.idx[i + 2]].pos - b.verts[b.idx[i]].pos));
    return a;
}

}  // namespace

TEST(stock_decal_volumes_record_decode) {
    // The converter fork's floor test decal (research 82 section 5.2): axes [[0,0,-1],[0,1,0],[1,0,0]], half (4, 32, 32).
    StockDecalVolume v;
    decodeStockDecalVolumeRecord({0, 0, 4}, 0xCFFFFDFFu, 0xA0280440u, 0x00FFFF00u, {0x00003C00u, 0x3C008000u, 0}, 0x80BFF800u, 0, v);
    CHECK(near(v.halfSize.x, 4) && near(v.halfSize.y, 32) && near(v.halfSize.z, 32));
    CHECK(near(v.axis[0].z, -1, 1e-4) && std::fabs(v.axis[0].x) < 0.01 && std::fabs(v.axis[0].y) < 0.01);
    CHECK(near(v.axis[1].y, 1, 1e-4) && near(v.axis[2].x, 1, 1e-4));
    CHECK(near(dot(cross(v.axis[0], v.axis[1]), v.axis[2]), 1, 1e-6));  // right handed
    CHECK(near(v.blendEdge0, 0) && near(v.blendEdge1, 1) && near(v.blendScale, 1) && near(v.feather, 0));
    CHECK(near(v.opacityScale, 1) && near(v.opacityBias, 0));
    CHECK(v.uvMatrix[0] == 1 && v.uvMatrix[1] == 0 && v.uvMatrix[2] == 0 && std::signbit(v.uvMatrix[2]) && v.uvMatrix[3] == 1);
    CHECK(v.uvOffset[0] == 0 && v.uvOffset[1] == 0);
    CHECK(v.normalBlendMode == 2 && v.drawOrder == 0 && v.mapDrawOrder == 0);
    CHECK((v.rawInfo & 0x7FF) == 0 && ((v.rawInfo >> 11) & 0x7FF) == kStockDecalNoIndex);

    // mp_m_speedball record 4: a red paint stripe on a wall (i/decal_paint_01_red, mask decal_mask_stain_06_c).
    StockDecalVolume r;
    decodeStockDecalVolumeRecord({-1039.5, -415.0, 203.5}, 0x4FF7FDFFu, 0x9D2D2460u, 0x00A5FF00u, {0x4644u, 0x3A848000u, 0xB188AA00u},
                                 0x84800801u, 0, r);
    CHECK(near(r.halfSize.x, 6) && near(r.halfSize.y, 200) && near(r.halfSize.z, 26));
    // v runs down the wall (axis 2 = -Z): a viewer facing the wall sees u to the right, v downwards (image upright).
    CHECK(near(r.axis[0].x, -1, 0.01) && near(r.axis[1].y, 1, 0.01) && near(r.axis[2].z, -1, 0.01));
    CHECK(near(r.blendScale, 165 / 255.0) && near(r.opacityScale, 165 / 255.0) && near(r.opacityBias, 0));
    CHECK(near(r.uvMatrix[0], 6.265625) && near(r.uvMatrix[3], 0.814453125) && near(r.uvOffset[0], -0.046875) &&
          near(r.uvOffset[1], -0.1728515625));
    CHECK((r.rawInfo & 0x7FF) == 1 && ((r.rawInfo >> 11) & 0x7FF) == 1 && r.mapDrawOrder == 4 && r.drawOrder == 0);

    // Degenerate blend-map edges (Speedball stencil letters store 0, 25, 255; edges equal fall back to 0, 1, 1).
    StockDecalVolume s;
    decodeStockDecalVolumeRecord({}, 0x0FF7FDFFu, 0x851F8380u, 0x00FF1900u, {0x3C00u, 0x3C008000u, 0}, 0x8880D80Au, 0, s);
    CHECK(near(s.opacityScale, 255.0 / 25.0, 1e-9) && near(s.opacityBias, 0));
    StockDecalVolume e;
    decodeStockDecalVolumeRecord({}, 0x0FF7FDFFu, 0x851F8380u, 0x66FF3333u, {0x3C00u, 0x3C008000u, 0}, 0, 0, e);
    CHECK(near(e.blendEdge0, 0.2) && near(e.blendEdge1, 0.2) && near(e.opacityScale, 1) && near(e.opacityBias, 0));
    CHECK(near(e.feather, 0.4));
    CHECK(e.drawOrder == -4);

    // Every packed quaternion decodes to a unit rotation.
    for (uint32_t q : {0x4FF7FDFFu, 0xC8341FBFu, 0x1147FDFFu, 0x98512809u, 0xD2D58C26u}) {
        const auto a = stockDecalAxesFromQuat(decodeStockDecalQuat(q));
        for (int i = 0; i < 3; ++i) CHECK(near(length(a[i]), 1, 0.01));
        CHECK(std::fabs(dot(a[0], a[1])) < 0.01 && near(dot(cross(a[0], a[1]), a[2]), 1, 0.02));
    }
}

TEST(stock_decal_volumes_parse) {
    const std::string gfx =
        R"({"asset":{"fields":{"decalVolumeVersion":0,"decalVolumeMaterialCount":2,"decalVolumeMaskCount":1,)"
        R"("mapDecalVolumeCount":3,"decalVolumeCollectionCount":2,)"
        R"("decalVolumeMaterials":{"type":"GfxStaticDecalVolumeMaterial","count":2,"values":[)"
        R"({"material":{"type":"Material","asset_reference":true,"name":",i/decal_puddle_muddy_01"}},)"
        R"({"material":{"type":"Material","asset_reference":true,"name":",i/decal_paint_01_red"}}]},)"
        R"("decalVolumeMasks":{"type":"GfxStaticDecalVolumeMask","count":1,"values":[)"
        R"({"blendMapOverride":{"type":"GfxDecalVolumeMask","asset_reference":true,"name":"decal_mask_stain_06_c"}}]},)"
        R"("decalVolumeCollections":{"type":"GfxDecalVolumeCollection","count":2,"values":[)"
        R"({"transientZoneIndex":0,"decalCount":2,"firstZoneDecal":0},{"transientZoneIndex":0,"decalCount":1,"firstZoneDecal":2}]}}}})";
    const std::string trz =
        R"({"asset":{"fields":{"name":{"string":"mp_x"},"transientZoneIndex":0,"drawVerts":{"posDataSize":0},)"
        R"("decalVolumes":{"type":"GfxDecalVolumeTransientData","stride":96,"count":1,"values":[{"decalVolumeCount":3,)"
        R"("firstCollection":0,"collectionCount":2,"decalVolumes":{"type":"GfxStaticDecalVolumePacked","values":[)"
        R"({"obbCenter":{"$union_views":true,"v":[-1039.5,-415.0,203.5]},"obbQuat":1341652479,"obbHalfSize":2636981344,)"
        R"("opacityParams":10878720,"uvMatrixAndOffset":[17988,981762048,2978523648],"packedMaterialInfo":2222981121,"reserved":0},)"
        R"({"obbCenter":{"v":[0,0,4]},"obbQuat":3489660415,"obbHalfSize":2686977088,"opacityParams":16776960,)"
        R"("uvMatrixAndOffset":[15360,1006665728,0],"packedMaterialInfo":2160064512,"reserved":0},)"
        R"({"obbCenter":{"__s1":{"x":1,"y":2,"z":3}},"obbQuat":3489660415,"obbHalfSize":2686977088,"opacityParams":16776960,)"
        R"("uvMatrixAndOffset":[15360,1006665728,0],"packedMaterialInfo":2160066559,"reserved":0}]},)"
        R"("collectionBounds":{"type":"Bounds","values":[{"midPoint":{"v":[1,2,3]},"halfSize":{"v":[4,5,6]}},)"
        R"({"midPoint":{"v":[0,0,0]},"halfSize":{"v":[1,1,1]}}]},)"
        R"("decalVolumeUids":{"values":[{"uid":11},{"uid":22},{"uid":33}]}}]}}}})";
    StockDecalVolumeSet set;
    std::string err;
    CHECK(parseStockDecalVolumes(gfx, {trz}, set, &err));
    CHECK(err.empty());
    CHECK(set.materials.size() == 2 && set.materials[1] == "i/decal_paint_01_red");
    CHECK(set.masks.size() == 1 && set.masks[0] == "decal_mask_stain_06_c");
    CHECK(set.collections.size() == 2 && set.collections[1].firstZoneDecal == 2 && set.collections[1].decalCount == 1);
    CHECK(set.collections[0].bounds.valid() && near(set.collections[0].bounds.maxs.z, 9));
    CHECK(set.volumes.size() == 3);
    if (set.volumes.size() == 3) {
        const StockDecalVolume& a = set.volumes[0];
        CHECK(a.material == "i/decal_paint_01_red" && a.materialIndex == 1);
        CHECK(a.mask == "decal_mask_stain_06_c" && a.maskIndex == 0);
        CHECK(a.collection == 0 && a.uid == 11 && near(a.center.x, -1039.5) && near(a.halfSize.y, 200));
        const StockDecalVolume& b = set.volumes[1];
        CHECK(b.material == "i/decal_puddle_muddy_01" && b.mask.empty() && b.maskIndex == -1 && b.collection == 0);
        const StockDecalVolume& c = set.volumes[2];
        CHECK(c.material.empty() && c.materialIndex == -1 && c.collection == 1 && c.uid == 33 && near(c.center.y, 2));
    }
    // No decal keys at all: an empty set.
    StockDecalVolumeSet none;
    CHECK(parseStockDecalVolumes(R"({"asset":{"fields":{}}})", {R"({"asset":{"fields":{"transientZoneIndex":0}}})"}, none, &err));
    CHECK(none.volumes.empty() && none.materials.empty());
    // A truncated table fails.
    StockDecalVolumeSet bad;
    CHECK(!parseStockDecalVolumes(R"({"decalVolumeMaterials":{"values":[{"material":)", {}, bad, &err));

    // Material and mask assets.
    StockDecalMaterial m;
    const std::string matJson =
        R"({"pool":"decalvolumematerial","asset":{"type":"GfxDecalVolumeMaterial","fields":{"name":{"type":"const char",)"
        R"("string":"i/decal_paint_01_red"},"channels":[{"type":"GfxImage","asset_reference":true,)"
        R"("name":",decal_paint_01_c&defaultspec~15253967373450432551"},{"name":",decal_paint_01_n&decal_paint_01_g~1"},null,null,null,null],)"
        R"("flags":1037,"colorTint":{"v":[0.5775809288024902,0.057805582880973816,0.03954628109931946]},"alphaDissolveParms":0.0,)"
        R"("emissiveScale":1.0,"textureAtlasRowCount":1,"textureAtlasColumnCount":1}}})";
    CHECK(parseStockDecalMaterial(matJson, m, &err));
    CHECK(m.name == "i/decal_paint_01_red" && m.flags == 0x40D && m.channels[0] == "decal_paint_01_c&defaultspec~15253967373450432551");
    CHECK(m.channels[2].empty() && near(m.colorTint.x, 0.5775809288024902));
    std::string mn, mi;
    CHECK(parseStockDecalMask(R"({"asset":{"fields":{"name":{"string":"decal_mask_paint_04_c"},"blendMap":{"name":"decal_mask_paint_04_c~atlasbc4"}}}})",
                              mn, mi, &err));
    CHECK(mn == "decal_mask_paint_04_c" && mi == "decal_mask_paint_04_c~atlasbc4");
}

TEST(stock_decal_volumes_coordinates) {
    // Floor decal: projects down (axis 0 = -Z), u along +Y, v along +X.
    const StockDecalVolume v = box({10, 20, 0}, {0, 0, -1}, {0, 1, 0}, {4, 32, 16});
    CHECK(near(v.axis[2].x, 1));
    StockDecalCoord c = stockDecalCoord(v, {10, 20, 0});
    CHECK(near(c.u, 0.5) && near(c.v, 0.5) && near(c.depth, 0));
    c = stockDecalCoord(v, {10 + 16, 20 - 32, 4});
    CHECK(near(c.u, 0) && near(c.v, 1) && near(c.depth, 1));  // depth +1 on the side the decal faces (above)
    const AABB b = stockDecalVolumeBounds(v);
    CHECK(near(b.mins.x, -6) && near(b.maxs.y, 52) && near(b.mins.z, -4));
    StockDecalVolume t = v;
    t.uvMatrix = {2, 0, 0, 0.5};
    t.uvOffset = {0.25, -0.5};
    const auto muv = stockDecalMaterialUv(t, 0.5, 1.0);
    CHECK(near(muv[0], 1.25) && near(muv[1], 0.0));
    // Angle fade: full facing the decal, 0 past the cutoff (86 degrees), 0 behind.
    CHECK(near(stockDecalAngleFade(v, {0, 0, 1}), 1));
    CHECK(near(stockDecalAngleFade(v, {0, 0, -1}), 0));
    const double r = 3.14159265358979323846 / 180.0;
    CHECK(near(stockDecalAngleFade(v, {std::sin(87 * r), 0, std::cos(87 * r)}), 0));
    CHECK(near(stockDecalAngleFade(v, {std::sin(50 * r), 0, std::cos(50 * r)}), 1));
    const double mid = stockDecalAngleFade(v, {std::sin(75 * r), 0, std::cos(75 * r)});
    CHECK(mid > 0.3 && mid < 0.6);
    // Edge fade only in the outer 1/128 of the half extent.
    CHECK(near(stockDecalEdgeFade(0.5, 0.5), 1) && near(stockDecalEdgeFade(0.995, 0.5), 1) && stockDecalEdgeFade(0.9999, 0.5) < 0.1);
}

TEST(stock_decal_volumes_opacity) {
    StockDecalVolume v = box({}, {0, 0, -1}, {0, 1, 0}, {4, 32, 32});
    StockDecalMaterial m;
    m.flags = 0x40D;  // paint: colour, normal, normal add (bit 0)
    v.maskIndex = 0;
    v.opacityScale = 165 / 255.0;
    StockDecalShading s = stockDecalShading(v, m);
    CHECK(s.maskFromVolume && !s.maskFromOpacityMap && !s.opacityMap && s.colour && s.normal && s.normalAdd);
    // op = m * saturate(m * A + B)
    CHECK(near(stockDecalOpacity(s, 1.0, 1.0, 0.5, 0.5, 0, 1), 165 / 255.0));
    CHECK(near(stockDecalOpacity(s, 0.5, 1.0, 0.5, 0.5, 0, 1), 0.5 * 0.5 * 165 / 255.0));
    CHECK(near(stockDecalOpacity(s, 1.0, 1.0, 0.5, 0.5, 0, 0.5), 0.5 * 165 / 255.0));
    CHECK(stockDecalOpacity(s, 1.0, 1.0, 0.5, 0.5, 1.0, 1) == 0);   // outside the box depth
    CHECK(stockDecalOpacity(s, 1.0, 1.0, 1.0, 0.5, 0.0, 1) == 0);   // u = 1 is outside
    CHECK(stockDecalOpacity(s, 0.03, 1.0, 0.5, 0.5, 0.0, 1) == 0);  // below the 1/255 threshold
    // Feather 0.25: fades over the last quarter of the depth.
    v.feather = 0.25;
    s = stockDecalShading(v, m);
    CHECK(near(stockDecalOpacity(s, 1.0, 1.0, 0.5, 0.5, 0.875, 1), 0.5 * 165 / 255.0));
    // Normal blend mode 0 overrides the material's add bit; mode 1 forces add.
    v.normalBlendMode = 0;
    CHECK(!stockDecalShading(v, m).normalAdd);
    v.normalBlendMode = 1;
    m.flags = 0x40C;
    CHECK(stockDecalShading(v, m).normalAdd);
    // Opacity map (channel 2, flag 64): sampled at the material UV, unless the volume has no mask, an identity UV and
    // A = 1, B = 0: then it moves into the raw-UV slot (and is remapped, op = a * a).
    StockDecalVolume w = box({}, {0, 0, -1}, {0, 1, 0}, {4, 32, 32});
    m.flags = 0x44C;
    s = stockDecalShading(w, m);
    CHECK(s.maskFromOpacityMap && !s.opacityMap && !s.maskFromVolume);
    CHECK(near(stockDecalOpacity(s, 0.5, 1.0, 0.5, 0.5, 0, 1), 0.25));
    w.uvMatrix = {2, 0, 0, 2};
    s = stockDecalShading(w, m);
    CHECK(!s.maskFromOpacityMap && s.opacityMap);
    CHECK(near(stockDecalOpacity(s, 1.0, 0.5, 0.5, 0.5, 0, 1), 0.5));
    // Alpha dissolve (flag 128): op = saturate(op * alphaDissolve).
    m.flags = 0x48C;
    m.alphaDissolve = 0.5;
    w.uvMatrix = {1, 0, 0, 1};
    s = stockDecalShading(w, m);
    CHECK(s.dissolve && near(stockDecalOpacity(s, 1.0, 1.0, 0.5, 0.5, 0, 1), 0.5));
}

TEST(stock_decal_volumes_clip_floor) {
    const ImportedMesh floor = quad(100, 0);
    StockDecalReceivers r;
    r.addMesh(floor);
    r.build();
    CHECK(r.triangleCount() == 2);
    // Box fully on the quad: the clipped area is the box's 64 x 32 footprint.
    const StockDecalVolume v = box({10, 20, 0}, {0, 0, -1}, {0, 1, 0}, {4, 32, 16});
    StockDecalBatch b;
    const size_t n = projectDecalVolume(r, v, b);
    CHECK(n >= 2 && b.idx.size() == 3 * n);
    CHECK(near(batchArea(b), 64.0 * 32.0, 1e-6));
    CHECK(b.candidateTriangles == 2 && b.clippedTriangles == 2);
    bool uvOk = true, onPlane = true, windingOk = true;
    for (const StockDecalVertex& o : b.verts) {
        uvOk = uvOk && near(o.u, 0.5 + 0.5 * (o.pos.y - 20) / 32, 1e-5) && near(o.v, 0.5 + 0.5 * (o.pos.x - 10) / 16, 1e-5);
        uvOk = uvOk && o.u >= 0 && o.u <= 1 && o.v >= 0 && o.v <= 1 && near(o.depth, 0) && near(o.fade, 1);
        uvOk = uvOk && near(o.mu, o.u, 1e-6) && near(o.mv, o.v, 1e-6) && o.lightmap == -1;
        onPlane = onPlane && near(o.pos.z, 0) && near(o.normal.z, 1);
    }
    for (size_t i = 0; i + 2 < b.idx.size(); i += 3) {
        const Vec3 nrm = cross(b.verts[b.idx[i + 1]].pos - b.verts[b.idx[i]].pos, b.verts[b.idx[i + 2]].pos - b.verts[b.idx[i]].pos);
        windingOk = windingOk && nrm.z > 0;  // the receiver's winding (counter-clockwise from +Z)
    }
    CHECK(uvOk && onPlane && windingOk);
    // The (u, v) corners: u = 0 at y = -12, v = 1 at x = 26.
    bool corner = false;
    for (const StockDecalVertex& o : b.verts) corner = corner || (near(o.pos.x, 26, 1e-6) && near(o.pos.y, -12, 1e-6) && near(o.u, 0) && near(o.v, 1));
    CHECK(corner);

    // Half on the quad's edge: x from 90 to 110 -> 10 of the 20 units are on the quad.
    StockDecalBatch e;
    projectDecalVolume(r, box({100, 0, 0}, {0, 0, -1}, {0, 1, 0}, {4, 8, 10}), e);
    CHECK(near(batchArea(e), 16.0 * 10.0, 1e-6));
    // The box above the quad (depth range 6..14) and beside it: nothing.
    StockDecalBatch above, beside;
    CHECK(projectDecalVolume(r, box({0, 0, 10}, {0, 0, -1}, {0, 1, 0}, {4, 8, 8}), above) == 0);
    CHECK(projectDecalVolume(r, box({300, 0, 0}, {0, 0, -1}, {0, 1, 0}, {4, 8, 8}), beside) == 0 && beside.candidateTriangles == 0);
    // Projected from below (the decal faces -Z): the quad faces away, dropped; kept when dropUnlit is off (fade 0).
    StockDecalBatch back, backKept;
    CHECK(projectDecalVolume(r, box({0, 0, 0}, {0, 0, 1}, {0, 1, 0}, {4, 8, 8}), back) == 0);
    StockDecalProjectOptions keep;
    keep.dropUnlit = false;
    CHECK(projectDecalVolume(r, box({0, 0, 0}, {0, 0, 1}, {0, 1, 0}, {4, 8, 8}), backKept, keep) >= 2);
    bool zeroFade = !backKept.verts.empty();
    for (const StockDecalVertex& o : backKept.verts) zeroFade = zeroFade && o.fade == 0;
    CHECK(zeroFade);
    // A box rotated 45 degrees about Z: the clipped footprint is still 2 h1 x 2 h2.
    const double s = std::sqrt(0.5);
    StockDecalBatch rot;
    projectDecalVolume(r, box({0, 0, 0}, {0, 0, -1}, {s, s, 0}, {2, 20, 10}), rot);
    CHECK(near(batchArea(rot), 40.0 * 20.0, 1e-6));
    // Depth offset along the normal.
    StockDecalProjectOptions push;
    push.offsetAlongNormal = 0.125;
    StockDecalBatch pushed;
    projectDecalVolume(r, v, pushed, push);
    CHECK(!pushed.verts.empty() && near(pushed.verts[0].pos.z, 0.125));
}

TEST(stock_decal_volumes_clip_wall_and_tilt) {
    // A wall in the plane x = 0 facing -X (towards the decal), a stripe projected along +X onto it.
    ImportedMesh wall;
    wall.pos = {{0, -50, 0}, {0, 50, 0}, {0, 50, 100}, {0, -50, 100}};
    wall.normal = {{-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0}};
    wall.idx = {0, 2, 1, 0, 3, 2};
    // A tilted plane through the same box: 80 degrees from the decal normal -> partial fade.
    ImportedMesh tilt;
    const double r = 80 * 3.14159265358979323846 / 180.0;
    const Vec3 n{-std::cos(r), 0, std::sin(r)};
    tilt.pos = {{-200, -40, 0}, {-200, 40, 0}, {-190, 0, 200}};
    tilt.normal = {n, n, n};
    tilt.idx = {0, 1, 2};
    StockDecalReceivers rr;
    rr.addMesh(wall);
    rr.build();
    const StockDecalVolume v = box({-2, 0, 50}, {1, 0, 0}, {0, -1, 0}, {6, 40, 10});  // u along -Y, v along -Z
    CHECK(near(v.axis[2].z, -1));
    StockDecalBatch b;
    projectDecalVolume(rr, v, b);
    CHECK(near(batchArea(b), 80.0 * 20.0, 1e-6));
    bool ok = !b.verts.empty();
    for (const StockDecalVertex& o : b.verts)
        ok = ok && near(o.pos.x, 0) && near(o.depth, -(0 - -2) / 6.0, 1e-6) && near(o.fade, 1) &&
             near(o.u, 0.5 + 0.5 * (-o.pos.y) / 40, 1e-5) && near(o.v, 0.5 + 0.5 * (50 - o.pos.z) / 10, 1e-5);
    CHECK(ok);
    StockDecalReceivers rt;
    rt.addMesh(tilt);
    rt.build();
    StockDecalBatch t;
    const StockDecalVolume tv = box({-195, 0, 100}, {1, 0, 0}, {0, -1, 0}, {50, 30, 30});
    projectDecalVolume(rt, tv, t);
    bool partial = !t.verts.empty();
    for (const StockDecalVertex& o : t.verts) partial = partial && o.fade > 0.2 && o.fade < 0.8;
    CHECK(partial);
}

TEST(stock_decal_volumes_bvh_matches_brute_force) {
    // 20,000 random small triangles; the tree query must return exactly the bounds-overlapping set.
    std::mt19937 rng(1234);
    std::uniform_real_distribution<double> pos(-2000, 2000), off(-20, 20);
    ImportedMesh m;
    for (int i = 0; i < 20000; ++i) {
        const Vec3 c{pos(rng), pos(rng), pos(rng) * 0.1};
        for (int k = 0; k < 3; ++k) m.pos.push_back(c + Vec3{off(rng), off(rng), off(rng)});
        m.normal.insert(m.normal.end(), 3, Vec3{0, 0, 1});
        m.idx.insert(m.idx.end(), {uint32_t(3 * i), uint32_t(3 * i + 1), uint32_t(3 * i + 2)});
    }
    StockDecalReceivers r;
    r.addMesh(m);
    r.build();
    CHECK(r.triangleCount() == 20000);
    bool same = true;
    for (int q = 0; q < 50; ++q) {
        AABB box;
        const Vec3 c{pos(rng), pos(rng), pos(rng) * 0.1};
        box.add(c - Vec3{150, 90, 60});
        box.add(c + Vec3{150, 90, 60});
        std::vector<char> hit(20000, 0);
        size_t found = 0;
        r.query(box, [&](uint32_t ti) {
            hit[r.tris()[ti].first / 3] = 1;
            ++found;
        });
        size_t expect = 0;
        for (int i = 0; i < 20000; ++i) {
            AABB t;
            for (int k = 0; k < 3; ++k) t.add(m.pos[3 * i + k]);
            const bool overlap = !(t.mins.x > box.maxs.x || t.maxs.x < box.mins.x || t.mins.y > box.maxs.y ||
                                   t.maxs.y < box.mins.y || t.mins.z > box.maxs.z || t.maxs.z < box.mins.z);
            if (overlap) {
                ++expect;
                same = same && hit[i];
            }
        }
        same = same && found == expect;  // float bounds are widened by one ulp: no extra hits at these sizes
    }
    CHECK(same);
    // The StockWorld overload: one mesh, the floor decal.
    StockWorld w;
    w.meshes.push_back(quad(100, 0));
    StockDecalBatch b;
    CHECK(projectDecalVolume(w, box({0, 0, 0}, {0, 0, -1}, {0, 1, 0}, {4, 32, 16}), b) >= 2);
    CHECK(near(batchArea(b), 64.0 * 32.0, 1e-6));
    // Draw order: ascending drawOrder, then index.
    StockDecalVolumeSet set;
    for (int i = 0; i < 4; ++i) {
        StockDecalVolume v = box({0, 0, 0}, {0, 0, -1}, {0, 1, 0}, {4, 8, 8});
        v.index = size_t(i);
        v.drawOrder = (i == 1) ? 3 : (i == 3 ? -1 : 0);
        set.volumes.push_back(v);
    }
    const auto order = stockDecalDrawOrder(set);
    CHECK(order == (std::vector<size_t>{3, 0, 2, 1}));
    StockDecalReceivers fr;
    const ImportedMesh fl = quad(100, 0);
    fr.addMesh(fl);
    fr.build();
    const auto batches = projectDecalVolumes(fr, set);
    CHECK(batches.size() == 4 && batches[0].volume == 3 && batches[3].volume == 1);
}
