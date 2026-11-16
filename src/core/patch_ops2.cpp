// IW8 Radiant - second batch of Radiant patch tools (see patch_ops2.h).
#include "patch_ops2.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace iw8r {

namespace {

constexpr double kCapEps = 1e-4;     // ring closure tolerance, map units
constexpr double kNoiseCell = 64.0;  // map units per noise lattice cell

size_t idx(const PatchGrid& g, int r, int c) {
    return static_cast<size_t>(r) * static_cast<size_t>(g.cols) + static_cast<size_t>(c);
}

bool validGrid(const PatchGrid& g) {
    return g.rows >= 1 && g.cols >= 1 && g.pos.size() == static_cast<size_t>(g.rows) * static_cast<size_t>(g.cols);
}

// Copy with st sized to pos (missing texture coordinates are zero).
PatchGrid normalized(PatchGrid g) {
    g.st.resize(g.pos.size(), {0.0, 0.0});
    return g;
}

PatchGrid makeGrid(int rows, int cols, const std::string& keyword, const std::string& material) {
    PatchGrid g;
    g.rows = rows;
    g.cols = cols;
    g.keyword = keyword;
    g.material = material;
    g.pos.assign(static_cast<size_t>(rows) * static_cast<size_t>(cols), Vec3{});
    g.st.assign(g.pos.size(), {0.0, 0.0});
    return g;
}

// Inclusive row and column ranges.
PatchGrid subGrid(const PatchGrid& g, int r0, int r1, int c0, int c1) {
    PatchGrid out = makeGrid(r1 - r0 + 1, c1 - c0 + 1, g.keyword, g.material);
    for (int r = r0; r <= r1; ++r)
        for (int c = c0; c <= c1; ++c) {
            out.pos[idx(out, r - r0, c - c0)] = g.pos[idx(g, r, c)];
            out.st[idx(out, r - r0, c - c0)] = g.st[idx(g, r, c)];
        }
    return out;
}

Vec3 pointAt(const PatchGrid& g, int r, int c) { return g.pos[idx(g, r, c)]; }

// Sum of the tessellator's triangle cross products over every cell: the grid's facing.
Vec3 facingSum(const PatchGrid& g) {
    Vec3 n;
    for (int i = 0; i + 1 < g.rows; ++i)
        for (int j = 0; j + 1 < g.cols; ++j) {
            const Vec3 a = pointAt(g, i, j), b = pointAt(g, i, j + 1);
            const Vec3 c = pointAt(g, i + 1, j), d = pointAt(g, i + 1, j + 1);
            n += cross(b - a, c - a) + cross(d - b, c - b);
        }
    return n;
}

// Index reorderings used to bring a shared edge into a fixed frame. Each one is its own inverse, so an
// operation list is undone by applying its reverse.
enum class Op { Transpose, Invert, FlipRows };

PatchGrid flipRows(const PatchGrid& g) {
    PatchGrid out = normalized(g);
    for (int r = 0; r < g.rows; ++r)
        for (int c = 0; c < g.cols; ++c) {
            out.pos[idx(out, r, c)] = g.pos[idx(g, g.rows - 1 - r, c)];
            out.st[idx(out, r, c)] = g.st[idx(g, g.rows - 1 - r, c)];
        }
    return out;
}

PatchGrid applyOps(const PatchGrid& g, const std::vector<Op>& ops) {
    PatchGrid out = normalized(g);
    for (Op op : ops) {
        switch (op) {
            case Op::Transpose: out = transposePatch(out); break;
            case Op::Invert: out = invertPatch(out); break;
            case Op::FlipRows: out = flipRows(out); break;
        }
    }
    return out;
}

std::vector<Op> inverseOps(std::vector<Op> ops) {
    std::reverse(ops.begin(), ops.end());
    return ops;
}

// Sides: 0 first column, 1 last column, 2 first row, 3 last row. Each list moves that side to the last column
// (toLastCol) or to the first column (toFirstCol), with the shared points still ascending down the rows.
std::vector<Op> toLastCol(int side) {
    switch (side) {
        case 0: return {Op::Invert};
        case 1: return {};
        case 2: return {Op::Transpose, Op::Invert};
        default: return {Op::Transpose};
    }
}

std::vector<Op> toFirstCol(int side) {
    switch (side) {
        case 0: return {};
        case 1: return {Op::Invert};
        case 2: return {Op::Transpose};
        default: return {Op::Transpose, Op::Invert};
    }
}

// Even spacing by arc length along one line of points. The two end points stay where they are.
void resampleEven(std::vector<Vec3>& pts, std::vector<std::array<double, 2>>& st) {
    const size_t n = pts.size();
    if (n < 3) return;
    std::vector<double> s(n, 0.0);
    for (size_t k = 1; k < n; ++k) s[k] = s[k - 1] + length(pts[k] - pts[k - 1]);
    const double total = s[n - 1];
    if (!(total > 0)) return;
    std::vector<Vec3> np = pts;
    auto ns = st;
    size_t j = 0;
    for (size_t k = 1; k + 1 < n; ++k) {
        const double t = total * static_cast<double>(k) / static_cast<double>(n - 1);
        while (j + 2 < n && s[j + 1] < t) ++j;
        const double seg = s[j + 1] - s[j];
        const double f = seg > 0 ? std::clamp((t - s[j]) / seg, 0.0, 1.0) : 0.0;
        np[k] = pts[j] + (pts[j + 1] - pts[j]) * f;
        ns[k] = {st[j][0] + (st[j + 1][0] - st[j][0]) * f, st[j][1] + (st[j + 1][1] - st[j][1]) * f};
    }
    pts = np;
    st = ns;
}

// Deterministic integer hash for lattice values (splitmix64 finaliser).
uint64_t mix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

// Lattice value in [-1, 1) for an integer cell, seed and channel salt.
double latticeValue(int64_t ix, int64_t iy, int64_t iz, uint32_t seed, uint32_t salt) {
    uint64_t h = mix64((static_cast<uint64_t>(seed) << 32) ^ salt);
    h = mix64(h ^ static_cast<uint64_t>(ix));
    h = mix64(h ^ static_cast<uint64_t>(iy) * 0x100000001B3ULL);
    h = mix64(h ^ static_cast<uint64_t>(iz) * 0xC2B2AE3D27D4EB4FULL);
    const double u = static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);  // [0, 1)
    return 2.0 * u - 1.0;
}

// Trilinear value noise with a smoothstep fade. The weights sum to one, so the result stays in [-1, 1].
double valueNoise(double x, double y, double z, uint32_t seed, uint32_t salt) {
    const double fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
    const int64_t ix = static_cast<int64_t>(fx), iy = static_cast<int64_t>(fy), iz = static_cast<int64_t>(fz);
    auto fade = [](double t) { return t * t * (3.0 - 2.0 * t); };
    const double u = fade(x - fx), v = fade(y - fy), w = fade(z - fz);
    double acc = 0.0;
    for (int k = 0; k < 8; ++k) {
        const int dx = k & 1, dy = (k >> 1) & 1, dz = (k >> 2) & 1;
        const double wx = dx ? u : 1.0 - u, wy = dy ? v : 1.0 - v, wz = dz ? w : 1.0 - w;
        acc += wx * wy * wz * latticeValue(ix + dx, iy + dy, iz + dz, seed, salt);
    }
    return acc;
}

// raiseLower falloff: (1 - t^2)^2 for XY distance d inside radius, zero at and beyond it.
double brushFalloff(double d, double radius) {
    if (!(d < radius)) return 0.0;
    const double t = d / radius;
    const double f = 1.0 - t * t;
    return f * f;
}

}  // namespace

std::vector<PatchGrid> capPatch(const PatchGrid& in) {
    const PatchGrid g = normalized(in);
    if (!validGrid(g) || g.rows < 2 || g.cols < 2) return {};

    Vec3 body;
    for (const Vec3& p : g.pos) body += p;
    body = body * (1.0 / static_cast<double>(g.pos.size()));

    std::vector<PatchGrid> caps;
    for (int end = 0; end < 2; ++end) {
        const int ringRow = end == 0 ? 0 : g.rows - 1;
        std::vector<Vec3> ring;
        for (int c = 0; c < g.cols; ++c) ring.push_back(pointAt(g, ringRow, c));

        // A closed ring repeats its first point at the end, so the centre averages the distinct points only.
        const bool closed = g.cols > 2 && length(ring.front() - ring.back()) <= kCapEps;
        const int distinct = closed ? g.cols - 1 : g.cols;
        Vec3 centre;
        for (int c = 0; c < distinct; ++c) centre += ring[static_cast<size_t>(c)];
        centre = centre * (1.0 / static_cast<double>(distinct));

        PatchGrid cap = makeGrid(3, g.cols, "mesh", g.material);
        for (int c = 0; c < g.cols; ++c) {
            cap.pos[idx(cap, 0, c)] = ring[static_cast<size_t>(c)];
            cap.pos[idx(cap, 1, c)] = (ring[static_cast<size_t>(c)] + centre) * 0.5;
            cap.pos[idx(cap, 2, c)] = centre;
        }
        cap = naturalTexture(cap);

        // Face away from the body: the cap's facing must point along (ring centre - body centre).
        const Vec3 outward = centre - body;
        if (dot(facingSum(cap), outward) < 0) cap = invertPatch(cap);
        caps.push_back(cap);
    }
    return caps;
}

std::optional<PatchGrid> weldPatches(const PatchGrid& a0, const PatchGrid& b0, double tol) {
    const PatchGrid a = normalized(a0);
    const PatchGrid b = normalized(b0);
    if (!validGrid(a) || !validGrid(b) || a.rows < 2 || a.cols < 2 || b.rows < 2 || b.cols < 2) return std::nullopt;
    if (a.keyword != b.keyword || tol < 0) return std::nullopt;

    for (int sa = 0; sa < 4; ++sa) {
        const std::vector<Op> opsA = toLastCol(sa);
        const PatchGrid ap = applyOps(a, opsA);  // shared edge is the last column of ap
        for (int sb = 0; sb < 4; ++sb) {
            PatchGrid bp = applyOps(b, toFirstCol(sb));  // shared edge is the first column of bp
            if (ap.rows != bp.rows) continue;

            bool same = true, reversed = true;
            for (int i = 0; i < ap.rows; ++i) {
                const Vec3 pa = pointAt(ap, i, ap.cols - 1);
                if (length(pa - pointAt(bp, i, 0)) > tol) same = false;
                if (length(pa - pointAt(bp, ap.rows - 1 - i, 0)) > tol) reversed = false;
            }
            if (!same && !reversed) continue;
            if (!same) bp = flipRows(bp);

            // Joined grid in ap's frame: all of ap, then bp without its shared first column.
            PatchGrid joined = makeGrid(ap.rows, ap.cols + bp.cols - 1, a.keyword, a.material);
            for (int r = 0; r < ap.rows; ++r) {
                for (int c = 0; c < ap.cols; ++c) {
                    joined.pos[idx(joined, r, c)] = pointAt(ap, r, c);
                    joined.st[idx(joined, r, c)] = ap.st[idx(ap, r, c)];
                }
                for (int c = 1; c < bp.cols; ++c) {
                    const size_t dst = idx(joined, r, ap.cols - 1 + c);
                    joined.pos[dst] = pointAt(bp, r, c);
                    joined.st[dst] = bp.st[idx(bp, r, c)];
                }
            }
            PatchGrid out = applyOps(joined, inverseOps(opsA));
            if (dot(facingSum(out), facingSum(a)) < 0) out = invertPatch(out);
            return out;
        }
    }
    return std::nullopt;
}

PatchGrid redisperse(const PatchGrid& in, bool rows, bool cols) {
    PatchGrid g = normalized(in);
    if (!validGrid(g)) return g;
    if (rows) {
        for (int r = 0; r < g.rows; ++r) {
            std::vector<Vec3> pts;
            std::vector<std::array<double, 2>> st;
            for (int c = 0; c < g.cols; ++c) {
                pts.push_back(pointAt(g, r, c));
                st.push_back(g.st[idx(g, r, c)]);
            }
            resampleEven(pts, st);
            for (int c = 0; c < g.cols; ++c) {
                g.pos[idx(g, r, c)] = pts[static_cast<size_t>(c)];
                g.st[idx(g, r, c)] = st[static_cast<size_t>(c)];
            }
        }
    }
    if (cols) {
        for (int c = 0; c < g.cols; ++c) {
            std::vector<Vec3> pts;
            std::vector<std::array<double, 2>> st;
            for (int r = 0; r < g.rows; ++r) {
                pts.push_back(pointAt(g, r, c));
                st.push_back(g.st[idx(g, r, c)]);
            }
            resampleEven(pts, st);
            for (int r = 0; r < g.rows; ++r) {
                g.pos[idx(g, r, c)] = pts[static_cast<size_t>(r)];
                g.st[idx(g, r, c)] = st[static_cast<size_t>(r)];
            }
        }
    }
    return g;
}

std::vector<PatchGrid> splitPatch(const PatchGrid& in, bool alongRows, int at) {
    const PatchGrid g = normalized(in);
    if (!validGrid(g) || g.rows < 3 || g.cols < 3) return {};
    const int n = alongRows ? g.rows : g.cols;
    // Both pieces must have an odd count: the shared index is even and the total is odd.
    if (n % 2 == 0 || at < 2 || at % 2 != 0 || at > n - 3) return {};
    if (alongRows) return {subGrid(g, 0, at, 0, g.cols - 1), subGrid(g, at, g.rows - 1, 0, g.cols - 1)};
    return {subGrid(g, 0, g.rows - 1, 0, at), subGrid(g, 0, g.rows - 1, at, g.cols - 1)};
}

PatchGrid makeTerrainPatch(const AABB& box, int rows, int cols, const std::string& material) {
    PatchGrid g = makeFlatPatch(box, 2, rows, cols, material);  // rows == 0 when the inputs are invalid
    for (Vec3& p : g.pos) p.z = box.mins.z;
    return g;
}

PatchGrid applyNoise(const PatchGrid& in, double amplitude, uint32_t seed, bool zOnly) {
    PatchGrid g = normalized(in);
    if (!validGrid(g) || !std::isfinite(amplitude) || amplitude == 0.0) return g;
    for (Vec3& p : g.pos) {
        const double x = p.x / kNoiseCell, y = p.y / kNoiseCell, z = p.z / kNoiseCell;
        if (zOnly) {
            p.z += amplitude * valueNoise(x, y, 0.0, seed, 3);
        } else {
            p.x += amplitude * valueNoise(x, y, z, seed, 1);
            p.y += amplitude * valueNoise(x, y, z, seed, 2);
            p.z += amplitude * valueNoise(x, y, z, seed, 3);
        }
    }
    return g;
}

PatchGrid smoothPatch(const PatchGrid& in, int iterations) {
    PatchGrid g = normalized(in);
    if (!validGrid(g) || iterations <= 0 || g.rows < 3 || g.cols < 3) return g;
    for (int it = 0; it < iterations; ++it) {
        std::vector<double> z(g.pos.size());
        for (size_t k = 0; k < z.size(); ++k) z[k] = g.pos[k].z;
        std::vector<double> next = z;
        for (int r = 1; r + 1 < g.rows; ++r)
            for (int c = 1; c + 1 < g.cols; ++c)
                next[idx(g, r, c)] = 0.25 * (z[idx(g, r - 1, c)] + z[idx(g, r + 1, c)] + z[idx(g, r, c - 1)] +
                                             z[idx(g, r, c + 1)]);
        for (size_t k = 0; k < z.size(); ++k) g.pos[k].z = next[k];
    }
    return g;
}

PatchGrid raiseLower(const PatchGrid& in, const Vec3& center, double radius, double amount) {
    PatchGrid g = normalized(in);
    if (!validGrid(g) || !(radius > 0) || amount == 0.0) return g;
    for (Vec3& p : g.pos) {
        const double d = std::hypot(p.x - center.x, p.y - center.y);
        if (d >= radius) continue;
        const double t = d / radius;
        const double falloff = 1.0 - t * t;
        p.z += amount * falloff * falloff;
    }
    return g;
}

PatchGrid smoothAround(const PatchGrid& in, const Vec3& center, double radius, double strength) {
    PatchGrid g = normalized(in);
    if (!validGrid(g) || !(radius > 0) || !std::isfinite(radius) || !(strength > 0)) return g;
    const double s = std::min(strength, 1.0);
    std::vector<double> z(g.pos.size());
    for (size_t k = 0; k < z.size(); ++k) z[k] = g.pos[k].z;
    for (int r = 0; r < g.rows; ++r)
        for (int c = 0; c < g.cols; ++c) {
            Vec3& p = g.pos[idx(g, r, c)];
            const double f = brushFalloff(std::hypot(p.x - center.x, p.y - center.y), radius);
            if (f == 0.0) continue;
            double sum = 0.0;
            int n = 0;
            const int dr[4] = {-1, 1, 0, 0}, dc[4] = {0, 0, -1, 1};
            for (int k = 0; k < 4; ++k) {
                const int nr = r + dr[k], nc = c + dc[k];
                if (nr < 0 || nr >= g.rows || nc < 0 || nc >= g.cols) continue;
                sum += z[idx(g, nr, nc)];
                ++n;
            }
            if (n == 0) continue;  // a 1x1 grid has no neighbours
            const double zi = z[idx(g, r, c)];
            p.z = zi + s * f * (sum / n - zi);
        }
    return g;
}

PatchGrid flattenAround(const PatchGrid& in, const Vec3& center, double radius, double targetZ, double strength) {
    PatchGrid g = normalized(in);
    if (!validGrid(g) || !(radius > 0) || !std::isfinite(radius) || !std::isfinite(targetZ) || !(strength > 0)) return g;
    const double s = std::min(strength, 1.0);
    for (Vec3& p : g.pos) {
        const double f = brushFalloff(std::hypot(p.x - center.x, p.y - center.y), radius);
        if (f == 0.0) continue;
        p.z += s * f * (targetZ - p.z);
    }
    return g;
}

PatchGrid noiseAround(const PatchGrid& in, const Vec3& center, double radius, double amplitude, uint32_t seed) {
    PatchGrid g = normalized(in);
    if (!validGrid(g) || !(radius > 0) || !std::isfinite(radius) || !std::isfinite(amplitude) || amplitude == 0.0)
        return g;
    for (Vec3& p : g.pos) {
        const double f = brushFalloff(std::hypot(p.x - center.x, p.y - center.y), radius);
        if (f == 0.0) continue;
        p.z += amplitude * f * valueNoise(p.x / kNoiseCell, p.y / kNoiseCell, 0.0, seed, 3);
    }
    return g;
}

}  // namespace iw8r
