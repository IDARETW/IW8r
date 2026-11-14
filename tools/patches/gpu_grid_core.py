import io

# Plan 1.9: the stock map's tetrahedral light grid (research 105 section 2.1), the grid the game is INFERRED to light
# dynamic models with (script models: what brushes added to a stock map are built as). Core: parse it from the
# gfx_map_trzone export, find the tetrahedron that holds a point, blend its four probes. The lightmap bake takes an
# "ambient at this point" function, so the preview bake of brushes on a stock map can use the grid.

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b, n=1):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    write(p, s.replace(a, b))


H = 'src/core/stock_lightgrid.h'
patch(H, '''// One R11G11B10F texel (texture 0) to linear rgb''', '''// ---- The tetrahedral light grid (research 105 section 2.1) ----------------------------------------------------------
// gpuLightGrid.gpuLightGrid of the gfx_map_trzone export: probes on an adaptive lattice, joined into tetrahedra. The
// static-model shader does not read it; it is INFERRED to light dynamic models (characters, script models). A point is
// lit by the four probes of the tetrahedron that holds it, blended by barycentric weights.
struct StockGpuGrid {
    std::vector<std::array<float, 3>> pos;        // probe positions
    std::vector<StockLgvSh> sh;                   // the probes, in the LGV's slot order (evalStockLgv reads them)
    std::vector<std::array<uint32_t, 4>> tets;    // probe indices of each tetrahedron
    float mins[3] = {0, 0, 0}, maxs[3] = {0, 0, 0};
    // A uniform grid over the bounds: the tetrahedra whose box touches each cell (cellStart has one more entry than
    // there are cells).
    double cell = 0;
    int nx = 0, ny = 0, nz = 0;
    std::vector<uint32_t> cellStart, cellTets;
    bool empty() const { return tets.empty(); }
};
// One probe record (GfxSHCompressedProbeData, 32 bytes): dword 0 the DC as R11G11B10F; byte 4 + 8 c + (i - 1) the
// coefficient i = 1..8 of channel c in standard order as n x 127 + 127.5, n = coefficient / (DC x sqrt3 | sqrt5).
// Decoded literally (research 105: the stock bytes agree in sign with the LGV).
StockLgvSh decodeStockGpuProbe(const uint8_t record[32]);
// Builds the grid from probe records, positions (three floats each) and tetrahedra (four u32 each; bit 31 is a flag).
// False when the sizes do not fit or an index is out of range.
bool buildStockGpuGrid(const std::vector<uint8_t>& probes, const std::vector<uint8_t>& positions, const std::vector<uint8_t>& tetrahedra,
                       StockGpuGrid& out, std::string* err = nullptr);
// The same from the trzone export's text (the members probes / probePositions / tetrahedrons of gpuLightGrid).
bool parseStockGpuGrid(const std::string& trzoneJson, StockGpuGrid& out, std::string* err = nullptr);
// The light at p: false when no tetrahedron holds it (outside the grid's hull).
bool sampleStockGpuGrid(const StockGpuGrid& g, const double p[3], StockLgvSh& out);

// One R11G11B10F texel (texture 0) to linear rgb''')
s = read(H)
if '#include <array>' not in s:
    i = s.index('#include <')
    s = s[:i] + '#include <array>\n' + s[i:]
    write(H, s)

C = 'src/core/stock_lightgrid.cpp'
s = read(C)
nl = '\r\n' if '\r\n' in s else '\n'
tail = '}  // namespace iw8r'
assert s.rstrip().endswith(tail)
body = '''
// ---- The tetrahedral light grid ----------------------------------------------------------------------------------------

StockLgvSh decodeStockGpuProbe(const uint8_t record[32]) {
    StockLgvSh out;
    uint32_t dw = 0;
    std::memcpy(&dw, record, 4);
    decodeR11G11B10F(dw, out.dc);
    // Standard order i = 1..8: -y, z, -x, xy, -yz, 3z^2-1, -xz, x^2-y^2. The LGV's slots: 0 z, 1 3z^2-1, 2 -y, 3 -x,
    // 4 -xz, 5 -yz, 6 x^2-y^2, 7 xy.
    static const int slotOf[9] = {-1, 2, 0, 3, 7, 5, 1, 4, 6};
    for (int c = 0; c < 3; ++c) {
        if (!std::isfinite(out.dc[c])) out.dc[c] = 0;
        for (int i = 1; i <= 8; ++i) {
            const double n = (double(record[4 + 8 * c + (i - 1)]) - 127.5) / 127.0;
            out.sh[slotOf[i]][c] = float(n * double(out.dc[c]) * (i < 4 ? 1.7320508075688772 : 2.23606797749979));
        }
    }
    return out;
}

bool buildStockGpuGrid(const std::vector<uint8_t>& probes, const std::vector<uint8_t>& positions, const std::vector<uint8_t>& tetrahedra,
                       StockGpuGrid& out, std::string* err) {
    out = StockGpuGrid{};
    if (probes.size() % 32 || positions.size() % 12 || tetrahedra.size() % 16) return fail(err, "light grid arrays are not whole records");
    const size_t n = probes.size() / 32, nt = tetrahedra.size() / 16;
    if (n == 0 || nt == 0) return fail(err, "the light grid has no probes or no tetrahedra");
    if (positions.size() / 12 != n) return fail(err, "the light grid has " + std::to_string(positions.size() / 12) + " positions for " + std::to_string(n) + " probes");
    out.pos.resize(n);
    out.sh.resize(n);
    for (int k = 0; k < 3; ++k) out.mins[k] = 1e30f, out.maxs[k] = -1e30f;
    for (size_t i = 0; i < n; ++i) {
        std::memcpy(out.pos[i].data(), positions.data() + i * 12, 12);
        for (int k = 0; k < 3; ++k) {
            if (!std::isfinite(out.pos[i][k])) return fail(err, "a light grid probe has no finite position");
            out.mins[k] = std::min(out.mins[k], out.pos[i][k]);
            out.maxs[k] = std::max(out.maxs[k], out.pos[i][k]);
        }
        out.sh[i] = decodeStockGpuProbe(probes.data() + i * 32);
    }
    out.tets.resize(nt);
    for (size_t t = 0; t < nt; ++t) {
        uint32_t idx[4];
        std::memcpy(idx, tetrahedra.data() + t * 16, 16);
        for (int k = 0; k < 4; ++k) {
            idx[k] &= 0x7FFFFFFFu;
            if (idx[k] >= n) return fail(err, "a light grid tetrahedron names probe " + std::to_string(idx[k]) + " of " + std::to_string(n));
            out.tets[t][size_t(k)] = idx[k];
        }
    }
    // The cells: about 100 units a side, at most 256 a direction.
    const double ext[3] = {double(out.maxs[0]) - out.mins[0], double(out.maxs[1]) - out.mins[1], double(out.maxs[2]) - out.mins[2]};
    out.cell = std::max({96.0, ext[0] / 256.0, ext[1] / 256.0, ext[2] / 256.0});
    out.nx = std::max(1, int(std::ceil(ext[0] / out.cell)));
    out.ny = std::max(1, int(std::ceil(ext[1] / out.cell)));
    out.nz = std::max(1, int(std::ceil(ext[2] / out.cell)));
    const size_t cells = size_t(out.nx) * size_t(out.ny) * size_t(out.nz);
    const auto range = [&](size_t t, int lo[3], int hi[3]) {
        const int dim[3] = {out.nx, out.ny, out.nz};
        for (int k = 0; k < 3; ++k) {
            float a = 1e30f, b = -1e30f;
            for (int v = 0; v < 4; ++v) a = std::min(a, out.pos[out.tets[t][size_t(v)]][size_t(k)]), b = std::max(b, out.pos[out.tets[t][size_t(v)]][size_t(k)]);
            lo[k] = std::clamp(int(std::floor((double(a) - out.mins[k]) / out.cell)), 0, dim[k] - 1);
            hi[k] = std::clamp(int(std::floor((double(b) - out.mins[k]) / out.cell)), 0, dim[k] - 1);
        }
    };
    std::vector<uint32_t> count(cells + 1, 0);
    for (size_t t = 0; t < nt; ++t) {
        int lo[3], hi[3];
        range(t, lo, hi);
        for (int z = lo[2]; z <= hi[2]; ++z)
            for (int y = lo[1]; y <= hi[1]; ++y)
                for (int x = lo[0]; x <= hi[0]; ++x) ++count[(size_t(z) * size_t(out.ny) + size_t(y)) * size_t(out.nx) + size_t(x) + 1];
    }
    for (size_t c = 0; c < cells; ++c) count[c + 1] += count[c];
    out.cellStart = count;
    out.cellTets.resize(count[cells]);
    std::vector<uint32_t> fill(count.begin(), count.end() - 1);
    for (size_t t = 0; t < nt; ++t) {
        int lo[3], hi[3];
        range(t, lo, hi);
        for (int z = lo[2]; z <= hi[2]; ++z)
            for (int y = lo[1]; y <= hi[1]; ++y)
                for (int x = lo[0]; x <= hi[0]; ++x) out.cellTets[fill[(size_t(z) * size_t(out.ny) + size_t(y)) * size_t(out.nx) + size_t(x)]++] = uint32_t(t);
    }
    return true;
}

bool parseStockGpuGrid(const std::string& trzoneJson, StockGpuGrid& out, std::string* err) {
    out = StockGpuGrid{};
    // Inside gpuLightGrid only: other parts of the export have members of the same names.
    const size_t at = findMemberValue(trzoneJson, "gpuLightGrid");
    if (at == std::string::npos) return fail(err, "trzone has no gpuLightGrid");
    const size_t end = valueEnd(trzoneJson, at);
    if (end == std::string::npos) return fail(err, "gpuLightGrid is not closed");
    const std::string part = trzoneJson.substr(at, end - at);
    const auto bytesOf = [&](const char* key, std::vector<uint8_t>& raw) {
        std::string perr;
        const auto node = parseMember(part, key, &perr);
        if (!node || node->type != Value::Type::Object) return fail(err, std::string("gpuLightGrid has no ") + key + (perr.empty() ? "" : ": " + perr));
        const Value* bytes = node->get("bytes");
        if (!bytes || !bytes->string() || !unhex(*bytes->string(), raw)) return fail(err, std::string("gpuLightGrid ") + key + " has no hex bytes");
        return true;
    };
    std::vector<uint8_t> probes, positions, tets;
    if (!bytesOf("probes", probes) || !bytesOf("probePositions", positions) || !bytesOf("tetrahedrons", tets)) return false;
    return buildStockGpuGrid(probes, positions, tets, out, err);
}

bool sampleStockGpuGrid(const StockGpuGrid& g, const double p[3], StockLgvSh& out) {
    if (g.empty() || g.cell <= 0) return false;
    int c[3];
    const int dim[3] = {g.nx, g.ny, g.nz};
    for (int k = 0; k < 3; ++k) {
        if (p[k] < double(g.mins[k]) - 1e-3 || p[k] > double(g.maxs[k]) + 1e-3) return false;
        c[k] = std::clamp(int(std::floor((p[k] - double(g.mins[k])) / g.cell)), 0, dim[k] - 1);
    }
    const size_t cellIndex = (size_t(c[2]) * size_t(g.ny) + size_t(c[1])) * size_t(g.nx) + size_t(c[0]);
    for (uint32_t k = g.cellStart[cellIndex]; k < g.cellStart[cellIndex + 1]; ++k) {
        const auto& tet = g.tets[g.cellTets[k]];
        const auto& a = g.pos[tet[0]];
        const auto& b = g.pos[tet[1]];
        const auto& cc = g.pos[tet[2]];
        const auto& d = g.pos[tet[3]];
        // Solve [a-d, b-d, c-d] w = p - d (Cramer).
        const double m[3][3] = {{double(a[0]) - d[0], double(b[0]) - d[0], double(cc[0]) - d[0]},
                                {double(a[1]) - d[1], double(b[1]) - d[1], double(cc[1]) - d[1]},
                                {double(a[2]) - d[2], double(b[2]) - d[2], double(cc[2]) - d[2]}};
        const double r[3] = {p[0] - d[0], p[1] - d[1], p[2] - d[2]};
        const double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                           m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
        if (std::fabs(det) < 1e-9) continue;
        const double w0 = (r[0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (r[1] * m[2][2] - m[1][2] * r[2]) +
                           m[0][2] * (r[1] * m[2][1] - m[1][1] * r[2])) / det;
        const double w1 = (m[0][0] * (r[1] * m[2][2] - m[1][2] * r[2]) - r[0] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                           m[0][2] * (m[1][0] * r[2] - r[1] * m[2][0])) / det;
        const double w2 = (m[0][0] * (m[1][1] * r[2] - r[1] * m[2][1]) - m[0][1] * (m[1][0] * r[2] - r[1] * m[2][0]) +
                           r[0] * (m[1][0] * m[2][1] - m[1][1] * m[2][0])) / det;
        const double w3 = 1.0 - w0 - w1 - w2;
        const double tol = -1e-4;
        if (w0 < tol || w1 < tol || w2 < tol || w3 < tol) continue;
        const double w[4] = {w0, w1, w2, w3};
        out = StockLgvSh{};
        for (int v = 0; v < 4; ++v) {
            const StockLgvSh& s = g.sh[tet[size_t(v)]];
            for (int ch = 0; ch < 3; ++ch) {
                out.dc[ch] += float(w[v] * double(s.dc[ch]));
                for (int j = 0; j < 8; ++j) out.sh[j][ch] += float(w[v] * double(s.sh[j][ch]));
            }
        }
        return true;
    }
    return false;
}

'''.replace('\n', nl)
s = s.rstrip()
s = s[:len(s) - len(tail)] + body.lstrip(nl) + nl + tail + nl
for inc in ('#include <cstring>', '#include <cmath>', '#include <algorithm>'):
    if inc not in s:
        i = s.index('#include <')
        s = s[:i] + inc + nl + s[i:]
write(C, s)

# --- the bake: an ambient given from outside ------------------------------------------------------------------------
BH = 'src/core/lightmap_bake.h'
patch(BH, '''    std::vector<BakeLight> lights;
};''', '''    std::vector<BakeLight> lights;
    // When set and it returns true for a texel, its result is the texel's ambient light and no sky or bounce ray is
    // traced there (brushes on a stock map: the map's own light grid, plan 1.9). Called from the bake's threads.
    std::function<bool(const Vec3& pos, const Vec3& normal, Vec3& ambient)> ambientAt;
};''')
BC = 'src/core/lightmap_bake.cpp'
patch(BC, '''                // Ambient: the sky and one bounce of the sun, cosine-weighted.
                Vec3 A{0, 0, 0};
''', '''                // Ambient: given from outside for this texel, else the sky and one bounce of the sun, cosine-weighted.
                Vec3 A{0, 0, 0};
                if (scene.ambientAt && scene.ambientAt(tx.pos, N, A)) {
                    r.ambient[i] = {float(A.x), float(A.y), float(A.z)};
                    continue;
                }
''')

# --- tests ----------------------------------------------------------------------------------------------------------
write('tests/stock_gpugrid_tests.cpp', '''#include "lightmap_bake.h"
#include "stock_lightgrid.h"
#include "test_harness.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

using namespace iw8r;

namespace {
// A probe record with the given DC (values the R11G11B10F form holds exactly) and one L1 coefficient byte.
std::vector<uint8_t> probe(uint32_t dcPacked, int coefIndex = 0, uint8_t coefByte = 128) {
    std::vector<uint8_t> r(32, 0);
    std::memcpy(r.data(), &dcPacked, 4);
    for (int c = 0; c < 3; ++c)
        for (int i = 1; i <= 8; ++i) r[size_t(4 + 8 * c + (i - 1))] = i == coefIndex ? coefByte : uint8_t(128);
    r[29] = 255;
    return r;
}
void putF(std::vector<uint8_t>& v, float f) {
    uint8_t b[4];
    std::memcpy(b, &f, 4);
    v.insert(v.end(), b, b + 4);
}
void putU(std::vector<uint8_t>& v, uint32_t u) {
    uint8_t b[4];
    std::memcpy(b, &u, 4);
    v.insert(v.end(), b, b + 4);
}
// R11G11B10F of (1, 1, 1): exponent 15, mantissa 0 in each field.
constexpr uint32_t kOne = (15u << 6) | ((15u << 6) << 11) | ((15u << 5) << 22);
constexpr uint32_t kTwo = (16u << 6) | ((16u << 6) << 11) | ((16u << 5) << 22);
}  // namespace

// Plan 1.9 (research 105 section 2.1): the stock tetrahedral light grid, sampled by barycentric weights.
TEST(stock_gpugrid_decode_build_and_sample) {
    float one[3], two[3];
    decodeR11G11B10F(kOne, one);
    decodeR11G11B10F(kTwo, two);
    CHECK(one[0] == 1.0f && one[2] == 1.0f && two[1] == 2.0f);
    // A probe: the DC, and coefficient 2 (the +z one) at byte 255 = +1.0039 x DC x sqrt3 in the LGV's slot 0.
    const std::vector<uint8_t> rec = probe(kTwo, 2, 255);
    const StockLgvSh p = decodeStockGpuProbe(rec.data());
    CHECK(p.dc[0] == 2.0f && p.dc[1] == 2.0f && p.dc[2] == 2.0f);
    CHECK(std::fabs(p.sh[0][0] - float((255 - 127.5) / 127.0 * 2.0 * std::sqrt(3.0))) < 1e-5);
    CHECK(std::fabs(p.sh[2][0] - float(0.5 / 127.0 * 2.0 * std::sqrt(3.0))) < 1e-5);  // byte 128: nearly 0
    // Lit from above, a face that looks up gets more light than one that looks down.
    double up[3], down[3];
    const double nUp[3] = {0, 0, 1}, nDown[3] = {0, 0, -1};
    evalStockLgv(p, nUp, up);
    evalStockLgv(p, nDown, down);
    CHECK(up[0] > down[0] && down[0] >= 0);

    // Two tetrahedra sharing a face: probes 0..3 at light 1, probe 4 at light 2.
    std::vector<uint8_t> probes, positions, tets;
    const float pts[5][3] = {{0, 0, 0}, {128, 0, 0}, {0, 128, 0}, {0, 0, 128}, {128, 128, 128}};
    for (int i = 0; i < 5; ++i) {
        const std::vector<uint8_t> r = probe(i == 4 ? kTwo : kOne);
        probes.insert(probes.end(), r.begin(), r.end());
        for (float f : pts[i]) putF(positions, f);
    }
    for (uint32_t i : {0u, 1u, 2u, 3u}) putU(tets, i);
    for (uint32_t i : {1u, 2u, 3u, 4u | 0x80000000u}) putU(tets, i);  // bit 31 is a flag, not part of the index
    StockGpuGrid g;
    std::string err;
    CHECK(buildStockGpuGrid(probes, positions, tets, g, &err));
    CHECK(g.pos.size() == 5 && g.tets.size() == 2 && g.tets[1][3] == 4 && g.maxs[0] == 128.0f && g.nx >= 1 && !g.cellTets.empty());
    StockLgvSh s;
    const double atVertex[3] = {0, 0, 0}, inFirst[3] = {16, 16, 16}, atFar[3] = {128, 128, 128}, outside[3] = {127, 127, 1}, away[3] = {900, 0, 0};
    CHECK(sampleStockGpuGrid(g, atVertex, s) && std::fabs(s.dc[0] - 1.0f) < 1e-5);
    CHECK(sampleStockGpuGrid(g, inFirst, s) && std::fabs(s.dc[1] - 1.0f) < 1e-5);
    CHECK(sampleStockGpuGrid(g, atFar, s) && std::fabs(s.dc[2] - 2.0f) < 1e-4);
    // Halfway between the shared face's centre and probe 4: between the two lights.
    const double mid[3] = {(128.0 / 3 + 128) / 2, (128.0 / 3 + 128) / 2, (128.0 / 3 + 128) / 2};
    CHECK(sampleStockGpuGrid(g, mid, s) && std::fabs(s.dc[0] - 1.5f) < 1e-4);
    CHECK(!sampleStockGpuGrid(g, outside, s));  // inside the box, in no tetrahedron
    CHECK(!sampleStockGpuGrid(g, away, s));

    // Refusals.
    StockGpuGrid bad;
    std::vector<uint8_t> shortPos(positions.begin(), positions.end() - 12);
    CHECK(!buildStockGpuGrid(probes, shortPos, tets, bad, &err) && err.find("positions") != std::string::npos);
    std::vector<uint8_t> wild = tets;
    const uint32_t big = 99;
    std::memcpy(wild.data(), &big, 4);
    CHECK(!buildStockGpuGrid(probes, positions, wild, bad, &err) && err.find("names probe 99") != std::string::npos);
    CHECK(!buildStockGpuGrid({}, {}, {}, bad, &err));
    CHECK(!parseStockGpuGrid("{}", bad, &err) && err.find("gpuLightGrid") != std::string::npos);

    // The export's text form.
    const auto hex = [](const std::vector<uint8_t>& v) {
        static const char* d = "0123456789abcdef";
        std::string o;
        for (uint8_t b : v) o += d[b >> 4], o += d[b & 15];
        return o;
    };
    const std::string json = "{\\"asset\\":{\\"fields\\":{\\"probes\\":{\\"bytes\\":\\"00\\"},\\"gpuLightGrid\\":{\\"gpuLightGrid\\":{\\"values\\":[{\\"probeCount\\":5,"
                             "\\"probes\\":{\\"stride\\":32,\\"bytes\\":\\"" + hex(probes) + "\\"},\\"probePositions\\":{\\"bytes\\":\\"" + hex(positions) +
                             "\\"},\\"tetrahedrons\\":{\\"bytes\\":\\"" + hex(tets) + "\\"}}]}}}}}";
    StockGpuGrid parsed;
    CHECK(parseStockGpuGrid(json, parsed, &err));
    CHECK(parsed.pos.size() == 5 && parsed.tets.size() == 2 && sampleStockGpuGrid(parsed, mid, s) && std::fabs(s.dc[0] - 1.5f) < 1e-4);
}

// The lightmap bake takes its ambient light from outside where a function gives it (the stock map's grid).
TEST(lightmap_bake_ambient_from_outside) {
    BakeScene scene;
    BakeSurface floor;
    floor.verts = {{Vec3{0, 0, 0}, Vec3{0, 0, 1}, Vec3{1, 0, 0}}, {Vec3{128, 0, 0}, Vec3{0, 0, 1}, Vec3{1, 0, 0}},
                   {Vec3{128, 128, 0}, Vec3{0, 0, 1}, Vec3{1, 0, 0}}, {Vec3{0, 128, 0}, Vec3{0, 0, 1}, Vec3{1, 0, 0}}};
    floor.idx = {0, 1, 2, 0, 2, 3};
    scene.surfaces.push_back(floor);
    scene.sunIntensity = 0;
    BakeSettings settings;
    settings.threads = 1;
    const BakeResult sky = bakeLightmap(scene, settings);
    // Left half: light 3 from outside; right half: the bake's own sky.
    scene.ambientAt = [](const Vec3& pos, const Vec3& normal, Vec3& ambient) {
        if (pos.x >= 64 || normal.z < 0.5) return false;
        ambient = Vec3{3, 2, 1};
        return true;
    };
    const BakeResult mixed = bakeLightmap(scene, settings);
    CHECK(sky.ok && mixed.ok && sky.width == mixed.width);
    size_t given = 0, own = 0;
    bool same = true;
    for (size_t i = 0; i < mixed.ambient.size() && i < sky.ambient.size(); ++i) {
        if (!mixed.covered[i]) continue;
        if (mixed.ambient[i][0] == 3.0f && mixed.ambient[i][1] == 2.0f && mixed.ambient[i][2] == 1.0f) ++given;
        else if (sky.covered[i]) ++own, same = same && std::fabs(mixed.ambient[i][0] - sky.ambient[i][0]) < 0.5f;
    }
    CHECK(given > 4 && own > 4 && same);
}

// On a real export (set IW8R_TEST_TRZONE to a gfx_map_trzone asset JSON; skipped without it): the grid parses, a
// probe's own position gives that probe's light back, and the light is in the range research 105 measured.
TEST(stock_gpugrid_real_export) {
    const char* path = std::getenv("IW8R_TEST_TRZONE");
    if (!path || !*path) return;
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    StockGpuGrid g;
    std::string err;
    CHECK(parseStockGpuGrid(ss.str(), g, &err));
    if (g.empty()) return;
    size_t found = 0, close = 0, bright = 0;
    double sum = 0;
    const size_t step = std::max<size_t>(1, g.pos.size() / 2000);
    for (size_t i = 0; i < g.pos.size(); i += step) {
        const double p[3] = {g.pos[i][0], g.pos[i][1], g.pos[i][2]};
        StockLgvSh s;
        if (!sampleStockGpuGrid(g, p, s)) continue;
        ++found;
        if (std::fabs(s.dc[1] - g.sh[i].dc[1]) <= 0.02f * std::max(1.0f, g.sh[i].dc[1])) ++close;
        double e[3];
        const double up[3] = {0, 0, 1};
        evalStockLgv(s, up, e);
        sum += e[1];
        if (e[1] > 0.05) ++bright;
    }
    std::printf("  gpu grid: %zu probes, %zu tetrahedra, %d x %d x %d cells of %.0f, %zu cell entries; %zu of %zu sampled probes found, "
                "%zu equal their own light, mean E(up) green %.3f\\n",
                g.pos.size(), g.tets.size(), g.nx, g.ny, g.nz, g.cell, g.cellTets.size(), found, (g.pos.size() + step - 1) / step, close,
                found ? sum / double(found) : 0.0);
    CHECK(found > 100 && close * 10 >= found * 9 && bright * 2 > found);
}
''')
print('ok')
