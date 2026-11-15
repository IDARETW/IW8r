// Stock smodel reader (src/core/stock_smodels.h): a synthetic ACTS gfx_map export in the smodels layout of
// docs/research/48 section 4, the axis / angles inverse against anglesToMatrix (prefab.h), and, when the editor's
// world cache for mp_shipment exists, a read of the real export (skipped silently when it does not).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "geometry.h"
#include "stock_smodels.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

namespace fs = std::filesystem;

std::string hexOf(const std::vector<uint8_t>& b) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (uint8_t v : b) {
        s += d[v >> 4];
        s += d[v & 15];
    }
    return s;
}

void putU32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i)));
}

void putI32(std::vector<uint8_t>& b, int32_t v) {
    uint32_t u;
    std::memcpy(&u, &v, 4);
    putU32(b, u);
}

void putF32(std::vector<uint8_t>& b, float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    putU32(b, u);
}

void putU16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
}

// Orientation component encoding used by the export: c in [-1, 1] -> u16, zero is 0x8000.
uint16_t enc(double c) { return uint16_t(std::lround((c + 1.0) * 32767.5)); }

// One GfxSModelInstanceData record: translation in 1/4096 inch, quaternion (x, y, z, w), scale.
void putInstance(std::vector<uint8_t>& b, Vec3 origin, double x, double y, double z, double w, float scale) {
    putI32(b, int32_t(std::lround(origin.x * 4096.0)));
    putI32(b, int32_t(std::lround(origin.y * 4096.0)));
    putI32(b, int32_t(std::lround(origin.z * 4096.0)));
    putU16(b, enc(x));
    putU16(b, enc(y));
    putU16(b, enc(z));
    putU16(b, enc(w));
    putF32(b, scale);
}

// One GfxStaticModelCollection record.
void putCollection(std::vector<uint8_t>& b, uint32_t firstInstance, uint32_t instanceCount, uint32_t smodelIndex) {
    putU32(b, firstInstance);
    putU32(b, instanceCount);
    putU32(b, smodelIndex);
    putU32(b, 0);
}

// Synthetic gfx_map JSON with only the smodels part the reader uses. Three models-worth of instances:
//   collection 0: model tm/a, instances 0 and 1 (drawn in that order)
//   collection 1: model tm/b, instance 2
std::string smodelsJson(bool badModelIndex) {
    std::vector<uint8_t> coll, inst;
    putCollection(coll, 0, 2, 0);
    putCollection(coll, 2, 1, badModelIndex ? 7 : 1);
    // inst 0: identity orientation, origin (100, -50.5, 8), scale 1
    putInstance(inst, Vec3{100, -50.5, 8}, 0, 0, 0, 1, 1.0f);
    // inst 1: yaw 90 about Z (quaternion (0, 0, sin 45, cos 45)), origin 0, scale 2.5
    const double s45 = std::sqrt(0.5);
    putInstance(inst, Vec3{0, 0, 0}, 0, 0, s45, s45, 2.5f);
    // inst 2: roll 30 about X (quaternion (sin 15, 0, 0, cos 15)), origin (-10, 20, 30), scale 1
    const double s15 = std::sin(15.0 * 3.14159265358979323846 / 180.0), c15 = std::cos(15.0 * 3.14159265358979323846 / 180.0);
    putInstance(inst, Vec3{-10, 20, 30}, s15, 0, 0, c15, 1.0f);

    // Lighting indirection per instance row: u32 lightingInstanceIndirection, u16 splined index, u16 worldTransient.
    std::vector<uint8_t> indir;
    putU32(indir, 7);
    putU16(indir, 0);
    putU16(indir, 0);
    putU32(indir, 0xFFFFFFFFu);
    putU16(indir, 0);
    putU16(indir, 0);
    putU32(indir, 2);
    putU16(indir, 5);
    putU16(indir, 3);

    std::string s = R"({"format":"mw19-asset-json","format_version":1,"profile":"replay-1.20","pool":"gfx_map",)"
                    R"("root_type":"GfxWorld","asset":{"type":"GfxWorld","fields":{"name":{"string":"maps/mp/test.d3dbsp"},)"
                    R"("smodels":{"collectionCount":2,"instanceCount":3,)"
                    R"("models":{"type":"GfxStaticModel","stride":16,"count":2,"values":[)"
                    R"({"model":{"type":"XModel","asset_reference":true,"name":"tm/a"},"flags":{"value":1}},)"
                    R"({"model":{"type":"XModel","asset_reference":true,"name":"tm/b"},"flags":{"value":5}}]},)"
                    R"("collections":{"type":"GfxStaticModelCollection","stride":16,"count":2,"encoding":"hex-little-endian","bytes":")" +
                    hexOf(coll) + R"("},)" + R"("smodelInstanceData":{"type":"GfxSModelInstanceData","stride":24,"count":3,)"
                    R"("encoding":"hex-little-endian","bytes":")" + hexOf(inst) + R"("},)" +
                    R"("modelStaticIndirection":{"type":"GfxModelStaticIndirection","stride":8,"count":3,)"
                    R"("encoding":"hex-little-endian","bytes":")" + hexOf(indir) + R"("}}}}})";
    return s;
}

void writeText(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f << text;
}

// The "value" of a key in an entity key/value list ("" when missing).
std::string entityValue(const std::vector<std::pair<std::string, std::string>>& kv, const std::string& key) {
    for (const auto& [k, v] : kv)
        if (k == key) return v;
    return {};
}

std::array<double, 3> parseTriple(const std::string& s) {
    double a = 0, b = 0, c = 0;
    std::sscanf(s.c_str(), "%lf %lf %lf", &a, &b, &c);
    return {a, b, c};
}

bool nearVec(const Vec3& a, const Vec3& b, double tol = 1e-6) { return length(a - b) < tol; }

bool sameAxis(const std::array<Vec3, 3>& a, const std::array<Vec3, 3>& b, double tol = 1e-6) {
    return nearVec(a[0], b[0], tol) && nearVec(a[1], b[1], tol) && nearVec(a[2], b[2], tol);
}

}  // namespace

TEST(stock_smodels_synthetic_export) {
    const fs::path zone = fs::path(iw8r::test::tmpDir()) / "stock_smodels_zone";
    writeText(zone / "assets" / "gfx_map" / "maps" / "mp" / "test.d3dbsp.1.asset.json", smodelsJson(false));

    std::vector<StockModelInstance> v;
    std::string err;
    CHECK(readStockModelInstances(zone.string(), v, &err));
    CHECK(err.empty());
    CHECK(v.size() == 3);
    if (v.size() != 3) return;

    // Collection order: tm/a (instances 0, 1), then tm/b (instance 2).
    CHECK(v[0].model == "tm/a");
    CHECK(v[1].model == "tm/a");
    CHECK(v[2].model == "tm/b");
    // Each placement knows its row in smodelInstanceData (11.5: the converter hides a prop by that row).
    CHECK(v[0].index == 0 && v[1].index == 1 && v[2].index == 2);
    // Lighting: modelStaticIndirection per instance row and the model flags (tm/a 1 = LIGHTPROBE, tm/b 5).
    CHECK(v[0].lightingIndex == 7 && v[0].worldTransient == 0 && v[0].modelFlags == 1);
    CHECK(v[1].lightingIndex == 0xFFFFFFFFu && v[1].worldTransient == 0 && v[1].modelFlags == 1);
    CHECK(v[2].lightingIndex == 2 && v[2].worldTransient == 3 && v[2].modelFlags == 5);

    CHECK(nearVec(v[0].origin, Vec3{100, -50.5, 8}));
    CHECK(sameAxis(v[0].axis, {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}}, 1e-4));
    CHECK(std::fabs(v[0].scale - 1.0) < 1e-6);

    // Yaw 90: model X points along +Y, model Y along -X.
    CHECK(nearVec(v[1].origin, Vec3{0, 0, 0}));
    CHECK(sameAxis(v[1].axis, {Vec3{0, 1, 0}, Vec3{-1, 0, 0}, Vec3{0, 0, 1}}, 1e-4));
    CHECK(std::fabs(v[1].scale - 2.5) < 1e-6);

    // Roll 30 about X: columns (1,0,0), (0,cos30,sin30), (0,-sin30,cos30).
    const double c30 = std::cos(30.0 * 3.14159265358979323846 / 180.0), s30 = 0.5;
    CHECK(nearVec(v[2].origin, Vec3{-10, 20, 30}));
    CHECK(sameAxis(v[2].axis, {Vec3{1, 0, 0}, Vec3{0, c30, s30}, Vec3{0, -s30, c30}}, 1e-4));

    // Usage counts.
    const auto use = stockModelUsage(v);
    CHECK(use.size() == 2);
    CHECK(use.count("tm/a") && use.at("tm/a") == 2);
    CHECK(use.count("tm/b") && use.at("tm/b") == 1);

    // Entities: classname, model, origin, angles (editor convention), modelscale.
    const auto e1 = stockModelInstancesAsEntities(v[1]);
    CHECK(entityValue(e1, "classname") == "misc_model");
    CHECK(entityValue(e1, "model") == "tm/a");
    const auto ang1 = parseTriple(entityValue(e1, "angles"));
    // The export stores the orientation in 16 bits per component: about 1e-5 in the quaternion, 0.01 degree in angles.
    CHECK(std::fabs(ang1[0]) < 1e-2 && std::fabs(ang1[1] - 90) < 1e-2 && std::fabs(ang1[2]) < 1e-2);
    CHECK(std::fabs(std::stod(entityValue(e1, "modelscale")) - 2.5) < 1e-6);

    const auto e2 = stockModelInstancesAsEntities(v[2]);
    const auto o2 = parseTriple(entityValue(e2, "origin"));
    CHECK(std::fabs(o2[0] + 10) < 1e-6 && std::fabs(o2[1] - 20) < 1e-6 && std::fabs(o2[2] - 30) < 1e-6);
    const auto ang2 = parseTriple(entityValue(e2, "angles"));
    CHECK(std::fabs(ang2[0]) < 1e-2 && std::fabs(ang2[1]) < 1e-2 && std::fabs(ang2[2] - 30) < 1e-2);
    CHECK(std::fabs(std::stod(entityValue(e2, "modelscale")) - 1.0) < 1e-6);

    // The gfx_map file itself is accepted as the path too.
    std::vector<StockModelInstance> viaFile;
    CHECK(readStockModelInstances((zone / "assets" / "gfx_map" / "maps" / "mp" / "test.d3dbsp.1.asset.json").string(),
                                  viaFile, &err));
    CHECK(viaFile.size() == 3);
}

// The "values" form (mp_m_speedball, mp_m_king): collections, instances and the lighting indirection as objects.
TEST(stock_smodels_lighting_values_form) {
    const auto json = [](bool withIndirection) {
        std::string s = R"({"asset":{"fields":{"smodels":{)"
                        R"("models":{"values":[{"model":{"name":"tm/lm"},"flags":{"value":2,"name":"LIGHTMAP"}},)"
                        R"({"model":{"name":"tm/p"},"flags":9}]},)"
                        R"("collections":{"values":[{"firstInstance":0,"instanceCount":1,"smodelIndex":1},)"
                        R"({"firstInstance":1,"instanceCount":1,"smodelIndex":0}]},)"
                        R"("smodelInstanceData":{"values":[)"
                        R"({"translation":[4096,0,0],"orientation":{"v":[2147516416,4294934528]},"scale":1.0},)"
                        R"({"translation":[0,8192,0],"orientation":{"v":[2147516416,4294934528]},"scale":2.0}]})";
        if (withIndirection)
            s += R"(,"modelStaticIndirection":{"stride":8,"count":2,"values":[)"
                 R"({"lightingInstanceIndirection":11,"splinedModelInstanceIndex":0,"worldTransient":2},)"
                 R"({"lightingInstanceIndirection":0,"splinedModelInstanceIndex":0,"worldTransient":0}]})";
        s += "}";  // smodels
        if (withIndirection)  // the lightmapped placement's rectangle (research 105 section 3.2)
            s += R"(,"lmapLookup":{"stride":20,"count":1,"values":[{"lmapIndex":3,"offset":{"$union_views":true,"v":[0.25,0.5]},)"
                 R"("scaleWidth":0.125,"scaleHeight":0.0625}]})";
        return s + "}}}";
    };
    const fs::path dir = fs::path(iw8r::test::tmpDir()) / "stock_smodels_values";
    const fs::path with = dir / "with.json", without = dir / "without.json";
    writeText(with, json(true));
    writeText(without, json(false));

    std::vector<StockModelInstance> v;
    std::string err;
    CHECK(readStockModelInstances(with.string(), v, &err));
    CHECK(v.size() == 2);
    if (v.size() == 2) {
        CHECK(v[0].model == "tm/p" && v[0].index == 0 && v[0].modelFlags == 9);
        CHECK(v[0].lightingIndex == 11 && v[0].worldTransient == 2);
        CHECK(v[1].model == "tm/lm" && v[1].index == 1 && v[1].modelFlags == 2);
        CHECK(v[1].lightingIndex == 0 && v[1].worldTransient == 0);
        // tm/lm is lightmapped (flag 2): lmapLookup[0]; tm/p (probe-lit) has none.
        CHECK(v[1].lmapPage == 3 && v[1].lmapRect[0] == 0.25f && v[1].lmapRect[1] == 0.5f && v[1].lmapRect[2] == 0.125f &&
              v[1].lmapRect[3] == 0.0625f);
        CHECK(v[0].lmapPage == -1);
        CHECK(nearVec(v[0].origin, Vec3{1, 0, 0}) && nearVec(v[1].origin, Vec3{0, 2, 0}));
    }

    // Without modelStaticIndirection the instances keep the defaults; the flags still come through.
    CHECK(readStockModelInstances(without.string(), v, &err));
    CHECK(v.size() == 2);
    if (v.size() == 2) {
        CHECK(v[0].lightingIndex == 0xFFFFFFFFu && v[0].worldTransient == 0 && v[0].modelFlags == 9);
        CHECK(v[1].lightingIndex == 0xFFFFFFFFu && v[1].modelFlags == 2);
    }
}

TEST(stock_smodels_rejects_bad_export) {
    const fs::path zone = fs::path(iw8r::test::tmpDir()) / "stock_smodels_bad";
    writeText(zone / "assets" / "gfx_map" / "maps" / "mp" / "bad.d3dbsp.1.asset.json", smodelsJson(true));
    std::vector<StockModelInstance> v;
    std::string err;
    CHECK(!readStockModelInstances(zone.string(), v, &err));
    CHECK(!err.empty());
    CHECK(v.empty());

    std::vector<StockModelInstance> none;
    CHECK(!readStockModelInstances((fs::path(iw8r::test::tmpDir()) / "stock_smodels_missing").string(), none, &err));
    CHECK(none.empty());
}

TEST(stock_smodels_axis_angles_round_trip) {
    const Vec3 samples[] = {
        {0, 0, 0},       {0, 90, 0},     {0, 0, 90},      {0, -90, 0},      {0, 180, 0},
        {30, -45, 60},   {-60, 170, -20}, {10, 10, 10},    {89.5, 10, 10},   {-35, 250, 75},
        {90, 30, 0},     {-90, 0, 45},   {90, 0, 0},      {-90, -60, 130},  {0.25, 0.5, 0.75},
    };
    for (const Vec3& a : samples) {
        const auto axis = anglesToAxis(a);
        // Orthonormal, right-handed rotation (anglesToMatrix is a proper rotation).
        CHECK(std::fabs(length(axis[0]) - 1) < 1e-9);
        CHECK(std::fabs(length(axis[1]) - 1) < 1e-9);
        CHECK(std::fabs(length(axis[2]) - 1) < 1e-9);
        CHECK(std::fabs(dot(axis[0], cross(axis[1], axis[2])) - 1) < 1e-9);

        // axis -> angles -> axis reproduces the axes exactly (including the gimbal-lock cases).
        const Vec3 back = axisToAngles(axis);
        CHECK(sameAxis(anglesToAxis(back), axis, 1e-9));

        // Away from gimbal lock the angles themselves come back.
        if (std::fabs(std::fabs(a.x) - 90) > 1e-6) {
            CHECK(std::fabs(back.x - a.x) < 1e-6);
            CHECK(std::fabs(back.y - a.y) < 1e-6 || std::fabs(std::fabs(back.y - a.y) - 360) < 1e-6);
            CHECK(std::fabs(back.z - a.z) < 1e-6 || std::fabs(std::fabs(back.z - a.z) - 360) < 1e-6);
        }
    }

    // Gimbal lock: pitch -90 folds the roll into yaw, roll comes back 0.
    const Vec3 gl = axisToAngles(anglesToAxis({-90, 0, 45}));
    CHECK(std::fabs(gl.z) < 1e-9);
    CHECK(std::fabs(gl.x + 90) < 1e-4);  // asin is ill-conditioned at -1

    // Scaled or unnormalised columns are normalised first.
    const auto scaled = anglesToAxis({0, 90, 0});
    const std::array<Vec3, 3> big{scaled[0] * 4.0, scaled[1] * 4.0, scaled[2] * 4.0};
    CHECK(sameAxis(anglesToAxis(axisToAngles(big)), scaled, 1e-9));
}

TEST(stock_smodels_real_mp_shipment_cache) {
    const char* local = std::getenv("LOCALAPPDATA");
    if (!local) return;
    const fs::path dir = fs::path(local) / "IW8 Radiant Suite" / "IW8 Radiant Suite" / "world_cache" / "mp_shipment";
    std::error_code ec;
    if (!fs::exists(dir, ec)) return;

    std::vector<StockModelInstance> v;
    std::string err;
    CHECK(readStockModelInstances(dir.string(), v, &err));
    CHECK(v.size() > 100);

    const auto use = stockModelUsage(v);
    std::string top;
    size_t best = 0;
    for (const auto& [model, n] : use)
        if (n > best) {
            best = n;
            top = model;
        }
    CHECK(!top.empty());
    CHECK(best > 0);

    // Lighting (docs/research/105 section 1.1): nearly every probe-lit instance has a box index.
    size_t probeLit = 0, indexed = 0;
    for (const StockModelInstance& s : v)
        if (s.modelFlags & 1) {
            ++probeLit;
            if (s.lightingIndex != 0xFFFFFFFFu) ++indexed;
        }
    CHECK(probeLit > 100);
    CHECK(indexed * 10 >= probeLit * 9);
}
