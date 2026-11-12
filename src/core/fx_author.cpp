#include "fx_author.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <sstream>

#include "json_lite.h"

namespace iw8r {
namespace {

std::string num(double v) {
    if (!std::isfinite(v)) v = 0;
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.9g", v);
    return buf;
}

std::string quote(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        o += c;
    }
    return o + "\"";
}

std::string arr(const double* v, size_t n) {
    std::string o = "[";
    for (size_t i = 0; i < n; ++i) o += (i ? "," : "") + num(v[i]);
    return o + "]";
}

uint32_t fnv(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) h = (h ^ c) * 16777619u;
    return h;
}

const char* kRange0 = "{\"base\":0,\"amplitude\":0}";

std::string range(double base, double amplitude) { return "{\"base\":" + num(base) + ",\"amplitude\":" + num(amplitude) + "}"; }
std::string irange(long long base, long long amplitude) {
    return "{\"base\":" + std::to_string(base) + ",\"amplitude\":" + std::to_string(amplitude) + "}";
}

bool readNum(const json::Value* v, double& out, double lo, double hi) {
    if (!v) return true;  // keep the default
    const auto n = v->number();
    if (!n || !std::isfinite(*n) || *n < lo || *n > hi) return false;
    out = *n;
    return true;
}

template <size_t N>
bool readArr(const json::Value* v, std::array<double, N>& out, double lo, double hi) {
    if (!v) return true;
    if (v->type != json::Value::Type::Array || v->arr.size() != N) return false;
    for (size_t i = 0; i < N; ++i) {
        const auto n = v->arr[i].number();
        if (!n || !std::isfinite(*n) || *n < lo || *n > hi) return false;
        out[i] = *n;
    }
    return true;
}

uint8_t byte(double v01) { return uint8_t(std::clamp(std::lround(v01 * 255.0), 0L, 255L)); }

}  // namespace

bool isValidAuthoredFxName(const std::string& name) {
    if (name.empty() || name.size() > 120 || name.front() == '/' || name.back() == '/' || name.find("..") != std::string::npos ||
        name.find("//") != std::string::npos)
        return false;
    for (char c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '/')) return false;
    return true;
}

bool parseAuthoredFx(const std::string& text, AuthoredFx& out, std::string* err) {
    const auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    std::string perr;
    const auto doc = json::parse(text, &perr);
    if (!doc || doc->type != json::Value::Type::Object) return fail("not a JSON object" + (perr.empty() ? "" : ": " + perr));
    AuthoredFx fx;
    if (const json::Value* n = doc->get("name"); n && n->string()) fx.name = *n->string();
    const json::Value* els = doc->get("elements");
    if (!els || els->type != json::Value::Type::Array || els->arr.empty() || els->arr.size() > 32)
        return fail("\"elements\" must be an array of 1 to 32 elements");
    for (const json::Value& ev : els->arr) {
        if (ev.type != json::Value::Type::Object) return fail("an element is not an object");
        AuthoredFxElement e;
        if (const json::Value* v = ev.get("name"); v && v->string()) e.name = *v->string();
        if (const json::Value* v = ev.get("image"); v && v->string()) e.image = *v->string();
        if (const json::Value* v = ev.get("kind"); v && v->string()) {
            if (*v->string() != "sprite" && *v->string() != "light" && *v->string() != "model")
                return fail("element '" + e.name + "': kind must be sprite, light or model");
            e.kind = *v->string();
        }
        if (const json::Value* v = ev.get("model"); v && v->string()) e.model = *v->string();
        if (e.kind == "model" && !isValidAuthoredFxName(e.model))
            return fail("element '" + e.name + "': a model element needs \"model\" (a custom model name)");
        if (const json::Value* v = ev.get("additive"); v && v->type == json::Value::Type::Bool) e.additive = v->b;
        if (const json::Value* v = ev.get("looping"); v && v->type == json::Value::Type::Bool) e.looping = v->b;
        double interval = e.intervalMs, count = e.count, life = e.lifeMs, lifeVar = e.lifeVarMs;
        if (!readNum(ev.get("interval_ms"), interval, 1, 600000) || !readNum(ev.get("count"), count, 1, 1000) ||
            !readNum(ev.get("life_ms"), life, 1, 600000) || !readNum(ev.get("life_var_ms"), lifeVar, 0, 600000) ||
            !readNum(ev.get("radius"), e.radius, 0, 100000) || !readNum(ev.get("gravity"), e.gravity, -100, 100) ||
            !readNum(ev.get("size_start"), e.sizeStart, 0, 100000) || !readNum(ev.get("size_end"), e.sizeEnd, 0, 100000) ||
            !readNum(ev.get("spin"), e.spin, -1000, 1000) || !readArr(ev.get("velocity_min"), e.velocityMin, -100000, 100000) ||
            !readArr(ev.get("velocity_max"), e.velocityMax, -100000, 100000) ||
            !readArr(ev.get("color_start"), e.colorStart, 0, 1) || !readArr(ev.get("color_end"), e.colorEnd, 0, 1))
            return fail("element '" + e.name + "': a value is missing its type or out of range");
        e.intervalMs = int(interval), e.count = int(count), e.lifeMs = int(life), e.lifeVarMs = int(lifeVar);
        if (const json::Value* ks = ev.get("keys")) {
            // [[t, size, r, g, b, a], ...]
            if (ks->type != json::Value::Type::Array || ks->arr.size() == 1 || ks->arr.size() > 16)
                return fail("element '" + e.name + "': \"keys\" must hold 2 to 16 keys [t, size, r, g, b, a]");
            for (const json::Value& kv : ks->arr) {
                std::array<double, 6> k{};
                if (!readArr(&kv, k, -100000, 100000) || k[0] < 0 || k[0] > 1 || k[1] < 0 || k[2] < 0 || k[2] > 1 || k[3] < 0 ||
                    k[3] > 1 || k[4] < 0 || k[4] > 1 || k[5] < 0 || k[5] > 1)
                    return fail("element '" + e.name + "': a key is not [t 0..1, size >= 0, r g b a 0..1]");
                e.keys.push_back({k[0], k[1], {k[2], k[3], k[4], k[5]}});
            }
            std::stable_sort(e.keys.begin(), e.keys.end(), [](const AuthoredFxKey& a, const AuthoredFxKey& b) { return a.t < b.t; });
        }
        fx.elements.push_back(e);
    }
    out = std::move(fx);
    return true;
}

std::string authoredFxJson(const AuthoredFx& fx) {
    std::ostringstream o;
    o << "{\n  \"name\": " << quote(fx.name) << ",\n  \"elements\": [";
    for (size_t i = 0; i < fx.elements.size(); ++i) {
        const auto& e = fx.elements[i];
        o << (i ? "," : "") << "\n    {\"name\": " << quote(e.name) << ", \"kind\": " << quote(e.kind) << (e.kind == "model" ? ", \"model\": " + quote(e.model) : std::string()) << ", \"image\": " << quote(e.image)
          << ", \"additive\": " << (e.additive ? "true" : "false") << ", \"looping\": " << (e.looping ? "true" : "false")
          << ",\n     \"interval_ms\": " << e.intervalMs << ", \"count\": " << e.count << ", \"life_ms\": " << e.lifeMs
          << ", \"life_var_ms\": " << e.lifeVarMs << ", \"radius\": " << num(e.radius) << ",\n     \"velocity_min\": "
          << arr(e.velocityMin.data(), 3) << ", \"velocity_max\": " << arr(e.velocityMax.data(), 3)
          << ", \"gravity\": " << num(e.gravity) << ",\n     \"size_start\": " << num(e.sizeStart) << ", \"size_end\": "
          << num(e.sizeEnd) << ", \"color_start\": " << arr(e.colorStart.data(), 4) << ", \"color_end\": "
          << arr(e.colorEnd.data(), 4) << ", \"spin\": " << num(e.spin);
        if (!e.keys.empty()) {
            o << ",\n     \"keys\": [";
            for (size_t k = 0; k < e.keys.size(); ++k) {
                const auto& key = e.keys[k];
                const double v[6] = {key.t, key.size, key.color[0], key.color[1], key.color[2], key.color[3]};
                o << (k ? ", " : "") << arr(v, 6);
            }
            o << "]";
        }
        o << "}";
    }
    o << "\n  ]\n}\n";
    return o.str();
}

std::string authoredFxMaterialName(const AuthoredFx& fx, size_t element) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "iw8rfx_%08x_%zu", unsigned(fnv(fx.name)), element);
    return buf;
}

std::string authoredFxImageName(const AuthoredFx& fx, size_t element) { return authoredFxMaterialName(fx, element) + "_c"; }

std::string authoredFxMaterialJson(const AuthoredFx& fx, size_t element) {
    const bool add = element < fx.elements.size() && fx.elements[element].additive;
    return "{\"_game\":\"iw3\",\"_type\":\"material\",\"_version\":1,\"techniqueSet\":" +
           quote(add ? "effect_zfeather_add" : "effect_zfeather") +
           ",\"textureAtlas\":{\"rows\":1,\"columns\":1},\"stateFlags\":16,\"cameraRegion\":\"emissive\"" +
           // One colour state: alpha blended (srcalpha / invsrcalpha, alpha test gt0) or additive scaled by alpha
           // (srcalpha / one), depth tested, no depth write (ReadFxMaterial's pinned mappings).
           ",\"stateBits\":[{\"colorWriteRgb\":true,\"colorWriteAlpha\":false,\"depthTest\":\"less_equal\",\"depthWrite\":false,"
           "\"blendOpRgb\":\"add\",\"alphaTest\":" + quote(add ? "disabled" : "gt0") + ",\"srcBlendRgb\":\"srcalpha\",\"dstBlendRgb\":" +
           quote(add ? "one" : "invsrcalpha") + ",\"cullFace\":\"none\"}]" +
           // The feathered families need featherParms (IW3's soft-particle depth fade) and every family a colorTint.
           ",\"constants\":[{\"name\":\"colorTint\",\"literal\":[1,1,1,1]},{\"name\":\"featherParms\",\"literal\":"
           "[0.0333333,30,30,0.025]}]" +
           ",\"textures\":[{\"name\":"
           "\"colorMap\",\"semantic\":\"colorMap\",\"image\":" +
           quote(authoredFxImageName(fx, element)) +
           ",\"samplerState\":{\"filter\":\"linear\",\"mipMap\":\"linear\",\"clampU\":true,\"clampV\":true,\"clampW\":true}}]}";
}

int authoredFxPeriodMs(const AuthoredFx& fx) {
    int period = 0;
    for (const auto& e : fx.elements)
        if (e.looping) period = std::max({period, 1000, std::min(e.intervalMs, 10000)});
    return period;
}

std::string authoredFxIw3Json(const AuthoredFx& fx) {
    // Every element is written one-shot (see authoredFxPeriodMs); the editor file keeps "looping".
    std::vector<size_t> order;
    for (size_t i = 0; i < fx.elements.size(); ++i) order.push_back(i);
    const size_t looping = 0;
    const int period = authoredFxPeriodMs(fx);
    int loopingLife = 0;
    std::string elems, deps;
    for (size_t k = 0; k < order.size(); ++k) {
        const size_t i = order[k];
        AuthoredFxElement e = fx.elements[i];
        const bool light = e.kind == "light";
        int delaySpread = 0;
        if (e.looping) {
            if (light) {
                // One light that lasts the whole period: the next play takes over as it ends.
                e.count = 1;
                e.lifeMs = period;
                e.lifeVarMs = 0;
            } else {
                const double spawns = double(period) / std::max(1, e.intervalMs);
                e.count = std::clamp(int(spawns * std::max(1, e.count) + 0.5), 1, 2000);
                delaySpread = period;
            }
            e.looping = false;
        }
        const bool model = e.kind == "model";
        const std::string mat = authoredFxMaterialName(fx, i);
        if (model)
            deps += std::string(deps.empty() ? "" : ",") + "{\"name\":" + quote(e.model) + ",\"type\":\"xmodel\"}";
        else if (!light)
            deps += std::string(deps.empty() ? "" : ",") + "{\"name\":" + quote(mat) + ",\"type\":\"material\"}";
        // Run relative to the world, spawned relative to the effect (0x2), sphere offset (0x10), world velocity graph
        // (0x2000000), gravity (0x4000000): IW3 FX_ELEM_* bits.
        int flags = 0x2 | 0x2000000;
        if (e.radius > 0) flags |= 0x10;
        if (e.gravity != 0) flags |= 0x4000000;
        // The spawn union: looping {interval, count} and one-shot {count base, amplitude} share two ints.
        const int raw0 = e.looping ? e.intervalMs : e.count, raw1 = e.looping ? e.count : 0;
        const std::string spawn = "{\"looping\":{\"count\":" + std::to_string(raw1) + ",\"interval_msec\":" + std::to_string(raw0) +
                                  "},\"one_shot\":{\"count\":" + irange(raw0, raw1) + "},\"raw\":[" + std::to_string(raw0) + "," +
                                  std::to_string(raw1) + "]}";
        // Velocity: two samples (start, end of life), world frame, units per millisecond; total delta = travelled so far.
        std::string vel;
        for (int s = 0; s < 2; ++s) {
            const double tMs = s == 0 ? 0.0 : double(e.lifeMs);
            double vb[3], va[3], db[3], da[3], z[3] = {0, 0, 0};
            for (int a = 0; a < 3; ++a) {
                vb[a] = e.velocityMin[size_t(a)] / 1000.0;
                va[a] = (e.velocityMax[size_t(a)] - e.velocityMin[size_t(a)]) / 1000.0;
                db[a] = vb[a] * tMs;
                da[a] = va[a] * tMs;
            }
            vel += std::string(s ? "," : "") + "{\"local\":{\"velocity\":{\"base\":" + arr(z, 3) + ",\"amplitude\":" + arr(z, 3) +
                   "},\"total_delta\":{\"base\":" + arr(z, 3) + ",\"amplitude\":" + arr(z, 3) + "}},\"world\":{\"velocity\":{\"base\":" +
                   arr(vb, 3) + ",\"amplitude\":" + arr(va, 3) + "},\"total_delta\":{\"base\":" + arr(db, 3) + ",\"amplitude\":" +
                   arr(da, 3) + "}}}";
        }
        // Visual states, evenly spaced over the life (start and end, or 9 samples of a key curve): colour bytes in IW3
        // order B, G, R, A; the amplitude carries the same colour (no random tint); size = width, height.
        const int visIntervals = e.keys.empty() ? 1 : 8;
        std::string vis;
        for (int s = 0; s <= visIntervals; ++s) {
            const double f = double(s) / visIntervals;
            const AuthoredFxKey key = sampleAuthoredFxKey(e, f);
            const auto& c = key.color;
            const double sz = key.size;
            const std::string col = "[" + std::to_string(byte(c[2])) + "," + std::to_string(byte(c[1])) + "," +
                                    std::to_string(byte(c[0])) + "," + std::to_string(byte(c[3])) + "]";
            const double rot = e.spin / 1000.0;  // radians per millisecond
            const double total = e.spin * f * e.lifeMs / 1000.0;
            vis += std::string(s ? "," : "") + "{\"base\":{\"color\":" + col + ",\"rotation_delta\":" + num(rot) +
                   ",\"rotation_total\":" + num(total) + ",\"scale\":" + num(model ? sz : 0.0) + ",\"size\":[" +
                   num(model ? 0.0 : sz) + "," + num(model ? 0.0 : sz) + "]},\"amplitude\":{\"color\":" + col + ",\"rotation_delta\":0,\"rotation_total\":0,\"scale\":0,\"size\":[0,0]}}";
        }
        elems += std::string(k ? "," : "") + "{\"angular_velocity\":[" + kRange0 + "," + kRange0 + "," + kRange0 +
                 "],\"atlas\":{\"behavior\":0,\"col_index_bits\":0,\"entry_count\":0,\"fps\":0,\"index\":0,\"loop_count\":0,"
                 "\"row_index_bits\":0},\"collision_maxs\":[1,1,1],\"collision_mins\":[-1,-1,-1],\"effect_emitted\":null,"
                 "\"effect_on_death\":null,\"effect_on_impact\":null,\"elem_type\":" + std::string(light ? "6" : model ? "5" : "0") +
                 ",\"elem_type_name\":" + quote(light ? "omni_light" : model ? "model" : "sprite_billboard") + ","
                 "\"emit_dist\":" + kRange0 + ",\"emit_dist_variance\":" + kRange0 + ",\"fade_in_range\":" + kRange0 +
                 ",\"fade_out_range\":" + kRange0 + ",\"flags\":" + std::to_string(flags) + ",\"gravity\":" + range(e.gravity, 0) +
                 ",\"initial_rotation\":" + range(0, 6.28318530718) + ",\"life_span_msec\":" + irange(e.lifeMs, e.lifeVarMs) +
                 ",\"lighting_frac\":0,\"reflection_factor\":" + kRange0 + ",\"sort_order\":5,\"spawn\":" + spawn +
                 ",\"spawn_angles\":[" + kRange0 + "," + kRange0 + "," + kRange0 + "],\"spawn_delay_msec\":" + irange(0, delaySpread) +
                 ",\"spawn_frustum_cull_radius\":0,\"spawn_offset_height\":" + kRange0 + ",\"spawn_offset_radius\":" +
                 range(e.radius, 0) + ",\"spawn_origin\":[" + kRange0 + "," + kRange0 + "," + kRange0 + "],\"spawn_range\":" +
                 kRange0 + ",\"trail\":null,\"use_item_clip\":0,\"vel_interval_count\":1,\"velocity_samples\":[" + vel +
                 "],\"vis_state_interval_count\":" + std::to_string(visIntervals) + ",\"visual_count\":" + std::string(light ? "0" : "1") + ",\"visual_samples\":[" + vis + "],\"visuals\":" +
                 (light   ? std::string("[],\"visuals_present\":false}")
                  : model ? "[{\"type\":\"xmodel\",\"name\":" + quote(e.model) + "}],\"visuals_present\":true}"
                          : "[{\"type\":\"material\",\"name\":" + quote(mat) + "}],\"visuals_present\":true}");
    }
    const size_t n = fx.elements.size();
    return "{\"schema\":1,\"asset_type\":\"iw3_fx\",\"name\":" + quote(fx.name) +
           ",\"layout\":{\"FxEffectDef\":32,\"FxElemDef\":252,\"FxTrailDef\":28},\"flags\":0,\"total_size\":" +
           std::to_string(32 + 252 * n) + ",\"msec_looping_life\":" + std::to_string(loopingLife) +
           ",\"element_counts\":{\"looping\":" + std::to_string(looping) + ",\"one_shot\":" + std::to_string(n - looping) +
           ",\"emission\":0,\"total\":" + std::to_string(n) + "},\"dependencies\":[" + deps + "],\"elements\":[" + elems + "]}";
}

AuthoredFxKey sampleAuthoredFxKey(const AuthoredFxElement& e, double f) {
    f = std::clamp(f, 0.0, 1.0);
    if (e.keys.empty()) {
        AuthoredFxKey k;
        k.t = f;
        k.size = e.sizeStart + (e.sizeEnd - e.sizeStart) * f;
        for (size_t c = 0; c < 4; ++c) k.color[c] = e.colorStart[c] + (e.colorEnd[c] - e.colorStart[c]) * f;
        return k;
    }
    if (f <= e.keys.front().t) return e.keys.front();
    if (f >= e.keys.back().t) return e.keys.back();
    for (size_t i = 0; i + 1 < e.keys.size(); ++i) {
        const AuthoredFxKey& a = e.keys[i];
        const AuthoredFxKey& b = e.keys[i + 1];
        if (f > b.t) continue;
        const double u = b.t > a.t ? (f - a.t) / (b.t - a.t) : 1.0;
        AuthoredFxKey k;
        k.t = f;
        k.size = a.size + (b.size - a.size) * u;
        for (size_t c = 0; c < 4; ++c) k.color[c] = a.color[c] + (b.color[c] - a.color[c]) * u;
        return k;
    }
    return e.keys.back();
}

std::vector<FxPreviewParticle> simulateAuthoredFx(const AuthoredFx& fx, double t, size_t maxParticles) {
    std::vector<FxPreviewParticle> out;
    const double g = 800.0;  // units per second squared (preview)
    for (size_t ei = 0; ei < fx.elements.size(); ++ei) {
        const AuthoredFxElement& e = fx.elements[ei];
        // Spawn times up to t.
        std::vector<double> spawns;
        if (e.looping) {
            const double step = std::max(1, e.intervalMs) / 1000.0;
            const double oldest = t - (e.lifeMs + e.lifeVarMs) / 1000.0;
            for (long k = std::max(0L, long(std::floor(oldest / step))); k * step <= t; ++k)
                for (int c = 0; c < e.count; ++c) spawns.push_back(k * step);
        } else {
            for (int c = 0; c < e.count; ++c) spawns.push_back(0);
        }
        for (size_t si = 0; si < spawns.size() && out.size() < maxParticles; ++si) {
            std::mt19937 rng(uint32_t(ei * 7919 + uint32_t(std::lround(spawns[si] * 1000)) * 131 + si % 997));
            std::uniform_real_distribution<double> u(0, 1);
            const double life = (e.lifeMs + e.lifeVarMs * u(rng)) / 1000.0;
            const double age = t - spawns[si];
            if (age < 0 || age > life) continue;
            const double f = life > 0 ? age / life : 1;
            FxPreviewParticle p{};
            double off[3] = {0, 0, 0};
            if (e.radius > 0) {
                double d[3], len = 0;
                do {
                    for (double& v : d) v = u(rng) * 2 - 1;
                    len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                } while (len > 1 || len < 1e-6);
                for (int a = 0; a < 3; ++a) off[a] = d[a] * e.radius;
            }
            for (int a = 0; a < 3; ++a) {
                const double v = e.velocityMin[size_t(a)] + (e.velocityMax[size_t(a)] - e.velocityMin[size_t(a)]) * u(rng);
                p.pos[a] = off[a] + v * age;
            }
            p.pos[2] -= 0.5 * g * e.gravity * age * age;
            const AuthoredFxKey key = sampleAuthoredFxKey(e, f);
            p.size = key.size;
            for (int c = 0; c < 4; ++c) p.color[c] = key.color[size_t(c)];
            p.element = ei;
            out.push_back(p);
        }
    }
    return out;
}

}  // namespace iw8r
