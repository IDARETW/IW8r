import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'src/core/light_grid.h'
C = 'src/core/light_grid.cpp'

patch(H, '''#include <cstdint>
#include <functional>''', '''#include <array>
#include <cstdint>
#include <functional>''')
patch(H, '''struct LightGridFiles {
    std::string json;  // <id>.d3dbsp.lightgrid.json
    std::string rowStarts, rowData, entries, colors;  // the four binary arrays
    size_t entryCount = 0, sunlit = 0;''', '''struct LightGridFiles {
    std::string json;  // <id>.d3dbsp.lightgrid.json
    std::string rowStarts, rowData, entries, colors;  // the four binary arrays
    size_t entryCount = 0, sunlit = 0;
    size_t colorCount = 1;  // palette entries''')
patch(H, '''    double bandBelow = 64, bandAbove = 320;
    size_t maxEntries = 4'000'000;  // larger grids fall back to every point sunlit (no traces)
};''', '''    double bandBelow = 64, bandAbove = 320;
    size_t maxEntries = 4'000'000;  // larger grids fall back to every point sunlit (no traces)
    // Per-point light (ROADMAP 5.20): the 56 samples (lightGridSampleDirections order) of the palette colour at a point,
    // in the grid's 8-bit units (255 = a surface lit by 1.0, the sun colour x 1 of the build). The converter projects them
    // to SH; models and viewmodels take that light (the sun comes on top through primaryLight). Called once per palette
    // cell (paletteCellXY x paletteCellXY x paletteCellZ lattice points; the cell's point nearest its centre among the
    // kept ones) from several threads. Unset: every point takes `ambient`.
    std::function<void(const Vec3& point, std::array<std::array<float, 3>, 56>& samples)> samplesAt;
    int paletteCellXY = 4, paletteCellZ = 1;
    int threads = 0;  // 0: all cores
};

// The 56 sample directions of a palette colour: the shell texels of the 4 x 4 x 4 model-lighting volume (z outer, then
// y, then x; the 8 interior texels left out), normalised (iw3_lightgrid.cpp LightGridProjection).
const std::array<Vec3, 56>& lightGridSampleDirections();''')

patch(C, '''    // row_data: per row (x) a 12-byte header, then column runs''', '''    // Palette (ROADMAP 5.20): with samplesAt, one colour per cell of the kept points, deduplicated; else one colour.
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

    // row_data: per row (x) a 12-byte header, then column runs''')
patch(C, '''                    put16(out.entries, 0);
                    out.entries.push_back(char(lit ? 1 : 0));''', '''                    put16(out.entries, colorOf(r, cc, z));
                    out.entries.push_back(char(lit ? 1 : 0));''')
patch(C, '''    // colors: one palette entry, 56 directional samples x RGB, all the ambient colour.
    for (int s = 0; s < 56; ++s)
        for (int ch = 0; ch < 3; ++ch)
            out.colors.push_back(char(uint8_t(std::clamp(std::lround(255.0 * in.ambient[ch]), 0L, 255L))));''', '''    // colors: the palette (56 directional samples x RGB each), or one entry of the flat ambient colour.
    if (!palette.empty()) {
        for (const auto& p : palette) out.colors.append(reinterpret_cast<const char*>(p.data()), p.size());
    } else {
        for (int s = 0; s < 56; ++s)
            for (int ch = 0; ch < 3; ++ch)
                out.colors.push_back(char(uint8_t(std::clamp(std::lround(255.0 * in.ambient[ch]), 0L, 255L))));
    }
    out.colorCount = out.colors.size() / 168;''')
patch(C, '''               "  \\"color_count\\": 1,\\n  \\"sun_primary_light_index\\": 1,\\n  \\"has_light_regions\\": false,\\n" +''',
      '''               "  \\"color_count\\": " + std::to_string(out.colorCount) + ",\\n  \\"sun_primary_light_index\\": 1,\\n  \\"has_light_regions\\": false,\\n" +''')
patch(C, '''    const bool okSizes = out.rowStarts.size() == size_t(X) * 2 && out.entries.size() == N * 4 && out.colors.size() == 168 &&''',
      '''    const bool okSizes = out.rowStarts.size() == size_t(X) * 2 && out.entries.size() == N * 4 && out.colors.size() == 168 * out.colorCount &&''')
patch(C, '''}  // namespace iw8r''', '''const std::array<Vec3, 56>& lightGridSampleDirections() {
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

}  // namespace iw8r''')
print('ok')
