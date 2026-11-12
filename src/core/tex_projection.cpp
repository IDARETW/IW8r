// IW8 Radiant Suite - shared Radiant/Quake texture projection (see tex_projection.h).
#include "tex_projection.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace iw8r {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDefaultTexSize = 128.0;  // compile_iw8.cpp kDefaultTexSize: width/height 0 or missing
constexpr double kNaturalTexSize = 64.0;   // naturalTexture(): most common N1/N2 in the corpus
constexpr double kShearLimit = 1e-3;       // |cos| between the uv axes above which a lock is refused

struct Axes {
    Vec3 s, t;
};

// Base axes for the dominant normal component (copy of compile_iw8.cpp dominantAxes).
Axes dominantAxes(const Vec3& n) {
    const double ax = std::fabs(n.x), ay = std::fabs(n.y), az = std::fabs(n.z);
    if (az >= ax && az >= ay) return {{1, 0, 0}, {0, -1, 0}};
    if (ax >= ay) return {{0, 1, 0}, {0, 0, -1}};
    return {{1, 0, 0}, {0, 0, -1}};
}

// Base axes rotated by `rot` degrees: s' = s cos - t sin, t' = s sin + t cos.
// Dot products with s' and t' give S and T of the compiler's projectPoint.
Axes rotatedAxes(const Axes& ax, double rot) {
    const double rad = rot * kPi / 180.0;
    const double c = std::cos(rad), sn = std::sin(rad);
    return {ax.s * c - ax.t * sn, ax.s * sn + ax.t * c};
}

// Width or height of 0 means the default size, as in the compiler.
TexParams effective(TexParams tp) {
    if (std::fabs(tp.width) < 1e-9) tp.width = kDefaultTexSize;
    if (std::fabs(tp.height) < 1e-9) tp.height = kDefaultTexSize;
    return tp;
}

// Tangent = g projected into the face plane and normalized (compile_iw8.cpp projectPoint).
Vec3 tangentOf(const Vec3& n, const Vec3& g) {
    Vec3 tan = g - n * dot(g, n);
    if (length(tan) < 1e-9) {
        const Vec3 r = std::fabs(n.x) < 0.9 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
        tan = r - n * dot(r, n);
    }
    return normalize(tan);
}

struct Extent {
    double umin = std::numeric_limits<double>::infinity(), umax = -std::numeric_limits<double>::infinity();
    double vmin = std::numeric_limits<double>::infinity(), vmax = -std::numeric_limits<double>::infinity();
};

// uv extremes over the winding. An affine function of position attains them at the vertices.
Extent uvExtent(const Vec3& normal, const TexParams& tp, const std::vector<Vec3>& winding) {
    Extent e;
    for (const Vec3& p : winding) {
        const auto uv = faceUV(normal, tp, p);
        e.umin = std::min(e.umin, uv[0]);
        e.umax = std::max(e.umax, uv[0]);
        e.vmin = std::min(e.vmin, uv[1]);
        e.vmax = std::max(e.vmax, uv[1]);
    }
    return e;
}

}  // namespace

std::optional<TexParams> texParamsOfFace(const Primitive& p, size_t face) {
    const auto f = p.textureFields(face);
    if (!f) return std::nullopt;
    TexParams tp;
    tp.width = (*f)[0];
    tp.height = (*f)[1];
    tp.xoff = (*f)[2];
    tp.yoff = (*f)[3];
    tp.rot = (*f)[4];
    return tp;
}

TexBasis texBasis(const Vec3& normal, const TexParams& tp) {
    const Axes r = rotatedAxes(dominantAxes(normalize(normal)), tp.rot);
    return {r.s, r.t};
}

std::array<double, 2> faceUV(const Vec3& normal, const TexParams& tp0, const Vec3& point) {
    const TexParams tp = effective(tp0);
    const Vec3 n = normalize(normal);
    const Axes ax = dominantAxes(n);
    const double rad = tp.rot * kPi / 180.0;
    const double c = std::cos(rad), sn = std::sin(rad);
    const double a = dot(point, ax.s), b = dot(point, ax.t);
    const double S = a * c - b * sn;
    const double T = a * sn + b * c;
    return {S / tp.width + tp.xoff / tp.width, T / tp.height + tp.yoff / tp.height};
}

Vec3 faceTangent(const Vec3& normal, const TexParams& tp) {
    const Vec3 n = normalize(normal);
    return tangentOf(n, rotatedAxes(dominantAxes(n), tp.rot).s);
}

std::optional<TexParams> lockTexture(const Vec3& oldNormal, const TexParams& oldTp, const std::array<Vec3, 3>& oldPoints,
                                     const std::function<Vec3(const Vec3&)>& xf) {
    if (!xf) return std::nullopt;
    const TexParams oldE = effective(oldTp);
    const Vec3 n0 = normalize(oldNormal);
    if (length(n0) < 0.5) return std::nullopt;

    // Linear part of xf (xf is affine): column k is xf(e_k) - xf(0).
    const Vec3 o = xf(Vec3{});
    const Vec3 c0 = xf(Vec3{1, 0, 0}) - o, c1 = xf(Vec3{0, 1, 0}) - o, c2 = xf(Vec3{0, 0, 1}) - o;
    const double det = dot(c0, cross(c1, c2));
    if (!(std::fabs(det) > 1e-12)) return std::nullopt;

    // Normal transforms by the inverse transpose L^-T n. Its numerator is (n.x c1xc2 + n.y c2xc0 + n.z c0xc1);
    // dividing by det flips it when the transform mirrors.
    Vec3 num = cross(c1, c2) * n0.x + cross(c2, c0) * n0.y + cross(c0, c1) * n0.z;
    if (det < 0) num = -num;
    const Vec3 n1 = normalize(num);
    if (length(n1) < 0.5) return std::nullopt;

    // New positions, and the old uv of each point (the targets to keep).
    std::array<Vec3, 3> q;
    std::array<std::array<double, 2>, 3> target;
    for (size_t i = 0; i < 3; ++i) {
        q[i] = xf(oldPoints[i]);
        target[i] = faceUV(n0, oldE, oldPoints[i]);
    }

    // Plane coordinates of the new points in the new face's unrotated dominant-axis basis.
    const Axes ax1 = dominantAxes(n1);
    double a[3], b[3];
    for (size_t i = 0; i < 3; ++i) {
        a[i] = dot(q[i], ax1.s);
        b[i] = dot(q[i], ax1.t);
    }

    // Fit uv = J (a, b) + K for u and for v, from the three points.
    const double d1a = a[1] - a[0], d1b = b[1] - b[0], d2a = a[2] - a[0], d2b = b[2] - b[0];
    const double det2 = d1a * d2b - d1b * d2a;
    const double scale = std::hypot(d1a, d1b) * std::hypot(d2a, d2b);
    if (!(scale > 0) || std::fabs(det2) <= 1e-9 * scale) return std::nullopt;
    double J[2][2], K[2];
    for (size_t k = 0; k < 2; ++k) {
        const double r1 = target[1][k] - target[0][k], r2 = target[2][k] - target[0][k];
        J[k][0] = (r1 * d2b - d1b * r2) / det2;
        J[k][1] = (d1a * r2 - r1 * d2a) / det2;
        K[k] = target[0][k] - (J[k][0] * a[0] + J[k][1] * b[0]);
    }

    // J = diag(1/w, 1/h) R(rot): the rows are orthogonal (no shear) with norms 1/|w| and 1/|h|.
    const double ru = std::hypot(J[0][0], J[0][1]), rv = std::hypot(J[1][0], J[1][1]);
    if (!(ru > 1e-12) || !(rv > 1e-12)) return std::nullopt;
    if (std::fabs(J[0][0] * J[1][0] + J[0][1] * J[1][1]) / (ru * rv) > kShearLimit) return std::nullopt;

    const double detJ = J[0][0] * J[1][1] - J[0][1] * J[1][0];  // = w * h
    const double sw = oldE.width < 0 ? -1.0 : 1.0;              // width keeps its sign
    const double w = sw / ru;
    const double h = (detJ < 0 ? -1.0 : 1.0) * sw / rv;
    const double c = w * J[0][0], sn = -w * J[0][1];            // R(rot) = (c, -sn; sn, c)
    if (std::fabs(h * J[1][0] - sn) + std::fabs(h * J[1][1] - c) > 1e-6) return std::nullopt;

    TexParams out;
    out.width = std::fabs(w - oldE.width) <= 1e-9 * std::fabs(oldE.width) ? oldE.width : w;
    out.height = std::fabs(h - oldE.height) <= 1e-9 * std::fabs(oldE.height) ? oldE.height : h;
    const double rotDeg = std::atan2(sn, c) * 180.0 / kPi;
    double dRot = std::fmod(rotDeg - oldTp.rot, 360.0);
    if (dRot > 180) dRot -= 360;
    if (dRot < -180) dRot += 360;
    out.rot = std::fabs(dRot) < 1e-7 ? oldTp.rot : rotDeg;
    // The constant term of uv is offset / size.
    out.xoff = w * K[0];
    out.yoff = h * K[1];
    return out;
}

TexParams fitTexture(const Vec3& normal, const TexParams& tp, const std::vector<Vec3>& winding, double timesU, double timesV) {
    if (winding.empty() || !(timesU > 0) || !(timesV > 0)) return tp;
    TexParams out = effective(tp);
    const Extent e = uvExtent(normal, out, winding);
    const double spanU = e.umax - e.umin, spanV = e.vmax - e.vmin;
    if (!(spanU > 1e-12) || !(spanV > 1e-12)) return tp;
    // The u span is S-span / width, so width * spanU / timesU makes it exactly timesU (same for v).
    out.width = out.width * spanU / timesU;
    out.height = out.height * spanV / timesV;
    // Offsets: shift so the minimum uv is 0.
    out.xoff = 0;
    out.yoff = 0;
    const Extent z = uvExtent(normal, out, winding);
    out.xoff = -z.umin * out.width;
    out.yoff = -z.vmin * out.height;
    return out;
}

TexParams naturalTexture(const TexParams& tp) {
    TexParams out = tp;
    out.width = kNaturalTexSize;
    out.height = kNaturalTexSize;
    return out;
}

TexParams justifyTexture(const Vec3& normal, const TexParams& tp, const std::vector<Vec3>& winding, Justify j) {
    if (winding.empty()) return tp;
    TexParams out = effective(tp);
    const Extent e = uvExtent(normal, out, winding);
    double du = 0, dv = 0;
    switch (j) {
        case Justify::Left: du = std::round(e.umin) - e.umin; break;
        case Justify::Right: du = std::round(e.umax) - e.umax; break;
        case Justify::Top: dv = std::round(e.vmin) - e.vmin; break;
        case Justify::Bottom: dv = std::round(e.vmax) - e.vmax; break;
        case Justify::Center: {
            const double cu = 0.5 * (e.umin + e.umax), cv = 0.5 * (e.vmin + e.vmax);
            du = std::round(cu) - cu;
            dv = std::round(cv) - cv;
            break;
        }
    }
    // A change of du in uv is a change of xoff / width (same for v).
    out.xoff += du * out.width;
    out.yoff += dv * out.height;
    return out;
}

bool applyTexParams(Primitive& p, size_t face, const TexParams& tp) {
    if (face >= p.faces.size()) return false;
    Primitive::TexFields f;
    f.width = tp.width;
    f.height = tp.height;
    f.xoff = tp.xoff;
    f.yoff = tp.yoff;
    f.rot = tp.rot;
    return p.setTextureFields(f, static_cast<int>(face));
}

size_t lockPrimitiveTextures(const Primitive& before, Primitive& after, const std::function<Vec3(const Vec3&)>& xf) {
    if (before.kind != PrimKind::Brush || after.kind != PrimKind::Brush || before.faces.size() != after.faces.size()) return 0;
    size_t locked = 0;
    for (size_t i = 0; i < before.faces.size(); ++i) {
        const auto tp = texParamsOfFace(before, i);
        const auto pl = planeFromPoints(before.faces[i].p[0], before.faces[i].p[1], before.faces[i].p[2]);
        if (!tp || !pl) continue;
        const std::array<Vec3, 3> pts = {before.faces[i].p[0], before.faces[i].p[1], before.faces[i].p[2]};
        if (auto ntp = lockTexture(pl->n, *tp, pts, xf)) {
            applyTexParams(after, i, *ntp);
            ++locked;
        }
    }
    return locked;
}

}  // namespace iw8r
