// IW8 Radiant - procedural scatter rules (roadmap 8.12). See scatter_rules.h.
#include "scatter_rules.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <unordered_map>

#include "brush_ops.h"
#include "compile_iw8.h"
#include "json_lite.h"
#include "patch_tess.h"

namespace iw8r {

namespace {

constexpr double kRad = 3.14159265358979323846 / 180.0;
constexpr double kDensityArea = 256.0 * 256.0;  // density is quoted per this much surface
constexpr int kPatchSubdiv = 4;                 // patch segments per sub-patch for collision-free scatter sampling
constexpr const char* kFormat = "iw8r-scatter-rules";
constexpr int kFormatVersion = 1;

// splitmix64. Our own generator: std distributions differ between standard libraries, and presets must
// reproduce the same props on every build.
class Rng {
public:
    explicit Rng(uint64_t seed) : s_(seed) {}
    uint64_t next() {
        s_ += 0x9E3779B97F4A7C15ull;
        uint64_t z = s_;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    // [0, 1)
    double uniform() { return double(next() >> 11) * (1.0 / 9007199254740992.0); }

private:
    uint64_t s_;
};

std::string lowerCopy(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Case-insensitive substring test. Empty needles are ignored by the callers.
bool containsCI(const std::string& hay, const std::string& needle) {
    return lowerCopy(hay).find(lowerCopy(needle)) != std::string::npos;
}

bool materialAllowed(const std::string& material, const ScatterRule& r) {
    for (const std::string& d : r.materialsDeny)
        if (!d.empty() && containsCI(material, d)) return false;
    bool anyAllow = false, hit = false;
    for (const std::string& a : r.materialsAllow) {
        if (a.empty()) continue;
        anyAllow = true;
        if (containsCI(material, a)) hit = true;
    }
    return !anyAllow || hit;
}

// Same convention as generators.cpp anglesFromNormal: {pitch, yaw, roll}, Radiant degrees.
Vec3 anglesFor(const Vec3& n, double yawDeg, bool alignToNormal) {
    if (!alignToNormal) return {0, yawDeg, 0};
    const double roll = std::asin(std::clamp(-n.y, -1.0, 1.0)) / kRad;
    const double pitch = std::atan2(n.x, n.z) / kRad;
    return {pitch, yawDeg, roll};
}

// Uniform grid over the placed origins with cell size == minSpacing. Any point closer than minSpacing lies in
// the 27 cells around a query point, so checking those cells is exact (hash collisions only add candidates).
class SpacingGrid {
public:
    explicit SpacingGrid(double cell) : cell_(cell) {}

    bool clearOf(const Vec3& p, double minDist) const {
        const int64_t cx = coord(p.x), cy = coord(p.y), cz = coord(p.z);
        const double r2 = minDist * minDist;
        for (int64_t dx = -1; dx <= 1; ++dx)
            for (int64_t dy = -1; dy <= 1; ++dy)
                for (int64_t dz = -1; dz <= 1; ++dz) {
                    auto it = buckets_.find(key(cx + dx, cy + dy, cz + dz));
                    if (it == buckets_.end()) continue;
                    for (const Vec3& q : it->second) {
                        const Vec3 d = p - q;
                        if (dot(d, d) < r2) return false;
                    }
                }
        return true;
    }

    void add(const Vec3& p) { buckets_[key(coord(p.x), coord(p.y), coord(p.z))].push_back(p); }

private:
    int64_t coord(double v) const { return int64_t(std::floor(v / cell_)); }
    static uint64_t key(int64_t x, int64_t y, int64_t z) {
        return uint64_t(x) * 0x9E3779B1ull ^ uint64_t(y) * 0x85EBCA77ull ^ uint64_t(z) * 0xC2B2AE3Dull;
    }
    double cell_;
    std::unordered_map<uint64_t, std::vector<Vec3>> buckets_;
};

// Index of the first cumulative weight >= r (r in (0, total]). Zero-weight entries are never returned.
size_t pickCumulative(const std::vector<double>& cum, double r) {
    auto it = std::lower_bound(cum.begin(), cum.end(), r);
    if (it == cum.end()) return cum.size() - 1;
    return size_t(it - cum.begin());
}

}  // namespace

std::vector<SurfaceTri> collectSurfaces(const MapFile& map, const AABB* region) {
    std::vector<SurfaceTri> out;
    const bool clip = region && region->valid();
    auto push = [&](const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& n, const std::string& mat, Id entity, Id item) {
        if (clip) {
            AABB bb;
            bb.add(a);
            bb.add(b);
            bb.add(c);
            if (bb.maxs.x < region->mins.x || bb.mins.x > region->maxs.x || bb.maxs.y < region->mins.y ||
                bb.mins.y > region->maxs.y || bb.maxs.z < region->mins.z || bb.mins.z > region->maxs.z)
                return;
        }
        SurfaceTri t;
        t.a = a;
        t.b = b;
        t.c = c;
        t.n = n;
        t.material = mat;
        t.entity = entity;
        t.item = item;
        out.push_back(std::move(t));
    };

    for (const Entity& e : map.entities) {
        const std::string cls = e.classname();
        if (cls != "worldspawn" && cls != "func_group") continue;
        for (const EntityItem& it : e.items) {
            const Primitive* p = it.prim();
            if (!p) continue;
            if (p->kind == PrimKind::Brush) {
                const std::vector<Winding> wins = faceWindingsOf(*p);
                for (size_t fi = 0; fi < p->faces.size() && fi < wins.size(); ++fi) {
                    const std::string mat = p->faces[fi].material();
                    if (isIw8ToolMaterial(mat)) continue;
                    const std::optional<Plane> pl = facePlane(*p, fi);
                    const Winding& w = wins[fi];
                    if (!pl || w.size() < 3) continue;
                    const Vec3 n = normalize(pl->n);
                    // Convex polygon: fan from vertex 0.
                    for (size_t k = 1; k + 1 < w.size(); ++k) push(w[0], w[k], w[k + 1], n, mat, e.id, it.id);
                }
            } else if (p->kind == PrimKind::Patch) {
                if (isIw8ToolMaterial(p->patchMaterial)) continue;
                const PatchMesh m = tessellatePrimitive(*p, kPatchSubdiv);
                for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
                    const uint32_t i0 = m.indices[t], i1 = m.indices[t + 1], i2 = m.indices[t + 2];
                    const Vec3& a = m.points[i0];
                    const Vec3& b = m.points[i1];
                    const Vec3& c = m.points[i2];
                    const Vec3 raw = cross(b - a, c - a);
                    const double len = length(raw);
                    if (!(len > 1e-12)) continue;
                    Vec3 n = raw * (1.0 / len);
                    // Orient toward the averaged vertex normals, so a patch faces the same way as its surface.
                    const Vec3 avg = m.normals[i0] + m.normals[i1] + m.normals[i2];
                    if (dot(n, avg) < 0) n = -n;
                    push(a, b, c, n, p->patchMaterial, e.id, it.id);
                }
            }
        }
    }
    return out;
}

std::vector<ScatterPlacement> scatter(const std::vector<SurfaceTri>& tris, const ScatterRule& rule, size_t maxCount) {
    std::vector<ScatterPlacement> out;
    if (maxCount == 0 || rule.models.empty()) return out;
    if (!(rule.density > 0) || !std::isfinite(rule.density)) return out;
    if (!(rule.minSpacing >= 0) || !std::isfinite(rule.minSpacing)) return out;

    // Model weights, cumulative. A size mismatch falls back to uniform choice.
    const bool useWeights = rule.weights.size() == rule.models.size();
    std::vector<double> modelCum;
    double modelTotal = 0;
    for (size_t i = 0; i < rule.models.size(); ++i) {
        double w = useWeights ? rule.weights[i] : 1.0;
        if (!(w > 0) || !std::isfinite(w)) w = 0;
        modelTotal += w;
        modelCum.push_back(modelTotal);
    }
    if (!(modelTotal > 0)) return out;

    // Eligible triangles: slope and material filters, cumulative area for area-weighted sampling.
    std::vector<const SurfaceTri*> cand;
    std::vector<double> areaCum;
    double total = 0;
    for (const SurfaceTri& t : tris) {
        if (!materialAllowed(t.material, rule)) continue;
        if (!(length(t.n) > 0)) continue;
        const Vec3 n = normalize(t.n);
        const double slope = std::acos(std::clamp(n.z, -1.0, 1.0)) / kRad;  // angle from +Z
        if (slope < rule.minSlopeDeg || slope > rule.maxSlopeDeg) continue;
        const double area = 0.5 * length(cross(t.b - t.a, t.c - t.a));
        if (!(area > 0) || !std::isfinite(area)) continue;
        total += area;
        cand.push_back(&t);
        areaCum.push_back(total);
    }
    if (cand.empty()) return out;

    Rng rng(rule.seed);
    // Expected count is density * area / 256^2. Stochastic rounding keeps small areas unbiased.
    const double want = rule.density * total / kDensityArea;
    size_t target;
    if (want >= double(maxCount)) {
        target = maxCount;
    } else {
        const double whole = std::floor(want);
        target = size_t(whole) + (rng.uniform() < want - whole ? 1 : 0);
        target = std::min(target, maxCount);
    }
    if (target == 0) return out;

    const double sink = (std::isfinite(rule.sinkDepth) && rule.sinkDepth > 0) ? rule.sinkDepth : 0.0;
    double lo = rule.scaleMin, hi = rule.scaleMax;
    if (!(lo > 0) || !std::isfinite(lo)) lo = 1.0;
    if (!(hi >= lo) || !std::isfinite(hi)) hi = lo;

    const double spacing = rule.minSpacing;
    SpacingGrid grid(spacing > 0 ? spacing : 1.0);
    // Dart throwing with a bounded budget: a saturated surface simply yields fewer points.
    const size_t maxAttempts = target * 64 + 1000;
    for (size_t attempt = 0; attempt < maxAttempts && out.size() < target; ++attempt) {
        const SurfaceTri& t = *cand[pickCumulative(areaCum, (1.0 - rng.uniform()) * total)];
        double u = rng.uniform(), v = rng.uniform();
        if (u + v > 1.0) {  // fold the unit square onto the triangle
            u = 1.0 - u;
            v = 1.0 - v;
        }
        const Vec3 p = t.a + (t.b - t.a) * u + (t.c - t.a) * v;
        if (spacing > 0) {
            if (!grid.clearOf(p, spacing)) continue;
            grid.add(p);
        }
        const size_t mi = pickCumulative(modelCum, (1.0 - rng.uniform()) * modelTotal);
        const double scale = lo + (hi - lo) * rng.uniform();
        const double yaw = 360.0 * rng.uniform();
        const Vec3 n = normalize(t.n);

        ScatterPlacement pl;
        pl.model = rule.models[mi];
        pl.origin = p - n * sink;
        pl.angles = anglesFor(n, yaw, rule.alignToNormal);
        pl.scale = scale;
        out.push_back(std::move(pl));
    }
    return out;
}

size_t placeScatter(Document& doc, const std::vector<ScatterPlacement>& placements) {
    if (placements.empty()) return 0;
    auto vec = [](const Vec3& v) { return formatNumber(v.x) + " " + formatNumber(v.y) + " " + formatNumber(v.z); };
    std::vector<Id> ids;
    ids.reserve(placements.size());
    // Grouped so the whole scatter is one undo step.
    doc.beginGroup("Scatter Rules");
    for (const ScatterPlacement& pl : placements) {
        std::vector<std::pair<std::string, std::string>> kv = {
            {"classname", "misc_model"}, {"model", pl.model}, {"origin", vec(pl.origin)}, {"angles", vec(pl.angles)}};
        if (std::fabs(pl.scale - 1.0) > 1e-6) kv.push_back({"modelscale", formatNumber(pl.scale)});
        ids.push_back(doc.addEntity(kv, {}, "Scatter Rules"));
    }
    doc.endGroup();
    // Selection is not part of the undo step; select every new prop, as the single-model tools do for one.
    doc.deselectAll();
    for (Id id : ids) doc.select({id, kNoId}, true);
    return ids.size();
}

namespace {

std::string jsonString(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (u < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\u%04x", unsigned(u));
            out += buf;
        } else out += c;
    }
    return out + "\"";
}

// %.17g round-trips every double exactly.
std::string jsonNumber(double v) {
    if (!std::isfinite(v)) v = 0;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.17g", v);
    return buf;
}

std::string jsonStringArray(const std::vector<std::string>& v) {
    std::string out = "[";
    for (size_t i = 0; i < v.size(); ++i) out += (i ? "," : "") + jsonString(v[i]);
    return out + "]";
}

}  // namespace

std::string scatterRulesToJson(const std::vector<ScatterRule>& rules) {
    std::string out = "{\"format\":\"" + std::string(kFormat) + "\",\"version\":" + std::to_string(kFormatVersion) + ",\"rules\":[";
    for (size_t i = 0; i < rules.size(); ++i) {
        const ScatterRule& r = rules[i];
        std::string w = "[";
        for (size_t k = 0; k < r.weights.size(); ++k) w += (k ? "," : "") + jsonNumber(r.weights[k]);
        w += "]";
        out += i ? ",\n " : "\n ";
        out += "{\"models\":" + jsonStringArray(r.models) + ",\"weights\":" + w +
               ",\"density\":" + jsonNumber(r.density) + ",\"minSpacing\":" + jsonNumber(r.minSpacing) +
               ",\"minSlopeDeg\":" + jsonNumber(r.minSlopeDeg) + ",\"maxSlopeDeg\":" + jsonNumber(r.maxSlopeDeg) +
               ",\"materialsAllow\":" + jsonStringArray(r.materialsAllow) +
               ",\"materialsDeny\":" + jsonStringArray(r.materialsDeny) +
               ",\"scaleMin\":" + jsonNumber(r.scaleMin) + ",\"scaleMax\":" + jsonNumber(r.scaleMax) +
               ",\"alignToNormal\":" + (r.alignToNormal ? "true" : "false") +
               ",\"sinkDepth\":" + jsonNumber(r.sinkDepth) + ",\"seed\":" + std::to_string(r.seed) + "}";
    }
    out += rules.empty() ? "]}" : "\n]}";
    return out;
}

namespace {

// Reads optional members of one JSON object. Absent members keep the rule's default.
struct RuleReader {
    std::string err;

    bool fail(const std::string& m) {
        err = m;
        return false;
    }
    bool number(const json::Value& o, const char* key, double& dst) {
        const json::Value* v = o.get(key);
        if (!v) return true;
        const auto n = v->number();
        if (!n) return fail(std::string("member '") + key + "' must be a number");
        dst = *n;
        return true;
    }
    bool boolean(const json::Value& o, const char* key, bool& dst) {
        const json::Value* v = o.get(key);
        if (!v) return true;
        if (v->type != json::Value::Type::Bool) return fail(std::string("member '") + key + "' must be a boolean");
        dst = v->b;
        return true;
    }
    bool strings(const json::Value& o, const char* key, std::vector<std::string>& dst) {
        const json::Value* v = o.get(key);
        if (!v) return true;
        if (v->type != json::Value::Type::Array) return fail(std::string("member '") + key + "' must be an array");
        dst.clear();
        for (const json::Value& e : v->arr) {
            const std::string* s = e.string();
            if (!s) return fail(std::string("member '") + key + "' must contain only strings");
            dst.push_back(*s);
        }
        return true;
    }
    bool numbers(const json::Value& o, const char* key, std::vector<double>& dst) {
        const json::Value* v = o.get(key);
        if (!v) return true;
        if (v->type != json::Value::Type::Array) return fail(std::string("member '") + key + "' must be an array");
        dst.clear();
        for (const json::Value& e : v->arr) {
            const auto n = e.number();
            if (!n) return fail(std::string("member '") + key + "' must contain only numbers");
            dst.push_back(*n);
        }
        return true;
    }
    bool seed(const json::Value& o, unsigned& dst) {
        const json::Value* v = o.get("seed");
        if (!v) return true;
        const auto n = v->number();
        if (!n || *n < 0 || *n > 4294967295.0 || std::floor(*n) != *n) return fail("member 'seed' must be an integer 0..4294967295");
        dst = unsigned(*n);
        return true;
    }
};

}  // namespace

bool scatterRulesFromJson(const std::string& text, std::vector<ScatterRule>& out, std::string* err) {
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    std::string perr;
    const std::optional<json::Value> root = json::parse(text, &perr);
    if (!root) return fail("invalid JSON: " + perr);
    if (root->type != json::Value::Type::Object) return fail("top level must be an object");
    const json::Value* fmt = root->get("format");
    if (!fmt || !fmt->string() || *fmt->string() != kFormat) return fail("not an iw8r scatter rule file");
    const json::Value* rules = root->get("rules");
    if (!rules || rules->type != json::Value::Type::Array) return fail("missing 'rules' array");

    std::vector<ScatterRule> result;
    RuleReader rd;
    for (const json::Value& rv : rules->arr) {
        if (rv.type != json::Value::Type::Object) return fail("each rule must be an object");
        ScatterRule r;
        const bool ok = rd.strings(rv, "models", r.models) && rd.numbers(rv, "weights", r.weights) &&
                        rd.number(rv, "density", r.density) && rd.number(rv, "minSpacing", r.minSpacing) &&
                        rd.number(rv, "minSlopeDeg", r.minSlopeDeg) && rd.number(rv, "maxSlopeDeg", r.maxSlopeDeg) &&
                        rd.strings(rv, "materialsAllow", r.materialsAllow) &&
                        rd.strings(rv, "materialsDeny", r.materialsDeny) && rd.number(rv, "scaleMin", r.scaleMin) &&
                        rd.number(rv, "scaleMax", r.scaleMax) && rd.boolean(rv, "alignToNormal", r.alignToNormal) &&
                        rd.number(rv, "sinkDepth", r.sinkDepth) && rd.seed(rv, r.seed);
        if (!ok) return fail(rd.err);
        result.push_back(std::move(r));
    }
    out = std::move(result);
    return true;
}

}  // namespace iw8r
