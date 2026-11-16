// FX preview simulation. Field paths, rules and defaults are documented in vfx_sim.h and research 138.
#include "vfx_sim.h"

#include "json_lite.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>

namespace iw8r {
namespace {

using json::Value;

// Emitter flag bits: 0x2 burst mode (research 138 section 8, VERIFIED on 870 emitters); 0x8 / 0x10 the IW7 names for
// infinite particle life and loop (kept as "plays on").
constexpr uint32_t kEmitterBurst = 0x2u;
constexpr uint32_t kEmitterInfiniteParticleLife = 0x8u;
constexpr uint32_t kEmitterLoopInfiniteParticles = 0x10u;

// Caps for values read from the export, so one bad field cannot make the preview spin.
constexpr double kMaxSpawnRate = 100000.0;  // particles per second
constexpr double kMaxBurstCount = 2000.0;
constexpr double kWorldGravity = 800.0;     // inches/s^2 (the engine's g_gravity default; research 138, INFERRED)

// Defaults used when the export has no value (see vfx_sim.h).
constexpr double kDefaultLife = 1.0;      // seconds
constexpr double kDefaultSize = 1.0;      // inches
constexpr float kDefaultColor = 1.0f;     // rgba, white and opaque

void setErr(std::string* err, const std::string& msg) {
    if (err) *err = msg;
}

double numberOr(const Value* v, double fallback) {
    const auto n = v ? v->number() : std::nullopt;
    return n ? *n : fallback;
}

bool boolOr(const Value* v, bool fallback) {
    if (!v) return fallback;
    if (v->type == Value::Type::Bool) return v->b;
    const auto n = v->number();
    return n ? *n != 0 : fallback;
}

// An enum field printed as {"value": n, "name": ...} or a plain number.
int enumOr(const Value* v, int fallback) {
    if (!v) return fallback;
    if (v->type == Value::Type::Object) return static_cast<int>(numberOr(v->get("value"), fallback));
    return static_cast<int>(numberOr(v, fallback));
}

// Reads a {"min": x, "max": y} range (ParticleFloatRange, ParticleIntRange). Returns false when absent, so the caller
// can use its default. A missing bound takes the other bound's value.
bool readRange(const Value* obj, double& lo, double& hi) {
    if (!obj || obj->type != Value::Type::Object) return false;
    const auto a = obj->get("min") ? obj->get("min")->number() : std::nullopt;
    const auto b = obj->get("max") ? obj->get("max")->number() : std::nullopt;
    if (!a && !b) return false;
    lo = a ? *a : *b;
    hi = b ? *b : lo;
    if (hi < lo) std::swap(lo, hi);
    return true;
}

// A range's two ends as stored (min, max), not ordered: the flare's ranges are the ends of a lerp (research 139).
bool readEnds(const Value* obj, double& a, double& b) {
    if (!obj || obj->type != Value::Type::Object) return false;
    const auto x = obj->get("min") ? obj->get("min")->number() : std::nullopt;
    const auto y = obj->get("max") ? obj->get("max")->number() : std::nullopt;
    if (!x && !y) return false;
    a = x ? *x : *y;
    b = y ? *y : a;
    return true;
}

double midpoint(double lo, double hi) { return 0.5 * (lo + hi); }

// Reads a vector stored as <field>.v.m128_f32 (an SSE value) or <field>.v (a float array). Returns false when absent.
bool readVec(const Value* payload, const char* field, std::vector<double>& out) {
    const Value* f = payload ? payload->get(field) : nullptr;
    const Value* v = f ? f->get("v") : nullptr;
    const Value* arr = v && v->type == Value::Type::Object ? v->get("m128_f32") : v;
    if (!arr || arr->type != Value::Type::Array) return false;
    out.clear();
    for (const auto& item : arr->arr) out.push_back(numberOr(&item, 0.0));
    return true;
}

// Every Material reference below `node` (asset_reference, type "Material"), in order. A leading ',' is removed.
// Every asset reference of `assetType` under `node` (asset_reference true, type, name; a leading ',' dropped), once each.
void findAssetRefs(const Value& node, int depth, const char* assetType, std::vector<std::string>& out) {
    if (depth > 64) return;
    if (node.type == Value::Type::Array) {
        for (const auto& item : node.arr) findAssetRefs(item, depth + 1, assetType, out);
        return;
    }
    if (node.type != Value::Type::Object) return;
    const Value* isRef = node.get("asset_reference");
    const Value* type = node.get("type");
    const Value* name = node.get("name");
    if (isRef && isRef->type == Value::Type::Bool && isRef->b && type && type->string() && *type->string() == assetType &&
        name && name->string()) {
        const std::string& n = *name->string();
        const std::string m = !n.empty() && n[0] == ',' ? n.substr(1) : n;
        if (!m.empty() && std::find(out.begin(), out.end(), m) == out.end()) out.push_back(m);
        return;
    }
    for (const auto& kv : node.obj) {
        if (kv.first == "$native") continue;
        findAssetRefs(kv.second, depth + 1, assetType, out);
    }
}

void findMaterialRefs(const Value& node, int depth, std::vector<std::string>& out) { findAssetRefs(node, depth, "Material", out); }

// The module base's m_flags: value."" (most modules) or value."".."" (spawn shapes, whose base is the shape base).
uint32_t moduleFlags(const Value* value) {
    const Value* b = value ? value->get("") : nullptr;
    for (int d = 0; d < 3 && b; ++d) {
        if (const Value* f = b->get("m_flags")) return static_cast<uint32_t>(numberOr(f, 0));
        b = b->get("");
    }
    return 0;
}

// The modules of an emitter's first state, by union member name (moduleData.active_member). Of two of one kind the one
// without m_flags & 0x1 wins (research 138 section 10).
std::map<std::string, const Value*> stateModules(const Value& em, int* elementType) {
    std::map<std::string, const Value*> out;
    const Value* states = em.get("stateDefs");
    const Value* rows = states ? states->get("values") : nullptr;
    if (!rows || rows->type != Value::Type::Array || rows->arr.empty()) return out;
    const Value& st = rows->arr[0];
    if (elementType) *elementType = enumOr(st.get("elementType"), 0);
    const Value* groups = st.get("moduleGroupDefs");
    const Value* gv = groups ? groups->get("values") : nullptr;
    if (!gv || gv->type != Value::Type::Array) return out;
    for (const auto& g : gv->arr) {
        if (boolOr(g.get("disabled"), false)) continue;
        const Value* mods = g.get("moduleDefs");
        const Value* mv = mods ? mods->get("values") : nullptr;
        if (!mv || mv->type != Value::Type::Array) continue;
        for (const auto& m : mv->arr) {
            const Value* data = m.get("moduleData");
            const Value* member = data ? data->get("active_member") : nullptr;
            const Value* value = data ? data->get("value") : nullptr;
            if (!member || !member->string() || !value) continue;
            auto it = out.find(*member->string());
            if (it == out.end()) {
                out[*member->string()] = value;
            } else if ((moduleFlags(it->second) & 1u) && !(moduleFlags(value) & 1u)) {
                it->second = value;  // the unflagged copy
            }
        }
    }
    return out;
}

VfxCurve readCurve(const Value& c) {
    VfxCurve out;
    out.scale = static_cast<float>(numberOr(c.get("scale"), 0));
    const Value* cp = c.get("controlPoints");
    const Value* pts = cp ? cp->get("values") : nullptr;
    if (pts && pts->type == Value::Type::Array)
        for (const auto& p : pts->arr) {
            out.t.push_back(static_cast<float>(numberOr(p.get("time"), 0)));
            out.v.push_back(static_cast<float>(numberOr(p.get("value"), 0)));
        }
    return out;
}

// A graph module of `perSet` curves per set.
VfxGraph readGraph(const Value* value, size_t perSet) {
    VfxGraph g;
    if (!value) return g;
    const Value* curves = value->get("m_curves");
    if (!curves || curves->type != Value::Type::Array || curves->arr.size() < perSet) return g;
    for (size_t k = 0; k < perSet; ++k) g.a.push_back(readCurve(curves->arr[k]));
    if (curves->arr.size() >= 2 * perSet)
        for (size_t k = 0; k < perSet; ++k) g.b.push_back(readCurve(curves->arr[perSet + k]));
    else
        g.b = g.a;
    const uint32_t flags = moduleFlags(value);
    g.random = (flags & 0x10u) != 0;
    g.world = (flags & 0x80u) != 0;
    g.replace = boolOr(value->get("firstCurve"), true);
    g.present = true;
    return g;
}

void vec3(const std::vector<double>& v, double out[3]) {
    for (int c = 0; c < 3; ++c) out[c] = c < static_cast<int>(v.size()) ? v[c] : 0.0;
}

VfxEmitterSim readEmitter(const Value& em, size_t index) {
    VfxEmitterSim e;
    e.name = "emitter" + std::to_string(index);

    const auto flags = static_cast<uint64_t>(numberOr(em.get("flags"), 0.0));
    e.burst = (flags & kEmitterBurst) != 0;

    double lo = 0, hi = 0;
    if (readRange(em.get("particleSpawnRate"), lo, hi)) e.spawnRate = std::clamp(midpoint(lo, hi), 0.0, kMaxSpawnRate);
    if (readRange(em.get("particleBurstCount"), lo, hi)) e.burstCount = static_cast<int>(std::clamp(std::round(midpoint(lo, hi)), 0.0, kMaxBurstCount));
    e.lifeMin = e.lifeMax = kDefaultLife;
    if (readRange(em.get("particleLife"), lo, hi)) {
        e.lifeMin = std::max(0.0, lo);
        e.lifeMax = std::max(e.lifeMin, hi);
    }
    if (readRange(em.get("emitterDelay"), lo, hi)) e.startDelay = std::max(0.0, midpoint(lo, hi));
    if (readRange(em.get("emitterLife"), lo, hi)) e.emitterLife = std::max(0.0, midpoint(lo, hi));
    e.countMax = static_cast<int>(std::clamp(numberOr(em.get("particleCountMax"), 0.0), 0.0, double(kVfxMaxParticles)));
    e.fadeOutMaxDistance = std::max(0.0, numberOr(em.get("fadeOutMaxDistance"), 0.0));
    // Plays on: the IW7 loop bits, or a continuous emitter that spawns while the effect plays.
    e.looping = (flags & (kEmitterInfiniteParticleLife | kEmitterLoopInfiniteParticles)) != 0 ||
                (!e.burst && e.spawnRate > 0 && e.emitterLife <= 0);

    const auto mods = stateModules(em, &e.element);
    const auto mod = [&](const char* name) -> const Value* {
        const auto it = mods.find(name);
        return it == mods.end() ? nullptr : it->second;
    };
    std::vector<double> v, w;
    if (const Value* a = mod("initAttributes")) {
        if (readVec(a, "m_velocityMin", v)) vec3(v, e.velMin);
        if (readVec(a, "m_velocityMax", v)) vec3(v, e.velMax);
        if (readVec(a, "m_sizeMin", v)) {
            if (!readVec(a, "m_sizeMax", w)) w = v;
            for (int c = 0; c < 2; ++c) {
                e.sizeMin[c] = c < static_cast<int>(v.size()) ? v[c] : kDefaultSize;
                e.sizeMax[c] = c < static_cast<int>(w.size()) ? w[c] : e.sizeMin[c];
            }
            e.hasAttrSize = true;
            e.sizeStart = e.sizeEnd = midpoint(e.sizeMin[0], e.sizeMax[0]);
        }
        if (readVec(a, "m_colorMin", v)) {
            if (!readVec(a, "m_colorMax", w)) w = v;
            for (int c = 0; c < 4; ++c) {
                e.colorMin[c] = static_cast<float>(c < static_cast<int>(v.size()) ? v[c] : kDefaultColor);
                e.colorMax[c] = static_cast<float>(c < static_cast<int>(w.size()) ? w[c] : e.colorMin[c]);
                e.colorStart[c] = e.colorEnd[c] = static_cast<float>(midpoint(e.colorMin[c], e.colorMax[c]));
            }
            e.hasAttrColor = true;
        }
    }
    if (const Value* m = mod("initMaterial")) findMaterialRefs(*m, 0, e.materials);
    // A decal element names its materials in initDecal (research 138 section 7).
    if (const Value* d = mod("initDecal"); d && e.materials.empty()) findMaterialRefs(*d, 0, e.materials);
    if (!e.materials.empty()) e.material = e.materials[0];
    if (const Value* r = mod("initRunner")) findAssetRefs(*r, 0, "ParticleSystemDef", e.runners);
    if (const Value* m = mod("initModel")) findAssetRefs(*m, 0, "XModel", e.models);
    for (const char* lightModule : {"initLightOmni", "initLightSpot"})
        if (const Value* l = mod(lightModule)) {
            e.light.present = true;
            e.light.spot = std::string(lightModule) == "initLightSpot";
            e.light.brightness = std::max(0.0, numberOr(l->get("m_brightness"), 1.0));
            break;
        }
    if (const Value* r = mod("initRelativeVelocity")) e.relVelType = enumOr(r->get("m_velocityType"), 0);
    // Spawn shapes: the base (value."") holds the offset and the axis flags.
    const auto shapeBase = [&](const Value* s) {
        const Value* b = s ? s->get("") : nullptr;
        if (!b) return;
        if (readVec(b, "m_offset", v)) vec3(v, e.shape.offset);
        e.shape.axisFlags = enumOr(b->get("m_axisFlags"), 63);
        e.shape.surface = (enumOr(b->get("m_spawnFlags"), 0) & 1) != 0;
        e.shape.axis = std::clamp(enumOr(b->get("m_normalAxis"), 2), 0, 2);
    };
    if (const Value* s = mod("initSpawnShapeBox")) {
        e.shape.type = 1;
        shapeBase(s);
        if (readVec(s, "m_dimensionsMin", v)) vec3(v, e.shape.dimMin);
        if (readVec(s, "m_dimensionsMax", v)) vec3(v, e.shape.dimMax);
    } else if (const Value* s2 = mod("initSpawnShapeSphere")) {
        e.shape.type = 2;
        shapeBase(s2);
        readRange(s2->get("m_radius"), e.shape.radiusMin, e.shape.radiusMax);
    } else if (const Value* s3 = mod("initSpawnShapeCylinder")) {
        e.shape.type = 3;
        shapeBase(s3);
        readRange(s3->get("m_radius"), e.shape.radiusMin, e.shape.radiusMax);
        e.shape.halfHeight = numberOr(s3->get("m_halfHeight"), 0);
    } else if (const Value* s4 = mod("initSpawnShapeEllipsoid")) {
        e.shape.type = 4;
        shapeBase(s4);
        if (readVec(s4, "m_radiusMin", v)) vec3(v, e.shape.dimMin);
        if (readVec(s4, "m_radiusMax", v)) vec3(v, e.shape.dimMax);
    }
    e.color = readGraph(mod("colorGraph"), 4);
    e.size = readGraph(mod("sizeGraph"), 3);
    e.velocity = readGraph(mod("velocityGraph"), 3);
    e.rotation = readGraph(mod("rotationGraph"), 1);
    if (const Value* r = mod("rotationGraph"); r && !boolOr(r->get("m_useRotationRate"), true)) e.rotation.present = false;
    e.intensity = readGraph(mod("intensityGraph"), 1);
    e.emissive = readGraph(mod("emissiveGraph"), 5);
    e.drag = readGraph(mod("forceDragGraph"), 1);
    if (const Value* g = mod("gravity")) {
        double a = 0, b = 0;
        if (readRange(g->get("m_gravityPercentage"), a, b)) e.gravityMin = a, e.gravityMax = b;
    }
    if (const Value* f = mod("force")) {
        if (readVec(f, "m_forceMin", v)) vec3(v, e.forceMin);
        if (readVec(f, "m_forceMax", v)) vec3(v, e.forceMax);
        e.forceWorld = (moduleFlags(f) & 0x80u) != 0;
    }
    if (const Value* r = mod("initRotation")) {
        readRange(r->get("m_rotationAngle"), e.rotAngleMin, e.rotAngleMax);
        readRange(r->get("m_rotationRate"), e.rotRateMin, e.rotRateMax);
    }
    if (const Value* a = mod("initAtlas")) {
        e.atlas.present = true;
        e.atlas.start = static_cast<int>(numberOr(a->get("m_startFrame"), 0));
        e.atlas.loops = static_cast<int>(numberOr(a->get("m_loopCount"), -1));
        e.atlas.randomIndex = boolOr(a->get("m_randomIndex"), false);
        e.atlas.overLife = boolOr(a->get("m_playOverLife"), false);
        e.atlas.random = (moduleFlags(a) & 0x10u) != 0;
        if (const Value* c = a->get("m_curves"); c && c->type == Value::Type::Array && !c->arr.empty()) {
            e.atlas.c0 = readCurve(c->arr[0]);
            e.atlas.c1 = c->arr.size() > 1 ? readCurve(c->arr[1]) : e.atlas.c0;
        }
    }
    if (const Value* m = mod("initMirrorTexture")) {
        e.mirrorH = enumOr(m->get("m_mirrorHorizontal"), 0);
        e.mirrorV = enumOr(m->get("m_mirrorVertical"), 0);
    }
    if (const Value* t = mod("initTail")) e.tailScaleWithVelocity = boolOr(t->get("m_scaleWithVelocity"), false);
    if (const Value* f = mod("initFlare")) {
        e.flare.present = true;
        e.flare.position = numberOr(f->get("m_position"), 1);
        if (readVec(f, "m_direction", v) && v.size() >= 3)
            for (int c = 0; c < 3; ++c) e.flare.direction[c] = v[c];
        e.flare.angularRotCount = static_cast<int>(numberOr(f->get("m_angularRotCount"), 0));
        e.flare.flags = static_cast<uint32_t>(numberOr(f->get("m_flareFlags"), 0));
        readEnds(f->get("m_depthScaleRange"), e.flare.depthRangeMin, e.flare.depthRangeMax);
        readEnds(f->get("m_depthScaleValue"), e.flare.depthValueMin, e.flare.depthValueMax);
        readEnds(f->get("m_radialRot"), e.flare.radialRotMin, e.flare.radialRotMax);
        readEnds(f->get("m_radialScaleX"), e.flare.scaleXMin, e.flare.scaleXMax);
        readEnds(f->get("m_radialScaleY"), e.flare.scaleYMin, e.flare.scaleYMax);
        if (const Value* c = f->get("m_curves"); c && c->type == Value::Type::Array)
            for (size_t k = 0; k < c->arr.size() && k < 4; ++k) e.flare.curves[k] = readCurve(c->arr[k]);
    }
    if (const Value* o = mod("initOcclusionQuery")) {
        e.occlusion.present = true;
        double lo = 1, hi = 1;
        readRange(o->get("m_scale"), lo, hi);
        e.occlusion.scale = hi;
        e.occlusion.depthBias = numberOr(o->get("m_depthBias"), 0);
        e.occlusion.offscreenRaycast = boolOr(o->get("m_offscreenRaycast"), false);
        if (const Value* ws = o->get("m_worldSize")) {
            const Value* arr = ws->get("v");
            if (arr && arr->type == Value::Type::Array && arr->arr.size() >= 2)
                for (int c = 0; c < 2; ++c) e.occlusion.worldSize[c] = numberOr(&arr->arr[size_t(c)], 0);
        }
    }
    if (const Value* o = mod("initOrientedSprite"))
        if (readVec(o, "m_orientationQuat", v) && v.size() >= 4)
            for (int c = 0; c < 4; ++c) e.orientQuat[c] = v[c];
    return e;
}

// splitmix64 finalizer, and a unit-interval hash of (seed, emitter, particle id, salt).
uint64_t mix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

double unitHash(uint32_t seed, size_t emitter, uint64_t id, uint32_t salt) {
    uint64_t h = mix64(seed);
    h = mix64(h ^ static_cast<uint64_t>(emitter));
    h = mix64(h ^ id);
    h = mix64(h ^ salt);
    return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);  // [0, 1)
}

constexpr uint64_t kBurstFlag = 1ull << 62;  // separates burst ids from continuous ids

// Salts of the per-particle randoms.
enum Salt : uint32_t {
    kLife = 1, kVel = 2, /* 3, 4 */ kShape = 5, /* 6, 7 */ kAttrSize = 8, kAttrColor = 9, kGraphColor = 10, kGraphSize = 11,
    kGraphVel = 12, kGraphRot = 13, kGraphInt = 14, kGraphDrag = 15, kRotAngle = 16, kRotRate = 17, kAtlasStart = 18,
    kAtlasCurve = 19, kMirrorU = 20, kMirrorV = 21, kMaterial = 22, kGravity = 23, kForce = 24, kGraphEmissive = 25,
};

// Effect axes to the placement's.
void toWorld(const VfxAxes& ax, const double l[3], double out[3]) {
    for (int c = 0; c < 3; ++c) out[c] = ax.x[c] * l[0] + ax.y[c] * l[1] + ax.z[c] * l[2];
}

// The spawn point in effect axes (research 138 section 4).
void spawnPoint(const VfxEmitterSim& e, uint32_t seed, size_t ei, uint64_t id, double p[3]) {
    const auto& s = e.shape;
    const double r0 = unitHash(seed, ei, id, kShape), r1 = unitHash(seed, ei, id, kShape + 1), r2 = unitHash(seed, ei, id, kShape + 2);
    p[0] = p[1] = p[2] = 0;
    double d[3] = {0, 0, 0};  // the point relative to the offset
    if (s.type == 1) {
        const double r[3] = {r0, r1, r2};
        for (int c = 0; c < 3; ++c) p[c] = s.dimMin[c] + r[c] * (s.dimMax[c] - s.dimMin[c]);
        return;  // absolute bounds: the offset is their centre
    }
    if (s.type == 2 || s.type == 4) {
        const double z = 2.0 * r0 - 1.0, phi = 6.283185307179586 * r1, q = std::sqrt(std::max(0.0, 1.0 - z * z));
        const double dir[3] = {q * std::cos(phi), q * std::sin(phi), z};
        if (s.type == 2) {
            const double rad = s.surface ? s.radiusMax : s.radiusMin + std::cbrt(r2) * (s.radiusMax - s.radiusMin);
            for (int c = 0; c < 3; ++c) d[c] = dir[c] * rad;
        } else {
            for (int c = 0; c < 3; ++c) d[c] = dir[c] * (s.dimMin[c] + std::cbrt(r2) * (s.dimMax[c] - s.dimMin[c]));
        }
    } else if (s.type == 3) {
        const double phi = 6.283185307179586 * r0;
        const double rad = s.surface ? s.radiusMax : s.radiusMin + std::sqrt(r1) * (s.radiusMax - s.radiusMin);
        const int a = s.axis, b = (a + 1) % 3, c = (a + 2) % 3;
        d[b] = rad * std::cos(phi), d[c] = rad * std::sin(phi), d[a] = (2.0 * r2 - 1.0) * s.halfHeight;
    }
    // Only the allowed half-axes: a disallowed side is mirrored onto the allowed one.
    for (int c = 0; c < 3; ++c) {
        const bool pos = (s.axisFlags >> (2 * c)) & 1, neg = (s.axisFlags >> (2 * c + 1)) & 1;
        if (pos && !neg) d[c] = std::fabs(d[c]);
        else if (neg && !pos) d[c] = -std::fabs(d[c]);
    }
    for (int c = 0; c < 3; ++c) p[c] = s.offset[c] + d[c];
}

// Adds the particle spawned at spawnT if it is still alive at t (age < life).
void emitParticle(const VfxEmitterSim& em, size_t ei, uint32_t seed, uint64_t id, double spawnT, double t, const VfxAxes& ax,
                  std::vector<VfxParticle>& out) {
    if (out.size() >= kVfxMaxParticles) return;
    const double age = t - spawnT;
    if (age < 0) return;
    const double life = em.lifeMin + unitHash(seed, ei, id, kLife) * (em.lifeMax - em.lifeMin);
    if (!(life > 0) || !(age < life)) return;
    const auto rnd = [&](uint32_t salt) { return static_cast<float>(unitHash(seed, ei, id, salt)); };
    const double f = age / life;  // [0, 1)

    VfxParticle p;
    p.emitter = static_cast<int>(ei);
    p.age = age, p.life = life;

    // Start point and start velocity (effect axes), then into the placement's axes.
    double sp[3];
    spawnPoint(em, seed, ei, id, sp);
    double v0l[3];
    for (int c = 0; c < 3; ++c) v0l[c] = em.velMin[c] + unitHash(seed, ei, id, kVel + c) * (em.velMax[c] - em.velMin[c]);
    double v0[3], start[3];
    toWorld(ax, sp, start);
    if (em.relVelType == 1) {
        for (int c = 0; c < 3; ++c) v0[c] = v0l[c];  // world
    } else if (em.relVelType == 2) {
        // Outward from the effect origin: x along the spawn point's direction (research 138 section 3, INFERRED).
        const double len = std::sqrt(sp[0] * sp[0] + sp[1] * sp[1] + sp[2] * sp[2]);
        VfxAxes fr;
        if (len > 1e-6) {
            for (int c = 0; c < 3; ++c) fr.x[c] = sp[c] / len;
            const double up[3] = {0, 0, 1};
            double yv[3] = {up[1] * fr.x[2] - up[2] * fr.x[1], up[2] * fr.x[0] - up[0] * fr.x[2], up[0] * fr.x[1] - up[1] * fr.x[0]};
            double yl = std::sqrt(yv[0] * yv[0] + yv[1] * yv[1] + yv[2] * yv[2]);
            if (yl < 1e-6) yv[0] = 0, yv[1] = 1, yv[2] = 0, yl = 1;
            for (int c = 0; c < 3; ++c) fr.y[c] = yv[c] / yl;
            fr.z[0] = fr.x[1] * fr.y[2] - fr.x[2] * fr.y[1], fr.z[1] = fr.x[2] * fr.y[0] - fr.x[0] * fr.y[2], fr.z[2] = fr.x[0] * fr.y[1] - fr.x[1] * fr.y[0];
        }
        double vl[3];
        toWorld(fr, v0l, vl);
        toWorld(ax, vl, v0);
    } else {
        toWorld(ax, v0l, v0);
    }
    // Constant acceleration: gravity (world -Z) and the force module.
    double acc[3] = {0, 0, -em.gravity};
    if (em.gravityMax > 0 || em.gravityMin > 0) acc[2] -= kWorldGravity * (em.gravityMin + rnd(kGravity) * (em.gravityMax - em.gravityMin));
    {
        const double rf = rnd(kForce);
        double fl[3];
        for (int c = 0; c < 3; ++c) fl[c] = em.forceMin[c] + rf * (em.forceMax[c] - em.forceMin[c]);
        double fw[3];
        if (em.forceWorld) for (int c = 0; c < 3; ++c) fw[c] = fl[c];
        else toWorld(ax, fl, fw);
        for (int c = 0; c < 3; ++c) acc[c] += fw[c];
    }
    // The velocity graph at age fraction x, in the placement's axes.
    const float rv = rnd(kGraphVel);
    const auto graphVel = [&](double x, double out3[3]) {
        out3[0] = out3[1] = out3[2] = 0;
        if (!em.velocity.present) return;
        double gl[3];
        for (int c = 0; c < 3; ++c) gl[c] = em.velocity.eval(size_t(c), static_cast<float>(x), rv);
        if (em.velocity.world) for (int c = 0; c < 3; ++c) out3[c] = gl[c];
        else toWorld(ax, gl, out3);
    };
    const float rd = rnd(kGraphDrag), rr = rnd(kGraphRot), ra = rnd(kAtlasCurve);
    const double rate0 = em.rotRateMin + rnd(kRotRate) * (em.rotRateMax - em.rotRateMin);
    // Integrals over the particle's life so far: position (velocity graph, or everything with drag), rotation, atlas.
    const int steps = std::clamp(static_cast<int>(std::ceil(age * 15.0)), 1, 48);
    const double dt = age / steps;
    double pos[3] = {start[0], start[1], start[2]}, vel[3];
    double angle = em.rotAngleMin + rnd(kRotAngle) * (em.rotAngleMax - em.rotAngleMin) + rate0 * age;
    double frames = 0;
    const auto atlasRate = [&](double x) {
        if (!em.atlas.present || em.atlas.overLife) return 0.0;
        const float a = em.atlas.c0.eval(static_cast<float>(x)), b = em.atlas.c1.eval(static_cast<float>(x));
        return double(em.atlas.random ? a + (b - a) * ra : a);
    };
    if (em.drag.present) {
        // Stepped: v = v_base + graph, v_base' = acc - k v (research 138 section 3, INFERRED form).
        double vb[3] = {v0[0], v0[1], v0[2]};
        for (int s = 0; s < steps; ++s) {
            const double x = (s + 0.5) * dt / life;
            double g[3];
            graphVel(x, g);
            const double k = em.drag.eval(0, static_cast<float>(x), rd);
            for (int c = 0; c < 3; ++c) {
                const double vt = vb[c] + g[c];
                pos[c] += vt * dt;
                vb[c] += (acc[c] - k * vt) * dt;
            }
        }
        double g[3];
        graphVel(f, g);
        for (int c = 0; c < 3; ++c) vel[c] = vb[c] + g[c];
    } else {
        // Exact for the constant parts; the graph's integral by the midpoint rule.
        double gi[3] = {0, 0, 0};
        if (em.velocity.present)
            for (int s = 0; s < steps; ++s) {
                double g[3];
                graphVel((s + 0.5) * dt / life, g);
                for (int c = 0; c < 3; ++c) gi[c] += g[c] * dt;
            }
        double g[3];
        graphVel(f, g);
        for (int c = 0; c < 3; ++c) {
            pos[c] += v0[c] * age + 0.5 * acc[c] * age * age + gi[c];
            vel[c] = v0[c] + acc[c] * age + g[c];
        }
    }
    if (em.rotation.present)
        for (int s = 0; s < steps; ++s) angle += em.rotation.eval(0, static_cast<float>((s + 0.5) * dt / life), rr) * dt;
    if (em.atlas.present && !em.atlas.overLife)
        for (int s = 0; s < steps; ++s) frames += atlasRate((s + 0.5) * dt / life) * dt;
    for (int c = 0; c < 3; ++c) p.pos[c] = pos[c], p.vel[c] = vel[c];
    p.angle = angle;

    // Size: the attribute (one random) or the over-life lerp, then the graph (replace or multiply).
    const float rs = rnd(kAttrSize), rgs = rnd(kGraphSize);
    double sz[2];
    for (int c = 0; c < 2; ++c)
        sz[c] = em.hasAttrSize ? em.sizeMin[c] + rs * (em.sizeMax[c] - em.sizeMin[c]) : em.sizeStart + (em.sizeEnd - em.sizeStart) * f;
    if (!em.hasAttrSize) sz[1] = sz[0];
    if (em.size.present)
        for (int c = 0; c < 2; ++c) {
            const double g = em.size.eval(size_t(c), static_cast<float>(f), rgs);
            sz[c] = em.size.replace ? g : sz[c] * g;
        }
    p.size2[0] = sz[0], p.size2[1] = sz[1];
    p.size = sz[0];
    // Colour likewise; the intensity graph scales RGB.
    const float rc = rnd(kAttrColor), rgc = rnd(kGraphColor), ri = rnd(kGraphInt);
    for (int c = 0; c < 4; ++c) {
        double col = em.hasAttrColor ? em.colorMin[c] + rc * (em.colorMax[c] - em.colorMin[c]) : em.colorStart[c] + (em.colorEnd[c] - em.colorStart[c]) * f;
        if (em.color.present) {
            const double g = em.color.eval(size_t(c), static_cast<float>(f), rgc);
            col = em.color.replace ? g : col * g;
        }
        p.rgba[c] = static_cast<float>(c < 3 ? std::max(0.0, col) : std::clamp(col, 0.0, 1.0));
    }
    if (em.intensity.present) {
        const double k = std::max(0.0f, em.intensity.eval(0, static_cast<float>(f), ri));
        for (int c = 0; c < 3; ++c) p.rgba[c] = static_cast<float>(p.rgba[c] * k);
    }
    if (em.emissive.present) {
        // RGB (curves 0-2) x intensity (curve 3), at the age fraction; one random for the A/B lerp.
        const float re = rnd(kGraphEmissive);
        const double k = std::max(0.0f, em.emissive.eval(3, static_cast<float>(f), re));
        for (int c = 0; c < 3; ++c) p.emissive[c] = static_cast<float>(std::max(0.0f, em.emissive.eval(size_t(c), static_cast<float>(f), re)) * k);
        p.hasEmissive = true;
    }
    // Atlas, mirror, material.
    if (em.atlas.present && em.atlas.overLife) {
        const float a = em.atlas.c0.eval(static_cast<float>(f)), b = em.atlas.c1.eval(static_cast<float>(f));
        p.atlasPos = std::clamp(double(em.atlas.random ? a + (b - a) * ra : a), 0.0, 1.0);
    }
    p.atlasFrames = frames;
    p.rnd = unitHash(seed, ei, id, kAtlasStart);
    p.flipU = em.mirrorH == 1 || (em.mirrorH == 2 && rnd(kMirrorU) < 0.5f);
    p.flipV = em.mirrorV == 1 || (em.mirrorV == 2 && rnd(kMirrorV) < 0.5f);
    p.material = em.materials.size() > 1 ? std::min(int(em.materials.size()) - 1, int(rnd(kMaterial) * em.materials.size())) : 0;
    out.push_back(p);
}

// Seconds a continuous emitter keeps spawning when it does not play on: its emitter life, else the particle life max.
double spawnWindow(const VfxEmitterSim& e) { return e.emitterLife > 0 ? e.emitterLife : e.lifeMax; }

void emitEmitter(const VfxEmitterSim& em, size_t ei, uint32_t seed, double t, const VfxAxes& ax, std::vector<VfxParticle>& out) {
    if (t < em.startDelay) return;
    // Burst mode (or no spawn rate): the burst once at the start.
    if (em.burst || !(em.spawnRate > 0)) {
        for (int k = 0; k < em.burstCount && out.size() < kVfxMaxParticles; ++k)
            emitParticle(em, ei, seed, kBurstFlag | static_cast<uint64_t>(k), em.startDelay, t, ax, out);
        return;
    }
    const double rate = em.spawnRate;
    // Spawn times ts_k = startDelay + k / rate while the emitter runs (forever when it plays on).
    const double last = em.looping ? t : std::min(t, em.startDelay + spawnWindow(em));
    const double kHi = std::floor((last - em.startDelay) * rate);
    if (!(kHi >= 0)) return;
    // Particles spawned before t - lifeMax are dead. The alive cap keeps the newest countMax.
    double kLoD = std::max(0.0, std::ceil((t - em.lifeMax - em.startDelay) * rate));
    const size_t before = out.size();
    for (double k = kHi; k >= kLoD && out.size() < kVfxMaxParticles; k -= 1.0) {
        if (em.countMax > 0 && out.size() - before >= size_t(em.countMax)) break;
        emitParticle(em, ei, seed, static_cast<uint64_t>(k), em.startDelay + k / rate, t, ax, out);
    }
    std::reverse(out.begin() + std::ptrdiff_t(before), out.end());  // oldest first, as before
}

}  // namespace

float VfxCurve::eval(float x) const {
    const size_t n = std::min(t.size(), v.size());
    if (n == 0) return 0.0f;
    float y;
    if (x <= t[0]) {
        y = v[0];
    } else if (x >= t[n - 1]) {
        y = v[n - 1];
    } else {
        size_t i = 1;
        while (i < n && t[i] < x) ++i;
        const float span = t[i] - t[i - 1];
        y = span > 1e-9f ? v[i - 1] + (x - t[i - 1]) / span * (v[i] - v[i - 1]) : v[i];
    }
    return y * scale;
}

float VfxGraph::eval(size_t k, float x, float r) const {
    if (k >= a.size()) return 0.0f;
    const float va = a[k].eval(x);
    if (!random || k >= b.size()) return va;
    return va + (b[k].eval(x) - va) * r;
}

VfxAxes vfxAxesFromAngles(double pitch, double yaw, double roll) {
    constexpr double d = 3.14159265358979323846 / 180.0;
    const double sp = std::sin(pitch * d), cp = std::cos(pitch * d), sy = std::sin(yaw * d), cy = std::cos(yaw * d);
    const double sr = std::sin(roll * d), cr = std::cos(roll * d);
    VfxAxes a;
    // AngleVectors: forward, right, up; left = -right.
    a.x[0] = cp * cy, a.x[1] = cp * sy, a.x[2] = -sp;
    const double right[3] = {-sr * sp * cy + cr * sy, -sr * sp * sy - cr * cy, -sr * cp};
    a.y[0] = -right[0], a.y[1] = -right[1], a.y[2] = -right[2];
    a.z[0] = cr * sp * cy + sr * sy, a.z[1] = cr * sp * sy - sr * cy, a.z[2] = cr * cp;
    return a;
}

bool buildVfxSim(const std::string& exportJson, VfxSimDef& out, std::string* err) {
    std::string parseErr;
    const auto root = json::parse(exportJson, &parseErr);
    if (!root) {
        setErr(err, "not JSON: " + parseErr);
        return false;
    }
    if (root->type != Value::Type::Object) {
        setErr(err, "root is not an object");
        return false;
    }
    const Value* format = root->get("format");
    if (!format || !format->string() || format->string()->rfind("mw19-asset-json", 0) != 0) {
        setErr(err, "not an mw19 asset-json export");
        return false;
    }
    const Value* rootType = root->get("root_type");
    if (!rootType || !rootType->string() || *rootType->string() != "ParticleSystemDef") {
        setErr(err, "root_type is not ParticleSystemDef");
        return false;
    }
    const Value* asset = root->get("asset");
    const Value* fields = asset ? asset->get("fields") : nullptr;
    if (!fields || fields->type != Value::Type::Object) {
        setErr(err, "asset.fields is missing");
        return false;
    }
    const Value* nameField = fields->get("name");
    const Value* nameStr = nameField ? nameField->get("string") : nullptr;
    if (!nameStr || !nameStr->string()) {
        setErr(err, "fields.name.string is missing");
        return false;
    }

    VfxSimDef def;
    def.name = *nameStr->string();
    const Value* block = fields->get("emitterDefs");
    const Value* rows = block ? block->get("values") : nullptr;
    if (rows && rows->type == Value::Type::Array) {
        for (size_t i = 0; i < rows->arr.size(); ++i) {
            if (rows->arr[i].type != Value::Type::Object) continue;
            def.emitters.push_back(readEmitter(rows->arr[i], i));
        }
    }

    // A looping effect runs forever. A one-shot effect ends when its last particle dies.
    bool anyLooping = false;
    double end = 0;
    for (const auto& e : def.emitters) {
        if (e.looping) anyLooping = true;
        const bool burstOnly = e.burst || !(e.spawnRate > 0);
        end = std::max(end, e.startDelay + (burstOnly ? 0.0 : spawnWindow(e)) + e.lifeMax);
    }
    def.duration = anyLooping ? -1.0 : end;
    out = std::move(def);
    return true;
}

std::vector<VfxParticle> sampleVfx(const VfxSimDef& def, double t, uint32_t seed, const VfxAxes* axes) {
    std::vector<VfxParticle> out;
    if (!(t >= 0)) return out;  // also rejects NaN
    if (def.duration >= 0 && t > def.duration) return out;  // one-shot effect has finished
    const VfxAxes ax = axes ? *axes : VfxAxes{};
    for (size_t ei = 0; ei < def.emitters.size() && out.size() < kVfxMaxParticles; ++ei) emitEmitter(def.emitters[ei], ei, seed, t, ax, out);
    return out;
}

int vfxAtlasFrame(const VfxEmitterSim& e, const VfxParticle& p, int frameCount) {
    if (!e.atlas.present || frameCount < 2) return 0;
    const int n = frameCount, start = std::clamp(e.atlas.start, 0, n - 1);
    if (e.atlas.overLife) {
        const double pos = std::clamp(p.atlasPos, 0.0, 1.0);
        return std::min(n - 1, start + static_cast<int>(std::floor(pos * (n - start))));
    }
    const int first = e.atlas.randomIndex ? std::min(n - 1, static_cast<int>(p.rnd * n)) : start;
    const long played = static_cast<long>(std::floor(std::max(0.0, p.atlasFrames)));
    if (e.atlas.loops > 0 && played >= long(e.atlas.loops) * n) return n - 1;  // played its loops: holds the last frame
    return static_cast<int>((first + played) % n);
}

AABB vfxBounds(const VfxSimDef& def, double loopSeconds, int steps, uint32_t seed) {
    AABB box;
    const double span = def.duration > 0 ? def.duration : loopSeconds;
    steps = std::max(2, steps);
    for (int i = 0; i < steps; ++i) {
        const double t = span * double(i) / double(steps - 1);
        for (const VfxParticle& p : sampleVfx(def, t, seed)) {
            const double h = std::max(p.size2[0], p.size2[1]) * 0.5;
            box.add(Vec3{p.pos[0] - h, p.pos[1] - h, p.pos[2] - h});
            box.add(Vec3{p.pos[0] + h, p.pos[1] + h, p.pos[2] + h});
        }
    }
    return box;
}

VfxFlareSprite vfxFlareSprite(const VfxEmitterSim& e, const double size[2], const double sourceNdc[2], double aspect, double depth) {
    VfxFlareSprite out;
    const VfxEmitterSim::Flare& f = e.flare;
    aspect = aspect > 1e-6 ? aspect : 1.0;
    // The source's distance from the centre in screen-height units, 1 at a corner.
    const double sx = sourceNdc[0] * aspect, sy = sourceNdc[1];
    const double corner = std::sqrt(aspect * aspect + 1.0);
    out.radial = std::min(1.0, std::sqrt(sx * sx + sy * sy) / corner);
    // Curves 0 / 1 / 2 run over the screen with the centre at 0.5 (their stock shapes are symmetric about it).
    const double t = 0.5 + 0.5 * out.radial;
    const auto lerp = [](double a, double b, double x) { return a + (b - a) * x; };
    const auto curve = [&](int k, double x) { return f.curves[k].t.empty() ? 1.0 : double(f.curves[k].eval(float(x))); };
    double depthScale = 1;
    if (f.depthRangeMax > f.depthRangeMin) {
        const double x = std::clamp((depth - f.depthRangeMin) / (f.depthRangeMax - f.depthRangeMin), 0.0, 1.0);
        depthScale = lerp(f.depthValueMin, f.depthValueMax, curve(3, x));
    }
    const double scaleX = lerp(f.scaleXMin, f.scaleXMax, curve(0, t)), scaleY = lerp(f.scaleYMin, f.scaleYMax, curve(1, t));
    // Along the line from the source through the centre.
    out.centre[0] = sourceNdc[0] * f.position, out.centre[1] = sourceNdc[1] * f.position;
    // Sizes are percent of the screen height (NDC height 2): half extent = size / 100 x scale.
    out.half[1] = size[1] / 100.0 * scaleY * depthScale;
    out.half[0] = size[0] / 100.0 * scaleX * depthScale / aspect;
    // Pieces with an angular rotation count turn with the source's angle about the centre (rays point at it), plus the
    // radial rotation.
    out.angle = lerp(f.radialRotMin, f.radialRotMax, curve(2, t));
    if (f.angularRotCount != 0 && (sx != 0 || sy != 0)) out.angle += std::atan2(sy, sx);
    return out;
}

VfxDistortion vfxDistortion(const double m[3], double vertexAlpha, const double scale[2], const double tangent[3], const double bitangent[3]) {
    VfxDistortion out;
    const auto sat = [](double x) { return x < 0 ? 0.0 : x > 1 ? 1.0 : x; };
    out.k = sat(m[2] * vertexAlpha - 0.25) * (4.0 / 3.0);
    const double dx = scale[0] * out.k * (2 * m[0] - 1), dy = scale[1] * out.k * (2 * m[1] - 1);
    for (int i = 0; i < 3; ++i) out.offset[i] = dx * tangent[i] + dy * bitangent[i];
    out.alpha = sat(4 * m[2] * vertexAlpha);
    return out;
}

}  // namespace iw8r
