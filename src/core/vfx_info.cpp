#include "vfx_info.h"

#include "json_lite.h"

#include <cstdint>
#include <fstream>
#include <sstream>

namespace iw8r {
namespace {

using json::Value;

// Emitter flag bits as named by the IW7 structs (src/games/iw7/structs.hpp PARTICLE_EMITTER_DEF_FLAG). The Replay 1.20
// bits are not confirmed; the export's emitter flags use these positions (21 of 1251 emitters in mp_shipment set 0x8).
constexpr uint32_t kEmitterInfiniteParticleLife = 0x8u;
constexpr uint32_t kEmitterLoopInfiniteParticles = 0x10u;

void setErr(std::string* err, const std::string& msg) {
    if (err) *err = msg;
}

// Shared (cross-zone) names carry a leading ',' in the export, for example ",elcq/vfx_ui_chevron_2_ds".
std::string stripSharedMarker(const std::string& s) {
    return !s.empty() && s[0] == ',' ? s.substr(1) : s;
}

void addUnique(std::vector<std::string>& list, const std::string& value) {
    if (value.empty()) return;
    for (const auto& existing : list) {
        if (existing == value) return;
    }
    list.push_back(value);
}

double numberOr(const Value* v, double fallback) {
    const auto n = v ? v->number() : std::nullopt;
    return n ? *n : fallback;
}

// {"min": x, "max": y} range objects of ParticleFloatRange.
void readRange(const Value* obj, double& lo, double& hi) {
    lo = numberOr(obj ? obj->get("min") : nullptr, 0.0);
    hi = numberOr(obj ? obj->get("max") : nullptr, 0.0);
}

// Collects every asset reference and inline sound alias below `node`. A reference is an object with
// "asset_reference": true, "type" and "name" (ACTS writes these for each linked asset). An inline sound alias is a
// linked-asset entry whose "active_member" is "sound" and whose "value" holds a "string".
void collectReferences(const Value& node, VfxInfo& out, int depth) {
    if (depth > 128) return;
    if (node.type == Value::Type::Array) {
        for (const auto& item : node.arr) collectReferences(item, out, depth + 1);
        return;
    }
    if (node.type != Value::Type::Object) return;

    const Value* isRef = node.get("asset_reference");
    if (isRef && isRef->type == Value::Type::Bool && isRef->b) {
        const Value* type = node.get("type");
        const Value* name = node.get("name");
        if (type && type->string() && name && name->string()) {
            const std::string& t = *type->string();
            const std::string n = stripSharedMarker(*name->string());
            if (t == "Material") addUnique(out.materials, n);
            else if (t == "XModel") addUnique(out.models, n);
            else if (t == "ParticleSystemDef") addUnique(out.childEffects, n);
        }
    }

    const Value* member = node.get("active_member");
    if (member && member->string() && *member->string() == "sound") {
        const Value* value = node.get("value");
        const Value* alias = value ? value->get("string") : nullptr;
        if (alias && alias->string()) addUnique(out.sounds, *alias->string());
    }

    for (const auto& kv : node.obj) {
        if (kv.first == "$native") continue;  // hex bytes, never references
        collectReferences(kv.second, out, depth + 1);
    }
}

}  // namespace

bool parseVfxExport(const std::string& jsonText, VfxInfo& out, std::string* err) {
    std::string parseErr;
    const auto root = json::parse(jsonText, &parseErr);
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

    VfxInfo info;
    info.name = *nameStr->string();

    const Value* emitterBlock = fields->get("emitterDefs");
    const Value* emitters = emitterBlock ? emitterBlock->get("values") : nullptr;
    const size_t emitterRows = emitters && emitters->type == Value::Type::Array ? emitters->arr.size() : 0;
    info.emitterCount = static_cast<int>(numberOr(fields->get("numEmitters"), static_cast<double>(emitterRows)));

    double runSeconds = 0;
    if (emitters && emitters->type == Value::Type::Array) {
        for (const auto& emitter : emitters->arr) {
            if (emitter.type != Value::Type::Object) continue;
            const auto flags = static_cast<uint32_t>(numberOr(emitter.get("flags"), 0.0));
            if (flags & (kEmitterInfiniteParticleLife | kEmitterLoopInfiniteParticles)) info.looping = true;

            double lifeLo = 0, lifeHi = 0, delayLo = 0, delayHi = 0, particleLo = 0, particleHi = 0;
            readRange(emitter.get("emitterLife"), lifeLo, lifeHi);
            readRange(emitter.get("emitterDelay"), delayLo, delayHi);
            readRange(emitter.get("particleLife"), particleLo, particleHi);
            // An emitter with no emitter life stops when its last particle dies.
            const double span = lifeHi > 0 ? lifeHi : particleHi;
            const double end = delayHi + span;
            if (end > runSeconds) runSeconds = end;
        }
    }
    info.lifeSeconds = info.looping ? -1.0 : runSeconds;

    collectReferences(*fields, info, 0);

    out = std::move(info);
    return true;
}

bool readVfxExport(const std::string& path, VfxInfo& out, std::string* err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        setErr(err, "cannot open " + path);
        return false;
    }
    std::ostringstream text;
    text << in.rdbuf();
    return parseVfxExport(text.str(), out, err);
}

}  // namespace iw8r
