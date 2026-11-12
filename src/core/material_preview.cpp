// IW8 Radiant Suite - software preview of one custom material (see material_preview.h).
#include "material_preview.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace iw8r {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;
constexpr double kViewHalf = 1.3;    // half the view width in object units (a sphere has radius 1)
constexpr double kCubeHalf = 0.8;    // cube half size
constexpr double kEyeDistance = 10;  // orthographic: rays start this far along +Y

struct Vec3 {
    double x, y, z;
};

Vec3 add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 scale(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Vec3 normalize(Vec3 a) {
    const double l = std::sqrt(dot(a, a));
    return l > 1e-12 ? scale(a, 1.0 / l) : Vec3{0, 0, 1};
}

double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Row-major 3 x 3 matrix applied to column vectors.
struct Mat3 {
    double m[3][3];
};

Mat3 mul(const Mat3& a, const Mat3& b) {
    Mat3 r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
    return r;
}

Vec3 apply(const Mat3& a, Vec3 v) {
    return {a.m[0][0] * v.x + a.m[0][1] * v.y + a.m[0][2] * v.z, a.m[1][0] * v.x + a.m[1][1] * v.y + a.m[1][2] * v.z,
            a.m[2][0] * v.x + a.m[2][1] * v.y + a.m[2][2] * v.z};
}

Vec3 applyTranspose(const Mat3& a, Vec3 v) {
    return {a.m[0][0] * v.x + a.m[1][0] * v.y + a.m[2][0] * v.z, a.m[0][1] * v.x + a.m[1][1] * v.y + a.m[2][1] * v.z,
            a.m[0][2] * v.x + a.m[1][2] * v.y + a.m[2][2] * v.z};
}

Mat3 rotZ(double rad) {
    const double c = std::cos(rad), s = std::sin(rad);
    return {{{c, -s, 0}, {s, c, 0}, {0, 0, 1}}};
}

Mat3 rotX(double rad) {
    const double c = std::cos(rad), s = std::sin(rad);
    return {{{1, 0, 0}, {0, c, -s}, {0, s, c}}};
}

double srgbToLinear(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }

double linearToSrgb(double c) {
    c = clampd(c, 0.0, 1.0);
    return c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
}

uint8_t toByte(double v01) {
    const long v = std::lround(clampd(v01, 0.0, 1.0) * 255.0);
    return static_cast<uint8_t>(std::clamp(v, 0L, 255L));
}

bool hasImage(const PreviewImage& img) {
    return img.width > 0 && img.height > 0 &&
           img.rgba.size() >= static_cast<size_t>(img.width) * static_cast<size_t>(img.height) * 4;
}

// Background colour (sRGB 0..1) at a pixel position in 0..size.
void backgroundColor(int kind, double px, double py, int size, double out[3]) {
    if (kind == 1) {
        out[0] = 40 / 255.0, out[1] = 42 / 255.0, out[2] = 46 / 255.0;
        return;
    }
    if (kind == 2) {
        const double t = clampd(py / size, 0.0, 1.0);  // 0 at the top
        const double top[3] = {120, 150, 190}, bottom[3] = {70, 60, 50};
        for (int c = 0; c < 3; ++c) out[c] = (top[c] * (1 - t) + bottom[c] * t) / 255.0;
        return;
    }
    const int cell = std::max(4, size / 8);
    const bool on = ((static_cast<int>(px) / cell + static_cast<int>(py) / cell) % 2) == 0;
    const double a[3] = {70, 72, 78}, b[3] = {50, 52, 58};
    for (int i = 0; i < 3; ++i) out[i] = (on ? a[i] : b[i]) / 255.0;
}

// Ray against the shape in object space (the ray direction is unit length). Returns the object-space hit point and
// the outward normal of the geometric surface.
bool intersectShape(PreviewShape shape, Vec3 o, Vec3 d, double& t, Vec3& p, Vec3& n) {
    if (shape == PreviewShape::Sphere) {
        const double b = 2 * dot(o, d);
        const double c = dot(o, o) - 1;
        const double disc = b * b - 4 * c;
        if (disc < 0) return false;
        t = (-b - std::sqrt(disc)) * 0.5;
        if (t <= 0) return false;
        p = add(o, scale(d, t));
        n = p;
        return true;
    }
    if (shape == PreviewShape::Plane) {
        // Plane y = 0, spanning x and z in [-1, 1]. Its geometric normal is +Y (front when the ray comes from +Y).
        if (std::fabs(d.y) < 1e-12) return false;
        t = -o.y / d.y;
        if (t <= 0) return false;
        p = add(o, scale(d, t));
        if (std::fabs(p.x) > 1 || std::fabs(p.z) > 1) return false;
        n = {0, 1, 0};
        return true;
    }
    // Cube: slab test.
    const double h = kCubeHalf;
    const double ov[3] = {o.x, o.y, o.z}, dv[3] = {d.x, d.y, d.z};
    double tNear = -1e30, tFar = 1e30;
    int nearAxis = 0;
    for (int k = 0; k < 3; ++k) {
        if (std::fabs(dv[k]) < 1e-12) {
            if (std::fabs(ov[k]) > h) return false;
            continue;
        }
        double t0 = (-h - ov[k]) / dv[k], t1 = (h - ov[k]) / dv[k];
        if (t0 > t1) std::swap(t0, t1);
        if (t0 > tNear) tNear = t0, nearAxis = k;
        tFar = std::min(tFar, t1);
    }
    if (tNear > tFar || tFar <= 0 || tNear <= 0) return false;
    t = tNear;
    p = add(o, scale(d, t));
    n = {0, 0, 0};
    const double sgn = dv[nearAxis] > 0 ? -1.0 : 1.0;  // the face the ray enters
    if (nearAxis == 0) n.x = sgn;
    if (nearAxis == 1) n.y = sgn;
    if (nearAxis == 2) n.z = sgn;
    return true;
}

struct Context {
    const MaterialPreviewInput* in = nullptr;
    int size = 0;
    Mat3 M{};   // object to world (world to object is the transpose)
    Vec3 L{};   // unit, towards the sun
    Vec3 V{};   // unit, towards the eye
};

// Shades one sample. Returns false when the sample shows the background (missed, or cut out by the alpha test).
// On true: col = sRGB colour 0..1, cov = texture alpha 0..1.
bool shadeSample(const Context& cx, double px, double py, double col[3], double& cov) {
    const MaterialPreviewInput& in = *cx.in;
    const double ndx = px / cx.size * 2 - 1, ndy = py / cx.size * 2 - 1;
    const Vec3 ow{-ndx * kViewHalf, kEyeDistance, -ndy * kViewHalf};  // screen right is -X, screen up is +Z
    const Vec3 dw{0, -1, 0};
    const Vec3 o = applyTranspose(cx.M, ow), d = applyTranspose(cx.M, dw);

    double t = 0;
    Vec3 p{}, n{};
    if (!intersectShape(in.shape, o, d, t, p, n)) return false;
    if (in.shape == PreviewShape::Plane && dot(n, d) > 0) {  // back face
        if (!in.twoSided) return false;
        n = scale(n, -1);
    }

    // Tangent frame (object space). B = cross(N, T) and the image's up direction is +B, the same frame as ps_tex.
    double u = 0, v = 0;
    Vec3 T{}, B{};
    if (in.shape == PreviewShape::Sphere) {
        u = 0.5 + std::atan2(p.y, p.x) / (2 * kPi);
        v = std::acos(clampd(p.z, -1.0, 1.0)) / kPi;
        const Vec3 horiz{-p.y, p.x, 0};
        T = std::sqrt(dot(horiz, horiz)) > 1e-9 ? normalize(horiz) : Vec3{1, 0, 0};
        B = cross(n, T);
    } else {
        const double half = in.shape == PreviewShape::Plane ? 1.0 : kCubeHalf;
        const Vec3 U = std::fabs(n.z) > 0.9 ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
        B = normalize(add(U, scale(n, -dot(U, n))));
        T = cross(B, n);
        u = 0.5 + dot(p, T) / (2 * half);
        v = 0.5 - dot(p, B) / (2 * half);
    }
    const double su = u * in.uvRepeat, sv = v * in.uvRepeat;

    double tex[4];
    samplePreviewImage(in.color, su, sv, tex);
    if (in.alphaTest && tex[3] < 0.5) return false;
    cov = tex[3];

    // Normal map: the converter's IW3 slope layout (normal_pack.h): slopeX = 4.08 R/255 - 2.08, slopeY = 4.0645 G/255 -
    // 2.0645, normal = normalize(slopeX, slopeY, 1). Flat when there is no map.
    Vec3 nn = n;
    if (hasImage(in.normal)) {
        double nt[4];
        samplePreviewImage(in.normal, su, sv, nt);
        const double sx = clampd(4.08 * nt[0] - 2.08, -2.0, 2.0);
        const double sy = clampd(4.0645 * nt[1] - 2.0645, -2.0, 2.0);
        nn = normalize(add(add(scale(T, sx), scale(B, sy)), n));
    }
    const Vec3 Nw = normalize(apply(cx.M, nn));

    double alb[3];
    for (int c = 0; c < 3; ++c) alb[c] = srgbToLinear(tex[c]) * in.tint[c];

    double lit[3];
    if (in.unlit) {
        for (int c = 0; c < 3; ++c) lit[c] = alb[c];
    } else {
        const double ndl = std::max(0.0, dot(Nw, cx.L));
        const double ndv = std::max(1e-3, dot(Nw, cx.V));
        double f0[3] = {0.04, 0.04, 0.04};
        double gloss = 0.3;
        if (hasImage(in.specular)) {
            double sp[4];
            samplePreviewImage(in.specular, su, sv, sp);
            for (int c = 0; c < 3; ++c) f0[c] = srgbToLinear(sp[c]);
            gloss = sp[3];
        }
        // GGX with roughness = 1 - gloss. alpha = roughness^2 (Disney), k = (roughness + 1)^2 / 8 for Smith.
        const double rough = clampd(1.0 - gloss, 0.05, 1.0);
        const double alpha = rough * rough, alpha2 = alpha * alpha;
        const Vec3 H = normalize(add(cx.L, cx.V));
        const double nh = std::max(0.0, dot(Nw, H)), vh = std::max(0.0, dot(cx.V, H));
        const double den = nh * nh * (alpha2 - 1) + 1;
        const double D = alpha2 / (kPi * den * den);
        const double k = (rough + 1) * (rough + 1) / 8;
        auto G1 = [k](double x) { return x / (x * (1 - k) + k); };
        const double G = G1(ndl) * G1(ndv);
        const double fres = std::pow(1 - vh, 5.0);
        for (int c = 0; c < 3; ++c) {
            const double F = f0[c] + (1 - f0[c]) * fres;
            const double spec = in.sunColor[c] * D * F * G / (4 * ndv);
            lit[c] = alb[c] * (in.ambient[c] + in.sunColor[c] * ndl) + spec;
        }
    }

    for (int c = 0; c < 3; ++c) {
        double x = lit[c] * in.exposure;
        if (in.emission > 1) {
            x *= in.emission;
            x = x / (1 + x);
        }
        col[c] = linearToSrgb(x);
    }
    return true;
}

}  // namespace

void samplePreviewImage(const PreviewImage& img, double u, double v, double out[4]) {
    if (!hasImage(img)) {
        out[0] = out[1] = out[2] = out[3] = 1;
        return;
    }
    const int w = img.width, h = img.height;
    const double fx = u * w - 0.5, fy = v * h - 0.5;
    const double x0f = std::floor(fx), y0f = std::floor(fy);
    const double tx = fx - x0f, ty = fy - y0f;
    auto wrapIndex = [](long long i, int n) {
        long long r = i % n;
        if (r < 0) r += n;
        return static_cast<int>(r);
    };
    const long long xi = static_cast<long long>(x0f), yi = static_cast<long long>(y0f);
    const int x0 = wrapIndex(xi, w), x1 = wrapIndex(xi + 1, w);
    const int y0 = wrapIndex(yi, h), y1 = wrapIndex(yi + 1, h);
    const uint8_t* p00 = &img.rgba[(static_cast<size_t>(y0) * w + x0) * 4];
    const uint8_t* p10 = &img.rgba[(static_cast<size_t>(y0) * w + x1) * 4];
    const uint8_t* p01 = &img.rgba[(static_cast<size_t>(y1) * w + x0) * 4];
    const uint8_t* p11 = &img.rgba[(static_cast<size_t>(y1) * w + x1) * 4];
    for (int c = 0; c < 4; ++c) {
        const double top = p00[c] * (1 - tx) + p10[c] * tx;
        const double bot = p01[c] * (1 - tx) + p11[c] * tx;
        out[c] = (top * (1 - ty) + bot * ty) / 255.0;
    }
}

PreviewImage renderMaterialPreview(const MaterialPreviewInput& in, int size) {
    PreviewImage out;
    if (size <= 0) return out;
    out.width = out.height = size;
    out.rgba.assign(static_cast<size_t>(size) * size * 4, 255);

    Context cx;
    cx.in = &in;
    cx.size = size;
    cx.M = mul(rotX(in.pitchDeg * kDeg), rotZ(in.yawDeg * kDeg));
    const double sy = in.sunYawDeg * kDeg, sp = in.sunPitchDeg * kDeg;
    // Sun direction: yaw 0 is in front of the eye, positive yaw moves it to the left; pitch is the elevation.
    cx.L = normalize({std::sin(sy) * std::cos(sp), std::cos(sy) * std::cos(sp), std::sin(sp)});
    cx.V = {0, 1, 0};

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            double acc[3] = {0, 0, 0};
            for (int sj = 0; sj < 2; ++sj) {
                for (int si = 0; si < 2; ++si) {
                    const double px = x + 0.25 + 0.5 * si, py = y + 0.25 + 0.5 * sj;
                    double bg[3];
                    backgroundColor(in.background, px, py, size, bg);
                    double col[3] = {bg[0], bg[1], bg[2]};
                    double cov = 1;
                    if (shadeSample(cx, px, py, col, cov)) {
                        double res[3];
                        for (int c = 0; c < 3; ++c) {
                            if (in.blendMode == 1)
                                res[c] = std::min(1.0, bg[c] + col[c]);
                            else if (in.blendMode == 2)
                                res[c] = bg[c] * col[c];
                            else if (in.blend)
                                res[c] = bg[c] * (1 - cov) + col[c] * cov;
                            else
                                res[c] = col[c];
                        }
                        for (int c = 0; c < 3; ++c) col[c] = res[c];
                    }
                    for (int c = 0; c < 3; ++c) acc[c] += col[c];
                }
            }
            const size_t o = (static_cast<size_t>(y) * size + x) * 4;
            for (int c = 0; c < 3; ++c) out.rgba[o + c] = toByte(acc[c] / 4);
        }
    }
    return out;
}

}  // namespace iw8r
