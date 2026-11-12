#include "uv_unwrap.h"

#include <cmath>
#include <deque>

#include "brush_ops.h"

namespace iw8r {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Projected coordinates (a, b) of a point for a face normal: faceUV with width = height = 1, no rotation or offset.
Uv projected(const Vec3& normal, const Vec3& p) {
    TexParams unit;
    unit.width = unit.height = 1;
    return faceUV(normal, unit, p);
}

// Least squares for y = P a + Q b + R over the samples. Returns false when singular.
bool solve3(const std::vector<Uv>& ab, const std::vector<double>& y, double& P, double& Q, double& R) {
    double m[3][4] = {};
    for (size_t i = 0; i < ab.size(); ++i) {
        const double r[3] = {ab[i][0], ab[i][1], 1.0};
        for (int a = 0; a < 3; ++a) {
            for (int b = 0; b < 3; ++b) m[a][b] += r[a] * r[b];
            m[a][3] += r[a] * y[i];
        }
    }
    for (int c = 0; c < 3; ++c) {
        int piv = c;
        for (int r = c + 1; r < 3; ++r)
            if (std::fabs(m[r][c]) > std::fabs(m[piv][c])) piv = r;
        if (std::fabs(m[piv][c]) < 1e-12) return false;
        for (int k = 0; k < 4; ++k) std::swap(m[c][k], m[piv][k]);
        for (int r = 0; r < 3; ++r) {
            if (r == c) continue;
            const double f = m[r][c] / m[c][c];
            for (int k = 0; k < 4; ++k) m[r][k] -= f * m[c][k];
        }
    }
    P = m[0][3] / m[0][0], Q = m[1][3] / m[1][1], R = m[2][3] / m[2][2];
    return true;
}

}  // namespace

TexParams fitTexParams(const Vec3& normal, const std::vector<Vec3>& points, const std::vector<Uv>& targets,
                       const TexParams& fallback) {
    if (points.size() < 2 || points.size() != targets.size()) return fallback;
    std::vector<Uv> ab;
    std::vector<double> us, vs;
    for (size_t i = 0; i < points.size(); ++i) {
        ab.push_back(projected(normal, points[i]));
        us.push_back(targets[i][0]);
        vs.push_back(targets[i][1]);
    }
    // u = a cos/w - b sin/w + xoff/w ; v = a sin/h + b cos/h + yoff/h  (tex_projection.cpp faceUV).
    double P, Q, R, X, Y, Z;
    if (!solve3(ab, us, P, Q, R) || !solve3(ab, vs, X, Y, Z)) {
        // Two points (or collinear): fall back to a uniform scale + rotation fit from the first two.
        if (points.size() < 2) return fallback;
        const double da = ab[1][0] - ab[0][0], db = ab[1][1] - ab[0][1];
        const double du = targets[1][0] - targets[0][0], dv = targets[1][1] - targets[0][1];
        const double len = std::hypot(da, db), tlen = std::hypot(du, dv);
        if (len < 1e-9 || tlen < 1e-12) return fallback;
        TexParams tp = fallback;
        tp.rot = (std::atan2(dv, du) - std::atan2(db, da)) * 180.0 / kPi;
        tp.width = tp.height = len / tlen;
        const TexParams zero{tp.width, tp.height, 0, 0, tp.rot};
        const Uv z = faceUV(normal, zero, points[0]);
        tp.xoff = (targets[0][0] - z[0]) * tp.width;
        tp.yoff = (targets[0][1] - z[1]) * tp.height;
        return tp;
    }
    // (P, Q) = (cos, -sin) / w and (X, Y) = (sin, cos) / h. A mirrored layout (det < 0) takes a negative height.
    const double det = P * Y - Q * X;
    const double sv = det < 0 ? -1.0 : 1.0;
    const double angU = std::atan2(-Q, P), angV = std::atan2(sv * X, sv * Y);
    const double mu = std::hypot(P, Q), mv = std::hypot(X, Y);
    if (mu < 1e-12 || mv < 1e-12) return fallback;
    // Circular mean of the two angle estimates, weighted by their magnitudes.
    const double ang = std::atan2(mu * std::sin(angU) + mv * std::sin(angV), mu * std::cos(angU) + mv * std::cos(angV));
    TexParams tp;
    tp.rot = ang * 180.0 / kPi;
    tp.width = 1.0 / mu;
    tp.height = sv / mv;
    // Offsets: the mean residual with the rotation and scales fixed.
    TexParams zero = tp;
    zero.xoff = zero.yoff = 0;
    double ru = 0, rv = 0;
    for (size_t i = 0; i < points.size(); ++i) {
        const Uv z = faceUV(normal, zero, points[i]);
        ru += targets[i][0] - z[0];
        rv += targets[i][1] - z[1];
    }
    tp.xoff = ru / double(points.size()) * tp.width;
    tp.yoff = rv / double(points.size()) * tp.height;
    return tp;
}

std::map<size_t, std::vector<Uv>> unfoldBrush(const Primitive& p, size_t root) {
    std::map<size_t, std::vector<Uv>> out;
    const std::vector<Winding> w = faceWindingsOf(p);
    if (root >= w.size() || w[root].size() < 3) return out;
    auto same = [](const Vec3& a, const Vec3& b) { return length(a - b) < 0.01; };
    // Local 2D frame of a face: origin at vertex 0, x along its first edge, y = normal x x (counter-clockwise).
    auto local = [&](size_t f) {
        std::vector<Uv> pts;
        const Winding& wf = w[f];
        const auto pl = facePlane(p, f);
        const Vec3 n = pl ? pl->n : normalize(cross(wf[1] - wf[0], wf[2] - wf[0]));
        const Vec3 ex = normalize(wf[1] - wf[0]);
        const Vec3 ey = cross(n, ex);
        for (const Vec3& v : wf) pts.push_back({dot(v - wf[0], ex), dot(v - wf[0], ey)});
        return pts;
    };
    out[root] = local(root);
    std::deque<size_t> queue{root};
    while (!queue.empty()) {
        const size_t f = queue.front();
        queue.pop_front();
        const Winding& wf = w[f];
        for (size_t i = 0; i < wf.size(); ++i) {
            const Vec3& A = wf[i];
            const Vec3& B = wf[(i + 1) % wf.size()];
            for (size_t g = 0; g < w.size(); ++g) {
                if (out.count(g) || w[g].size() < 3) continue;
                int ia = -1, ib = -1;
                for (size_t k = 0; k < w[g].size(); ++k) {
                    if (same(w[g][k], A)) ia = int(k);
                    if (same(w[g][k], B)) ib = int(k);
                }
                if (ia < 0 || ib < 0) continue;
                // Rigid 2D transform taking g's local edge (A, B) onto f's placed edge (A, B).
                const std::vector<Uv> lg = local(g);
                const Uv pa = out[f][i], pb = out[f][(i + 1) % wf.size()];
                const Uv la = lg[size_t(ia)], lb = lg[size_t(ib)];
                const double angT = std::atan2(pb[1] - pa[1], pb[0] - pa[0]), angL = std::atan2(lb[1] - la[1], lb[0] - la[0]);
                const double r = angT - angL, c = std::cos(r), s = std::sin(r);
                std::vector<Uv> placed;
                for (const Uv& q : lg) {
                    const double x = q[0] - la[0], y = q[1] - la[1];
                    placed.push_back({pa[0] + x * c - y * s, pa[1] + x * s + y * c});
                }
                out[g] = std::move(placed);
                queue.push_back(g);
            }
        }
    }
    return out;
}

std::map<size_t, TexParams> unwrapBrush(const Primitive& p, size_t root) {
    std::map<size_t, TexParams> out;
    const std::vector<Winding> w = faceWindingsOf(p);
    const auto net = unfoldBrush(p, root);
    if (!net.count(root)) return out;
    const auto rootPlane = facePlane(p, root);
    const TexParams rootTp = texParamsOfFace(p, root).value_or(TexParams{});
    if (!rootPlane) return out;
    // Map net coordinates to UV with the affine map that reproduces the root face's current UVs exactly.
    std::vector<Uv> rootUv;
    for (const Vec3& v : w[root]) rootUv.push_back(faceUV(rootPlane->n, rootTp, v));
    const std::vector<Uv>& rn = net.at(root);
    // Solve u = P x + Q y + R, v = X x + Y y + Z from the root face (exact for its planar projection).
    double P = 0, Q = 0, R = 0, X = 0, Y = 0, Z = 0;
    std::vector<double> us, vs;
    for (const Uv& t : rootUv) us.push_back(t[0]), vs.push_back(t[1]);
    if (!solve3(rn, us, P, Q, R) || !solve3(rn, vs, X, Y, Z)) return out;
    for (const auto& [f, pts] : net) {
        const auto pl = facePlane(p, f);
        if (!pl) continue;
        std::vector<Uv> targets;
        for (const Uv& q : pts) targets.push_back({P * q[0] + Q * q[1] + R, X * q[0] + Y * q[1] + Z});
        out[f] = fitTexParams(pl->n, w[f], targets, texParamsOfFace(p, f).value_or(TexParams{}));
    }
    out[root] = rootTp;
    return out;
}

}  // namespace iw8r
