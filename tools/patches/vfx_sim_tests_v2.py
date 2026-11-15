import io

# Research 138: the vfx_sim tests follow the decoded emitter rules (burst flag 0x2; a continuous emitter without an
# emitter life plays on; a burst-only effect ends at its delay + life) and cover the new modules.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if s.count(a) == 0 and b in s:
        return
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


T = 'tests/vfx_sim_tests.cpp'
patch(T, '''    CHECK(!info.looping);
    CHECK(def.duration > 0);''', '''    CHECK(!info.looping);
    // Its emitters are continuous with no emitter life: they spawn while the effect plays (research 138 section 8).
    CHECK(def.duration == -1.0);''')
patch(T, '''    // No spawn rate: only the burst. Burst at start 0, life 1 s, so duration = 0 + 1 + 1 = 2 s.
    VfxSimDef def;
    CHECK(buildVfxSim(oneEmitterExport(0, 0.0, 5, 1.0, 0.0, 0.0), def, nullptr));
    CHECK(def.duration == 2.0);''', '''    // No spawn rate: only the burst. Burst at start 0, life 1 s, so the effect ends at 0 + 1 = 1 s.
    VfxSimDef def;
    CHECK(buildVfxSim(oneEmitterExport(0, 0.0, 5, 1.0, 0.0, 0.0), def, nullptr));
    CHECK(def.duration == 1.0);''')
patch(T, '''    CHECK(m.duration == 2.0);
    CHECK(sampleVfx(m, 0.5).empty());  // no spawn, no burst''', '''    CHECK(m.duration == 1.0);
    CHECK(sampleVfx(m, 0.5).empty());  // no spawn, no burst''')

NEW = r'''
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
'''
s = io.open(ROOT + T, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
if 'vfx_sim_curves_and_graphs' not in s:
    if '#include <cstdlib>' not in s:
        s = s.replace('#include <cmath>' + nl, '#include <cmath>' + nl + '#include <cstdlib>' + nl, 1)
    s = s.rstrip() + nl + NEW.replace('\n', nl)
    io.open(ROOT + T, 'w', encoding='utf-8', newline='').write(s)
print('ok')
