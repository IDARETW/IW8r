// IW8 Radiant - terrain height-map import/export, erosion and slope-based blend weights (see heightmap.h).
#include "heightmap.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <utility>

#include "custom_materials.h"

namespace iw8r {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSqrt2 = 1.41421356237309504880;

bool finite(double v) { return std::isfinite(v); }

bool validMap(const HeightMap& hm) {
    if (hm.w < 1 || hm.h < 1) return false;
    const size_t n = static_cast<size_t>(hm.w) * static_cast<size_t>(hm.h);
    if (hm.z.size() != n) return false;
    for (float v : hm.z)
        if (!std::isfinite(v)) return false;
    return true;
}

bool validGridShape(const PatchGrid& g) {
    if (g.rows < 1 || g.cols < 1) return false;
    return g.pos.size() == static_cast<size_t>(g.rows) * static_cast<size_t>(g.cols);
}

// Source coordinate (fractional index) of destination index i out of n, on a source axis with m samples.
// flip = true reverses the axis, which maps grid row 0 (min Y) to map row m - 1 (the south edge of the map).
// Equal sizes return the exact integer index so an unchanged size never picks up rounding.
double srcCoord(int i, int n, int m, bool flip) {
    if (m <= 1) return 0.0;
    if (n == m) return static_cast<double>(flip ? (m - 1 - i) : i);
    const double t = n <= 1 ? 0.5 : static_cast<double>(i) / (n - 1);
    return (flip ? 1.0 - t : t) * (m - 1);
}

// Bilinear sample at fractional (column, row) with the edges clamped.
double sampleBilinear(const HeightMap& hm, double fc, double fr) {
    fc = std::clamp(fc, 0.0, static_cast<double>(hm.w - 1));
    fr = std::clamp(fr, 0.0, static_cast<double>(hm.h - 1));
    const int c0 = static_cast<int>(std::floor(fc));
    const int r0 = static_cast<int>(std::floor(fr));
    const int c1 = std::min(c0 + 1, hm.w - 1);
    const int r1 = std::min(r0 + 1, hm.h - 1);
    const double tx = fc - c0, ty = fr - r0;
    auto at = [&](int r, int c) { return static_cast<double>(hm.z[static_cast<size_t>(r) * hm.w + c]); };
    const double top = at(r0, c0) * (1 - tx) + at(r0, c1) * tx;
    const double bot = at(r1, c0) * (1 - tx) + at(r1, c1) * tx;
    return top * (1 - ty) + bot * ty;
}

// The eight neighbours of a cell, with the distance between cell centres (1 or sqrt 2).
struct Step {
    int dr, dc;
    double dist;
};
constexpr Step kSteps[8] = {{-1, 0, 1.0},  {1, 0, 1.0},          {0, -1, 1.0},         {0, 1, 1.0},
                            {-1, -1, kSqrt2}, {-1, 1, kSqrt2}, {1, -1, kSqrt2}, {1, 1, kSqrt2}};

// splitmix64 mixed from (seed, step, cell). Integer-only, so it is the same on every compiler.
uint64_t mix(uint64_t seed, uint64_t step, uint64_t cell) {
    uint64_t x = seed * 0x9E3779B97F4A7C15ull + step * 0xBF58476D1CE4E5B9ull + cell * 0x94D049BB133111EBull;
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// Uniform in [0, 1) from the top 53 bits.
double unitFrom(uint64_t bits) { return static_cast<double>(bits >> 11) * (1.0 / 9007199254740992.0); }

// Reads a whole file. False when it cannot be opened.
bool readBytes(const std::string& path, std::vector<unsigned char>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

// Parses one unsigned decimal integer at pos, skipping PNM whitespace and '#' comments first.
bool pnmInt(const std::vector<unsigned char>& b, size_t& pos, long& value) {
    while (pos < b.size()) {
        if (b[pos] == '#') {
            while (pos < b.size() && b[pos] != '\n') ++pos;
        } else if (std::isspace(b[pos])) {
            ++pos;
        } else {
            break;
        }
    }
    if (pos >= b.size() || !std::isdigit(b[pos])) return false;
    long v = 0;
    while (pos < b.size() && std::isdigit(b[pos])) {
        v = v * 10 + (b[pos] - '0');
        if (v > 1000000) return false;  // far beyond any sane map size, also stops overflow
        ++pos;
    }
    value = v;
    return true;
}

bool readPgm(const std::vector<unsigned char>& b, HeightMap& out, std::string* err) {
    size_t pos = 2;  // past "P5"
    long w = 0, h = 0, maxval = 0;
    if (!pnmInt(b, pos, w) || !pnmInt(b, pos, h) || !pnmInt(b, pos, maxval)) {
        if (err) *err = "PGM header is incomplete";
        return false;
    }
    // Exactly one whitespace byte separates the header from the samples.
    if (pos >= b.size() || !std::isspace(b[pos])) {
        if (err) *err = "PGM header has no separator before the samples";
        return false;
    }
    ++pos;
    if (w < 1 || h < 1 || maxval < 1 || maxval > 65535) {
        if (err) *err = "PGM size or maxval is out of range";
        return false;
    }
    const size_t bytesPer = maxval < 256 ? 1 : 2;
    const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
    if (b.size() - pos < n * bytesPer) {
        if (err) *err = "PGM data is shorter than its header says";
        return false;
    }
    out.w = static_cast<int>(w);
    out.h = static_cast<int>(h);
    out.z.resize(n);
    const double inv = 1.0 / static_cast<double>(maxval);
    for (size_t i = 0; i < n; ++i) {
        unsigned long s = b[pos];
        if (bytesPer == 2) s = (s << 8) | b[pos + 1];
        pos += bytesPer;
        out.z[i] = static_cast<float>(std::min<double>(s, maxval) * inv);
    }
    return true;
}

}  // namespace

HeightMap heightMapFromPatch(const PatchGrid& g) {
    HeightMap hm;
    if (!validGridShape(g)) return hm;
    hm.w = g.cols;
    hm.h = g.rows;
    hm.z.resize(static_cast<size_t>(hm.w) * static_cast<size_t>(hm.h));
    for (int k = 0; k < hm.h; ++k) {
        const int gridRow = g.rows - 1 - k;  // map row 0 is the max-Y row of the patch
        for (int c = 0; c < hm.w; ++c)
            hm.z[static_cast<size_t>(k) * hm.w + c] =
                static_cast<float>(g.pos[static_cast<size_t>(gridRow) * g.cols + c].z);
    }
    return hm;
}

bool applyHeightMap(PatchGrid& g, const HeightMap& hm, double zMin, double zMax) {
    if (!validMap(hm) || !validGridShape(g) || !finite(zMin) || !finite(zMax) || zMax < zMin) return false;
    const auto [mnIt, mxIt] = std::minmax_element(hm.z.begin(), hm.z.end());
    const double lo = *mnIt, span = static_cast<double>(*mxIt) - lo;
    for (int r = 0; r < g.rows; ++r) {
        const double fr = srcCoord(r, g.rows, hm.h, true);
        for (int c = 0; c < g.cols; ++c) {
            const double fc = srcCoord(c, g.cols, hm.w, false);
            const double v = sampleBilinear(hm, fc, fr);
            // A flat map has no range to stretch, so it lands on zMin.
            const double t = span > 0 ? std::clamp((v - lo) / span, 0.0, 1.0) : 0.0;
            g.pos[static_cast<size_t>(r) * g.cols + c].z = zMin + t * (zMax - zMin);
        }
    }
    return true;
}

bool readHeightMapImage(const std::string& path, HeightMap& out, std::string* err) {
    std::vector<unsigned char> bytes;
    if (!readBytes(path, bytes)) {
        if (err) *err = "cannot open '" + path + "'";
        return false;
    }
    HeightMap hm;
    if (bytes.size() >= 2 && bytes[0] == 'P' && bytes[1] == '5') {
        if (!readPgm(bytes, hm, err)) return false;
        out = std::move(hm);
        return true;
    }
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    if (!decodeImageFile(path, w, h, rgba, err)) return false;
    hm.w = w;
    hm.h = h;
    hm.z.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
    for (size_t i = 0; i < hm.z.size(); ++i) {
        const uint8_t* p = &rgba[i * 4];
        hm.z[i] = static_cast<float>((0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]) / 255.0);
    }
    out = std::move(hm);
    return true;
}

bool writeHeightMapPgm(const std::string& path, const HeightMap& hm, double zMin, double zMax, std::string* err) {
    if (!validMap(hm) || !finite(zMin) || !finite(zMax) || zMax <= zMin) {
        if (err) *err = "height map or height range is invalid";
        return false;
    }
    std::string data = "P5\n" + std::to_string(hm.w) + " " + std::to_string(hm.h) + "\n65535\n";
    data.reserve(data.size() + hm.z.size() * 2);
    const double span = zMax - zMin;
    for (float zf : hm.z) {
        const double t = std::clamp((static_cast<double>(zf) - zMin) / span, 0.0, 1.0);
        const auto s = static_cast<uint16_t>(std::lround(t * 65535.0));
        data.push_back(static_cast<char>(s >> 8));  // big-endian, as PGM requires
        data.push_back(static_cast<char>(s & 0xFF));
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f || !f.write(data.data(), static_cast<std::streamsize>(data.size())) || !f.flush()) {
        if (err) *err = "cannot write '" + path + "'";
        return false;
    }
    return true;
}

void thermalErosion(HeightMap& hm, int iterations, double talus) {
    if (!validMap(hm) || iterations <= 0 || !finite(talus) || talus < 0) return;
    const size_t n = hm.z.size();
    std::vector<double> delta(n);
    for (int it = 0; it < iterations; ++it) {
        std::fill(delta.begin(), delta.end(), 0.0);
        for (int r = 0; r < hm.h; ++r) {
            for (int c = 0; c < hm.w; ++c) {
                const size_t i = static_cast<size_t>(r) * hm.w + c;
                // Only the steepest downhill neighbour takes material. Moving half the excess keeps the pair from
                // overshooting, which spreading to several neighbours at once could do.
                double bestExcess = 0;
                int best = -1;
                for (int k = 0; k < 8; ++k) {
                    const int nr = r + kSteps[k].dr, nc = c + kSteps[k].dc;
                    if (nr < 0 || nr >= hm.h || nc < 0 || nc >= hm.w) continue;
                    const size_t j = static_cast<size_t>(nr) * hm.w + nc;
                    const double drop = static_cast<double>(hm.z[i]) - hm.z[j];
                    const double excess = drop - talus * kSteps[k].dist;
                    if (excess > bestExcess) {
                        bestExcess = excess;
                        best = static_cast<int>(j);
                    }
                }
                if (best < 0) continue;
                const double moved = 0.5 * bestExcess;
                delta[i] -= moved;
                delta[static_cast<size_t>(best)] += moved;
            }
        }
        for (size_t i = 0; i < n; ++i) hm.z[i] = static_cast<float>(hm.z[i] + delta[i]);
    }
}

void hydraulicErosionSimple(HeightMap& hm, int iterations, double rain, double solubility, unsigned seed) {
    if (!validMap(hm) || iterations <= 0 || !finite(rain) || !finite(solubility) || rain <= 0 || solubility <= 0)
        return;
    const size_t n = hm.z.size();
    std::vector<double> delta(n);
    for (int it = 0; it < iterations; ++it) {
        std::fill(delta.begin(), delta.end(), 0.0);
        for (int r = 0; r < hm.h; ++r) {
            for (int c = 0; c < hm.w; ++c) {
                const size_t i = static_cast<size_t>(r) * hm.w + c;
                // Rain varies by cell and step (seeded), so the erosion is not perfectly uniform.
                const double water = rain * (0.5 + unitFrom(mix(seed, static_cast<uint64_t>(it), i)));
                double bestSlope = 0;
                int best = -1;
                double bestDrop = 0;
                for (int k = 0; k < 8; ++k) {
                    const int nr = r + kSteps[k].dr, nc = c + kSteps[k].dc;
                    if (nr < 0 || nr >= hm.h || nc < 0 || nc >= hm.w) continue;
                    const size_t j = static_cast<size_t>(nr) * hm.w + nc;
                    const double drop = static_cast<double>(hm.z[i]) - hm.z[j];
                    const double slope = drop / kSteps[k].dist;
                    if (slope > bestSlope) {
                        bestSlope = slope;
                        bestDrop = drop;
                        best = static_cast<int>(j);
                    }
                }
                if (best < 0) continue;  // a pit: nothing flows out
                // Material carried downhill; capped at half the drop so the water cannot dig past the next cell.
                const double moved = std::min(solubility * water * bestSlope, 0.5 * bestDrop);
                delta[i] -= moved;
                delta[static_cast<size_t>(best)] += moved;
            }
        }
        for (size_t i = 0; i < n; ++i) hm.z[i] = static_cast<float>(hm.z[i] + delta[i]);
    }
}

std::vector<float> slopeBlendAlpha(const PatchGrid& g, double startDeg, double endDeg) {
    if (!validGridShape(g) || !finite(startDeg) || !finite(endDeg) || endDeg <= startDeg) return {};
    std::vector<float> out(g.pos.size(), 0.0f);
    auto at = [&](int r, int c) -> const Vec3& { return g.pos[static_cast<size_t>(r) * g.cols + c]; };
    for (int r = 0; r < g.rows; ++r) {
        for (int c = 0; c < g.cols; ++c) {
            // Tangents from the neighbours (one-sided at the edges). Their cross product is the surface normal,
            // so this works for any orientation of the grid, not just one lying in XY.
            const int c0 = std::max(c - 1, 0), c1 = std::min(c + 1, g.cols - 1);
            const int r0 = std::max(r - 1, 0), r1 = std::min(r + 1, g.rows - 1);
            const Vec3 tu = at(r, c1) - at(r, c0);
            const Vec3 tv = at(r1, c) - at(r0, c);
            const Vec3 nrm{tu.y * tv.z - tu.z * tv.y, tu.z * tv.x - tu.x * tv.z, tu.x * tv.y - tu.y * tv.x};
            // Angle from +Z: atan2 keeps it stable for near-vertical walls.
            const double deg = std::atan2(std::hypot(nrm.x, nrm.y), std::fabs(nrm.z)) * 180.0 / kPi;
            const double t = std::clamp((deg - startDeg) / (endDeg - startDeg), 0.0, 1.0);
            out[static_cast<size_t>(r) * g.cols + c] = static_cast<float>(t);
        }
    }
    return out;
}

}  // namespace iw8r
