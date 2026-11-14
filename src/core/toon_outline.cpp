#include "toon_outline.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <tuple>

#include "compile_iw8.h"

namespace iw8r {

namespace {

constexpr double kPi = 3.14159265358979323846;

// A vertex key at 1/8 unit, so the same edge of two touching brushes matches.
std::tuple<long long, long long, long long> key(const Vec3& p) {
    return {std::llround(p.x * 8), std::llround(p.y * 8), std::llround(p.z * 8)};
}

}  // namespace

std::string inkFaceMaterial(const std::string& suffix) {
    std::istringstream in(suffix);
    std::string m;
    in >> m;
    return m;
}

std::vector<InkEdge> brushInkEdges(const ConvexBrush& brush, const InkOptions& opt) {
    std::vector<InkEdge> out;
    const std::vector<Winding> w = brushWindings(brush);
    if (w.size() != brush.size()) return out;
    // Each winding edge belongs to two faces: pair them by the edge's end points.
    using K = std::tuple<long long, long long, long long>;
    std::map<std::pair<K, K>, std::pair<size_t, std::pair<Vec3, Vec3>>> open;
    const double cosLimit = std::cos(opt.minAngleDeg * kPi / 180.0);
    std::vector<bool> drawn(brush.size(), true);
    if (opt.skipHiddenFaces)
        for (size_t f = 0; f < brush.size(); ++f) drawn[f] = !isIw8ToolMaterial(inkFaceMaterial(brush[f].suffix));
    for (size_t f = 0; f < w.size(); ++f) {
        const Winding& wf = w[f];
        if (wf.size() < 3) continue;
        for (size_t i = 0; i < wf.size(); ++i) {
            const Vec3 a = wf[i], b = wf[(i + 1) % wf.size()];
            if (length(b - a) < opt.minLength) continue;
            const K ka = key(a), kb = key(b);
            const auto k = ka < kb ? std::make_pair(ka, kb) : std::make_pair(kb, ka);
            const auto it = open.find(k);
            if (it == open.end()) {
                open.emplace(k, std::make_pair(f, std::make_pair(a, b)));
                continue;
            }
            const Vec3 n1 = brush[it->second.first].plane.n, n2 = brush[f].plane.n;
            // Faces turning by more than minAngle (their normals more than that apart).
            if (dot(n1, n2) <= cosLimit && (drawn[it->second.first] || drawn[f])) {
                Vec3 bis = n1 + n2;
                bis = length(bis) > 1e-9 ? normalize(bis) : n1;
                out.push_back({it->second.second.first, it->second.second.second, bis, n1, n2});
            }
            open.erase(it);
        }
    }
    return out;
}

std::vector<InkEdge> uniqueInkEdges(const std::vector<ConvexBrush>& brushes, const InkOptions& opt) {
    std::vector<InkEdge> out;
    // The solids that can cover a face: drawn brushes (any face not a tool material), with their bounds.
    std::vector<std::pair<AABB, const ConvexBrush*>> solids;
    if (opt.skipBuried)
        for (const ConvexBrush& b : brushes) {
            bool drawn = false;
            for (const auto& f : b) drawn |= !isIw8ToolMaterial(inkFaceMaterial(f.suffix));
            if (drawn && isValidBrush(b)) solids.push_back({brushBounds(b), &b});
        }
    const auto inside = [&](const Vec3& p) {
        for (const auto& [box, b] : solids) {
            if (p.x < box.mins.x || p.y < box.mins.y || p.z < box.mins.z || p.x > box.maxs.x || p.y > box.maxs.y ||
                p.z > box.maxs.z)
                continue;
            bool in = true;
            for (const auto& f : *b)
                if (dot(f.plane.n, p) - f.plane.d > -1e-3) {
                    in = false;
                    break;
                }
            if (in) return true;
        }
        return false;
    };
    // Points a little off each face beside the edge, at a quarter and three quarters of it (a short neighbour may
    // cover only part of a long edge: the edge is kept when either face is open at either point).
    const auto visible = [&](const InkEdge& e) {
        constexpr double eps = 0.5;
        for (double t : {0.25, 0.5, 0.75}) {
            const Vec3 m = e.a + (e.b - e.a) * t;
            if (!inside(m + e.n1 * eps - e.n2 * eps) && !inside(m + e.n2 * eps - e.n1 * eps)) return true;
        }
        return false;
    };
    std::map<std::pair<std::tuple<long long, long long, long long>, std::tuple<long long, long long, long long>>, bool> seen;
    for (const ConvexBrush& b : brushes)
        for (const InkEdge& e : brushInkEdges(b, opt)) {
            const auto ka = key(e.a), kb = key(e.b);
            const auto k = ka < kb ? std::make_pair(ka, kb) : std::make_pair(kb, ka);
            if (seen.emplace(k, true).second && (!opt.skipBuried || visible(e))) out.push_back(e);
        }
    return out;
}

ConvexBrush inkStripBrush(const InkEdge& e, const InkOptions& opt, const std::string& suffix) {
    ConvexBrush out;
    const Vec3 dir0 = e.b - e.a;
    const double len = length(dir0);
    if (len < 1e-6 || opt.width <= 0) return out;
    const Vec3 d = dir0 * (1.0 / len);
    // The strip's cross axes: u along the faces' bisector (made perpendicular to the edge), v across it.
    Vec3 u = e.bisector - d * dot(e.bisector, d);
    if (length(u) < 1e-6) u = std::fabs(d.z) < 0.9 ? cross(d, Vec3{0, 0, 1}) : cross(d, Vec3{1, 0, 0});
    u = normalize(u);
    const Vec3 v = normalize(cross(d, u));
    const double h = opt.width * 0.5;
    const Vec3 mid = (e.a + e.b) * 0.5;
    const double half = len * 0.5 + h;  // past both ends so neighbouring strips meet at corners
    const auto face = [&](const Vec3& n, double extent) {
        BrushFaceSpec f;
        f.plane.n = n;
        f.plane.d = dot(n, mid) + extent;
        f.suffix = suffix;
        out.push_back(f);
    };
    face(d, half), face(d * -1.0, half);
    face(u, h), face(u * -1.0, h);
    face(v, h), face(v * -1.0, h);
    return out;
}

InkResult applyInkOutlines(Document& doc, const InkOptions& opt, size_t maxStrips) {
    InkResult r;
    std::vector<ConvexBrush> brushes;
    Id oldInk = kNoId;
    for (const auto& e : doc.map().entities)
        if (const std::string* note = e.value("iw8r_note"); note && *note == kInkNote) oldInk = e.id;
    const auto addBrush = [&](const Primitive* p) {
        if (!p || p->kind != PrimKind::Brush) return;
        ConvexBrush b = brushFromPrimitive(*p);
        if (b.empty() || inkFaceMaterial(b.front().suffix) == kInkMaterial) return;
        brushes.push_back(std::move(b));
    };
    for (const auto& s : doc.selection()) {
        if (s.item == kNoId || s.entity == oldInk) continue;
        const EntityItem* it = doc.findItem(s.entity, s.item);
        const Primitive* p = it ? it->prim() : nullptr;
        if (!p || p->kind != PrimKind::Brush) continue;
        addBrush(p);
        r.fromSelection = true;
    }
    if (!r.fromSelection)
        for (const auto& e : doc.map().entities) {
            const std::string c = e.classname();
            if (e.id == oldInk || (c != "worldspawn" && c != "func_group")) continue;
            for (const auto& it : e.items) addBrush(it.prim());
        }
    r.brushes = brushes.size();
    const std::string suffix = std::string(" ") + kInkMaterial + " 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0";
    std::vector<Primitive> strips;
    for (const InkEdge& e : uniqueInkEdges(brushes, opt)) {
        if (strips.size() >= maxStrips) {
            r.capped = true;
            break;
        }
        if (auto p = inkStripPrimitive(e, opt, suffix, doc.map().eol)) strips.push_back(std::move(*p));
    }
    r.strips = strips.size();
    if (strips.empty()) return r;
    doc.beginGroup("Toon Ink Outlines");
    if (oldInk != kNoId) {
        doc.deselectAll();
        doc.select({oldInk, kNoId}, false);
        r.replaced = doc.deleteSelection();
    }
    doc.addEntity({{"classname", "func_group"}, {"iw8r_note", kInkNote}}, std::move(strips), "Toon ink outlines");
    doc.endGroup();
    return r;
}

std::optional<Primitive> inkStripPrimitive(const InkEdge& e, const InkOptions& opt, const std::string& suffix,
                                           const std::string& eol) {
    const ConvexBrush b = inkStripBrush(e, opt, suffix);
    if (b.empty()) return std::nullopt;
    std::optional<Primitive> p = makeBrushPrimitive(b, eol);
    if (!p) return std::nullopt;
    for (size_t i = 0; i < p->lines.size(); ++i)
        if (p->lines[i].text == "{") {
            p->lines.insert(p->lines.begin() + i + 1, TextLine{" contents nonColliding;", eol});
            break;
        }
    reparsePrimitive(*p);
    return p;
}

}  // namespace iw8r
