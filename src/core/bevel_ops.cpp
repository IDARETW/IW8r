#include "bevel_ops.h"

#include <cmath>
#include <iterator>
#include <utility>

#include "convex_hull.h"

namespace iw8r {

namespace {

constexpr double kNear = 1e-6;  // winding vertices closer than this are the same point
constexpr double kEps = 1e-9;   // smallest length, area or dot margin that still counts

bool validAmount(double v) { return std::isfinite(v) && v > 0; }

bool nearPoint(const Vec3& a, const Vec3& b) { return length(a - b) <= kNear; }

// True when the winding has the segment a-b as one of its sides (either direction).
bool hasSide(const Winding& w, const Vec3& a, const Vec3& b) {
    for (size_t i = 0; i < w.size(); ++i) {
        const Vec3& p = w[i];
        const Vec3& q = w[(i + 1) % w.size()];
        if ((nearPoint(p, a) && nearPoint(q, b)) || (nearPoint(p, b) && nearPoint(q, a))) return true;
    }
    return false;
}

// Vertex average: inside a convex polygon, so it gives a point strictly inside the face.
Vec3 average(const Winding& w) {
    Vec3 c;
    for (const Vec3& p : w) c += p;
    return c * (1.0 / static_cast<double>(w.size()));
}

// Keeps the faces that contribute to the solid, in their original order.
ConvexBrush reduceBrush(const ConvexBrush& b) {
    const auto ws = brushWindings(b);
    ConvexBrush out;
    for (size_t i = 0; i < b.size(); ++i)
        if (!ws[i].empty()) out.push_back(b[i]);
    return out;
}

// Cut face that removes `size` from both faces next to `edge` (definition in bevel_ops.h).
// Why this plane: inward directions u1, u2 of the two faces satisfy u1.n2 == u2.n1, so both points lie on the plane
// with normal n1+n2 and the cut contains the edge direction.
std::optional<BrushFaceSpec> edgeCut(const ConvexBrush& b, const std::vector<Winding>& ws, const BrushEdge& edge, double size) {
    std::vector<size_t> adj;
    for (size_t i = 0; i < ws.size(); ++i)
        if (!ws[i].empty() && hasSide(ws[i], edge.a, edge.b)) adj.push_back(i);
    if (adj.size() != 2) return std::nullopt;  // not an edge of the brush
    const size_t f1 = adj[0];                  // ascending, so f1 is the lower index: its suffix is used

    const Vec3 along = edge.b - edge.a;
    const double len = length(along);
    if (!(len > kEps)) return std::nullopt;
    const Vec3 e = along * (1.0 / len);

    const Vec3 n1 = normalize(b[f1].plane.n);
    const Vec3 n2 = normalize(b[adj[1]].plane.n);
    const Vec3 nSum = n1 + n2;
    const double nLen = length(nSum);
    if (!(nLen > kEps)) return std::nullopt;
    const Vec3 m = nSum * (1.0 / nLen);

    // Direction inside face f, perpendicular to the edge (from the edge towards the face's interior).
    auto inward = [&](size_t f) {
        Vec3 v = average(ws[f]) - edge.a;
        v = v - e * dot(v, e);
        return normalize(v);
    };
    const Vec3 q1 = edge.a + inward(f1) * size;  // point `size` from the edge on face f1
    return BrushFaceSpec{{m, dot(m, q1)}, b[f1].suffix};
}

// Intersects `b` with the cut half-spaces. nullopt when a face of `b` or a cut face vanishes, or the result is invalid.
std::optional<ConvexBrush> cutBrush(const ConvexBrush& b, const std::vector<BrushFaceSpec>& cuts) {
    if (!isValidBrush(b)) return std::nullopt;
    size_t kept = 0;
    for (const Winding& w : brushWindings(b)) kept += !w.empty();

    ConvexBrush all = b;
    all.insert(all.end(), cuts.begin(), cuts.end());
    const auto ws = brushWindings(all);
    ConvexBrush out;
    for (size_t i = 0; i < all.size(); ++i)
        if (!ws[i].empty()) out.push_back(all[i]);
    // Every original face and every cut must survive; a face that shrinks to zero area is dropped here.
    if (out.size() != kept + cuts.size()) return std::nullopt;
    if (!isValidBrush(out)) return std::nullopt;
    return out;
}

}  // namespace

std::optional<ConvexBrush> chamferEdge(const ConvexBrush& b, const BrushEdge& edge, double size) {
    if (!validAmount(size) || !isValidBrush(b)) return std::nullopt;
    const auto ws = brushWindings(b);
    const auto cut = edgeCut(b, ws, edge, size);
    if (!cut) return std::nullopt;
    return cutBrush(b, {*cut});
}

std::optional<ConvexBrush> chamferAllEdges(const ConvexBrush& b, double size) {
    if (!validAmount(size) || !isValidBrush(b)) return std::nullopt;
    const auto ws = brushWindings(b);
    std::vector<BrushFaceSpec> cuts;
    for (const BrushEdge& e : brushEdges(b)) {
        const auto cut = edgeCut(b, ws, e, size);
        if (!cut) return std::nullopt;  // one failed cut fails the whole chamfer
        cuts.push_back(*cut);
    }
    return cutBrush(b, cuts);
}

std::vector<ConvexBrush> insetFace(const ConvexBrush& b, size_t faceIndex, double inset, double depth) {
    if (!validAmount(inset) || !validAmount(depth) || !isValidBrush(b) || faceIndex >= b.size()) return {};
    const auto ws = brushWindings(b);
    const Winding& w = ws[faceIndex];
    if (w.empty()) return {};
    const size_t n = w.size();

    const double nLen = length(b[faceIndex].plane.n);
    const Vec3 nrm = b[faceIndex].plane.n * (1.0 / nLen);  // outward
    const double dFace = b[faceIndex].plane.d / nLen;
    const std::string& suffix = b[faceIndex].suffix;

    // The winding is CCW seen from nrm, so cross(nrm, edge) points into the face.
    std::vector<Vec3> edgeDir(n), inDir(n);
    for (size_t k = 0; k < n; ++k) {
        edgeDir[k] = normalize(w[(k + 1) % n] - w[k]);
        inDir[k] = normalize(cross(nrm, edgeDir[k]));
    }
    // Inner outline corner k: the point `inset` inside both edges that meet at w[k] (a mitre).
    std::vector<Vec3> inner(n);
    for (size_t k = 0; k < n; ++k) {
        const Vec3& prev = inDir[(k + n - 1) % n];
        const Vec3& next = inDir[k];
        inner[k] = w[k] + (prev + next) * (inset / (1.0 + dot(prev, next)));
    }
    // Reject an outline that has flipped (inset past the face's inradius): its edges run against the outer edges.
    double twiceArea = 0;
    for (size_t k = 0; k < n; ++k) {
        const Vec3& p = inner[k];
        const Vec3& q = inner[(k + 1) % n];
        if (dot(q - p, edgeDir[k]) <= kEps) return {};
        twiceArea += dot(cross(p, q), nrm);
    }
    if (!(twiceArea > kEps)) return {};

    // Main brush: the face moved `depth` inward. It must still have that face.
    ConvexBrush body = b;
    body[faceIndex].plane = {nrm, dFace - depth};
    if (brushWindings(body)[faceIndex].empty() || !isValidBrush(body)) return {};

    std::vector<ConvexBrush> out;
    out.push_back(reduceBrush(body));

    // Ring pieces: each outer edge k with its two mitre planes and the inner edge. Outer walls come from `b` itself,
    // so the silhouette is unchanged. The mitre planes are shared by neighbours with opposite sides, so pieces
    // do not overlap.
    for (size_t k = 0; k < n; ++k) {
        const Vec3& v0 = w[k];
        const Vec3& v1 = w[(k + 1) % n];
        const Vec3& q0 = inner[k];
        const Vec3& q1 = inner[(k + 1) % n];
        const Vec3 centre = (v0 + v1 + q0 + q1) * 0.25;  // strictly inside the quad: tells which side to keep

        // Vertical plane (contains nrm) through p and r, keeping the side that holds `centre`.
        auto wall = [&](const Vec3& p, const Vec3& r) {
            Vec3 m = normalize(cross(r - p, nrm));
            double d = dot(m, p);
            if (dot(m, centre) > d) {
                m = -m;
                d = -d;
            }
            return BrushFaceSpec{{m, d}, suffix};
        };

        ConvexBrush piece = b;
        piece.push_back({{-nrm, -(dFace - depth)}, suffix});  // floor of the recess
        piece.push_back(wall(v0, q0));                         // mitre at v0
        piece.push_back(wall(v1, q1));                         // mitre at v1
        piece.push_back(wall(q0, q1));                         // inner edge
        piece = reduceBrush(piece);
        if (isValidBrush(piece)) out.push_back(std::move(piece));
    }
    return out;
}

size_t chamferSelection(Document& doc, double size) {
    std::vector<Document::PrimEdit> edits;
    for (const SelRef& r : doc.selection()) {
        if (r.item == kNoId) continue;  // point entities have no edges
        const EntityItem* it = doc.findItem(r.entity, r.item);
        const Primitive* p = it ? it->prim() : nullptr;
        if (!p || p->kind != PrimKind::Brush) continue;
        const auto chamfered = chamferAllEdges(brushFromPrimitive(*p), size);
        if (!chamfered) continue;
        auto prim = makeBrushPrimitive(*chamfered, doc.eol());
        if (!prim) continue;
        edits.push_back({r.entity, r.item, std::move(prim)});
    }
    if (edits.empty()) return 0;
    if (!doc.editPrimitives(edits, "Chamfer Edges")) return 0;
    return edits.size();
}

std::optional<ConvexBrush> bridgeFaces(const Primitive& a, size_t faceA, const Primitive& b, size_t faceB) {
    if (a.kind != PrimKind::Brush || b.kind != PrimKind::Brush || faceA >= a.faces.size() || faceB >= b.faces.size()) return std::nullopt;
    const std::vector<Winding> wa = faceWindingsOf(a), wb = faceWindingsOf(b);
    if (faceA >= wa.size() || faceB >= wb.size() || wa[faceA].size() < 3 || wb[faceB].size() < 3) return std::nullopt;
    std::vector<Vec3> points(wa[faceA].begin(), wa[faceA].end());
    points.insert(points.end(), wb[faceB].begin(), wb[faceB].end());
    const std::vector<Plane> planes = convexHullPlanes(points, 0.5);
    if (planes.size() < 4) return std::nullopt;
    ConvexBrush out;
    for (const Plane& pl : planes) out.push_back(BrushFaceSpec{pl, a.faces[faceA].suffix});
    if (!isValidBrush(out)) return std::nullopt;
    return out;
}

bool bridgeSelectedFaces(Document& doc) {
    if (doc.faceSelection().size() != 2) return false;
    const FaceRef fa = *doc.faceSelection().begin(), fb = *std::next(doc.faceSelection().begin());
    const EntityItem* ia = doc.findItem(fa.entity, fa.item);
    const EntityItem* ib = doc.findItem(fb.entity, fb.item);
    const Primitive* pa = ia ? ia->prim() : nullptr;
    const Primitive* pb = ib ? ib->prim() : nullptr;
    if (!pa || !pb) return false;
    const auto brush = bridgeFaces(*pa, fa.face, *pb, fb.face);
    if (!brush) return false;
    auto prim = makeBrushPrimitive(*brush, doc.eol());
    if (!prim) return false;
    return doc.editPrimitives({{fa.entity, kNoId, std::move(prim)}}, "Bridge Faces");
}

bool insetSelectedFace(Document& doc, const FaceRef& face, double inset, double depth) {
    const EntityItem* it = doc.findItem(face.entity, face.item);
    const Primitive* p = it ? it->prim() : nullptr;
    if (!p || p->kind != PrimKind::Brush || face.face >= p->faces.size()) return false;

    // brushFromPrimitive skips faces without a plane, so map the Primitive face index to the brush face index.
    size_t brushIndex = 0;
    bool found = false;
    for (size_t i = 0; i < p->faces.size(); ++i) {
        if (!facePlane(*p, i)) continue;
        if (i == face.face) {
            found = true;
            break;
        }
        ++brushIndex;
    }
    if (!found) return false;

    const auto pieces = insetFace(brushFromPrimitive(*p), brushIndex, inset, depth);
    if (pieces.empty()) return false;
    std::vector<Document::PrimEdit> edits;
    for (size_t k = 0; k < pieces.size(); ++k) {
        auto prim = makeBrushPrimitive(pieces[k], doc.eol());
        if (!prim) return false;
        // The first piece replaces the brush; the others are new primitives in the same entity.
        edits.push_back({face.entity, k == 0 ? face.item : kNoId, std::move(prim)});
    }
    return doc.editPrimitives(edits, "Inset Face");
}

}  // namespace iw8r
