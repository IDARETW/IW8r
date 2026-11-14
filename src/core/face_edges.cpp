#include "face_edges.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace iw8r {

namespace {

constexpr double kPi = 3.14159265358979323846;
// A cut passes through a vertex within this distance of it instead of shaving a sliver off (world units). The
// converter refuses triangles that vanish in 32-bit positions, and a sliver is exactly that.
constexpr double kClipEps = 0.01;
constexpr double kWeld = 0.004;     // vertices of different pieces closer than this are one
constexpr double kMinHeight = 0.02; // a triangle flatter than this is not made
constexpr double kMinSpan = 0.25;  // world units: shorter edge stretches and shorter seams are ignored

double polyArea(const Winding& w) {
    Vec3 s;
    for (size_t i = 1; i + 1 < w.size(); ++i) s += cross(w[i] - w[0], w[i + 1] - w[0]);
    return 0.5 * length(s);
}

// The part of a convex polygon where dot(m, x) - c <= 0 (empty when nothing with an area is left).
Winding clipKeep(const Winding& w, const Vec3& m, double c) {
    Winding out;
    const size_t n = w.size();
    out.reserve(n + 1);
    for (size_t i = 0; i < n; ++i) {
        const Vec3& A = w[i];
        const Vec3& B = w[(i + 1) % n];
        const double fa = dot(m, A) - c, fb = dot(m, B) - c;
        if (fa <= kClipEps) out.push_back(A);
        if ((fa < -kClipEps && fb > kClipEps) || (fa > kClipEps && fb < -kClipEps)) out.push_back(A + (B - A) * (fa / (fa - fb)));
    }
    // Points that fell together.
    Winding tidy;
    for (const Vec3& p : out)
        if (tidy.empty() || length(p - tidy.back()) > kWeld) tidy.push_back(p);
    while (tidy.size() > 1 && length(tidy.front() - tidy.back()) <= kWeld) tidy.pop_back();
    if (tidy.size() < 3 || polyArea(tidy) < 1e-4) return {};
    return tidy;
}

// The distance field of one span: d(x) = dot(g, x) - c, g in the face, pointing away from the edge into the face.
struct Field {
    Vec3 g, dir;
    double c = 0;
    double a0 = 0, len = 0;  // along(x) = dot(dir, x) - a0, in [0, len] beside the span
    bool cutA = false, cutB = false;
    float fold = 0, dirAngle = 0;
};

struct Piece {
    Winding poly;
    int span = -1;
};

std::vector<Piece> facePieces(const Winding& face, const Vec3& n, const std::vector<EdgeSpan>& spans, double range,
                              std::vector<Field>& fields) {
    fields.clear();
    std::vector<Piece> pieces;
    if (face.size() < 3) return pieces;
    Vec3 centre;
    for (const Vec3& p : face) centre += p;
    centre = centre * (1.0 / double(face.size()));
    Vec3 ba, bb;
    edgeFaceBasis(n, ba, bb);
    pieces.push_back({face, -1});
    for (const EdgeSpan& s : spans) {
        const double len = length(s.b - s.a);
        if (len < 1e-6) continue;
        Field f;
        f.dir = (s.b - s.a) * (1.0 / len);
        f.g = normalize(cross(n, f.dir));
        if (dot(f.g, centre - s.a) < 0) f.g = f.g * -1.0;
        f.c = dot(f.g, s.a);
        f.a0 = dot(f.dir, s.a);
        f.len = len;
        f.cutA = s.cutA, f.cutB = s.cutB;
        f.fold = float(s.fold);
        double ang = std::atan2(dot(f.g, bb), dot(f.g, ba));
        if (ang < 0) ang += 2 * kPi;
        f.dirAngle = float(ang);
        const int k = int(fields.size());
        fields.push_back(f);
        // The half spaces of the span's band: within `range` of its line, and beside the span at a cut end.
        struct Half {
            Vec3 m;
            double c;
        };
        std::vector<Half> band{{f.g, f.c + range}};
        if (f.cutA) band.push_back({f.dir * -1.0, -f.a0});
        if (f.cutB) band.push_back({f.dir, f.a0 + f.len});
        std::vector<Piece> next;
        for (Piece& pc : pieces) {
            Winding cur = std::move(pc.poly);
            for (const Half& h : band) {
                if (cur.empty()) break;
                Winding outside = clipKeep(cur, h.m * -1.0, -h.c);
                if (!outside.empty()) next.push_back({std::move(outside), pc.span});
                cur = clipKeep(cur, h.m, h.c);
            }
            if (cur.empty()) continue;
            if (pc.span < 0) {
                next.push_back({std::move(cur), k});
                continue;
            }
            // Already another span's: this one takes the part nearer to it.
            const Field& o = fields[size_t(pc.span)];
            const Vec3 m = f.g - o.g;
            const double c = f.c - o.c;
            if (length(m) < 1e-9) {
                next.push_back({std::move(cur), pc.span});  // the same line: the first span keeps it
                continue;
            }
            Winding mine = clipKeep(cur, m, c);
            Winding theirs = clipKeep(cur, m * -1.0, -c);
            if (!mine.empty()) next.push_back({std::move(mine), k});
            if (!theirs.empty()) next.push_back({std::move(theirs), pc.span});
        }
        pieces = std::move(next);
    }
    // The cuts above leave each span's part (and the part without an edge) in fragments. Where the fragments of one
    // span make a convex whole (always, unless a span ends inside an edge), they become that one polygon again.
    {
        // Is the face wound counter-clockwise in (ba, bb)?
        double faceTurn = 0;
        for (size_t i = 0; i < face.size(); ++i) {
            const Vec3& p = face[i];
            const Vec3& q = face[(i + 1) % face.size()];
            faceTurn += dot(p, ba) * dot(q, bb) - dot(q, ba) * dot(p, bb);
        }
        std::vector<Piece> merged;
        for (int span = -1; span < int(fields.size()); ++span) {
            std::vector<const Piece*> group;
            double area = 0;
            for (const Piece& pc : pieces)
                if (pc.span == span) group.push_back(&pc), area += polyArea(pc.poly);
            if (group.size() < 2) {
                for (const Piece* pc : group) merged.push_back(*pc);
                continue;
            }
            // Convex hull of the fragments' corners in the face plane (monotone chain).
            struct P2 {
                double x, y;
                Vec3 p;
            };
            std::vector<P2> pts;
            for (const Piece* pc : group)
                for (const Vec3& p : pc->poly) pts.push_back({dot(p, ba), dot(p, bb), p});
            std::sort(pts.begin(), pts.end(), [](const P2& l, const P2& r) { return l.x < r.x || (l.x == r.x && l.y < r.y); });
            const auto turn = [](const P2& o, const P2& u, const P2& v) { return (u.x - o.x) * (v.y - o.y) - (u.y - o.y) * (v.x - o.x); };
            std::vector<P2> hull(2 * pts.size());
            size_t k = 0;
            for (size_t i = 0; i < pts.size(); ++i) {
                while (k >= 2 && turn(hull[k - 2], hull[k - 1], pts[i]) <= 1e-9) --k;
                hull[k++] = pts[i];
            }
            for (size_t i = pts.size() - 1, t = k + 1; i > 0; --i) {
                while (k >= t && turn(hull[k - 2], hull[k - 1], pts[i - 1]) <= 1e-9) --k;
                hull[k++] = pts[i - 1];
            }
            hull.resize(k > 1 ? k - 1 : k);
            Winding whole;
            for (const P2& h : hull) whole.push_back(h.p);
            if (faceTurn < 0) std::reverse(whole.begin(), whole.end());
            if (whole.size() >= 3 && std::fabs(polyArea(whole) - area) <= 1e-6 * std::max(1.0, area)) {
                merged.push_back({std::move(whole), span});
            } else {
                for (const Piece* pc : group) merged.push_back(*pc);
            }
        }
        pieces = std::move(merged);
    }
    return pieces;
}

// Pieces as index lists into one welded vertex list; vertices lying on another piece's edge are added to that edge.
struct Welded {
    std::vector<Vec3> pos;
    struct Poly {
        std::vector<uint32_t> idx;
        int span = -1;
        size_t face = 0;
    };
    std::vector<Poly> polys;

    uint32_t add(const Vec3& p) {
        for (size_t i = 0; i < pos.size(); ++i)
            if (std::fabs(pos[i].x - p.x) < kWeld && std::fabs(pos[i].y - p.y) < kWeld && std::fabs(pos[i].z - p.z) < kWeld) return uint32_t(i);
        pos.push_back(p);
        return uint32_t(pos.size() - 1);
    }
    void addPiece(const Piece& pc, size_t face) {
        Poly q;
        q.span = pc.span;
        q.face = face;
        for (const Vec3& p : pc.poly) {
            const uint32_t i = add(p);
            if (q.idx.empty() || q.idx.back() != i) q.idx.push_back(i);
        }
        while (q.idx.size() > 1 && q.idx.front() == q.idx.back()) q.idx.pop_back();
        if (q.idx.size() >= 3) polys.push_back(std::move(q));
    }
    void fixJunctions() {
        for (Poly& q : polys) {
            AABB box;
            for (uint32_t i : q.idx) box.add(pos[i]);
            std::vector<uint32_t> near;
            for (size_t k = 0; k < pos.size(); ++k)
                if (box.contains(pos[k], kWeld)) near.push_back(uint32_t(k));
            std::vector<uint32_t> out;
            const size_t n = q.idx.size();
            for (size_t e = 0; e < n; ++e) {
                const uint32_t iu = q.idx[e], iv = q.idx[(e + 1) % n];
                out.push_back(iu);
                const Vec3 u = pos[iu], d = pos[iv] - u;
                const double dd = dot(d, d);
                if (dd < 1e-12) continue;
                std::vector<std::pair<double, uint32_t>> on;
                for (uint32_t k : near) {
                    if (k == iu || k == iv) continue;
                    const double t = dot(pos[k] - u, d) / dd;
                    if (t <= 0 || t >= 1) continue;
                    if (length(pos[k] - (u + d * t)) > kWeld) continue;
                    if (std::find(q.idx.begin(), q.idx.end(), k) != q.idx.end()) continue;
                    on.push_back({t, k});
                }
                std::sort(on.begin(), on.end());
                for (const auto& o : on) out.push_back(o.second);
            }
            q.idx = std::move(out);
        }
    }
};

void triangulate(const Welded& w, const Welded::Poly& q, const std::vector<Field>& fields, double range, EdgeFaceMesh& mesh) {
    const Field* f = q.span >= 0 ? &fields[size_t(q.span)] : nullptr;
    const auto vertex = [&](const Vec3& p) {
        EdgeVertex v;
        v.pos = p;
        if (f) {
            v.edge = float(std::clamp((dot(f->g, p) - f->c) / range, 0.0, 1.0));
            v.fold = f->fold;
            v.dir = f->dirAngle;
        }
        return v;
    };
    // Ear clipping: a convex outline with vertices inside its straight runs (added where another piece ends on the
    // edge). A corner is cut off when its triangle has a height; a vertex inside a run stays until its neighbours went.
    const uint32_t base = uint32_t(mesh.verts.size());
    for (uint32_t i : q.idx) mesh.verts.push_back(vertex(w.pos[i]));
    std::vector<uint32_t> ring(q.idx.size());
    std::iota(ring.begin(), ring.end(), 0u);
    const auto height = [&](uint32_t a, uint32_t b, uint32_t c) {
        const Vec3& A = w.pos[q.idx[a]];
        const Vec3& B = w.pos[q.idx[b]];
        const Vec3& C = w.pos[q.idx[c]];
        const double longest = std::max(length(B - A), std::max(length(C - B), length(A - C)));
        return longest > 1e-12 ? length(cross(B - A, C - A)) / longest : 0.0;
    };
    while (ring.size() >= 3) {
        // The best ear: the corner whose triangle is the least flat.
        size_t best = 0;
        double bestH = -1;
        for (size_t i = 0; i < ring.size(); ++i) {
            const size_t m = ring.size();
            const uint32_t pp = ring[(i + m - 2) % m], pv = ring[(i + m - 1) % m], nx = ring[(i + 1) % m], nn = ring[(i + 2) % m];
            // Not when the cut would run along a straight part of the outline: the vertices on it would be left on a
            // triangle's edge.
            if (m > 3 && (height(pv, nx, nn) < 1e-6 || height(pp, pv, nx) < 1e-6)) continue;
            const double h = height(pv, ring[i], nx);
            if (h > bestH) bestH = h, best = i;
        }
        if (bestH < kMinHeight) break;  // what is left has no area
        const size_t n = ring.size();
        mesh.idx.push_back(base + ring[(best + n - 1) % n]);
        mesh.idx.push_back(base + ring[best]);
        mesh.idx.push_back(base + ring[(best + 1) % n]);
        ring.erase(ring.begin() + long(best));
    }
}

// The stretch [t0, t1] of segment a..b inside the convex brush (planes with a tolerance); false when none.
bool segmentInBrush(const EdgeBrush& c, const Vec3& a, const Vec3& b, double& t0, double& t1) {
    t0 = 0, t1 = 1;
    for (const Plane& p : c.planes) {
        // A point within the tolerance of a plane is on it; where the segment really crosses a plane, the stretch
        // ends exactly on that plane (the spans cut there must meet the other brush's corner, not miss it by the
        // tolerance).
        const double fa = dot(p.n, a) - p.d, fb = dot(p.n, b) - p.d;
        if (fa > 1e-4 && fb > 1e-4) return false;
        if (fa > 1e-4) t0 = std::max(t0, fa / (fa - fb));
        else if (fb > 1e-4) t1 = std::min(t1, fa / (fa - fb));
    }
    return t1 > t0;
}

}  // namespace

void finishEdgeBrush(EdgeBrush& b) {
    b.bounds = AABB{};
    for (const Winding& w : b.wins)
        for (const Vec3& p : w) b.bounds.add(p);
    if (b.drawn.size() < b.planes.size()) b.drawn.resize(b.planes.size(), 1);
    if (b.wins.size() < b.planes.size()) b.wins.resize(b.planes.size());
}

std::vector<std::vector<uint32_t>> edgeNeighbours(const std::vector<EdgeBrush>& brushes) {
    std::vector<std::vector<uint32_t>> out(brushes.size());
    std::vector<uint32_t> order;
    for (size_t i = 0; i < brushes.size(); ++i)
        if (brushes[i].bounds.valid()) order.push_back(uint32_t(i));
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return brushes[a].bounds.mins.x < brushes[b].bounds.mins.x; });
    constexpr double pad = 0.5;
    for (size_t i = 0; i < order.size(); ++i) {
        const EdgeBrush& a = brushes[order[i]];
        for (size_t j = i + 1; j < order.size(); ++j) {
            const EdgeBrush& b = brushes[order[j]];
            if (b.bounds.mins.x > a.bounds.maxs.x + pad) break;
            if (a.set != b.set) continue;
            if (b.bounds.mins.y > a.bounds.maxs.y + pad || b.bounds.maxs.y < a.bounds.mins.y - pad ||
                b.bounds.mins.z > a.bounds.maxs.z + pad || b.bounds.maxs.z < a.bounds.mins.z - pad)
                continue;
            out[order[i]].push_back(order[j]);
            out[order[j]].push_back(order[i]);
        }
    }
    for (auto& l : out) std::sort(l.begin(), l.end());
    return out;
}

std::vector<EdgeSpan> faceEdgeSpans(const std::vector<EdgeBrush>& brushes, size_t self, size_t face,
                                  const std::vector<uint32_t>& neighbours) {
    std::vector<EdgeSpan> out;
    if (self >= brushes.size()) return out;
    const EdgeBrush& B = brushes[self];
    if (face >= B.wins.size() || face >= B.planes.size() || B.wins[face].size() < 3) return out;
    if (face < B.drawn.size() && !B.drawn[face]) return out;
    const Winding& w = B.wins[face];
    const Vec3 nA = normalize(B.planes[face].n);
    Vec3 centre;
    for (const Vec3& p : w) centre += p;
    centre = centre * (1.0 / double(w.size()));
    const double softCos = std::cos(kEdgeSoftAngle);
    for (size_t i = 0; i < w.size(); ++i) {
        const Vec3 a = w[i], b = w[(i + 1) % w.size()];
        const double len = length(b - a);
        if (len < kMinSpan) continue;
        // The face of this brush across the edge.
        double hardFold = kPi / 2;
        {
            int best = -1;
            for (size_t f = 0; f < B.planes.size(); ++f) {
                if (f == face) continue;
                const Plane& p = B.planes[f];
                if (std::fabs(dot(p.n, a) - p.d) > 0.02 || std::fabs(dot(p.n, b) - p.d) > 0.02) continue;
                if (dot(normalize(p.n), nA) > 0.99999) continue;
                if (best < 0 || (B.wins[size_t(best)].empty() && f < B.wins.size() && !B.wins[f].empty())) best = int(f);
            }
            if (best >= 0) hardFold = std::acos(std::clamp(dot(normalize(B.planes[size_t(best)].n), nA), -1.0, 1.0));
        }
        if (hardFold < 1e-3) continue;
        const Vec3 dir = (b - a) * (1.0 / len);
        Vec3 outward = cross(dir, nA);
        if (dot(outward, centre - a) > 0) outward = outward * -1.0;
        // Where the surface carries on past the edge into another brush: [t0, t1] with the fold there (0 = a seam).
        struct Cover {
            double t0, t1, fold;
        };
        std::vector<Cover> covers;
        AABB eb;
        eb.add(a), eb.add(b);
        for (uint32_t ci : neighbours) {
            if (ci == self || ci >= brushes.size()) continue;
            const EdgeBrush& C = brushes[ci];
            if (C.set != B.set) continue;
            if (C.bounds.mins.x > eb.maxs.x + 0.1 || C.bounds.maxs.x < eb.mins.x - 0.1 || C.bounds.mins.y > eb.maxs.y + 0.1 ||
                C.bounds.maxs.y < eb.mins.y - 0.1 || C.bounds.mins.z > eb.maxs.z + 0.1 || C.bounds.maxs.z < eb.mins.z - 0.1)
                continue;
            // The face itself is inside or against this brush beside the edge (a point a little into the face from
            // the edge, a little in front of it): hidden there, so no edge. Overlapping brushes and the side faces
            // between the slices of a wall are such faces; their edges lie in the visible wall's plane and would
            // fight with it for the pixels of a line.
            if (std::find(C.drawn.begin(), C.drawn.end(), uint8_t(1)) != C.drawn.end()) {
                const Vec3 inFront = outward * -0.05 + nA * 0.01;
                double t0, t1;
                if (segmentInBrush(C, a + inFront, b + inFront, t0, t1) && (t1 - t0) * len >= kMinSpan) covers.push_back({t0, t1, 0.0});
            }
            for (size_t f = 0; f < C.planes.size(); ++f) {
                if (f < C.drawn.size() && !C.drawn[f]) continue;
                if (f >= C.wins.size() || C.wins[f].size() < 3) continue;
                const Vec3 nF = normalize(C.planes[f].n);
                const double cosang = dot(nF, nA);
                if (cosang < softCos) continue;
                const double dF = C.planes[f].d / std::max(1e-12, length(C.planes[f].n));
                if (std::fabs(dot(nF, a) - dF) > 0.02 || std::fabs(dot(nF, b) - dF) > 0.02) continue;
                // The surface of C beside the edge: a little out along its own plane, a little under it.
                Vec3 outF = outward - nF * dot(outward, nF);
                if (length(outF) < 1e-6) continue;
                outF = normalize(outF);
                const Vec3 shift = outF * 0.05 - nF * 0.01;
                double t0, t1;
                if (!segmentInBrush(C, a + shift, b + shift, t0, t1)) continue;
                if ((t1 - t0) * len < kMinSpan) continue;
                // Within 2 degrees of the plane the surface simply carries on (a seam); a larger convex bend is a soft fold.
                double fold = 0;
                if (cosang < 0.9994) fold = dot(nF, outward) > 0 ? std::acos(std::clamp(cosang, -1.0, 1.0)) : 0.0;
                covers.push_back({t0, t1, fold});
            }
        }
        // The edge in stretches of one fold each: the smallest fold of the covers there, else the fold to this brush's
        // own next face.
        std::vector<double> cuts{0.0, 1.0};
        for (const Cover& c : covers) cuts.push_back(c.t0), cuts.push_back(c.t1);
        std::sort(cuts.begin(), cuts.end());
        std::vector<Cover> runs;
        for (size_t k = 0; k + 1 < cuts.size(); ++k) {
            const double t0 = cuts[k], t1 = cuts[k + 1];
            if (t1 - t0 < 1e-9) continue;
            const double mid = 0.5 * (t0 + t1);
            double fold = hardFold;
            for (const Cover& c : covers)
                if (mid > c.t0 && mid < c.t1) fold = std::min(fold, c.fold);
            if (!runs.empty() && std::fabs(runs.back().fold - fold) < 1e-6 && std::fabs(runs.back().t1 - t0) < 1e-9) runs.back().t1 = t1;
            else runs.push_back({t0, t1, fold});
        }
        for (const Cover& r : runs) {
            if (r.fold < 1e-3 || (r.t1 - r.t0) * len < kMinSpan) continue;
            EdgeSpan s;
            s.a = a + (b - a) * r.t0;
            s.b = a + (b - a) * r.t1;
            s.fold = r.fold;
            s.cutA = r.t0 > 1e-6;
            s.cutB = r.t1 < 1 - 1e-6;
            out.push_back(s);
        }
    }
    return out;
}

std::vector<Winding> coplanarCovers(const std::vector<EdgeBrush>& brushes, size_t self, size_t face,
                                    const std::vector<uint32_t>& neighbours) {
    std::vector<Winding> out;
    if (self >= brushes.size()) return out;
    const EdgeBrush& B = brushes[self];
    if (face >= B.wins.size() || face >= B.planes.size() || B.wins[face].size() < 3) return out;
    if (face < B.drawn.size() && !B.drawn[face]) return out;
    const Winding& A = B.wins[face];
    const Vec3 nA = normalize(B.planes[face].n);
    const double dA = B.planes[face].d / std::max(1e-12, length(B.planes[face].n));
    const double areaA = polyArea(A);
    AABB ab;
    for (const Vec3& p : A) ab.add(p);
    for (uint32_t ci : neighbours) {
        if (ci == self || ci >= brushes.size()) continue;
        const EdgeBrush& C = brushes[ci];
        if (C.set != B.set) continue;
        if (C.bounds.mins.x > ab.maxs.x || C.bounds.maxs.x < ab.mins.x || C.bounds.mins.y > ab.maxs.y || C.bounds.maxs.y < ab.mins.y ||
            C.bounds.mins.z > ab.maxs.z || C.bounds.maxs.z < ab.mins.z)
            continue;
        for (size_t g = 0; g < C.planes.size() && g < C.wins.size(); ++g) {
            const Winding& F = C.wins[g];
            if (F.size() < 3 || (g < C.drawn.size() && !C.drawn[g])) continue;
            const Vec3 nF = normalize(C.planes[g].n);
            if (dot(nF, nA) < 0.99999) continue;
            if (std::fabs(C.planes[g].d / std::max(1e-12, length(C.planes[g].n)) - dA) > 0.02) continue;
            // Who is drawn where they overlap: the smaller face, the later brush of two equal ones.
            const double areaF = polyArea(F);
            const double tol = 1e-6 * std::max(1.0, std::max(areaA, areaF));
            const bool wins = areaF < areaA - tol || (std::fabs(areaF - areaA) <= tol && ci > self);
            if (!wins) continue;
            // Do they overlap at all (more than touching)?
            Winding common = A;
            Vec3 fc;
            for (const Vec3& p : F) fc += p;
            fc = fc * (1.0 / double(F.size()));
            for (size_t k = 0; k < F.size() && !common.empty(); ++k) {
                Vec3 m = cross(F[(k + 1) % F.size()] - F[k], nF);
                if (length(m) < 1e-12) continue;
                m = normalize(m);
                if (dot(m, fc - F[k]) > 0) m = m * -1.0;  // outward of F in the plane
                common = clipKeep(common, m, dot(m, F[k]));
            }
            if (common.empty() || polyArea(common) < 0.05) continue;
            out.push_back(F);
        }
    }
    return out;
}

std::vector<Winding> hiddenCovers(const std::vector<EdgeBrush>& brushes, size_t self, size_t face,
                                  const std::vector<uint32_t>& neighbours) {
    std::vector<Winding> out;
    if (self >= brushes.size()) return out;
    const EdgeBrush& B = brushes[self];
    if (face >= B.wins.size() || face >= B.planes.size() || B.wins[face].size() < 3) return out;
    const Vec3 nA = normalize(B.planes[face].n);
    // The face a little in front of itself: where that lies in an opaque brush, nothing can look at the face. (Well
    // clear of the cut tolerance: a coplanar face of the other brush, facing the same way, is not "in front".)
    constexpr double lift = 0.05;
    Winding lifted = B.wins[face];
    AABB ab;
    for (Vec3& p : lifted) {
        p = p + nA * lift;
        ab.add(p);
    }
    for (uint32_t ci : neighbours) {
        if (ci == self || ci >= brushes.size()) continue;
        const EdgeBrush& C = brushes[ci];
        if (C.set != B.set || !C.opaque) continue;
        if (C.bounds.mins.x > ab.maxs.x || C.bounds.maxs.x < ab.mins.x || C.bounds.mins.y > ab.maxs.y || C.bounds.maxs.y < ab.mins.y ||
            C.bounds.mins.z > ab.maxs.z || C.bounds.maxs.z < ab.mins.z)
            continue;
        Winding in = lifted;
        for (size_t g = 0; g < C.planes.size() && !in.empty(); ++g) {
            const double len = length(C.planes[g].n);
            if (len < 1e-12) continue;
            in = clipKeep(in, C.planes[g].n * (1.0 / len), C.planes[g].d / len);
        }
        if (in.empty() || polyArea(in) < 0.05) continue;
        for (Vec3& p : in) p = p - nA * lift;
        out.push_back(std::move(in));
    }
    return out;
}

namespace {

// The pieces without the part under a convex polygon of their plane.
std::vector<Piece> withoutCover(std::vector<Piece> pieces, const Winding& cover, const Vec3& n) {
    Vec3 cc;
    for (const Vec3& p : cover) cc += p;
    cc = cc * (1.0 / double(cover.size()));
    std::vector<Piece> out;
    for (Piece& pc : pieces) {
        Winding cur = std::move(pc.poly);
        for (size_t k = 0; k < cover.size() && !cur.empty(); ++k) {
            Vec3 m = cross(cover[(k + 1) % cover.size()] - cover[k], n);
            if (length(m) < 1e-12) continue;
            m = normalize(m);
            if (dot(m, cc - cover[k]) > 0) m = m * -1.0;
            const double c = dot(m, cover[k]);
            Winding outside = clipKeep(cur, m * -1.0, -c);
            if (!outside.empty()) out.push_back({std::move(outside), pc.span});
            cur = clipKeep(cur, m, c);
        }
        // What is left of cur lies under the cover: not drawn.
    }
    return out;
}

}  // namespace

EdgeFaceMesh edgeFaceMesh(const Winding& face, const Vec3& normal, const std::vector<EdgeSpan>& spans, double range) {
    EdgeFaceMesh mesh;
    if (face.size() < 3) return mesh;
    std::vector<Field> fields;
    const std::vector<Piece> pieces = facePieces(face, normalize(normal), spans, range, fields);
    Welded w;
    for (const Piece& p : pieces) w.addPiece(p, 0);
    w.fixJunctions();
    for (const auto& q : w.polys) triangulate(w, q, fields, range, mesh);
    return mesh;
}

namespace {

// The pieces of one face before welding: its spans' bands, without the parts under coplanar faces of other brushes.
std::vector<Piece> brushFacePieces(const std::vector<EdgeBrush>& brushes, size_t self, size_t f, const std::vector<uint32_t>& neighbours,
                                   double range, bool withEdges, std::vector<Field>& fields, std::vector<Winding>* coversOut) {
    const EdgeBrush& B = brushes[self];
    const std::vector<EdgeSpan> spans = withEdges ? faceEdgeSpans(brushes, self, f, neighbours) : std::vector<EdgeSpan>();
    const Vec3 n = normalize(B.planes[f].n);
    std::vector<Piece> pieces = facePieces(B.wins[f], n, spans, range, fields);
    std::vector<Winding> covers = coplanarCovers(brushes, self, f, neighbours);
    // ... and the parts no one can see (inside or against an opaque brush).
    for (Winding& hidden : hiddenCovers(brushes, self, f, neighbours)) covers.push_back(std::move(hidden));
    for (const Winding& cover : covers) pieces = withoutCover(std::move(pieces), cover, n);
    if (coversOut) *coversOut = covers;
    return pieces;
}

}  // namespace

std::vector<EdgeFaceMesh> edgeBrushMeshes(const std::vector<EdgeBrush>& brushes, size_t self,
                                        const std::vector<uint32_t>& neighbours, double range, bool withEdges,
                                        const std::vector<std::vector<uint32_t>>* allNear) {
    std::vector<EdgeFaceMesh> out;
    if (self >= brushes.size()) return out;
    const EdgeBrush& B = brushes[self];
    out.resize(B.wins.size());
    std::vector<std::vector<Field>> fields(B.wins.size());
    Welded w;
    for (size_t f = 0; f < B.wins.size() && f < B.planes.size(); ++f) {
        if (B.wins[f].size() < 3) continue;
        std::vector<Winding> covers;
        const std::vector<Piece> pieces = brushFacePieces(brushes, self, f, neighbours, range, withEdges, fields[f], &covers);
        for (const Piece& p : pieces) w.addPiece(p, f);
        // The covers' corners become vertices of the pieces' edges they lie on (fixJunctions): no gap beside them.
        if (!pieces.empty())
            for (const Winding& cover : covers)
                for (const Vec3& p : cover) w.add(p);
    }
    // The touching brushes' own piece corners, where they lie on this brush: fixJunctions puts them on the edges they
    // sit on, and they put this brush's on theirs.
    if (allNear && !w.polys.empty()) {
        AABB mine = B.bounds;
        mine.mins = mine.mins - Vec3{kWeld, kWeld, kWeld};
        mine.maxs = mine.maxs + Vec3{kWeld, kWeld, kWeld};
        std::vector<Field> scratch;
        for (uint32_t ni : neighbours) {
            if (ni == self || ni >= brushes.size() || ni >= allNear->size() || brushes[ni].set != B.set) continue;
            const EdgeBrush& N = brushes[ni];
            for (size_t f = 0; f < N.wins.size() && f < N.planes.size(); ++f) {
                if (N.wins[f].size() < 3) continue;
                for (const Piece& p : brushFacePieces(brushes, ni, f, (*allNear)[ni], range, withEdges, scratch, nullptr))
                    for (const Vec3& v : p.poly)
                        if (mine.contains(v)) w.add(v);
            }
        }
    }
    w.fixJunctions();
    for (const auto& q : w.polys) triangulate(w, q, fields[q.face], range, out[q.face]);
    return out;
}

void edgeFaceBasis(const Vec3& nIn, Vec3& a, Vec3& b) {
    const Vec3 n = normalize(nIn);
    a = std::fabs(n.z) < 0.999 ? cross(Vec3{0, 0, 1}, n) : Vec3{1, 0, 0} - n * n.x;
    a = normalize(a);
    b = cross(n, a);
}

uint32_t packEdge(const EdgeVertex& v) {
    const uint32_t e = uint32_t(std::lround(std::clamp(double(v.edge), 0.0, 1.0) * 255.0));
    const uint32_t f = uint32_t(std::lround(std::clamp(double(v.fold) / kPi, 0.0, 1.0) * 255.0));
    const uint32_t d = uint32_t(std::lround(double(v.dir) / (2 * kPi) * 256.0)) & 255u;
    return e | (f << 8) | (d << 16) | 0xFF000000u;
}

}  // namespace iw8r
