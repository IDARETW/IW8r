// IW8 Radiant Suite - texel density of brush faces (see texel_density.h).
#include "texel_density.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "brush_ops.h"
#include "tex_projection.h"

namespace iw8r {

namespace {

// Finite-difference step in world units. faceUV is linear in the point, so any step gives the same slope.
constexpr double kStep = 1.0;

}  // namespace

FaceDensity faceDensity(const Primitive& brush, size_t face, int textureWidthPx, int textureHeightPx, double lightmapScale) {
    FaceDensity out{0.0, 0.0, 0.0};
    if (brush.kind != PrimKind::Brush) return out;

    // Lightmap: the scale is taken as world units per texel when it is positive and finite.
    if (std::isfinite(lightmapScale) && lightmapScale > 0) out.lightmapUnitsPerTexel = lightmapScale;

    const std::vector<Winding> windings = faceWindingsOf(brush);
    if (face >= windings.size()) return out;
    const Winding& w = windings[face];
    if (w.size() < 3) return out;

    // Area of a planar polygon: half the length of the Newell vector.
    Vec3 newell{};
    for (size_t i = 0; i < w.size(); ++i) newell = newell + cross(w[i], w[(i + 1) % w.size()]);
    out.area = 0.5 * length(newell);
    if (!(out.area > 0)) return out;

    const auto pl = facePlane(brush, face);
    if (!pl) return out;
    const Vec3 n = normalize(pl->n);

    // Missing texture fields use the compiler's default TexParams (width and height 128).
    const TexParams tp = texParamsOfFace(brush, face).value_or(TexParams{});
    if (textureWidthPx <= 0 || textureHeightPx <= 0) return out;

    // In-plane unit directions: U is the face's +u tangent, V is perpendicular to it in the plane.
    const Vec3 dirU = normalize(faceTangent(n, tp));
    const Vec3 dirV = normalize(cross(n, dirU));
    const Vec3 p0 = w[0];
    const auto uv0 = faceUV(n, tp, p0);
    const auto uvU = faceUV(n, tp, p0 + dirU * kStep);
    const auto uvV = faceUV(n, tp, p0 + dirV * kStep);

    // Texture pixels per world unit along each direction: |d(u or v) / dl| times the texture size in pixels.
    const double pxU = std::fabs(uvU[0] - uv0[0]) / kStep * textureWidthPx;
    const double pxV = std::fabs(uvV[1] - uv0[1]) / kStep * textureHeightPx;
    if (pxU > 0 && pxV > 0) out.texturePxPerUnit = std::sqrt(pxU * pxV);
    return out;
}

std::array<float, 3> densityColor(double value, double target) {
    const std::array<float, 3> grey{0.5f, 0.5f, 0.5f};
    const std::array<float, 3> blue{0.f, 0.f, 1.f}, green{0.f, 1.f, 0.f}, red{1.f, 0.f, 0.f};
    if (!(target > 0) || !std::isfinite(target) || std::isnan(value)) return grey;

    const double ratio = value / target;
    if (ratio <= 0.5) return blue;
    if (ratio >= 2.0) return red;

    // ratio in (0.5, 2): t = log2(ratio) in (-1, 1). Blue at t = -1, green at 0, red at +1, linear in t.
    const double t = std::log2(ratio);
    std::array<float, 3> c{};
    if (t <= 0) {
        const float f = static_cast<float>(t + 1.0);  // 0 at blue, 1 at green
        for (size_t k = 0; k < 3; ++k) c[k] = blue[k] * (1.f - f) + green[k] * f;
    } else {
        const float f = static_cast<float>(t);  // 0 at green, 1 at red
        for (size_t k = 0; k < 3; ++k) c[k] = green[k] * (1.f - f) + red[k] * f;
    }
    return c;
}

DensityStats densityStats(const std::vector<std::pair<double, double>>& valueArea, double target) {
    DensityStats s{0.0, 0.0, 0.0, 0, 0};
    if (valueArea.empty()) return s;

    const double lo = target / 2.0, hi = target * 2.0;
    double mn = std::numeric_limits<double>::infinity(), mx = -std::numeric_limits<double>::infinity();
    double sumVA = 0, sumA = 0, sumV = 0;
    for (const auto& [v, a] : valueArea) {
        mn = std::min(mn, v);
        mx = std::max(mx, v);
        sumVA += v * a;
        sumA += a;
        sumV += v;
        if (v < lo || v > hi) ++s.outliers;
    }
    s.min = mn;
    s.max = mx;
    s.mean = sumA > 0 ? sumVA / sumA : sumV / static_cast<double>(valueArea.size());
    s.faces = static_cast<int>(valueArea.size());
    return s;
}

}  // namespace iw8r
