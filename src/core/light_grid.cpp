#include "light_grid.h"

#include <algorithm>
#include <cmath>

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
                    put16(out.entries, 0);
                    out.entries.push_back(char(lit ? 1 : 0));
                    out.entries.push_back(char(0));
                    out.sunlit += lit ? 1 : 0;
                    ++entry;
                }
            c += run;
        }
        while (out.rowData.size() % 4) out.rowData.push_back(char(0));
    }
    // colors: one palette entry, 56 directional samples x RGB, all the ambient colour.
    for (int s = 0; s < 56; ++s)
        for (int ch = 0; ch < 3; ++ch)
            out.colors.push_back(char(uint8_t(std::clamp(std::lround(255.0 * in.ambient[ch]), 0L, 255L))));

    auto file = [&](const char* part, size_t bytes) {
        return std::string("{\"file\": \"") + lightGridPartPath(in.mapId, part) + "\", \"bytes\": " + std::to_string(bytes) + "}";
    };
    out.json = "{\n  \"schema\": 1,\n  \"name\": \"maps/mp/" + in.mapId + ".d3dbsp\",\n  \"available\": true,\n" +
               "  \"row_axis\": 0,\n  \"col_axis\": 1,\n" +
               "  \"mins\": [" + std::to_string(i0) + ", " + std::to_string(j0) + ", " + std::to_string(k0) + "],\n" +
               "  \"maxs\": [" + std::to_string(i1) + ", " + std::to_string(j1) + ", " + std::to_string(k1) + "],\n" +
               "  \"row_count\": " + std::to_string(X) + ",\n  \"entry_count\": " + std::to_string(N) + ",\n" +
               "  \"color_count\": 1,\n  \"sun_primary_light_index\": 1,\n  \"has_light_regions\": false,\n" +
               "  \"row_starts\": " + file("row_starts", out.rowStarts.size()) + ",\n" +
               "  \"row_data\": " + file("row_data", out.rowData.size()) + ",\n" +
               "  \"entries\": " + file("entries", out.entries.size()) + ",\n" +
               "  \"colors\": " + file("colors", out.colors.size()) + "\n}\n";
    // Self-check (the converter throws on any mismatch, which would fail the whole build).
    const bool okSizes = out.rowStarts.size() == size_t(X) * 2 && out.entries.size() == N * 4 && out.colors.size() == 168 &&
                         out.rowData.size() % 4 == 0 && N > 0 && N <= kMaxEntries;
    if (!okSizes) {
        if (err) *err = "light grid self-check failed";
        out = {};
        return false;
    }
    return true;
}

}  // namespace iw8r
