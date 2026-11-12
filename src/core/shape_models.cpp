#include "shape_models.h"

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "geometry.h"

namespace iw8r {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSize = 64;  // units across
constexpr double kR = kSize / 2;

struct V {
    Vec3 p, n;  // Z-up map units
    double u = 0, v = 0;
};

struct Mesh {
    std::vector<V> verts;
    std::vector<uint32_t> idx;  // triangles, CCW seen from outside (Z-up)

    uint32_t add(const Vec3& p, const Vec3& n, double u, double v) {
        verts.push_back({p, n, u, v});
        return uint32_t(verts.size() - 1);
    }
    void tri(uint32_t a, uint32_t b, uint32_t c) { idx.insert(idx.end(), {a, b, c}); }
    // Quad a b c d counter-clockwise from outside.
    void quad(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
        tri(a, b, c);
        tri(a, c, d);
    }
    // A flat polygon face: corners counter-clockwise from outside, its own normal and planar UVs.
    void face(const std::vector<Vec3>& c) {
        const Vec3 n = normalize(cross(c[1] - c[0], c[2] - c[0]));
        // Planar UV on the two axes the normal is least aligned with.
        const int drop = std::fabs(n.z) >= std::fabs(n.x) && std::fabs(n.z) >= std::fabs(n.y) ? 2 : std::fabs(n.y) >= std::fabs(n.x) ? 1 : 0;
        const int ua = drop == 0 ? 1 : 0, va = drop == 2 ? 1 : 2;
        const uint32_t base = uint32_t(verts.size());
        for (const Vec3& p : c) add(p, n, p[ua] / kSize, p[va] / kSize);
        for (size_t k = 1; k + 1 < c.size(); ++k) tri(base, base + uint32_t(k), base + uint32_t(k + 1));
    }
};

// Surface of revolution around Z: profile points (radius, z) from bottom to top, `seg` segments; smooth normals from the
// profile tangent. Caps are separate faces.
void lathe(Mesh& m, const std::vector<std::pair<double, double>>& prof, int seg) {
    const size_t rows = prof.size();
    std::vector<uint32_t> ring(rows * size_t(seg + 1));
    for (size_t i = 0; i < rows; ++i) {
        // Profile normal: perpendicular to the neighbouring segment (outward).
        const auto& a = prof[i > 0 ? i - 1 : i];
        const auto& b = prof[i + 1 < rows ? i + 1 : i];
        const double dr = b.first - a.first, dz = b.second - a.second;
        double nr = dz, nz = -dr;
        const double len = std::hypot(nr, nz);
        if (len > 0) nr /= len, nz /= len;
        for (int s = 0; s <= seg; ++s) {
            const double t = 2 * kPi * s / seg;
            const double c = std::cos(t), sn = std::sin(t);
            ring[i * size_t(seg + 1) + size_t(s)] =
                m.add({prof[i].first * c, prof[i].first * sn, prof[i].second}, normalize(Vec3{nr * c, nr * sn, nz}), double(s) / seg * 4,
                      prof[i].second / kSize);
        }
    }
    for (size_t i = 0; i + 1 < rows; ++i)
        for (int s = 0; s < seg; ++s) {
            const uint32_t a = ring[i * size_t(seg + 1) + size_t(s)], b = ring[i * size_t(seg + 1) + size_t(s + 1)];
            const uint32_t c = ring[(i + 1) * size_t(seg + 1) + size_t(s + 1)], d = ring[(i + 1) * size_t(seg + 1) + size_t(s)];
            if (prof[i].first < 1e-9) m.tri(a, c, d);            // apex at the bottom
            else if (prof[i + 1].first < 1e-9) m.tri(a, b, c);   // apex at the top
            else m.quad(a, b, c, d);
        }
}

void cap(Mesh& m, double r, double z, int seg, bool up) {
    std::vector<Vec3> c;
    for (int s = 0; s < seg; ++s) {
        const double t = 2 * kPi * (up ? s : seg - s) / seg;
        c.push_back({r * std::cos(t), r * std::sin(t), z});
    }
    m.face(c);
}

Mesh build(ShapeKind k) {
    Mesh m;
    const double s = kSize, h = kR;
    switch (k) {
        case ShapeKind::Cube:
        case ShapeKind::Wedge: {
            const bool wedge = k == ShapeKind::Wedge;
            const Vec3 p000{-h, -h, 0}, p100{h, -h, 0}, p110{h, h, 0}, p010{-h, h, 0};
            const Vec3 p001{-h, -h, s}, p101{h, -h, wedge ? 0.0 : s}, p111{h, h, wedge ? 0.0 : s}, p011{-h, h, s};
            m.face({p000, p010, p110, p100});  // bottom
            if (!wedge) {
                m.face({p001, p101, p111, p011});  // top
                m.face({p100, p110, p111, p101});  // +x
            } else {
                m.face({p001, p101, p111, p011});  // slope (high at -x)
            }
            m.face({p000, p001, p011, p010});  // -x
            if (wedge) {
                m.face({p000, p100, p001});  // -y triangle
                m.face({p010, p011, p110});  // +y triangle
            } else {
                m.face({p000, p100, p101, p001});  // -y
                m.face({p010, p011, p111, p110});  // +y
            }
            break;
        }
        case ShapeKind::Pyramid: {
            const Vec3 a{-h, -h, 0}, b{h, -h, 0}, c{h, h, 0}, d{-h, h, 0}, top{0, 0, s};
            m.face({a, d, c, b});
            m.face({a, b, top});
            m.face({b, c, top});
            m.face({c, d, top});
            m.face({d, a, top});
            break;
        }
        case ShapeKind::Plane:
            m.face({{-h, -h, 0}, {h, -h, 0}, {h, h, 0}, {-h, h, 0}});
            break;
        case ShapeKind::Sphere: {
            std::vector<std::pair<double, double>> prof;
            const int rings = 12;
            for (int i = 0; i <= rings; ++i) {
                const double a = -kPi / 2 + kPi * i / rings;
                prof.push_back({kR * std::cos(a), kR + kR * std::sin(a)});
            }
            prof.front().first = prof.back().first = 0;
            lathe(m, prof, 24);
            break;
        }
        case ShapeKind::Capsule: {
            std::vector<std::pair<double, double>> prof;
            const double r = kR / 2, mid = s - 2 * r;
            for (int i = 0; i <= 6; ++i) {
                const double a = -kPi / 2 + (kPi / 2) * i / 6;
                prof.push_back({r * std::cos(a), r + r * std::sin(a)});
            }
            for (int i = 0; i <= 6; ++i) {
                const double a = (kPi / 2) * i / 6;
                prof.push_back({r * std::cos(a), r + mid + r * std::sin(a)});
            }
            prof.front().first = prof.back().first = 0;
            lathe(m, prof, 20);
            break;
        }
        case ShapeKind::Cylinder:
            lathe(m, {{kR, 0}, {kR, s}}, 24);
            cap(m, kR, 0, 24, false);
            cap(m, kR, s, 24, true);
            break;
        case ShapeKind::Cone:
            lathe(m, {{kR, 0}, {0, s}}, 24);
            cap(m, kR, 0, 24, false);
            break;
        case ShapeKind::Torus: {
            const double R = kR * 0.75, r = kR * 0.25;
            const int seg = 32, sides = 12;
            std::vector<uint32_t> grid(size_t(seg + 1) * size_t(sides + 1));
            for (int i = 0; i <= seg; ++i)
                for (int j = 0; j <= sides; ++j) {
                    const double a = 2 * kPi * i / seg, b = 2 * kPi * j / sides;
                    const Vec3 n{std::cos(b) * std::cos(a), std::cos(b) * std::sin(a), std::sin(b)};
                    const Vec3 p{(R + r * std::cos(b)) * std::cos(a), (R + r * std::cos(b)) * std::sin(a), r + r * std::sin(b)};
                    grid[size_t(i) * size_t(sides + 1) + size_t(j)] = m.add(p, n, double(i) / seg * 4, double(j) / sides);
                }
            for (int i = 0; i < seg; ++i)
                for (int j = 0; j < sides; ++j) {
                    const auto at = [&](int a, int b) { return grid[size_t(a) * size_t(sides + 1) + size_t(b)]; };
                    m.quad(at(i, j), at(i + 1, j), at(i + 1, j + 1), at(i, j + 1));
                }
            break;
        }
    }
    return m;
}

uint32_t fnv(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) h = (h ^ c) * 16777619u;
    return h;
}

std::string num(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.5g", std::fabs(v) < 1e-9 ? 0.0 : v);
    return b;
}

}  // namespace

const std::vector<ShapeInfo>& shapeKinds() {
    static const std::vector<ShapeInfo> k = {
        {ShapeKind::Cube, "cube", "Cube"},       {ShapeKind::Sphere, "sphere", "Sphere"}, {ShapeKind::Cylinder, "cylinder", "Cylinder"},
        {ShapeKind::Cone, "cone", "Cone"},       {ShapeKind::Torus, "torus", "Torus"},    {ShapeKind::Capsule, "capsule", "Capsule"},
        {ShapeKind::Plane, "plane", "Plane"},    {ShapeKind::Wedge, "wedge", "Wedge"},    {ShapeKind::Pyramid, "pyramid", "Pyramid"},
    };
    return k;
}

std::string shapeModelName(ShapeKind kind, const std::string& material) {
    const char* id = "shape";
    for (const auto& s : shapeKinds())
        if (s.kind == kind) id = s.id;
    char hash[16];
    std::snprintf(hash, sizeof hash, "%08x", fnv(material));
    return std::string("iw8r_shape_") + id + "_" + hash;
}

std::string shapeObj(ShapeKind kind, const std::string& material) {
    const Mesh m = build(kind);
    // OBJ is Y-up: obj (x, y, z) = map (x, z, -y); the editor and compiler map it back with (x, -z, y).
    std::string o = "# IW8 Radiant shape model (64 units, origin at the base centre)\n";
    for (const V& v : m.verts) o += "v " + num(v.p.x) + " " + num(v.p.z) + " " + num(-v.p.y) + "\n";
    for (const V& v : m.verts) o += "vt " + num(v.u) + " " + num(v.v) + "\n";
    for (const V& v : m.verts) o += "vn " + num(v.n.x) + " " + num(v.n.z) + " " + num(-v.n.y) + "\n";
    o += "usemtl " + material + "\n";
    for (size_t i = 0; i + 2 < m.idx.size(); i += 3) {
        o += "f";
        for (int k = 0; k < 3; ++k) {
            const std::string n = std::to_string(m.idx[i + size_t(k)] + 1);
            o += " " + n + "/" + n + "/" + n;
        }
        o += "\n";
    }
    return o;
}

}  // namespace iw8r
