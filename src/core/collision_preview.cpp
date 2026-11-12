#include "collision_preview.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>

#include "json_lite.h"

namespace iw8r {

namespace {
bool vec3(const json::Value* v, Vec3& out) {
    if (!v || v->type != json::Value::Type::Array || v->arr.size() < 3) return false;
    for (int i = 0; i < 3; ++i) {
        const auto n = v->arr[size_t(i)].number();
        if (!n) return false;
        out[i] = *n;
    }
    return true;
}
}  // namespace

bool readCollisionHulls(const std::string& text, std::vector<CollisionHullEdges>& out, std::string* err) {
    out.clear();
    const auto root = json::parse(text, err);
    if (!root) return false;
    const json::Value* brushes = root->get("brushes");
    if (!brushes || brushes->type != json::Value::Type::Array) {
        if (err) *err = "no brushes array";
        return false;
    }
    for (const json::Value& b : brushes->arr) {
        Vec3 mins, maxs;
        if (!vec3(b.get("mins"), mins) || !vec3(b.get("maxs"), maxs)) continue;
        std::vector<Plane> planes;
        if (const json::Value* ps = b.get("planes"); ps && ps->type == json::Value::Type::Array)
            for (const json::Value& p : ps->arr) {
                if (p.type != json::Value::Type::Array || p.arr.size() < 4) continue;
                Plane pl;
                for (int i = 0; i < 3; ++i) pl.n[i] = p.arr[size_t(i)].number().value_or(0);
                pl.d = p.arr[3].number().value_or(0);
                planes.push_back(pl);
            }
        for (int a = 0; a < 3; ++a) {  // the reader adds the six box planes (iw3_fastfile.cpp:7170-7183)
            Vec3 n;
            n[a] = 1;
            planes.push_back({n, maxs[a]});
            n[a] = -1;
            planes.push_back({n, -mins[a]});
        }
        CollisionHullEdges h;
        h.contents = uint32_t(b.get("contents") ? b.get("contents")->number().value_or(0) : 0);
        h.ladder = b.get("ladder_planes") != nullptr;
        auto same = [](const Vec3& p, const Vec3& q) { return length(p - q) < 0.01; };
        for (const Winding& w : buildBrushWindings(planes))
            for (size_t i = 0; i < w.size(); ++i) {
                const Vec3 &p = w[i], &q = w[(i + 1) % w.size()];
                if (same(p, q)) continue;
                const bool dup = std::any_of(h.edges.begin(), h.edges.end(), [&](const auto& e) {
                    return (same(e.first, p) && same(e.second, q)) || (same(e.first, q) && same(e.second, p));
                });
                if (!dup) h.edges.push_back({p, q});
            }
        if (!h.edges.empty()) out.push_back(std::move(h));
    }
    return true;
}

bool readCompiledSurfaces(const std::string& jsonText, std::vector<std::pair<Vec3, Vec3>>& edges, size_t* triangles,
                          std::string* err) {
    edges.clear();
    if (triangles) *triangles = 0;
    const auto root = json::parse(jsonText, err);
    if (!root) return false;
    const json::Value* surfs = root->get("surfaces");
    if (!surfs || surfs->type != json::Value::Type::Array) {
        if (err) *err = "no surfaces array";
        return false;
    }
    std::set<std::pair<std::array<long long, 3>, std::array<long long, 3>>> seen;
    const auto key = [](const Vec3& v) { return std::array<long long, 3>{std::llround(v.x * 8), std::llround(v.y * 8), std::llround(v.z * 8)}; };
    for (const json::Value& s : surfs->arr) {
        const json::Value* verts = s.get("vertices");
        const json::Value* idx = s.get("indices");
        if (!verts || !idx || verts->type != json::Value::Type::Array || idx->type != json::Value::Type::Array) continue;
        std::vector<Vec3> pos;
        pos.reserve(verts->arr.size());
        for (const json::Value& v : verts->arr) {
            Vec3 p;
            vec3(v.get("position"), p);
            pos.push_back(p);
        }
        // Indices are a flat list or a list of triangles.
        std::vector<size_t> flat;
        for (const json::Value& i : idx->arr) {
            if (i.type == json::Value::Type::Array) {
                for (const json::Value& j : i.arr) flat.push_back(size_t(j.number().value_or(0)));
            } else {
                flat.push_back(size_t(i.number().value_or(0)));
            }
        }
        for (size_t t = 0; t + 2 < flat.size(); t += 3) {
            if (flat[t] >= pos.size() || flat[t + 1] >= pos.size() || flat[t + 2] >= pos.size()) continue;
            if (triangles) ++*triangles;
            const Vec3 tri[3] = {pos[flat[t]], pos[flat[t + 1]], pos[flat[t + 2]]};
            for (int k = 0; k < 3; ++k) {
                auto a = key(tri[k]), b = key(tri[(k + 1) % 3]);
                if (b < a) std::swap(a, b);
                if (seen.insert({a, b}).second) edges.push_back({tri[k], tri[(k + 1) % 3]});
            }
        }
    }
    return true;
}

}  // namespace iw8r
