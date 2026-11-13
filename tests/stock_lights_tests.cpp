// Stock local lights (src/core/stock_lights.h, docs/research/106): ComWorld and lightdef parsing, Replay's packed
// record and the shader's diffuse, against research 106 section 5.6 (mp_hideout lights 82, 118 and 9, transcribed from
// the game's packer and world shader by locallight.py). The light fields are those of the cached hideout export.
#include <cmath>
#include <string>
#include <vector>

#include "stock_lights.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

bool near(double a, double b, double rel) { return std::fabs(a - b) <= rel * std::max(std::fabs(b), 1e-6); }

const char* kComWorld = R"({"asset":{"type":"ComWorld","fields":{"primaryLightCount":5,"primaryLights":{"values":[
{"type":0,"intensity":0.0,"colorLinearSrgb":{"$union_views":true,"v":[0,0,0]},"radius":0.0},
{"type":1,"intensity":5.2,"colorLinearSrgb":{"$union_views":true,"v":[1,1,1]},"dir":{"v":[0.3,0.2,0.9]}},
{"type":2,"canUseShadowMap":1,"needsDynamicShadows":0,"isVolumetric":1,"intensity":123345.328125,
 "colorLinearSrgb":{"$union_views":true,"v":[0.8430442810058594,1.0389790534973145,1.0760520696640015]},
 "dir":{"$union_views":true,"v":[0.0010421324986964464,-5.427864380180836e-09,0.999999463558197]},
 "up":{"$union_views":true,"v":[-0.999999463558197,5.201494786888361e-06,0.0010421324986964464]},
 "origin":{"$union_views":true,"v":[-128.00599670410156,392.3819885253906,286.47698974609375]},"radius":250.0,
 "bulbRadius":2.0,"bulbLength":{"$union_views":true,"v":[0.00013003744243178517,25.0,-0.0]},
 "cosHalfFovOuter":0.4999999701976776,"cosHalfFovInner":0.9990482330322266,"shadowNearPlaneBias":8.0,
 "shadowSoftness":0.550000011920929,"shadowBias":0.8500000238418579,"distanceFalloff":0.20000000298023224,
 "cosHalfFovCollimation":-4.371138828673793e-08,"rotationLimit":1.0,"defName":{"string":"ies_6"}},
{"type":3,"canUseShadowMap":1,"isVolumetric":1,"intensity":740.0719604492188,
 "colorLinearSrgb":{"$union_views":true,"v":[1.3206156492233276,0.9618439078330994,0.4338829219341278]},
 "dir":{"$union_views":true,"v":[-0.011975016444921494,-0.010361455380916595,0.9998746514320374]},
 "origin":{"$union_views":true,"v":[13.3774995803833,-2152.10009765625,79.99569702148438]},"radius":150.0,
 "bulbRadius":1.0,"bulbLength":{"$union_views":true,"v":[0.002565966220572591,-0.002965555526316166,1.8261227599514207e-11]},
 "cosHalfFovOuter":0.08715580403804779,"cosHalfFovInner":0.9990482330322266,"distanceFalloff":0.20000000298023224,
 "cosHalfFovCollimation":0.0,"rotationLimit":1.0,"defName":{"string":"light_point_linear"}},
{"type":2,"canUseShadowMap":1,"needsDynamicShadows":1,"isVolumetric":1,"intensity":78941.0078125,
 "colorLinearSrgb":{"$union_views":true,"v":[1.1562989950180054,0.98490309715271,0.6893081665039062]},
 "dir":{"$union_views":true,"v":[-0.005930157843977213,-0.005139872431755066,0.9999691247940063]},
 "origin":{"$union_views":true,"v":[-1368.1199951171875,-1520.4000244140625,118.66500091552734]},"radius":280.0,
 "bulbRadius":5.0,"bulbLength":{"$union_views":true,"v":[0.0025684700813144445,-0.00296338670887053,3.652245606639015e-12]},
 "cosHalfFovOuter":0.5735764503479004,"cosHalfFovInner":0.9990482330322266,"distanceFalloff":0.20000000298023224,
 "cosHalfFovCollimation":-4.371138828673793e-08,"rotationLimit":1.0,"defName":{"string":"ies_6"}}
]}}}})";

const char* kDef = R"({"format":"mw19-asset-json","pool":"lightdef","asset":{"type":"GfxLightDef","fields":{
"name":{"type":"const char","string":"ies_6"},"iesProfile":{"type":"const char","string":"IES_6"},
"firstPixel":128,"coordOffset":0.2509765625,"coordScale":0.498046875}}})";

}  // namespace

TEST(stock_lights_parse_and_filter) {
    std::vector<StockPrimaryLight> lights;
    std::string err;
    CHECK(parseStockPrimaryLights(kComWorld, lights, &err));
    CHECK(lights.size() == 5);
    CHECK(lights[1].type == 1 && lights[2].type == 2 && lights[3].type == 3);
    CHECK(lights[2].index == 2 && lights[2].defName == "ies_6" && lights[2].canUseShadowMap && lights[2].isVolumetric);
    CHECK(near(lights[2].bulbLength[1], 25.0, 1e-6) && near(lights[2].origin[2], 286.477, 1e-5));
    StockGameLight g;
    CHECK(!packStockLight(lights[0], nullptr, g));  // the empty light
    CHECK(!packStockLight(lights[1], nullptr, g));  // the sun is not a local light
    StockPrimaryLight closed = lights[2];
    closed.cosHalfFovOuter = 1.0f;
    CHECK(!packStockLight(closed, nullptr, g));
    StockPrimaryLight tiny = lights[3];
    tiny.radius = 1.0f;
    CHECK(!packStockLight(tiny, nullptr, g));
    std::vector<StockPrimaryLight> none;
    CHECK(!parseStockPrimaryLights("{\"asset\":{\"fields\":{}}}", none, &err));
}

TEST(stock_lights_lightdef_and_half) {
    StockLightDef d;
    std::string err;
    CHECK(parseStockLightDef(kDef, d, &err));
    CHECK(d.name == "ies_6" && d.iesProfile == "IES_6" && d.firstPixel == 128);
    CHECK(near(halfToFloat(floatToHalf(d.coordOffset)), 0.2509766, 1e-6));
    CHECK(near(halfToFloat(floatToHalf(d.coordScale)), 0.4980469, 1e-6));
    CHECK(floatToHalf(1.0f) == 0x3C00 && floatToHalf(-2.0f) == 0xC000 && floatToHalf(0.0f) == 0);
    CHECK(halfToFloat(0x3555) > 0.333f && halfToFloat(0x3555) < 0.3334f);
    CHECK(near(halfToFloat(floatToHalf(65504.0f)), 65504.0, 1e-9));
    CHECK(halfToFloat(floatToHalf(1e-7f)) < 1.2e-7f);  // subnormal
}

TEST(stock_lights_pack_matches_research_106) {
    std::vector<StockPrimaryLight> lights;
    CHECK(parseStockPrimaryLights(kComWorld, lights));
    StockLightDef def;
    CHECK(parseStockLightDef(kDef, def));
    StockGameLight g82, g118, g9;
    CHECK(packStockLight(lights[2], &def, g82));
    CHECK(packStockLight(lights[3], nullptr, g118));
    CHECK(packStockLight(lights[4], &def, g9));
    CHECK(stockGameLightType(g82) == 2 && stockGameLightType(g118) == 3);
    CHECK((g82.packed & 0xFFu) == 0xFFu && (g82.packed & (1u << 30)) != 0);
    // Light 82: diffuse (26087.38, 32150.43, 33297.63); spot (2.003814, -1.001907, 1.154700, 5.0); bulbDir
    // (0.00013, 25, 0); bulbRadius 2; invRadius 0.004; IES halves (0.2509766, 0.4980469).
    CHECK(near(g82.diffuse[0], 26087.38, 1e-4) && near(g82.diffuse[1], 32150.43, 1e-4) && near(g82.diffuse[2], 33297.63, 1e-4));
    CHECK(near(g82.spot[0], 2.003814, 1e-5) && near(g82.spot[1], -1.001907, 1e-5) && near(g82.spot[2], 1.154700, 1e-5) &&
          near(g82.spot[3], 5.0, 1e-6));
    CHECK(near(g82.bulbDir[1], 25.0, 1e-6) && std::fabs(g82.bulbDir[2]) < 1e-6 && near(g82.bulbRadius, 2.0, 1e-9));
    CHECK(near(g82.invRadius, 0.004, 1e-6));
    CHECK(near(halfToFloat(uint16_t(g82.iesLookup & 0xFFFF)), 0.2509766, 1e-6) &&
          near(halfToFloat(uint16_t(g82.iesLookup >> 16)), 0.4980469, 1e-6));
    // Light 118 (a shadowed omni: it keeps its cone for the shadow): diffuse (980.77, 714.32, 322.23); spot
    // (1.096621, -0.095577, 0.087489, 5.0); no IES.
    CHECK(near(g118.diffuse[0], 980.77, 1e-4) && near(g118.diffuse[1], 714.32, 1e-4) && near(g118.diffuse[2], 322.23, 1e-4));
    CHECK(near(g118.spot[0], 1.096621, 1e-5) && near(g118.spot[1], -0.095577, 1e-4) && near(g118.spot[2], 0.087489, 1e-4));
    CHECK(g118.iesLookup == 0);
    // Light 9: diffuse (3663.95, 3120.85, 2184.20); spot (2.350332, -1.348095, 3.501038, 5.0).
    CHECK(near(g9.diffuse[0], 3663.95, 1e-4) && near(g9.diffuse[1], 3120.85, 1e-4) && near(g9.diffuse[2], 2184.20, 1e-4));
    CHECK(near(g9.spot[0], 2.350332, 1e-5) && near(g9.spot[1], -1.348095, 1e-5) && near(g9.spot[2], 3.501038, 1e-5));
    // An unshadowed omni has no cone.
    StockPrimaryLight open = lights[3];
    open.canUseShadowMap = false;
    StockGameLight go;
    CHECK(packStockLight(open, nullptr, go) && go.spot[0] == 0 && go.spot[1] == 0 && near(go.spot[3], 5.0, 1e-6));
    // No bulb length: the game's 1/255 sphere value.
    StockPrimaryLight ball = lights[3];
    ball.bulbLength[0] = ball.bulbLength[1] = ball.bulbLength[2] = 0;
    CHECK(packStockLight(ball, nullptr, go) && near(go.bulbDir[0], 1.0 / 255.0, 1e-6));
    // A bulb length in the light's frame (rotationLimit < 1): x is along -dir.
    StockPrimaryLight tube = lights[3];
    tube.dir[0] = 0, tube.dir[1] = 0, tube.dir[2] = 1;
    tube.bulbLength[0] = 10, tube.bulbLength[1] = 0, tube.bulbLength[2] = 0;
    tube.rotationLimit = 0;
    CHECK(packStockLight(tube, nullptr, go) && near(go.bulbDir[2], -10.0, 1e-6) && std::fabs(go.bulbDir[0]) < 1e-6);
}

TEST(stock_lights_diffuse_matches_research_106) {
    std::vector<StockPrimaryLight> lights;
    CHECK(parseStockPrimaryLights(kComWorld, lights));
    StockLightDef def;
    CHECK(parseStockLightDef(kDef, def));
    StockGameLight g82, g118, g9;
    CHECK(packStockLight(lights[2], &def, g82) && packStockLight(lights[3], nullptr, g118) && packStockLight(lights[4], &def, g9));
    const double up[3] = {0, 0, 1};
    // On the axis the ies_6 strip reads 250/255 (its 0-degree texel); texel 0 (no profile) is white.
    const auto ies6 = [](float coord) { return coord < 0.2 ? 1.0f : 250.0f / 255.0f; };
    {
        const double P[3] = {-128.006, 392.382, 176.0};  // the floor below light 82
        const StockLightEval e = evalStockLightDiffuse(g82, P, up, ies6);
        CHECK(near(e.E[0], 8.382, 0.01) && near(e.E[1], 10.330, 0.01) && near(e.E[2], 10.699, 0.01));
        CHECK(near(e.att, 0.9804, 0.002) && near(e.cosA, 1.0, 1e-4));
    }
    {
        const double P[3] = {-128.006, 392.382, 186.477};  // 100 below the origin
        const StockLightEval e = evalStockLightDiffuse(g82, P, up, ies6);
        CHECK(near(e.E[0], 10.230, 0.01) && near(e.E[1], 12.608, 0.01) && near(e.E[2], 13.058, 0.01));
    }
    {
        const double P[3] = {-128.006, 392.382, 286.477 - 0.85 * 250.0};  // 0.85 R below
        const StockLightEval e = evalStockLightDiffuse(g82, P, up, ies6);
        CHECK(near(e.E[0], 1.912, 0.01) && near(e.E[1], 2.356, 0.01) && near(e.E[2], 2.440, 0.01));
        CHECK(near(e.att, 0.8272, 0.002));
    }
    {
        const double P[3] = {-78.01, 392.38, 199.87};  // 100 units at 30 degrees: the cone cosine
        const StockLightEval e = evalStockLightDiffuse(g82, P, up, ies6);
        CHECK(near(e.cosA, 0.8684, 0.002));
    }
    {
        const double P[3] = {13.3775, -2152.1, -20.0043};  // 100 below light 118
        const StockLightEval e = evalStockLightDiffuse(g118, P, up, ies6);
        CHECK(near(e.E[0], 0.0981, 0.01) && near(e.E[1], 0.0714, 0.01) && near(e.E[2], 0.0322, 0.01));
        CHECK(near(e.att, 1.0, 1e-3));
    }
    {
        const double P[3] = {-1368.12, -1520.4, 18.665};  // 100 below light 9
        const StockLightEval e = evalStockLightDiffuse(g9, P, up, ies6);
        CHECK(near(e.E[0], 8.982, 0.01) && near(e.E[1], 7.650, 0.01) && near(e.E[2], 5.354, 0.01));
        CHECK(near(e.att, 0.9805, 0.002));
    }
    {
        // Behind a spot (above its origin): nothing.
        const double P[3] = {-128.006, 392.382, 330.0};
        CHECK(evalStockLightDiffuse(g82, P, up, ies6).att == 0);
        // Past the radius: nothing.
        const double Q[3] = {-128.006, 392.382, 286.477 - 260.0};
        CHECK(evalStockLightDiffuse(g82, Q, up, ies6).att == 0);
    }
}

TEST(stock_lights_shadow_camera) {
    std::vector<StockPrimaryLight> lights;
    CHECK(parseStockPrimaryLights(kComWorld, lights));
    StockGameLight g82, g118;
    CHECK(packStockLight(lights[2], nullptr, g82) && packStockLight(lights[3], nullptr, g118));
    // Light 82 (research 106 section 4.2): a 25-unit half tube of bulb radius 2 across the axis, so the camera sits
    // 27 cos60 / sin60 = 15.59 behind the origin; near = 15.59 + 8, far = 15.59 + 250; k = 0.45 x 88 + 0.55 = 40.15.
    const StockLightShadow s = stockLightShadowCamera(lights[2], g82);
    CHECK(s.cast);
    CHECK(near(s.fov, 2.0 * std::acos(0.5), 1e-4));
    CHECK(near(s.eye[2], 286.477 + 15.588, 2e-4) && near(s.forward[2], -1.0, 1e-5));
    CHECK(near(s.zNear, 23.588, 1e-3) && near(s.zFar, 265.588, 1e-4));
    CHECK(near(s.k, 40.15, 1e-4) && near(s.bias, 0.85, 1e-5));
    CHECK(std::fabs(s.up[0] * s.forward[0] + s.up[1] * s.forward[1] + s.up[2] * s.forward[2]) < 1e-5);
    float M[16];
    stockLightShadowMatrix(s, M);
    const auto clip = [&](double x, double y, double z, double out[4]) {
        for (int c = 0; c < 4; ++c) out[c] = x * M[c] + y * M[4 + c] + z * M[8 + c] + M[12 + c];
    };
    double c[4];
    clip(-128.006, 392.382, 176.0, c);  // the floor straight below: centre of the map, w = its distance
    CHECK(std::fabs(c[0] / c[3]) < 1e-3 && std::fabs(c[1] / c[3]) < 1e-3 && near(c[3], 286.477 + 15.588 - 176.0, 1e-3));
    CHECK(c[2] / c[3] > 0 && c[2] / c[3] < 1);
    clip(s.eye[0] + s.forward[0] * s.zNear, s.eye[1] + s.forward[1] * s.zNear, s.eye[2] + s.forward[2] * s.zNear, c);
    CHECK(std::fabs(c[2] / c[3]) < 1e-4);  // depth 0 at near
    clip(s.eye[0] + s.forward[0] * s.zFar, s.eye[1] + s.forward[1] * s.zFar, s.eye[2] + s.forward[2] * s.zFar, c);
    CHECK(std::fabs(c[2] / c[3] - 1.0) < 1e-4);  // depth 1 at far
    // A point on the cone edge (60 degrees off the axis) lands on the map's edge.
    const double d = 100.0, off = d * std::tan(std::acos(0.5));
    clip(s.eye[0] + s.up[0] * off + s.forward[0] * d, s.eye[1] + s.up[1] * off + s.forward[1] * d, s.eye[2] + s.up[2] * off + s.forward[2] * d, c);
    CHECK(near(std::fabs(c[1] / c[3]), 1.0, 1e-3));
    // No shadow map: nothing to draw.
    StockPrimaryLight open = lights[3];
    open.canUseShadowMap = false;
    CHECK(!stockLightShadowCamera(open, g118).cast);
}
