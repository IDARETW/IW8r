// FX preview simulation (src/core/vfx_sim.*): one real export (tests/data/vfx_sim), synthetic exports, and sampling rules.
#include "vfx_sim.h"
#include "vfx_info.h"
#include "stock_lightgrid.h"
#include "test_harness.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace iw8r;

namespace {

// tests/data/vfx_sim/vfx_hp_chev_grey.vfx.33.asset.json, found by walking up from the working directory.
std::string sampleExportPath() {
    namespace fs = std::filesystem;
    fs::path p = fs::current_path();
    for (int i = 0; i < 6; ++i) {
        const fs::path candidate = p / "tests" / "data" / "vfx_sim" / "vfx_hp_chev_grey.vfx.33.asset.json";
        if (fs::exists(candidate)) return candidate.string();
        if (!p.has_parent_path() || p.parent_path() == p) break;
        p = p.parent_path();
    }
    return {};
}

std::string readAll(const std::string& path, bool& ok) {
    std::ifstream in(path, std::ios::binary);
    ok = static_cast<bool>(in);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

bool sameParticles(const std::vector<VfxParticle>& a, const std::vector<VfxParticle>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].emitter != b[i].emitter || a[i].size != b[i].size) return false;
        for (int c = 0; c < 3; ++c) {
            if (a[i].pos[c] != b[i].pos[c]) return false;
        }
        for (int c = 0; c < 4; ++c) {
            if (a[i].rgba[c] != b[i].rgba[c]) return false;
        }
    }
    return true;
}

// One emitter export. flags 8 = infinite particle life (looping). Ranges have min == max.
std::string oneEmitterExport(unsigned flags, double rate, int burst, double life, double delay, double emitterLife) {
    std::ostringstream s;
    s << R"({"format":"mw19-asset-json","format_version":1,"pool":"vfx","pool_id":44,"root_type":"ParticleSystemDef",)"
      << R"("asset":{"type":"ParticleSystemDef","fields":{"name":{"type":"const char","string":"vfx/test/synth.vfx"},)"
      << R"("emitterDefs":{"count":1,"values":[{"flags":)" << flags << R"(,"particleSpawnRate":{"min":)" << rate
      << R"(,"max":)" << rate << R"(},"particleBurstCount":{"min":)" << burst << R"(,"max":)" << burst
      << R"(},"particleLife":{"min":)" << life << R"(,"max":)" << life << R"(},"emitterDelay":{"min":)" << delay
      << R"(,"max":)" << delay << R"(},"emitterLife":{"min":)" << emitterLife << R"(,"max":)" << emitterLife
      << R"(}}]},"numEmitters":1}}})";
    return s.str();
}

// A hand-built emitter for sampling tests (no JSON).
VfxEmitterSim emitter(double rate, int burst, double lifeMin, double lifeMax, double delay, double emitterLife,
                      bool looping) {
    VfxEmitterSim e;
    e.name = "test";
    e.spawnRate = rate;
    e.burstCount = burst;
    e.lifeMin = lifeMin;
    e.lifeMax = lifeMax;
    e.startDelay = delay;
    e.emitterLife = emitterLife;
    e.looping = looping;
    e.sizeStart = 1.0;
    e.sizeEnd = 3.0;
    return e;
}

}  // namespace

TEST(vfx_sim_real_export_matches_vfx_info) {
    const std::string path = sampleExportPath();
    CHECK(!path.empty());
    if (path.empty()) return;
    bool ok = false;
    const std::string text = readAll(path, ok);
    CHECK(ok);

    VfxSimDef def;
    std::string err;
    CHECK(buildVfxSim(text, def, &err));
    CHECK(err.empty());

    // Same file through the existing reader: the emitter count and name must agree.
    VfxInfo info;
    CHECK(readVfxExport(path, info, nullptr));
    CHECK(def.name == info.name);
    CHECK(static_cast<int>(def.emitters.size()) == info.emitterCount);
    CHECK(def.emitters.size() == 4);
    CHECK(!info.looping);
    // Its emitters are continuous with no emitter life: they spawn while the effect plays (research 138 section 8).
    CHECK(def.duration == -1.0);

    // Emitter 0 (from the export): spawnRate 1.9, burst 1, particle life 2.0, velocity z 25..50, size 8 x 5.5.
    const VfxEmitterSim& e0 = def.emitters[0];
    CHECK(std::fabs(e0.spawnRate - 1.9) < 1e-3);
    CHECK(e0.burstCount == 1);
    CHECK(std::fabs(e0.lifeMin - 2.0) < 1e-6 && std::fabs(e0.lifeMax - 2.0) < 1e-6);
    CHECK(std::fabs(e0.velMin[2] - 25.0) < 1e-6 && std::fabs(e0.velMax[2] - 50.0) < 1e-6);
    CHECK(std::fabs(e0.sizeStart - 8.0) < 1e-6 && e0.sizeEnd == e0.sizeStart);
    CHECK(e0.gravity == 0.0);
    CHECK(e0.startDelay == 0.0);

    // Every emitter material that is set must be one of the materials vfx_info lists.
    bool anyMaterial = false;
    for (const auto& e : def.emitters) {
        if (e.material.empty()) continue;
        anyMaterial = true;
        bool found = false;
        for (const auto& m : info.materials) found = found || (m == e.material);
        CHECK(found);
    }
    CHECK(anyMaterial);
}

TEST(vfx_sim_t0_and_t05_differ) {
    const std::string path = sampleExportPath();
    CHECK(!path.empty());
    if (path.empty()) return;
    bool ok = false;
    VfxSimDef def;
    CHECK(buildVfxSim(readAll(path, ok), def, nullptr));
    const auto a = sampleVfx(def, 0.0);
    const auto b = sampleVfx(def, 0.5);
    CHECK(!a.empty());
    CHECK(!b.empty());
    CHECK(!sameParticles(a, b));  // the same particles have aged, so their positions differ
}

TEST(vfx_sim_deterministic) {
    const std::string path = sampleExportPath();
    CHECK(!path.empty());
    if (path.empty()) return;
    bool ok = false;
    VfxSimDef def;
    CHECK(buildVfxSim(readAll(path, ok), def, nullptr));
    CHECK(sameParticles(sampleVfx(def, 0.7, 7), sampleVfx(def, 0.7, 7)));
    // Another seed changes the random life and velocity, so the particles differ.
    CHECK(!sameParticles(sampleVfx(def, 0.7, 7), sampleVfx(def, 0.7, 8)));
}

TEST(vfx_sim_ages_stay_within_life) {
    // Life fixed at L, velocity 0 on z and gravity 0: age = size fraction * L, and must lie in [0, L).
    VfxSimDef def;
    def.name = "ages";
    def.emitters.push_back(emitter(20.0, 0, 2.0, 2.0, 0.0, 1.0, false));
    def.emitters[0].velMin[2] = def.emitters[0].velMax[2] = 10.0;
    def.duration = 3.0;
    for (double t : {0.1, 0.7, 1.9, 2.5, 2.99}) {
        const auto ps = sampleVfx(def, t, 3);
        CHECK(!ps.empty());
        for (const auto& p : ps) {
            const double age = (p.size - 1.0) / 2.0 * 2.0;  // size lerp 1 -> 3 over life 2
            CHECK(age >= 0.0 && age < 2.0);
            CHECK(std::fabs(p.pos[2] - 10.0 * age) < 1e-6);  // position uses the same age
        }
    }
}

TEST(vfx_sim_gravity_term) {
    // Gravity 32 in/s^2 down, zero velocity: z = -0.5 * g * age^2 = -16 * age^2.
    VfxSimDef def;
    def.name = "gravity";
    def.emitters.push_back(emitter(0.0, 1, 2.0, 2.0, 0.0, 0.0, false));
    def.emitters[0].gravity = 32.0;
    def.duration = 2.0;
    const auto ps = sampleVfx(def, 1.0, 1);
    CHECK(ps.size() == 1);
    if (ps.size() == 1) {
        const double age = (ps[0].size - 1.0) / 2.0 * 2.0;  // life 2
        CHECK(std::fabs(age - 1.0) < 1e-6);
        CHECK(std::fabs(ps[0].pos[2] - (-16.0)) < 1e-6);
    }
}

TEST(vfx_sim_one_shot_ends_at_duration) {
    // rate 10/s, life 1 s, emitter life 0.5 s: spawns until 0.5 s, the last particle dies at 1.5 s.
    VfxSimDef def;
    std::string err;
    CHECK(buildVfxSim(oneEmitterExport(0, 10.0, 0, 1.0, 0.0, 0.5), def, &err));
    CHECK(def.duration == 1.5);
    CHECK(!sampleVfx(def, 0.1).empty());
    CHECK(!sampleVfx(def, 1.2).empty());
    CHECK(sampleVfx(def, 1.6).empty());  // t > duration: nothing left
    CHECK(sampleVfx(def, 100.0).empty());
}

TEST(vfx_sim_looping_never_ends) {
    VfxSimDef def;
    CHECK(buildVfxSim(oneEmitterExport(8, 10.0, 0, 1.0, 0.0, 0.5), def, nullptr));
    CHECK(def.duration == -1.0);
    CHECK(def.emitters.size() == 1 && def.emitters[0].looping);
    CHECK(!sampleVfx(def, 100.0).empty());
}

TEST(vfx_sim_burst_only_and_defaults) {
    // No spawn rate: only the burst. Burst at start 0, life 1 s, so the effect ends at 0 + 1 = 1 s.
    VfxSimDef def;
    CHECK(buildVfxSim(oneEmitterExport(0, 0.0, 5, 1.0, 0.0, 0.0), def, nullptr));
    CHECK(def.duration == 1.0);
    CHECK(sampleVfx(def, 0.5).size() == 5);
    CHECK(sampleVfx(def, 1.5).empty());  // all five died at t = 1

    // A minimal export with no modules: defaults from vfx_sim.h.
    const char* minimal = R"({"format":"mw19-asset-json","root_type":"ParticleSystemDef",
        "asset":{"fields":{"name":{"string":"vfx/test/min.vfx"},"emitterDefs":{"values":[{"flags":0}]}}}})";
    VfxSimDef m;
    CHECK(buildVfxSim(minimal, m, nullptr));
    CHECK(m.emitters.size() == 1);
    if (m.emitters.size() == 1) {
        const auto& e = m.emitters[0];
        CHECK(e.spawnRate == 0.0 && e.burstCount == 0);
        CHECK(e.lifeMin == 1.0 && e.lifeMax == 1.0);
        CHECK(e.startDelay == 0.0 && e.emitterLife == 0.0);
        CHECK(e.sizeStart == 1.0 && e.sizeEnd == 1.0);
        CHECK(e.colorStart[0] == 1.0f && e.colorStart[3] == 1.0f);
        CHECK(e.material.empty() && !e.looping && e.gravity == 0.0);
    }
    CHECK(m.duration == 1.0);
    CHECK(sampleVfx(m, 0.5).empty());  // no spawn, no burst
}

TEST(vfx_sim_particle_cap) {
    // A looping emitter at 100000/s with 2 s life has far more than 2000 live particles: the cap applies.
    VfxSimDef def;
    def.name = "cap";
    def.emitters.push_back(emitter(100000.0, 0, 2.0, 2.0, 0.0, 0.0, true));
    def.duration = -1.0;
    const auto ps = sampleVfx(def, 5.0, 1);
    CHECK(ps.size() == kVfxMaxParticles);
    CHECK(kVfxMaxParticles == 2000);
}

TEST(vfx_sim_time_edges) {
    VfxSimDef def;
    def.name = "edges";
    def.emitters.push_back(emitter(10.0, 0, 1.0, 1.0, 0.0, 0.0, true));
    def.duration = -1.0;
    CHECK(sampleVfx(def, -1.0).empty());
    CHECK(sampleVfx(def, std::nan("")).empty());
    CHECK(sampleVfx(VfxSimDef{}, 1.0).empty());  // no emitters
}

TEST(vfx_sim_rejects_malformed_input) {
    VfxSimDef def;
    std::string err;
    CHECK(!buildVfxSim("not json at all", def, &err));
    CHECK(!err.empty());
    err.clear();
    CHECK(!buildVfxSim("[1,2,3]", def, &err));
    CHECK(!buildVfxSim(R"({"format":"other-format","root_type":"ParticleSystemDef","asset":{"fields":{}}})", def, &err));
    CHECK(!err.empty());
    // A material export has the same envelope but another root type.
    err.clear();
    CHECK(!buildVfxSim(R"({"format":"mw19-asset-json-v1","root_type":"Material","asset":{"fields":{}}})", def, &err));
    CHECK(err.find("ParticleSystemDef") != std::string::npos);
    // No name.
    CHECK(!buildVfxSim(R"({"format":"mw19-asset-json","root_type":"ParticleSystemDef","asset":{"fields":{"numEmitters":0}}})",
                       def, &err));
    CHECK(!buildVfxSim("", def, nullptr));
    CHECK(!buildVfxSim("{}", def, nullptr));

    // A real export cut short is not valid JSON.
    const std::string path = sampleExportPath();
    if (!path.empty()) {
        bool ok = false;
        const std::string text = readAll(path, ok);
        CHECK(ok);
        CHECK(!buildVfxSim(text.substr(0, text.size() / 2), def, nullptr));
    }
}

TEST(vfx_sim_bounds_cover_the_particles) {
    VfxSimDef def;
    std::string err;
    bool ok = false;
    const std::string path = sampleExportPath();
    if (path.empty()) return;
    const std::string text = readAll(path, ok);
    CHECK(ok && buildVfxSim(text, def, &err));
    const AABB b = vfxBounds(def);
    CHECK(b.valid());
    // Every particle sampled mid-run lies inside.
    const double mid = def.duration > 0 ? def.duration / 2 : 1.0;
    for (const VfxParticle& p : sampleVfx(def, mid))
        CHECK(p.pos[0] >= b.mins.x - 1e-6 && p.pos[0] <= b.maxs.x + 1e-6 && p.pos[2] >= b.mins.z - 1e-6 && p.pos[2] <= b.maxs.z + 1e-6);
    CHECK(!vfxBounds(VfxSimDef{}).valid());
}

namespace {

// A one-emitter export whose first state holds the given module JSON objects (group 0 and group 1).
std::string moduleExport(unsigned flags, double rate, int burst, double life, int countMax, const std::string& init,
                         const std::string& update, int element = 0) {
    std::ostringstream s;
    s << R"({"format":"mw19-asset-json","root_type":"ParticleSystemDef","asset":{"fields":{"name":{"string":"vfx/test/mod.vfx"},)"
      << R"("emitterDefs":{"values":[{"flags":)" << flags << R"(,"particleSpawnRate":{"min":)" << rate << R"(,"max":)" << rate
      << R"(},"particleBurstCount":{"min":)" << burst << R"(,"max":)" << burst << R"(},"particleLife":{"min":)" << life
      << R"(,"max":)" << life << R"(},"particleCountMax":)" << countMax
      << R"(,"stateDefs":{"values":[{"elementType":{"value":)" << element << R"(},"moduleGroupDefs":{"values":[)"
      << R"({"moduleDefs":{"values":[)" << init << R"(]}},{"moduleDefs":{"values":[)" << update << R"(]}}]}}]}}]}}}})";
    return s.str();
}

std::string vec4(double x, double y, double z, double w) {
    std::ostringstream s;
    s << R"({"v":{"m128_f32":[)" << x << "," << y << "," << z << "," << w << "]}}";
    return s.str();
}

// A curve of (time, value) points with a scale.
std::string curve(std::initializer_list<std::pair<double, double>> pts, double scale) {
    std::ostringstream s;
    s << R"({"controlPoints":{"values":[)";
    bool first = true;
    for (const auto& [t, v] : pts) {
        s << (first ? "" : ",") << R"({"time":)" << t << R"(,"value":)" << v << "}";
        first = false;
    }
    s << R"(]},"scale":)" << scale << "}";
    return s.str();
}

std::string module(const char* member, int flags, const std::string& body) {
    return std::string(R"({"moduleData":{"active_member":")") + member + R"(","value":{"":{"m_flags":)" + std::to_string(flags) + "}" +
           (body.empty() ? "" : "," + body) + "}}}";
}

}  // namespace

TEST(vfx_sim_curves_and_graphs) {
    VfxCurve c;
    c.t = {0.0f, 0.25f, 1.0f};
    c.v = {0.0f, 1.0f, 0.5f};
    c.scale = 2.0f;
    CHECK(std::fabs(c.eval(-1.0f)) < 1e-6f && std::fabs(c.eval(0.125f) - 1.0f) < 1e-6f);
    CHECK(std::fabs(c.eval(0.25f) - 2.0f) < 1e-6f && std::fabs(c.eval(0.625f) - 1.5f) < 1e-6f && std::fabs(c.eval(3.0f) - 1.0f) < 1e-6f);
    CHECK(VfxCurve{}.eval(0.5f) == 0.0f);

    // A size graph (set A 10 -> 30, set B 20 -> 60, random 0x10, firstCurve) replaces the attribute size; a colour graph
    // without firstCurve multiplies it; an intensity graph of 2 doubles RGB.
    const std::string sizeCurves = curve({{0, 1}, {1, 3}}, 10) + "," + curve({{0, 1}, {1, 3}}, 10) + "," + curve({{0, 1}, {1, 1}}, 0) + "," +
                                   curve({{0, 1}, {1, 3}}, 20) + "," + curve({{0, 1}, {1, 3}}, 20) + "," + curve({{0, 1}, {1, 1}}, 0);
    std::string colorCurves;
    for (int k = 0; k < 8; ++k) colorCurves += (k ? "," : "") + curve({{0, 0.5}, {1, 0.5}}, 1);
    const std::string init = module("initAttributes", 0, R"("m_sizeMin":)" + vec4(4, 4, 4, 0) + R"(,"m_sizeMax":)" + vec4(4, 4, 4, 0) +
                                                             R"(,"m_colorMin":)" + vec4(1, 0.5, 0.25, 1) + R"(,"m_colorMax":)" + vec4(1, 0.5, 0.25, 1));
    const std::string update = module("sizeGraph", 16, R"("firstCurve":true,"m_curves":[)" + sizeCurves + "]") + "," +
                               module("colorGraph", 0, R"("firstCurve":false,"m_curves":[)" + colorCurves + "]") + "," +
                               module("intensityGraph", 0, R"("firstCurve":true,"m_curves":[)" + curve({{0, 2}, {1, 2}}, 1) + "," +
                                                               curve({{0, 2}, {1, 2}}, 1) + "]");
    VfxSimDef def;
    std::string err;
    CHECK(buildVfxSim(moduleExport(2, 0, 1, 2.0, 1, init, update), def, &err));
    CHECK(def.emitters.size() == 1);
    if (def.emitters.size() != 1) return;
    const VfxEmitterSim& e = def.emitters[0];
    CHECK(e.burst && e.size.present && e.size.random && e.size.replace && e.color.present && !e.color.replace && e.intensity.present);
    CHECK(def.duration == 2.0);  // burst at 0, life 2
    const auto ps = sampleVfx(def, 1.0);  // age 1 of 2: f = 0.5
    CHECK(ps.size() == 1);
    if (ps.size() == 1) {
        const VfxParticle& p = ps[0];
        CHECK(p.size2[0] >= 20.0 - 1e-4 && p.size2[0] <= 40.0 + 1e-4);  // between set A (20) and set B (40) at f = 0.5
        CHECK(std::fabs(p.size2[0] - p.size2[1]) < 1e-4);
        CHECK(std::fabs(p.rgba[0] - 1.0f) < 1e-5f && std::fabs(p.rgba[1] - 0.5f) < 1e-5f && std::fabs(p.rgba[2] - 0.25f) < 1e-5f);  // x 0.5 x 2
        CHECK(std::fabs(p.rgba[3] - 0.5f) < 1e-5f);  // alpha x 0.5, intensity does not touch it
    }
}

TEST(vfx_sim_burst_flag_cap_and_spawn_shapes) {
    // Continuous (no 0x2) at 10/s, life 5 s, alive cap 3: never more than 3, the newest kept; a box spawn shape keeps
    // every particle inside its absolute bounds.
    const std::string box = module("initSpawnShapeBox", 0, R"("m_dimensionsMin":)" + vec4(-10, -20, 0, 0) + R"(,"m_dimensionsMax":)" +
                                                         vec4(10, 20, 5, 0));
    VfxSimDef def;
    CHECK(buildVfxSim(moduleExport(0, 10, 7, 5.0, 3, box, ""), def, nullptr));
    CHECK(def.emitters.size() == 1 && !def.emitters[0].burst && def.emitters[0].looping && def.duration == -1.0);
    for (double t : {0.05, 0.35, 2.0, 60.0}) {
        const auto ps = sampleVfx(def, t);
        CHECK(!ps.empty() && ps.size() <= 3);
        for (const auto& p : ps) {
            CHECK(p.pos[0] >= -10 && p.pos[0] <= 10 && p.pos[1] >= -20 && p.pos[1] <= 20 && p.pos[2] >= 0 && p.pos[2] <= 5);
            CHECK(p.age <= 0.3 + 1e-9);  // the three newest of a 10/s emitter are at most 0.2-0.3 s old
        }
    }
    // The same emitter with the burst flag: its 7 particles once, then nothing (burst count ignored without the flag).
    VfxSimDef b;
    CHECK(buildVfxSim(moduleExport(2, 10, 7, 5.0, 7, box, ""), b, nullptr));
    CHECK(b.emitters.size() == 1 && b.emitters[0].burst && !b.emitters[0].looping && b.duration == 5.0);
    CHECK(sampleVfx(b, 1.0).size() == 7 && sampleVfx(b, 5.5).empty());

    // A sphere shell (ONLY_ON_SURFACE) of radius 50 around (300, 0, 200), with a +x-only axis mask.
    const std::string sphere = std::string(R"({"moduleData":{"active_member":"initSpawnShapeSphere","value":{"":{"":{"m_flags":0},)") +
                               R"("m_axisFlags":{"value":61},"m_spawnFlags":{"value":1},"m_offset":)" + vec4(300, 0, 200, 0) +
                               R"(},"m_radius":{"min":0,"max":50}}}})";
    VfxSimDef s;
    CHECK(buildVfxSim(moduleExport(2, 0, 20, 1.0, 20, sphere, ""), s, nullptr));
    CHECK(s.emitters.size() == 1 && s.emitters[0].shape.type == 2 && s.emitters[0].shape.surface);
    for (const auto& p : sampleVfx(s, 0.0)) {
        const double d = std::sqrt((p.pos[0] - 300) * (p.pos[0] - 300) + p.pos[1] * p.pos[1] + (p.pos[2] - 200) * (p.pos[2] - 200));
        CHECK(std::fabs(d - 50.0) < 1e-6 && p.pos[0] >= 300.0 - 1e-9);
    }
}

TEST(vfx_sim_atlas_frames_and_axes) {
    VfxEmitterSim e;
    e.atlas.present = true;
    e.atlas.overLife = true;
    e.atlas.start = 0;
    VfxParticle p;
    p.atlasPos = 0.0;
    CHECK(vfxAtlasFrame(e, p, 4) == 0);
    p.atlasPos = 0.5;
    CHECK(vfxAtlasFrame(e, p, 4) == 2);
    p.atlasPos = 1.0;
    CHECK(vfxAtlasFrame(e, p, 4) == 3);  // clamped to the last frame
    CHECK(vfxAtlasFrame(e, p, 1) == 0);
    e.atlas.overLife = false;
    e.atlas.randomIndex = false;
    e.atlas.loops = -1;
    p.atlasFrames = 9.5;
    CHECK(vfxAtlasFrame(e, p, 4) == 1);  // 9 frames played, wrapping
    e.atlas.loops = 2;
    CHECK(vfxAtlasFrame(e, p, 4) == 3);  // two loops of 4 played: holds the last frame
    e.atlas.present = false;
    CHECK(vfxAtlasFrame(e, p, 4) == 0);

    // Placement axes: yaw 90 turns the effect's x onto the world's y; a local velocity follows, a world one does not.
    const VfxAxes ax = vfxAxesFromAngles(0, 90, 0);
    CHECK(std::fabs(ax.x[0]) < 1e-9 && std::fabs(ax.x[1] - 1.0) < 1e-9 && std::fabs(ax.z[2] - 1.0) < 1e-9);
    CHECK(std::fabs(ax.y[0] + 1.0) < 1e-9);  // left of a +y facing effect is -x
    VfxSimDef def;
    def.name = "axes";
    VfxEmitterSim m;
    m.burstCount = 1;
    m.lifeMin = m.lifeMax = 2.0;
    m.velMin[0] = m.velMax[0] = 10.0;
    def.emitters.push_back(m);
    def.duration = 2.0;
    const auto ps = sampleVfx(def, 1.0, 1, &ax);
    CHECK(ps.size() == 1);
    if (ps.size() == 1) CHECK(std::fabs(ps[0].pos[0]) < 1e-9 && std::fabs(ps[0].pos[1] - 10.0) < 1e-9);
    def.emitters[0].relVelType = 1;  // world
    const auto pw = sampleVfx(def, 1.0, 1, &ax);
    CHECK(pw.size() == 1);
    if (pw.size() == 1) CHECK(std::fabs(pw[0].pos[0] - 10.0) < 1e-9 && std::fabs(pw[0].pos[1]) < 1e-9);
}

// Opt-in (IW8R_VFX_SHIPMENT = the folder holding vfx_shipment_distant_fog.*.asset.json and vfx_shipment_smoke_stack.*,
// e.g. <AppLocalData>\vfx_cache\mp_shipment\mw19replay\mp_shipment\assets\vfx\vfx\iw8_mp\level\shipment): the values
// research 138 section 11 quotes.
TEST(vfx_sim_shipment_exports_real_data) {
    const char* dir = std::getenv("IW8R_VFX_SHIPMENT");
    if (!dir) return;
    namespace fs = std::filesystem;
    const auto find = [&](const std::string& stem) {
        for (const auto& f : fs::directory_iterator(dir))
            if (f.path().filename().string().rfind(stem + ".", 0) == 0) return f.path().string();
        return std::string();
    };
    bool ok = false;
    VfxSimDef fog, smoke;
    CHECK(buildVfxSim(readAll(find("vfx_shipment_distant_fog"), ok), fog, nullptr));
    CHECK(buildVfxSim(readAll(find("vfx_shipment_smoke_stack"), ok), smoke, nullptr));
    CHECK(fog.emitters.size() == 1 && smoke.emitters.size() == 2);
    if (fog.emitters.size() == 1) {
        const VfxEmitterSim& e = fog.emitters[0];
        CHECK(!e.burst && std::fabs(e.spawnRate - 0.4) < 1e-6 && std::fabs(e.lifeMax - 15.0) < 1e-6 && e.countMax == 5);
        CHECK(e.size.present && e.size.random && e.size.replace);
        CHECK(std::fabs(e.size.eval(0, 0.0f, 0.0f) - 1066.284f) < 0.01f && std::fabs(e.size.eval(0, 1.0f, 0.0f) - 1215.0f) < 0.01f);
        CHECK(std::fabs(e.size.eval(0, 0.0f, 1.0f) - 861.192f) < 0.01f);
        CHECK(e.velocity.present && e.velocity.world);
        CHECK(e.shape.type == 1 && std::fabs(e.shape.dimMin[0] + 760.0) < 1e-6 && std::fabs(e.shape.dimMax[2] - 300.0) < 1e-6);
        for (const auto& p : sampleVfx(fog, 100.0)) CHECK(p.rgba[3] >= 0.0f && p.rgba[3] <= 0.6f);
    }
    if (smoke.emitters.size() == 2) {
        const VfxEmitterSim& e = smoke.emitters[0];
        CHECK(std::fabs(e.startDelay - 2.2) < 1e-6 && std::fabs(e.spawnRate - 3.0) < 1e-6 && e.countMax == 15);
        CHECK(e.shape.type == 2 && std::fabs(e.shape.radiusMax - 50.0) < 1e-6 && std::fabs(e.shape.offset[0] - 300.0) < 1e-6);
        CHECK(e.atlas.present && e.atlas.loops == -1 && e.atlas.randomIndex && !e.atlas.overLife);
        CHECK(sampleVfx(smoke, 2.1).empty() || sampleVfx(smoke, 2.1)[0].emitter != 0);  // nothing of emitter 0 before 2.2 s
        size_t n0 = 0;
        for (const auto& p : sampleVfx(smoke, 40.0)) n0 += p.emitter == 0 ? 1 : 0;
        CHECK(n0 > 0 && n0 <= 15);
        CHECK(smoke.emitters[1].atlas.present && smoke.emitters[1].atlas.overLife);
    }
}

// Research 139 section 2: the effect distortion is research 132 G13's world rule on the quad's tangent frame.
TEST(vfx_distortion_follows_the_world_rule) {
    const double T[3] = {1, 0, 0}, B[3] = {0, 1, 0}, scale[2] = {30, 30};
    {
        const double m[3] = {1, 0.5, 1};  // full strength, x pushed right, y centred
        const VfxDistortion d = vfxDistortion(m, 1.0, scale, T, B);
        CHECK(std::fabs(d.k - 1.0) < 1e-9 && std::fabs(d.offset[0] - 30) < 1e-9 && std::fabs(d.offset[1]) < 1e-9 && std::fabs(d.alpha - 1) < 1e-9);
    }
    {
        const double m[3] = {0, 1, 0.5};  // m.z x a = 0.2 is under the 0.25 floor: no offset, alpha 0.8
        const VfxDistortion d = vfxDistortion(m, 0.4, scale, T, B);
        CHECK(d.k == 0 && d.offset[0] == 0 && d.offset[1] == 0 && std::fabs(d.alpha - 0.8) < 1e-9);
    }
    {
        const double m[3] = {0.75, 0.25, 0.6};  // k = 0.35 x 4/3; D = 30 k (0.5 T - 0.5 B) = (7, -7, 0)
        const VfxDistortion d = vfxDistortion(m, 1.0, scale, T, B);
        CHECK(std::fabs(d.k - 0.35 * 4.0 / 3.0) < 1e-9 && std::fabs(d.offset[0] - 7) < 1e-9 && std::fabs(d.offset[1] + 7) < 1e-9);
        CHECK(std::fabs(d.offset[2]) < 1e-12 && std::fabs(d.alpha - 1) < 1e-9);
    }
    {
        const double m[3] = {0.75, 0.75, 1}, Tz[3] = {0, 0, 1}, Bx[3] = {-1, 0, 0}, s2[2] = {2, 4};  // another frame and scale
        const VfxDistortion d = vfxDistortion(m, 1.0, s2, Tz, Bx);
        CHECK(std::fabs(d.offset[2] - 1.0) < 1e-9 && std::fabs(d.offset[0] + 2.0) < 1e-9);
    }
}

// Research 139 section 3: a particle's light from the grid is the probe averaged over all directions.
TEST(stock_lgv_average_is_the_mean_over_directions) {
    StockLgvSh s;
    s.dc[0] = 1.0f, s.dc[1] = 2.0f, s.dc[2] = 3.0f;
    s.sh[0][0] = 0.3f, s.sh[2][1] = -0.4f, s.sh[3][2] = 0.2f, s.sh[1][0] = 0.1f, s.sh[6][1] = 0.15f, s.sh[7][2] = -0.1f;
    double avg[3];
    stockLgvAverage(s, avg);
    double sum[3] = {0, 0, 0};
    const int n = 4000;
    for (int i = 0; i < n; ++i) {  // Fibonacci sphere
        const double z = 1 - (2.0 * i + 1) / n, r = std::sqrt(std::max(0.0, 1 - z * z)), phi = i * 2.399963229728653;
        const double dir[3] = {r * std::cos(phi), r * std::sin(phi), z};
        double rgb[3];
        evalStockLgv(s, dir, rgb);
        for (int c = 0; c < 3; ++c) sum[c] += rgb[c];
    }
    for (int c = 0; c < 3; ++c) CHECK(std::fabs(sum[c] / n - avg[c]) < 2e-3);
    CHECK(std::fabs(avg[2] - 0.282095 * 3.0) < 1e-6);
    StockLgvSh dark;
    dark.dc[0] = -1.0f;
    stockLgvAverage(dark, avg);
    CHECK(avg[0] == 0.0);
}

// Research 139 section 1: Shipment's sun flare (tests/data/vfx_sim/vfx_shipment_sunflare.12.asset.json, 6 flare
// emitters) read into flare modules, and the screen layout of its pieces.
TEST(vfx_flare_modules_and_screen_layout) {
    namespace fs = std::filesystem;
    std::string path = sampleExportPath();
    if (!path.empty()) path = (fs::path(path).parent_path() / "vfx_shipment_sunflare.12.asset.json").string();
    bool ok = false;
    const std::string text = path.empty() ? std::string() : readAll(path, ok);
    CHECK(ok);
    if (!ok) return;
    VfxSimDef def;
    CHECK(buildVfxSim(text, def));
    CHECK(def.emitters.size() == 6);
    if (def.emitters.size() != 6) return;
    for (const VfxEmitterSim& e : def.emitters) {
        CHECK(e.element == 3 && e.flare.present && e.occlusion.present);
        CHECK(std::fabs(e.occlusion.scale - 1.5) < 1e-6 && std::fabs(e.occlusion.worldSize[0] - 1000) < 1e-6 && e.occlusion.offscreenRaycast);
        CHECK(std::fabs(e.flare.depthRangeMax - 280) < 1e-6 && std::fabs(e.flare.depthValueMax - 1) < 1e-6);
    }
    const VfxEmitterSim &lens = def.emitters[0], &anamorphic = def.emitters[1], &rays = def.emitters[2];
    CHECK(std::fabs(lens.flare.position - 1) < 1e-6 && std::fabs(lens.flare.scaleXMax - 1.5) < 1e-6 && std::fabs(lens.flare.direction[0] - 1) < 1e-6);
    CHECK(std::fabs(anamorphic.flare.position - 0.9) < 1e-6 && anamorphic.flare.flags == 4u && std::fabs(anamorphic.flare.scaleYMin - 0.5) < 1e-6);
    CHECK(rays.flare.angularRotCount == -1 && std::fabs(rays.flare.radialRotMin - 0.1) < 1e-6 && std::fabs(rays.flare.radialRotMax + 0.2) < 1e-6);
    // The lens at the centre: radial 0, curve 0 at 0.5 = 0.449 -> scale x 1 + 0.5 x 0.449; size 70 = 70 % of the height.
    {
        const double size[2] = {70, 70}, src[2] = {0, 0};
        const VfxFlareSprite s = vfxFlareSprite(lens, size, src, 16.0 / 9.0, 5000.0);
        CHECK(s.radial == 0 && s.centre[0] == 0 && s.centre[1] == 0);
        const double sc = 1 + 0.5 * double(lens.flare.curves[0].eval(0.5f));
        CHECK(std::fabs(sc - 1.2245) < 2e-3 && std::fabs(s.half[1] - 0.7 * sc) < 1e-6 && std::fabs(s.half[0] - 0.7 * sc / (16.0 / 9.0)) < 1e-6);
    }
    // The anamorphic streak sits at 0.9 of the way from the centre to its source.
    {
        const double size[2] = {200, 18}, src[2] = {0.5, 0.25};
        const VfxFlareSprite s = vfxFlareSprite(anamorphic, size, src, 16.0 / 9.0, 5000.0);
        CHECK(std::fabs(s.centre[0] - 0.45) < 1e-6 && std::fabs(s.centre[1] - 0.225) < 1e-6 && s.radial > 0 && s.radial < 1);
        CHECK(s.half[0] > s.half[1]);  // a wide streak
    }
    // The rays turn with the source's angle about the centre (angular rotation), plus the radial rotation (curve 2 = 0).
    {
        const double size[2] = {35, 35}, src[2] = {0, 0.5};
        const VfxFlareSprite s = vfxFlareSprite(rays, size, src, 1.0, 5000.0);
        CHECK(std::fabs(s.angle - (0.1 + std::atan2(0.5, 0.0))) < 1e-6);
    }
    // Near the camera the depth scale shrinks a piece (depth 0..280 maps to value 0..1 through curve 3).
    {
        const double size[2] = {70, 70}, src[2] = {0, 0};
        const VfxFlareSprite nearS = vfxFlareSprite(lens, size, src, 1.0, 140.0), farS = vfxFlareSprite(lens, size, src, 1.0, 5000.0);
        CHECK(nearS.half[1] <= farS.half[1]);
    }
    // Research 139 section 5: the emissive graph (RGB, intensity) is read and gives each particle the colour an unlit set
    // adds; the anamorphic streak's is constant (0.98, 0.70, 0.31) x 4.
    {
        CHECK(anamorphic.emissive.present && anamorphic.emissive.a.size() == 5);
        bool seen = false;
        for (const VfxParticle& p : sampleVfx(def, 1.0, 1)) {
            CHECK(p.hasEmissive);
            if (p.emitter != 1) continue;
            seen = true;
            CHECK(std::fabs(p.emissive[0] - 0.98 * 4) < 0.05 && std::fabs(p.emissive[1] - 0.70 * 4) < 0.05 && std::fabs(p.emissive[2] - 0.31 * 4) < 0.05);
        }
        CHECK(seen);
    }
}
