// Simplified FX preview simulation. Field paths and defaults are documented in vfx_sim.h.
#include "vfx_sim.h"

#include "json_lite.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>

namespace iw8r {
namespace {

using json::Value;

// Emitter flag bits as in vfx_info.cpp (IW7 names; the Replay 1.20 values are not confirmed).
constexpr uint32_t kEmitterInfiniteParticleLife = 0x8u;
constexpr uint32_t kEmitterLoopInfiniteParticles = 0x10u;

// Caps for values read from the export, so one bad field cannot make the preview spin.
constexpr double kMaxSpawnRate = 100000.0;  // particles per second
constexpr double kMaxBurstCount = 2000.0;

// Defaults used when the export has no value (see vfx_sim.h).
constexpr double kDefaultSpawnRate = 0.0;
constexpr double kDefaultBurstCount = 0.0;
constexpr double kDefaultLife = 1.0;      // seconds
constexpr double kDefaultDelay = 0.0;     // seconds
constexpr double kDefaultEmitterLife = 0.0;
constexpr double kDefaultVelocity = 0.0;  // inches/s, each axis
constexpr double kDefaultGravity = 0.0;   // inches/s^2 (always used; gravity is not decoded)
constexpr double kDefaultSize = 1.0;      // inches
constexpr float kDefaultColor = 1.0f;     // rgba, white and opaque

void setErr(std::string* err, const std::string& msg) {
    if (err) *err = msg;
}

double numberOr(const Value* v, double fallback) {
    const auto n = v ? v->number() : std::nullopt;
    return n ? *n : fallback;
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

double midpoint(double lo, double hi) { return 0.5 * (lo + hi); }

// Finds the first object below `node` whose "moduleType".name equals `typeName` (the module object, which holds
// "moduleData").
const Value* findModule(const Value& node, const char* typeName, int depth) {
    if (depth > 64) return nullptr;
    if (node.type == Value::Type::Array) {
        for (const auto& item : node.arr) {
            if (const Value* r = findModule(item, typeName, depth + 1)) return r;
        }
        return nullptr;
    }
    if (node.type != Value::Type::Object) return nullptr;
    const Value* mt = node.get("moduleType");
    const Value* nm = mt && mt->type == Value::Type::Object ? mt->get("name") : nullptr;
    if (nm && nm->string() && *nm->string() == typeName) return &node;
    for (const auto& kv : node.obj) {
        if (kv.first == "$native") continue;
        if (const Value* r = findModule(kv.second, typeName, depth + 1)) return r;
    }
    return nullptr;
}

// moduleData.value: the decoded module payload (INIT_ATTRIBUTES fields, INIT_MATERIAL linked assets).
const Value* moduleValue(const Value* module) {
    const Value* data = module ? module->get("moduleData") : nullptr;
    return data ? data->get("value") : nullptr;
}

// Reads a vector stored as <field>.v.m128_f32 (the x, y, z, w floats of an SSE value). Returns false when absent.
bool readVec(const Value* payload, const char* field, std::vector<double>& out) {
    const Value* f = payload ? payload->get(field) : nullptr;
    const Value* v = f ? f->get("v") : nullptr;
    const Value* arr = v ? v->get("m128_f32") : nullptr;
    if (!arr || arr->type != Value::Type::Array) return false;
    out.clear();
    for (const auto& item : arr->arr) out.push_back(numberOr(&item, 0.0));
    return true;
}

// The first Material reference below `node` (asset_reference, type "Material"). A leading ',' (shared name) is removed.
std::string findMaterialRef(const Value& node, int depth) {
    if (depth > 64) return {};
    if (node.type == Value::Type::Array) {
        for (const auto& item : node.arr) {
            std::string s = findMaterialRef(item, depth + 1);
            if (!s.empty()) return s;
        }
        return {};
    }
    if (node.type != Value::Type::Object) return {};
    const Value* isRef = node.get("asset_reference");
    const Value* type = node.get("type");
    const Value* name = node.get("name");
    if (isRef && isRef->type == Value::Type::Bool && isRef->b && type && type->string() && *type->string() == "Material" &&
        name && name->string()) {
        const std::string& n = *name->string();
        return !n.empty() && n[0] == ',' ? n.substr(1) : n;
    }
    for (const auto& kv : node.obj) {
        if (kv.first == "$native") continue;
        std::string s = findMaterialRef(kv.second, depth + 1);
        if (!s.empty()) return s;
    }
    return {};
}

VfxEmitterSim readEmitter(const Value& em, size_t index) {
    VfxEmitterSim e;
    e.name = "emitter" + std::to_string(index);

    // Looping: infinite particle life or loop flag (vfx_info.cpp rule).
    const auto flags = static_cast<uint32_t>(numberOr(em.get("flags"), 0.0));
    e.looping = (flags & (kEmitterInfiniteParticleLife | kEmitterLoopInfiniteParticles)) != 0;

    double lo = 0, hi = 0;
    e.spawnRate = kDefaultSpawnRate;
    if (readRange(em.get("particleSpawnRate"), lo, hi)) e.spawnRate = std::clamp(midpoint(lo, hi), 0.0, kMaxSpawnRate);

    e.burstCount = static_cast<int>(kDefaultBurstCount);
    if (readRange(em.get("particleBurstCount"), lo, hi)) {
        e.burstCount = static_cast<int>(std::clamp(std::round(midpoint(lo, hi)), 0.0, kMaxBurstCount));
    }

    e.lifeMin = e.lifeMax = kDefaultLife;
    if (readRange(em.get("particleLife"), lo, hi)) {
        e.lifeMin = std::max(0.0, lo);
        e.lifeMax = std::max(e.lifeMin, hi);
    }

    e.startDelay = kDefaultDelay;
    if (readRange(em.get("emitterDelay"), lo, hi)) e.startDelay = std::max(0.0, midpoint(lo, hi));

    e.emitterLife = kDefaultEmitterLife;
    if (readRange(em.get("emitterLife"), lo, hi)) e.emitterLife = std::max(0.0, midpoint(lo, hi));

    e.gravity = kDefaultGravity;
    for (int c = 0; c < 3; ++c) {
        e.velMin[c] = e.velMax[c] = kDefaultVelocity;
    }
    e.sizeStart = e.sizeEnd = kDefaultSize;
    for (int c = 0; c < 4; ++c) {
        e.colorStart[c] = e.colorEnd[c] = kDefaultColor;
    }

    const Value* attrs = moduleValue(findModule(em, "PARTICLE_MODULE_INIT_ATTRIBUTES", 0));
    if (attrs) {
        std::vector<double> v;
        if (readVec(attrs, "m_velocityMin", v)) {
            for (int c = 0; c < 3 && c < static_cast<int>(v.size()); ++c) e.velMin[c] = v[c];
        }
        if (readVec(attrs, "m_velocityMax", v)) {
            for (int c = 0; c < 3 && c < static_cast<int>(v.size()); ++c) e.velMax[c] = v[c];
        }
        // Size: the x component of the size range, midpoint. The size end is the start (no size-over-life decoded).
        std::vector<double> sMin, sMax;
        const bool hasSMin = readVec(attrs, "m_sizeMin", sMin);
        const bool hasSMax = readVec(attrs, "m_sizeMax", sMax);
        if (hasSMin || hasSMax) {
            const double a = hasSMin && !sMin.empty() ? sMin[0] : (hasSMax && !sMax.empty() ? sMax[0] : kDefaultSize);
            const double b = hasSMax && !sMax.empty() ? sMax[0] : a;
            e.sizeStart = e.sizeEnd = midpoint(a, b);
        }
        // Colour: midpoint of the rgba range. The end colour is the start (no colour-over-life decoded).
        std::vector<double> cMin, cMax;
        const bool hasCMin = readVec(attrs, "m_colorMin", cMin);
        const bool hasCMax = readVec(attrs, "m_colorMax", cMax);
        if (hasCMin || hasCMax) {
            for (int c = 0; c < 4; ++c) {
                const double a = hasCMin && c < static_cast<int>(cMin.size()) ? cMin[c] : kDefaultColor;
                const double b = hasCMax && c < static_cast<int>(cMax.size()) ? cMax[c] : a;
                e.colorStart[c] = e.colorEnd[c] = static_cast<float>(midpoint(a, b));
            }
        }
    }

    if (const Value* mat = findModule(em, "PARTICLE_MODULE_INIT_MATERIAL", 0)) {
        e.material = findMaterialRef(*mat, 0);
    }
    return e;
}

// Seconds an emitter keeps spawning: emitterLife when set, otherwise until the particle life max.
double spawnWindow(const VfxEmitterSim& e) { return e.emitterLife > 0 ? e.emitterLife : e.lifeMax; }

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

// Adds the particle spawned at spawnT if it is still alive at t (age < life). Does nothing when it is dead or the list is full.
void emitParticle(const VfxEmitterSim& em, size_t ei, uint32_t seed, uint64_t id, double spawnT, double t,
                  std::vector<VfxParticle>& out) {
    if (out.size() >= kVfxMaxParticles) return;
    const double age = t - spawnT;
    if (age < 0) return;
    const double life = em.lifeMin + unitHash(seed, ei, id, 1) * (em.lifeMax - em.lifeMin);
    if (!(life > 0) || !(age < life)) return;

    VfxParticle p;
    p.emitter = static_cast<int>(ei);
    for (int c = 0; c < 3; ++c) {
        const double v = em.velMin[c] + unitHash(seed, ei, id, static_cast<uint32_t>(2 + c)) * (em.velMax[c] - em.velMin[c]);
        p.pos[c] = v * age;
    }
    // Gravity pulls down the effect Z axis: position = v * age + 0.5 * g * age^2 with g = (0, 0, -gravity).
    p.pos[2] -= 0.5 * em.gravity * age * age;

    const double f = age / life;  // [0, 1) because age < life
    p.size = em.sizeStart + (em.sizeEnd - em.sizeStart) * f;
    for (int c = 0; c < 4; ++c) {
        p.rgba[c] = static_cast<float>(em.colorStart[c] + (em.colorEnd[c] - em.colorStart[c]) * f);
    }
    out.push_back(p);
}

void emitEmitter(const VfxEmitterSim& em, size_t ei, uint32_t seed, double t, std::vector<VfxParticle>& out) {
    if (t < em.startDelay) return;

    // Burst: all particles at the start.
    for (int k = 0; k < em.burstCount && out.size() < kVfxMaxParticles; ++k) {
        emitParticle(em, ei, seed, kBurstFlag | static_cast<uint64_t>(k), em.startDelay, t, out);
    }

    if (!(em.spawnRate > 0)) return;
    const double rate = em.spawnRate;
    // Spawn times ts_k = startDelay + k / rate, for k >= 0 and ts_k <= last (a one-shot stops at its window end).
    const double last = em.looping ? t : std::min(t, em.startDelay + spawnWindow(em));
    const double kHi = std::floor((last - em.startDelay) * rate);
    if (!(kHi >= 0)) return;
    // Particles spawned before t - lifeMax are dead, so start the scan there.
    const double kLoD = std::max(0.0, std::ceil((t - em.lifeMax - em.startDelay) * rate));
    for (double k = kLoD; k <= kHi && out.size() < kVfxMaxParticles; k += 1.0) {
        const auto id = static_cast<uint64_t>(k);
        emitParticle(em, ei, seed, id, em.startDelay + k / rate, t, out);
    }
}

}  // namespace

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

    // A looping effect runs forever. A one-shot effect ends when its last particle dies: start + window + life max.
    bool anyLooping = false;
    double end = 0;
    for (const auto& e : def.emitters) {
        if (e.looping) anyLooping = true;
        end = std::max(end, e.startDelay + spawnWindow(e) + e.lifeMax);
    }
    def.duration = anyLooping ? -1.0 : end;

    out = std::move(def);
    return true;
}

std::vector<VfxParticle> sampleVfx(const VfxSimDef& def, double t, uint32_t seed) {
    std::vector<VfxParticle> out;
    if (!(t >= 0)) return out;  // also rejects NaN
    if (def.duration >= 0 && t > def.duration) return out;  // one-shot effect has finished
    for (size_t ei = 0; ei < def.emitters.size() && out.size() < kVfxMaxParticles; ++ei) {
        emitEmitter(def.emitters[ei], ei, seed, t, out);
    }
    return out;
}

AABB vfxBounds(const VfxSimDef& def, double loopSeconds, int steps, uint32_t seed) {
    AABB box;
    const double span = def.duration > 0 ? def.duration : loopSeconds;
    steps = std::max(2, steps);
    for (int i = 0; i < steps; ++i) {
        const double t = span * double(i) / double(steps - 1);
        for (const VfxParticle& p : sampleVfx(def, t, seed)) {
            const double h = p.size * 0.5;
            box.add(Vec3{p.pos[0] - h, p.pos[1] - h, p.pos[2] - h});
            box.add(Vec3{p.pos[0] + h, p.pos[1] + h, p.pos[2] + h});
        }
    }
    return box;
}

}  // namespace iw8r
