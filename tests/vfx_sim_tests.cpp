// FX preview simulation (src/core/vfx_sim.*): one real export (tests/data/vfx_sim), synthetic exports, and sampling rules.
#include "vfx_sim.h"
#include "vfx_info.h"
#include "test_harness.h"

#include <cmath>
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
    CHECK(def.duration > 0);

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
    // No spawn rate: only the burst. Burst at start 0, life 1 s, so duration = 0 + 1 + 1 = 2 s.
    VfxSimDef def;
    CHECK(buildVfxSim(oneEmitterExport(0, 0.0, 5, 1.0, 0.0, 0.0), def, nullptr));
    CHECK(def.duration == 2.0);
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
    CHECK(m.duration == 2.0);
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

