#include "iw8_mapents.h"

#include <cmath>
#include <cstdint>
#include <limits>

#include "json_lite.h"
#include "map_text.h"

namespace iw8r {

namespace {

int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool hexDecode(const std::string& hex, std::string& out) {
    if (hex.size() % 2) return false;
    out.resize(hex.size() / 2);
    for (size_t i = 0; i < out.size(); ++i) {
        const int hi = hexNibble(hex[2 * i]), lo = hexNibble(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = char((hi << 4) | lo);
    }
    return true;
}

// Reads {"value": N} or a bare number.
std::optional<double> scalar(const json::Value* v) {
    if (!v) return std::nullopt;
    if (auto n = v->number()) return n;
    if (const json::Value* inner = v->get("value")) return inner->number();
    return std::nullopt;
}

// Converts a JSON number to an index or count. Rejects NaN and infinity, negatives, fractions and anything above
// `max`, so the cast never has undefined behaviour. Callers treat a rejected value as a missing field.
std::optional<uint64_t> toIndex(double v, uint64_t max) {
    constexpr double kTwo64 = 18446744073709551616.0;  // 2^64: the cast below is defined only under it
    if (!std::isfinite(v) || v < 0 || v != std::floor(v) || v >= kTwo64) return std::nullopt;
    if (uint64_t(v) > max) return std::nullopt;
    return uint64_t(v);
}

// Reads a vec3 dumped as {"v": [x, y, z], ...} or [x, y, z].
bool vec3(const json::Value* v, Vec3& out) {
    if (!v) return false;
    const json::Value* arr = v->type == json::Value::Type::Array ? v : v->get("v");
    if (!arr || arr->arr.size() < 3) return false;
    for (int a = 0; a < 3; ++a) {
        auto n = arr->arr[a].number();
        if (!n || !std::isfinite(*n)) return false;
        out[a] = *n;
    }
    return true;
}

std::string scriptString(const std::vector<std::string>& table, const json::Value* v) {
    auto n = scalar(v);
    if (!n) return {};
    const auto idx = toIndex(*n, std::numeric_limits<size_t>::max());
    if (!idx) return {};  // not a valid index: same as a missing field
    if (*idx < table.size()) return table[size_t(*idx)];
    return "#" + std::to_string(*idx);
}

}  // namespace

bool loadActsMapEnts(const std::string& assetJson, const std::string& scriptStringsJson, Iw8MapEntsDump& out, std::string* err) {
    auto fail = [&](const std::string& m) {
        if (err) *err = assetJson + ": " + m;
        return false;
    };
    std::string text;
    if (!readFile(assetJson, text)) return fail("cannot read file");
    std::string perr;
    auto root = json::parse(text, &perr);
    if (!root) return fail("invalid JSON (" + perr + ")");
    const json::Value* fmt = root->get("format");
    const json::Value* profile = root->get("profile");
    const json::Value* pool = root->get("pool");
    if (!fmt || !fmt->string() || *fmt->string() != "mw19-asset-json") return fail("not an ACTS mw19-asset-json dump");
    if (!profile || !profile->string() || *profile->string() != "replay-1.20")
        return fail("profile is not replay-1.20 (this build of the editor targets MW2019 Replay 1.20.4.7623265 only)");
    if (!pool || !pool->string() || *pool->string() != "map_ents") return fail("asset pool is not map_ents");
    out = {};
    out.profile = *profile->string();
    const json::Value* fields = root->get("asset") ? root->get("asset")->get("fields") : nullptr;
    if (!fields) return fail("missing asset.fields");
    if (const json::Value* n = fields->get("name")) {
        if (n->string()) out.assetName = *n->string();
        else if (const json::Value* s = n->get("string"); s && s->string()) out.assetName = *s->string();
    }

    const json::Value* es = fields->get("entityString");
    const json::Value* bytes = es ? es->get("bytes") : nullptr;
    if (!bytes || !bytes->string()) return fail("missing entityString.bytes");
    if (!hexDecode(*bytes->string(), out.entityString)) return fail("entityString.bytes is not hex");
    if (auto n = scalar(fields->get("numEntityChars")))
        if (auto c = toIndex(*n, std::numeric_limits<uint64_t>::max())) out.numEntityChars = *c;
    if (out.numEntityChars != out.entityString.size())
        return fail("numEntityChars (" + std::to_string(out.numEntityChars) + ") does not match entityString size (" +
                    std::to_string(out.entityString.size()) + ")");

    std::vector<std::string> strings;
    if (!scriptStringsJson.empty()) {
        std::string st;
        if (!readFile(scriptStringsJson, st)) return fail("cannot read " + scriptStringsJson);
        auto sj = json::parse(st, &perr);
        if (!sj || sj->type != json::Value::Type::Array) return fail("script strings file is not a JSON array");
        for (const auto& v : sj->arr) strings.push_back(v.string() ? *v.string() : std::string());
    }

    if (const json::Value* sl = fields->get("spawnList")) {
        const json::Value* spawns = sl->get("spawns");
        const json::Value* values = spawns ? spawns->get("values") : nullptr;
        if (values && values->type == json::Value::Type::Array) {
            for (const auto& r : values->arr) {
                Iw8SpawnRecord s;
                if (auto n = scalar(r.get("index")))
                    if (auto idx = toIndex(*n, std::numeric_limits<uint32_t>::max())) s.index = uint32_t(*idx);
                s.name = scriptString(strings, r.get("name"));
                s.target = scriptString(strings, r.get("target"));
                s.noteworthy = scriptString(strings, r.get("script_noteworthy"));
                vec3(r.get("origin"), s.origin);
                vec3(r.get("angles"), s.angles);
                out.spawns.push_back(std::move(s));
            }
        }
        if (auto n = scalar(sl->get("spawnsCount")))
            if (auto c = toIndex(*n, std::numeric_limits<size_t>::max()); c && *c != out.spawns.size())
                return fail("spawnsCount does not match decoded spawn records");
    }
    auto count = [&](const char* section, const json::Value* v) {
        if (auto n = scalar(v))
            if (auto c = toIndex(*n, std::numeric_limits<uint64_t>::max())) out.sections[section] = *c;
    };
    if (const json::Value* t = fields->get("trigger")) count("trigger.models", t->get("count"));
    if (const json::Value* ct = fields->get("clientTrigger"))
        if (const json::Value* t = ct->get("trigger")) count("clientTrigger.models", t->get("count"));
    if (const json::Value* sp = fields->get("splineList")) count("splineList.splines", sp->get("splineCount"));
    count("spawnList.spawns", fields->get("spawnList") ? fields->get("spawnList")->get("spawnsCount") : nullptr);
    count("cmodels", fields->get("numSubModels"));
    count("clientModels", fields->get("numClientModels"));
    count("dynEnts", fields->get("dynEntCountTotal"));
    count("createFxEffects", fields->get("createFxEffectTotal"));
    count("audioPASpeakers", fields->get("audioPASpeakerCount"));
    count("audioPropNodes", fields->get("numAudioPropNodes"));
    count("collmaps", fields->get("numCollmaps"));
    return true;
}

}  // namespace iw8r
