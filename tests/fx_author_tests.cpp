#include <cmath>
#include <string>

#include "fx_author.h"
#include "json_lite.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

AuthoredFx sample() {
    AuthoredFx fx;
    fx.name = "iw8r/campfire";
    AuthoredFxElement smoke;
    smoke.name = "smoke";
    smoke.image = "smoke.png";
    smoke.intervalMs = 200;
    smoke.count = 2;
    smoke.lifeMs = 3000;
    smoke.lifeVarMs = 500;
    smoke.radius = 6;
    smoke.colorStart = {0.8, 0.7, 0.6, 0.9};
    smoke.colorEnd = {0.5, 0.5, 0.5, 0};
    AuthoredFxElement sparks;
    sparks.name = "sparks";
    sparks.image = "spark.png";
    sparks.additive = true;
    sparks.looping = false;
    sparks.count = 12;
    sparks.lifeMs = 800;
    sparks.velocityMin = {-60, -60, 100};
    sparks.velocityMax = {60, 60, 220};
    sparks.gravity = 0.5;
    fx.elements = {sparks, smoke};  // one-shot first on purpose: the graph puts looping first
    return fx;
}

long long inum(const json::Value* v) { return v && v->number() ? (long long)std::llround(*v->number()) : -999999; }

}  // namespace

TEST(fx_author_roundtrip) {
    const AuthoredFx fx = sample();
    AuthoredFx back;
    std::string err;
    CHECK(parseAuthoredFx(authoredFxJson(fx), back, &err));
    CHECK(err.empty() && back.name == fx.name && back.elements.size() == 2);
    if (back.elements.size() != 2) return;
    CHECK(back.elements[0].additive && !back.elements[0].looping && back.elements[0].count == 12);
    CHECK(std::fabs(back.elements[1].colorStart[3] - 0.9) < 1e-9 && back.elements[1].lifeVarMs == 500);
    CHECK(!parseAuthoredFx("{\"elements\": []}", back, &err) && !err.empty());
    CHECK(!parseAuthoredFx("{\"elements\": [{\"count\": 0}]}", back, &err));
    CHECK(!parseAuthoredFx("{\"elements\": [{\"color_start\": [2, 0, 0, 1]}]}", back, &err));
    CHECK(isValidAuthoredFxName("iw8r/campfire_01") && !isValidAuthoredFxName("/abs") && !isValidAuthoredFxName("a/../b") &&
          !isValidAuthoredFxName("bad name"));
}

// The converter's own rules (x64-zt iw3_fastfile.cpp ReadPreparedFx / FxElement) on the written graph.
TEST(fx_author_iw3_graph_matches_converter_rules) {
    const AuthoredFx fx = sample();
    std::string err;
    const auto doc = json::parse(authoredFxIw3Json(fx), &err);
    CHECK(doc.has_value());
    if (!doc) return;
    CHECK(inum(doc->get("schema")) == 1 && doc->get("asset_type") && *doc->get("asset_type")->string() == "iw3_fx");
    const json::Value* layout = doc->get("layout");
    CHECK(layout && inum(layout->get("FxEffectDef")) == 32 && inum(layout->get("FxElemDef")) == 252 && inum(layout->get("FxTrailDef")) == 28);
    const json::Value* counts = doc->get("element_counts");
    const json::Value* els = doc->get("elements");
    CHECK(counts && els && inum(counts->get("looping")) == 0 && inum(counts->get("one_shot")) == 2 && inum(counts->get("total")) == 2 &&
          els->arr.size() == 2);
    CHECK(inum(doc->get("msec_looping_life")) == 0);
    CHECK(authoredFxPeriodMs(fx) == 1000);
    CHECK(doc->get("dependencies") && doc->get("dependencies")->arr.size() == 2);
    if (!els || els->arr.size() != 2) return;
    // Source order. Every element is a burst (the converter has no looping emitters): the looping smoke becomes
    // period / interval spawns with their delays spread over the period, replayed by the map script.
    const json::Value& sparks = els->arr[0];
    const json::Value& smoke = els->arr[1];
    CHECK(inum(smoke.get("spawn_delay_msec")->get("amplitude")) == 1000 && inum(sparks.get("spawn_delay_msec")->get("amplitude")) == 0);
    CHECK(smoke.get("visuals")->arr[0].get("name")->str == authoredFxMaterialName(fx, 1));
    for (const json::Value* e : {&smoke, &sparks}) {
        const json::Value* spawn = e->get("spawn");
        const json::Value* raw = spawn->get("raw");
        // Spawn union views agree.
        CHECK(inum(raw->at(0)) == inum(spawn->get("looping")->get("interval_msec")) &&
              inum(raw->at(1)) == inum(spawn->get("looping")->get("count")) &&
              inum(raw->at(0)) == inum(spawn->get("one_shot")->get("count")->get("base")) &&
              inum(raw->at(1)) == inum(spawn->get("one_shot")->get("count")->get("amplitude")));
        CHECK(e->get("velocity_samples")->arr.size() == size_t(inum(e->get("vel_interval_count")) + 1));
        CHECK(e->get("visual_samples")->arr.size() == size_t(inum(e->get("vis_state_interval_count")) + 1));
        CHECK(e->get("visuals")->arr.size() == size_t(inum(e->get("visual_count"))) && e->get("visuals_present")->b);
        CHECK(inum(e->get("elem_type")) == 0 && e->get("elem_type_name")->str == "sprite_billboard");
        CHECK(e->get("trail")->isNull() && e->get("effect_on_impact")->isNull());
        for (const char* k : {"angular_velocity", "spawn_angles", "spawn_origin"}) CHECK(e->get(k)->arr.size() == 3);
    }
    CHECK(inum(smoke.get("spawn")->get("raw")->at(0)) == 10 && inum(smoke.get("spawn")->get("raw")->at(1)) == 0);
    CHECK(inum(sparks.get("spawn")->get("raw")->at(0)) == 12 && inum(sparks.get("spawn")->get("raw")->at(1)) == 0);
    // Flags: sphere offset on the smoke, gravity on the sparks, the world velocity graph on both.
    CHECK((inum(smoke.get("flags")) & 0x10) && !(inum(smoke.get("flags")) & 0x4000000));
    CHECK((inum(sparks.get("flags")) & 0x4000000) && (inum(sparks.get("flags")) & 0x2000000));
    // Velocity in units per millisecond: 100 units/s -> 0.1.
    const json::Value* v0 = sparks.get("velocity_samples")->arr[0].get("world")->get("velocity");
    CHECK(std::fabs(*v0->get("base")->arr[2].number() - 0.1) < 1e-9 && std::fabs(*v0->get("amplitude")->arr[2].number() - 0.12) < 1e-9);
    // Colour bytes B, G, R, A: the smoke starts at R 0.8 (204), G 0.7 (179), B 0.6 (153), A 0.9 (230).
    const json::Value* c0 = smoke.get("visual_samples")->arr[0].get("base")->get("color");
    CHECK(inum(c0->at(0)) == 153 && inum(c0->at(1)) == 179 && inum(c0->at(2)) == 204 && inum(c0->at(3)) == 230);
    const json::Value* c1 = smoke.get("visual_samples")->arr[1].get("base")->get("color");
    CHECK(inum(c1->at(3)) == 0);
}

TEST(fx_author_material_and_preview) {
    const AuthoredFx fx = sample();
    std::string err;
    const auto m0 = json::parse(authoredFxMaterialJson(fx, 0), &err);
    const auto m1 = json::parse(authoredFxMaterialJson(fx, 1), &err);
    CHECK(m0 && m1);
    if (!m0 || !m1) return;
    CHECK(m0->get("techniqueSet")->str == "effect_zfeather_add" && m1->get("techniqueSet")->str == "effect_zfeather");
    CHECK(m0->get("_game")->str == "iw3" && m0->get("_type")->str == "material" && inum(m0->get("_version")) == 1);
    CHECK(inum(m0->get("stateFlags")) == 16 && m0->get("cameraRegion")->str == "emissive");
    const json::Value* tex = m0->get("textures");
    CHECK(tex && tex->arr.size() == 1 && tex->arr[0].get("semantic")->str == "colorMap" &&
          tex->arr[0].get("image")->str == authoredFxImageName(fx, 0));
    CHECK(authoredFxMaterialName(fx, 0) != authoredFxMaterialName(fx, 1));
    // ReadFxMaterial's blend mappings: additive srcalpha / one, alpha srcalpha / invsrcalpha with alpha test gt0; constants.
    const json::Value* st0 = m0->get("stateBits");
    const json::Value* st1 = m1->get("stateBits");
    CHECK(st0 && st0->arr.size() == 1 && st0->arr[0].get("dstBlendRgb")->str == "one" && st0->arr[0].get("srcBlendRgb")->str == "srcalpha" &&
          !st0->arr[0].get("depthWrite")->b && st0->arr[0].get("blendOpRgb")->str == "add");
    CHECK(st1 && st1->arr[0].get("dstBlendRgb")->str == "invsrcalpha" && st1->arr[0].get("alphaTest")->str == "gt0");
    const json::Value* k = m0->get("constants");
    CHECK(k && k->arr.size() == 2 && k->arr[1].get("name")->str == "featherParms" && *k->arr[1].get("literal")->arr[0].number() > 0);
    // Preview: the sparks burst at t = 0.4 s (all 12 alive, risen), the smoke keeps spawning.
    const auto p = simulateAuthoredFx(fx, 0.4);
    size_t sparks = 0, smoke = 0;
    double sparkZ = 0;
    for (const auto& q : p) {
        if (q.element == 0) ++sparks, sparkZ += q.pos[2];
        else ++smoke;
    }
    CHECK(sparks == 12 && sparkZ / 12 > 10);
    CHECK(smoke >= 4 && smoke <= 6);  // spawns at 0, 0.2, 0.4 s, two each
    CHECK(simulateAuthoredFx(fx, 0.9).size() > 0 && simulateAuthoredFx(fx, 0.9).size() < p.size() + 4);
}

// 8.2: a multi-key size / colour curve: sampled linearly, round trips, and becomes 9 evenly spaced IW3 visual samples.
TEST(fx_author_key_curves) {
    AuthoredFx fx;
    fx.name = "iw8r/keys";
    AuthoredFxElement e;
    e.keys = {{0, 4, {1, 1, 1, 0}}, {0.25, 20, {1, 0.5, 0, 1}}, {1, 40, {0.2, 0.2, 0.2, 0}}};
    fx.elements.push_back(e);
    const AuthoredFxKey mid = sampleAuthoredFxKey(e, 0.125);
    CHECK(std::fabs(mid.size - 12) < 1e-9 && std::fabs(mid.color[3] - 0.5) < 1e-9);
    CHECK(std::fabs(sampleAuthoredFxKey(e, -1).size - 4) < 1e-9 && std::fabs(sampleAuthoredFxKey(e, 2).size - 40) < 1e-9);
    AuthoredFx back;
    std::string err;
    CHECK(parseAuthoredFx(authoredFxJson(fx), back, &err) && back.elements.size() == 1 && back.elements[0].keys.size() == 3);
    CHECK(!parseAuthoredFx("{\"elements\": [{\"keys\": [[0, 1, 1, 1, 1, 1]]}]}", back, &err));          // one key
    CHECK(!parseAuthoredFx("{\"elements\": [{\"keys\": [[0, 1, 1, 1, 1, 1], [2, 1, 1, 1, 1, 1]]}]}", back, &err));  // t > 1
    const auto doc = json::parse(authoredFxIw3Json(fx), &err);
    CHECK(doc.has_value());
    if (!doc) return;
    const json::Value& el = doc->get("elements")->arr[0];
    CHECK(inum(el.get("vis_state_interval_count")) == 8 && el.get("visual_samples")->arr.size() == 9);
    // Sample 2 is t = 0.25: size 20, colour R 1 G 0.5 B 0 A 1 -> B, G, R, A bytes 0, 128, 255, 255.
    const json::Value& s2 = el.get("visual_samples")->arr[2];
    CHECK(std::fabs(*s2.get("base")->get("size")->arr[0].number() - 20) < 1e-6);
    const json::Value* c = s2.get("base")->get("color");
    CHECK(inum(c->at(0)) == 0 && inum(c->at(1)) == 128 && inum(c->at(2)) == 255 && inum(c->at(3)) == 255);
    // The preview follows the curve too.
    fx.elements[0].looping = false;
    fx.elements[0].count = 1;
    fx.elements[0].lifeMs = 1000;
    fx.elements[0].velocityMin = fx.elements[0].velocityMax = {0, 0, 0};
    const auto p = simulateAuthoredFx(fx, 0.25);
    CHECK(p.size() == 1 && std::fabs(p[0].size - 20) < 1e-6);
}

// 8.2: an omni light element (IW3 type 6): no visual, no material dependency; radius = size, colour = the curve.
TEST(fx_author_light_element) {
    AuthoredFx fx;
    fx.name = "iw8r/lamp";
    AuthoredFxElement glow;
    glow.kind = "light";
    glow.name = "light";
    glow.sizeStart = glow.sizeEnd = 200;
    glow.colorStart = glow.colorEnd = {1, 0.6, 0.3, 1};
    fx.elements.push_back(glow);
    std::string err;
    const auto doc = json::parse(authoredFxIw3Json(fx), &err);
    CHECK(doc.has_value());
    if (!doc) return;
    const json::Value& el = doc->get("elements")->arr[0];
    CHECK(inum(el.get("elem_type")) == 6 && el.get("elem_type_name")->str == "omni_light");
    CHECK(inum(el.get("visual_count")) == 0 && el.get("visuals")->arr.empty() && !el.get("visuals_present")->b);
    CHECK(doc->get("dependencies")->arr.empty());
    CHECK(std::fabs(*el.get("visual_samples")->arr[0].get("base")->get("size")->arr[0].number() - 200) < 1e-6);
    AuthoredFx back;
    CHECK(parseAuthoredFx(authoredFxJson(fx), back, &err) && back.elements[0].kind == "light");
    CHECK(!parseAuthoredFx("{\"elements\": [{\"kind\": \"model\"}]}", back, &err));
}

// 8.2: a model element (IW3 type 5): the xmodel visual and dependency; the size curve is the model's scale.
TEST(fx_author_model_element) {
    AuthoredFx fx;
    fx.name = "iw8r/debris";
    AuthoredFxElement chunk;
    chunk.kind = "model";
    chunk.model = "my_rock";
    chunk.sizeStart = 1;
    chunk.sizeEnd = 0.5;
    fx.elements.push_back(chunk);
    std::string err;
    const auto doc = json::parse(authoredFxIw3Json(fx), &err);
    CHECK(doc.has_value());
    if (!doc) return;
    const json::Value& el = doc->get("elements")->arr[0];
    CHECK(inum(el.get("elem_type")) == 5 && el.get("elem_type_name")->str == "model");
    CHECK(el.get("visuals")->arr.size() == 1 && el.get("visuals")->arr[0].get("type")->str == "xmodel" &&
          el.get("visuals")->arr[0].get("name")->str == "my_rock");
    CHECK(doc->get("dependencies")->arr.size() == 1 && doc->get("dependencies")->arr[0].get("type")->str == "xmodel");
    const json::Value* b0 = el.get("visual_samples")->arr[0].get("base");
    CHECK(std::fabs(*b0->get("scale")->number() - 1) < 1e-9 && std::fabs(*b0->get("size")->arr[0].number()) < 1e-9);
    AuthoredFx back;
    CHECK(parseAuthoredFx(authoredFxJson(fx), back, &err) && back.elements[0].kind == "model" && back.elements[0].model == "my_rock");
    CHECK(!parseAuthoredFx("{\"elements\": [{\"kind\": \"model\"}]}", back, &err));  // no model named
}
