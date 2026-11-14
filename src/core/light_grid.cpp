#include "light_grid.h"
#include "ray_bvh.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <thread>

namespace iw8r {

namespace {

constexpr double kOrigin = 131072.0;
constexpr double kStepXY = 32.0, kStepZ = 64.0;
constexpr size_t kMaxEntries = 8'388'608;

void put16(std::string& s, uint32_t v) {
    s.push_back(char(v & 0xff));
    s.push_back(char((v >> 8) & 0xff));
}
void put32(std::string& s, uint32_t v) {
    put16(s, v & 0xffff);
    put16(s, v >> 16);
}

}  // namespace

std::string lightGridPartPath(const std::string& mapId, const std::string& part) {
    return "maps/mp/" + mapId + ".d3dbsp.lightgrid." + part + ".bin";
}

bool buildLightGrid(const LightGridInput& in, LightGridFiles& out, std::string* err) {
    out = {};
    if (!in.bounds.valid() || in.mapId.empty()) {
        if (err) *err = "no map bounds";
        return false;
    }
    // Lattice indices covering the bounds plus one cell (iw3_lightgrid.cpp: world = index * step - 131072).
    const int i0 = int(std::floor((in.bounds.mins.x - kStepXY + kOrigin) / kStepXY));
    const int i1 = int(std::ceil((in.bounds.maxs.x + kStepXY + kOrigin) / kStepXY));
    const int j0 = int(std::floor((in.bounds.mins.y - kStepXY + kOrigin) / kStepXY));
    const int j1 = int(std::ceil((in.bounds.maxs.y + kStepXY + kOrigin) / kStepXY));
    const int k0 = int(std::floor((in.bounds.mins.z - kStepZ + kOrigin) / kStepZ));
    const int k1 = int(std::ceil((in.bounds.maxs.z + kStepZ + kOrigin) / kStepZ));
    if (i0 < 0 || j0 < 0 || k0 < 0 || i1 > 65535 || j1 > 65535 || k1 > 65535) {
        if (err) *err = "map outside the light grid lattice";
        return false;
    }
    const int X = i1 - i0 + 1, Y = j1 - j0 + 1, Z = k1 - k0 + 1;
    if (Z > 255) {
        if (err) *err = "map taller than 255 light grid cells (16320 units)";
        return false;
    }
    const size_t box = size_t(X) * size_t(Y) * size_t(Z);
    if (box > kMaxEntries * 8) {
        if (err) *err = "map too large for a light grid (" + std::to_string(box) + " lattice points)";
        return false;
    }
    out.dims[0] = X, out.dims[1] = Y, out.dims[2] = Z;

    // Per column (x row r, y column c) the z range kept: [zLo, zLo + count). Full height without floor data.
    std::vector<uint8_t> zLo(size_t(X) * size_t(Y), 0), zCount(size_t(X) * size_t(Y), uint8_t(Z));
    if (in.floorsAt)
        for (int r = 0; r < X; ++r)
            for (int c = 0; c < Y; ++c) {
                const double x = (i0 + r) * kStepXY - kOrigin, y = (j0 + c) * kStepXY - kOrigin;
                const std::vector<double> floors = in.floorsAt(x, y);
                const size_t at = size_t(r) * size_t(Y) + size_t(c);
                if (floors.empty()) {
                    zCount[at] = 0;
                    continue;
                }
                const double lo = *std::min_element(floors.begin(), floors.end()) - in.bandBelow;
                const double hi = *std::max_element(floors.begin(), floors.end()) + in.bandAbove;
                const int a = std::clamp(int(std::floor((lo + kOrigin) / kStepZ)) - k0, 0, Z - 1);
                const int b = std::clamp(int(std::ceil((hi + kOrigin) / kStepZ)) - k0, a, Z - 1);
                zLo[at] = uint8_t(a);
                zCount[at] = uint8_t(b - a + 1);
            }
    size_t N = 0;
    for (uint8_t n : zCount) N += n;
    out.entryCount = N;

    // Palette (ROADMAP 5.20): with samplesAt, one colour per cell of the kept points, deduplicated; else one colour.
    const int cxy = std::max(1, in.paletteCellXY), cz = std::max(1, in.paletteCellZ);
    const int CX = (X + cxy - 1) / cxy, CY = (Y + cxy - 1) / cxy, CZ = (Z + cz - 1) / cz;
    std::vector<int32_t> cellColor;  // per cell: palette index (-1 = not used)
    std::vector<std::array<uint8_t, 168>> palette;
    if (in.samplesAt && N <= in.maxEntries) {
        // The cells that hold kept points, and for each the kept point nearest its centre.
        struct Cell {
            size_t index;
            Vec3 point;
            double best = 1e300;
        };
        std::vector<int64_t> cellAt(size_t(CX) * CY * CZ, -1);
        std::vector<Cell> cells;
        for (int r = 0; r < X; ++r)
            for (int c = 0; c < Y; ++c) {
                const size_t at = size_t(r) * size_t(Y) + size_t(c);
                for (int z = zLo[at]; z < zLo[at] + zCount[at]; ++z) {
                    const size_t ci = (size_t(r / cxy) * CY + size_t(c / cxy)) * CZ + size_t(z / cz);
                    if (cellAt[ci] < 0) cellAt[ci] = int64_t(cells.size()), cells.push_back({ci, Vec3{}, 1e300});
                    Cell& cell = cells[size_t(cellAt[ci])];
                    const double dr = r - ((r / cxy) * cxy + (cxy - 1) * 0.5), dc = c - ((c / cxy) * cxy + (cxy - 1) * 0.5),
                                 dz = z - ((z / cz) * cz + (cz - 1) * 0.5);
                    const double d2 = dr * dr + dc * dc + dz * dz;
                    if (d2 < cell.best) {
                        cell.best = d2;
                        cell.point = Vec3{(i0 + r) * kStepXY - kOrigin, (j0 + c) * kStepXY - kOrigin, (k0 + z) * kStepZ - kOrigin};
                    }
                }
            }
        std::vector<std::array<uint8_t, 168>> bytes(cells.size());
        std::atomic<size_t> next{0};
        const auto work = [&] {
            std::array<std::array<float, 3>, 56> s{};
            for (size_t k; (k = next.fetch_add(1)) < cells.size();) {
                in.samplesAt(cells[k].point, s);
                for (int i = 0; i < 56; ++i)
                    for (int ch = 0; ch < 3; ++ch)
                        bytes[k][size_t(i * 3 + ch)] = uint8_t(std::clamp(std::lround(double(s[size_t(i)][size_t(ch)])), 0L, 255L));
            }
        };
        const unsigned nt = in.threads > 0 ? unsigned(in.threads) : std::max(1u, std::thread::hardware_concurrency());
        std::vector<std::thread> pool;
        for (unsigned t = 1; t < nt; ++t) pool.emplace_back(work);
        work();
        for (std::thread& t : pool) t.join();
        // Deduplicated palette (u16 indices: at most 65535 colours; the cells only ever give fewer).
        std::map<std::array<uint8_t, 168>, int32_t> seen;
        cellColor.assign(size_t(CX) * CY * CZ, -1);
        for (size_t k = 0; k < cells.size(); ++k) {
            auto [it, fresh] = seen.emplace(bytes[k], int32_t(palette.size()));
            if (fresh) palette.push_back(bytes[k]);
            cellColor[cells[k].index] = it->second;
        }
        if (palette.size() > 65535) {
            if (err) *err = "light grid palette over 65535 colours";
            return false;
        }
    }
    const auto colorOf = [&](int r, int c, int z) -> uint32_t {
        if (cellColor.empty()) return 0;
        const int32_t v = cellColor[(size_t(r / cxy) * CY + size_t(c / cxy)) * CZ + size_t(z / cz)];
        return v < 0 ? 0u : uint32_t(v);
    };

    // row_data: per row (x) a 12-byte header, then column runs (u8 run <= 255 columns sharing one z range, u8 count,
    // u8 startZ when count > 0), padded to 4 bytes; row_starts: the row's start in units of 4 bytes (u16). Entries in
    // reader order: run by run, column by column, z fastest (iw3_lightgrid.cpp ~200-265).
    const bool trace = in.sunVisible && N <= in.maxEntries && in.sunToward.z > 0;
    const bool sunUp = in.sunToward.z > 0;
    out.entries.reserve(N * 4);
    size_t entry = 0;
    for (int r = 0; r < X; ++r) {
        const size_t start = out.rowData.size();
        if (start / 4 > 0xFFFE) {
            if (err) *err = "light grid row data too large";
            out = {};
            return false;
        }
        put16(out.rowStarts, uint32_t(start / 4));
        put16(out.rowData, uint32_t(j0));
        put16(out.rowData, uint32_t(Y));
        put16(out.rowData, uint32_t(k0));
        put16(out.rowData, uint32_t(Z));
        put32(out.rowData, uint32_t(entry));
        for (int c = 0; c < Y;) {
            const size_t at = size_t(r) * size_t(Y) + size_t(c);
            int run = 1;
            while (c + run < Y && run < 255 && zLo[at + size_t(run)] == zLo[at] && zCount[at + size_t(run)] == zCount[at]) ++run;
            out.rowData.push_back(char(run));
            out.rowData.push_back(char(zCount[at]));
            if (zCount[at]) out.rowData.push_back(char(zLo[at]));
            for (int cc = c; cc < c + run; ++cc)
                for (int z = zLo[at]; z < zLo[at] + zCount[at]; ++z) {
                    bool lit = sunUp;
                    if (trace) {
                        const Vec3 p{(i0 + r) * kStepXY - kOrigin, (j0 + cc) * kStepXY - kOrigin, (k0 + z) * kStepZ - kOrigin};
                        lit = in.sunVisible(p);
                    }
                    put16(out.entries, colorOf(r, cc, z));
                    out.entries.push_back(char(lit ? 1 : 0));
                    out.entries.push_back(char(0));
                    out.sunlit += lit ? 1 : 0;
                    ++entry;
                }
            c += run;
        }
        while (out.rowData.size() % 4) out.rowData.push_back(char(0));
    }
    // colors: the palette (56 directional samples x RGB each), or one entry of the flat ambient colour.
    if (!palette.empty()) {
        for (const auto& p : palette) out.colors.append(reinterpret_cast<const char*>(p.data()), p.size());
    } else {
        for (int s = 0; s < 56; ++s)
            for (int ch = 0; ch < 3; ++ch)
                out.colors.push_back(char(uint8_t(std::clamp(std::lround(255.0 * in.ambient[ch]), 0L, 255L))));
    }
    out.colorCount = out.colors.size() / 168;

    auto file = [&](const char* part, size_t bytes) {
        return std::string("{\"file\": \"") + lightGridPartPath(in.mapId, part) + "\", \"bytes\": " + std::to_string(bytes) + "}";
    };
    out.json = "{\n  \"schema\": 1,\n  \"name\": \"maps/mp/" + in.mapId + ".d3dbsp\",\n  \"available\": true,\n" +
               "  \"row_axis\": 0,\n  \"col_axis\": 1,\n" +
               "  \"mins\": [" + std::to_string(i0) + ", " + std::to_string(j0) + ", " + std::to_string(k0) + "],\n" +
               "  \"maxs\": [" + std::to_string(i1) + ", " + std::to_string(j1) + ", " + std::to_string(k1) + "],\n" +
               "  \"row_count\": " + std::to_string(X) + ",\n  \"entry_count\": " + std::to_string(N) + ",\n" +
               "  \"color_count\": " + std::to_string(out.colorCount) + ",\n  \"sun_primary_light_index\": 1,\n  \"has_light_regions\": false,\n" +
               "  \"row_starts\": " + file("row_starts", out.rowStarts.size()) + ",\n" +
               "  \"row_data\": " + file("row_data", out.rowData.size()) + ",\n" +
               "  \"entries\": " + file("entries", out.entries.size()) + ",\n" +
               "  \"colors\": " + file("colors", out.colors.size()) + "\n}\n";
    // Self-check (the converter throws on any mismatch, which would fail the whole build).
    const bool okSizes = out.rowStarts.size() == size_t(X) * 2 && out.entries.size() == N * 4 && out.colors.size() == 168 * out.colorCount &&
                         out.rowData.size() % 4 == 0 && N > 0 && N <= kMaxEntries;
    if (!okSizes) {
        if (err) *err = "light grid self-check failed";
        out = {};
        return false;
    }
    return true;
}

void lightGridSamplesAt(const RayBvh& bvh, const std::vector<Vec3>& albedos, const Vec3& sunToward, const Vec3& sunColour, const Vec3& sky,
                        double reach, const Vec3& p, std::array<std::array<float, 3>, 56>& out) {
    constexpr double kPi = 3.14159265358979323846;
    static const std::vector<Vec3> rays = [] {  // 96 directions spread evenly over the sphere
        std::vector<Vec3> r;
        const double ga = 3.14159265358979323846 * (3.0 - std::sqrt(5.0));
        for (int k = 0; k < 96; ++k) {
            const double z = 1.0 - (k + 0.5) * (2.0 / 96.0), rr = std::sqrt(std::max(0.0, 1.0 - z * z));
            r.push_back(Vec3{std::cos(ga * k) * rr, std::sin(ga * k) * rr, z});
        }
        return r;
    }();
    const auto& sdirs = lightGridSampleDirections();
    double acc[56][3] = {};
    const double w = 4.0 * kPi / double(rays.size());
    for (const Vec3& d : rays) {
        Vec3 rad{0, 0, 0};
        RayBvh::Hit h;
        if (!bvh.intersect(p, d, 0.5, reach, h)) {
            if (d.z > 0) rad = sky * (1.0 / kPi);  // the open sky; below the map, nothing
        } else {
            const RayBvh::Triangle& t = bvh.triangles()[h.tri];
            Vec3 n = cross(t.b - t.a, t.c - t.a);
            const double nl = length(n);
            if (nl < 1e-12) continue;
            n = n * (1.0 / nl);
            if (dot(n, d) > 0) n = n * -1.0;
            const Vec3 hp = p + d * h.t + n * 0.5;
            const double ns = dot(n, sunToward);
            const double eSun = ns > 0 && !bvh.occluded(hp, sunToward, 0.1, reach) ? ns : 0.0;
            const Vec3 e = sunColour * eSun + sky * (0.25 * (1.0 + n.z));  // about half its sky seen
            const Vec3 rho = t.id > 0 && t.id <= albedos.size() ? albedos[t.id - 1] : Vec3{0.45, 0.45, 0.45};
            rad = Vec3{e.x * rho.x, e.y * rho.y, e.z * rho.z} * (1.0 / kPi);
        }
        for (int s = 0; s < 56; ++s) {
            const double c = dot(sdirs[size_t(s)], d);
            if (c <= 0) continue;
            acc[s][0] += rad.x * c * w, acc[s][1] += rad.y * c * w, acc[s][2] += rad.z * c * w;
        }
    }
    for (int s = 0; s < 56; ++s)
        for (int ch = 0; ch < 3; ++ch) out[size_t(s)][size_t(ch)] = float(255.0 * acc[s][ch]);
}

Vec3 modelLightFromSamples(const std::array<std::array<float, 3>, 56>& samples, const Vec3& nIn) {
    // Texel (x, y, z) of the 4 x 4 x 4 volume -> its colour (the converter's kModelLightingTexelColors: the shell in
    // sample order; the eight inner texels repeat shell colours and are never reached from the cube's surface).
    static const uint8_t colourOf[64] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 0,
                                         3,  21, 22, 12, 15, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 40, 43, 33, 34, 52, 55, 35,
                                         36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55};
    const double len = length(nIn);
    const Vec3 n = len > 1e-12 ? nIn * (1.0 / len) : Vec3{0, 0, 1};
    const double m = std::max(std::max(std::fabs(n.x), std::fabs(n.y)), std::max(std::fabs(n.z), 1e-6));
    const double c[3] = {n.x / m * 1.5 + 1.5, n.y / m * 1.5 + 1.5, n.z / m * 1.5 + 1.5};
    int i[3];
    double f[3];
    for (int a = 0; a < 3; ++a) i[a] = std::min(int(std::floor(c[a])), 2), f[a] = c[a] - i[a];
    const auto texel = [&](int x, int y, int z, int ch) {
        // A byte of the model lighting image: the sample, cut at 255.
        return std::clamp(double(samples[colourOf[x + 4 * y + 16 * z]][size_t(ch)]), 0.0, 255.0) / 255.0;
    };
    Vec3 out;
    double* o[3] = {&out.x, &out.y, &out.z};
    for (int ch = 0; ch < 3; ++ch) {
        const double x00 = texel(i[0], i[1], i[2], ch) * (1 - f[0]) + texel(i[0] + 1, i[1], i[2], ch) * f[0];
        const double x10 = texel(i[0], i[1] + 1, i[2], ch) * (1 - f[0]) + texel(i[0] + 1, i[1] + 1, i[2], ch) * f[0];
        const double x01 = texel(i[0], i[1], i[2] + 1, ch) * (1 - f[0]) + texel(i[0] + 1, i[1], i[2] + 1, ch) * f[0];
        const double x11 = texel(i[0], i[1] + 1, i[2] + 1, ch) * (1 - f[0]) + texel(i[0] + 1, i[1] + 1, i[2] + 1, ch) * f[0];
        *o[ch] = ((x00 * (1 - f[1]) + x10 * f[1]) * (1 - f[2]) + (x01 * (1 - f[1]) + x11 * f[1]) * f[2]) * 2.0;
    }
    return out;
}

const std::array<Vec3, 56>& lightGridSampleDirections() {
    static const std::array<Vec3, 56> dirs = [] {
        std::array<Vec3, 56> d{};
        size_t n = 0;
        for (int z = 0; z < 4; ++z)
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x) {
                    if (x > 0 && x < 3 && y > 0 && y < 3 && z > 0 && z < 3) continue;
                    d[n++] = normalize(Vec3{x * (2.0 / 3.0) - 1.0, y * (2.0 / 3.0) - 1.0, z * (2.0 / 3.0) - 1.0});
                }
        return d;
    }();
    return dirs;
}

}  // namespace iw8r
