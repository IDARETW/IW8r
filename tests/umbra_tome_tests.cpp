// Umbra 3 tome parser and visibility query (src/core/umbra_tome.h, docs/research/110_umbra_tome.md):
//   - synthetic tomes built here: the converter's conservative tome (a port of x64-zt BuildConservativeUmbraTome)
//     and a two-tile tome with a door portal, a sealed cell and an external portal (parse, checks, point lookups,
//     queries, corruption);
//   - real stock tomes cut out of the world cache's gfx_map exports when present on this machine (skipped
//     otherwise): mp_shipment and seven more MP zones, and Verdansk's main zone (mp_donetsk): CRC fingerprints,
//     counts and self-consistency, and queries from sample points.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include "test_harness.h"
#include "umbra_tome.h"

using namespace iw8r;

namespace {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------------------------------------------
// A small tome writer (test only): 16-byte aligned sections, rank directories, the stock record layouts.
// ---------------------------------------------------------------------------------------------------------------

struct Buf {
    std::vector<uint8_t> b;
    uint32_t alloc(size_t n) {
        while (b.size() % 16) b.push_back(0);
        const uint32_t o = uint32_t(b.size());
        b.resize(b.size() + n, 0);
        return o;
    }
    void u32(size_t o, uint32_t v) { std::memcpy(b.data() + o, &v, 4); }
    void f32(size_t o, float v) { std::memcpy(b.data() + o, &v, 4); }
    void u16(size_t o, uint16_t v) { std::memcpy(b.data() + o, &v, 2); }
};

// 2-bit codes in breadth-first order + rank directory, the layout UmbraTome reads.
std::vector<uint8_t> serializeTree(const std::vector<uint8_t>& codes) {
    const uint32_t n = uint32_t(codes.size());
    const uint32_t words = (n * 2 + 31) / 32, supers = (words + 15) / 16;
    std::vector<uint32_t> w(words, 0);
    for (uint32_t i = 0; i < n; ++i) w[i >> 4] |= uint32_t(codes[i] & 3) << (2 * (i & 15));
    std::vector<uint32_t> before(size_t(words) * 16 + 1, 0);
    for (size_t i = 0; i + 1 < before.size(); ++i) before[i + 1] = before[i] + (i < n && codes[i] != 3);
    const uint32_t blocks = (words + 4095) / 4096;
    std::vector<uint8_t> out(words * 4);
    std::memcpy(out.data(), w.data(), words * 4);
    for (uint32_t b = 1; b < blocks; ++b)
        for (int k = 0; k < 4; ++k) out.push_back(uint8_t(before[b * 65536] >> (8 * k)));
    for (uint32_t s = 1; s < supers; ++s) {
        if (s % 256 == 0) continue;
        const uint16_t v = uint16_t(before[s * 256] - before[(s / 256) * 65536]);
        out.push_back(uint8_t(v));
        out.push_back(uint8_t(v >> 8));
    }
    while (out.size() % 4) out.push_back(0);
    for (uint32_t s = 0; s < supers; ++s)
        for (uint32_t k = s * 16 + 1; k < std::min(words, s * 16 + 16); ++k) out.push_back(uint8_t(before[k * 16] - before[s * 256]));
    while (out.size() % 4) out.push_back(0);
    return out;
}

void packBits(std::vector<uint8_t>& out, const std::vector<uint32_t>& values, unsigned width) {
    out.assign((values.size() * width + 7) / 8 + 4, 0);
    uint64_t bit = 0;
    for (uint32_t v : values) {
        for (unsigned k = 0; k < width; ++k, ++bit)
            if ((v >> k) & 1) out[bit >> 3] |= uint8_t(1u << (bit & 7));
    }
}

uint32_t packPair(uint16_t first, uint16_t second) { return uint32_t(first) << 16 | second; }

struct SynCell {
    uint16_t qmin[3], qmax[3];
    std::vector<uint32_t> objects;
    std::vector<std::array<uint32_t, 4>> portals;  // raw portal words
};
struct SynTile {
    float mn[3], mx[3];
    float portalExpand = 0;
    std::vector<SynCell> cells;
    std::vector<uint8_t> viewCodes;      // view tree, breadth first
    std::vector<uint32_t> leafMap;       // per view leaf: word index into cellIndexList
    std::vector<uint32_t> cellIndexList; // runs of local cells ended by 0xFFFFFFFF
};
struct SynObject {
    float mn[3], mx[3];
    float farSq = std::numeric_limits<float>::infinity();
    uint32_t userId = 0;
};
struct SynTome {
    float mn[3], mx[3];
    std::vector<uint8_t> treeCodes;
    std::vector<float> splits;
    std::vector<int> tileOfNode;  // -1: no tile
    std::vector<SynTile> tiles;
    std::vector<SynObject> objects;
};

std::vector<uint8_t> buildTome(const SynTome& s) {
    Buf t;
    t.alloc(kUmbraHeaderSize);
    const uint32_t nObj = uint32_t(s.objects.size());
    unsigned width = 1;
    while (width < 31 && (uint32_t(1) << width) < nObj) ++width;
    t.u32(0x00, kUmbraTomeVersion);
    t.f32(0x0C, 128.0f);
    for (int a = 0; a < 3; ++a) {
        t.f32(0x14 + 4 * a, s.mn[a]);
        t.f32(0x20 + 4 * a, s.mx[a]);
        t.f32(0x150 + 4 * a, s.mn[a]);
        t.f32(0x15C + 4 * a, s.mx[a]);
    }
    const uint32_t nNodes = uint32_t(s.treeCodes.size());
    uint32_t leaves = 0;
    for (uint8_t c : s.treeCodes) leaves += c == 3;
    const auto tree = serializeTree(s.treeCodes);
    const uint32_t treeOff = t.alloc(tree.size());
    std::memcpy(t.b.data() + treeOff, tree.data(), tree.size());
    t.u32(0x2C, nNodes << 5);
    t.u32(0x30, treeOff);
    bool anyInner = false;
    for (uint8_t c : s.treeCodes) anyInner |= c != 3;
    if (anyInner) {
        const uint32_t sp = t.alloc(nNodes * 4);
        for (uint32_t i = 0; i < nNodes; ++i) t.f32(sp + 4 * i, s.splits[i]);
        t.u32(0x38, nNodes);
        t.u32(0x3C, sp);
    }
    // Objects.
    t.u32(0x40, nObj);
    const uint32_t ob = t.alloc(nObj * 24), od = t.alloc(nObj * 32), uid = t.alloc(nObj * 4);
    for (uint32_t o = 0; o < nObj; ++o) {
        for (int a = 0; a < 3; ++a) {
            t.f32(ob + 24 * o + 4 * a, s.objects[o].mn[a]);
            t.f32(ob + 24 * o + 12 + 4 * a, s.objects[o].mx[a]);
            t.f32(od + 32 * o + 4 * a, s.objects[o].mn[a]);
            t.f32(od + 32 * o + 16 + 4 * a, s.objects[o].mx[a]);
        }
        t.f32(od + 32 * o + 12, 0.0f);
        t.f32(od + 32 * o + 28, s.objects[o].farSq);
        t.u32(uid + 4 * o, s.objects[o].userId);
    }
    t.u32(0x44, ob);
    t.u32(0x48, od);
    t.u32(0x50, uid);
    t.u32(0x54, width);
    std::vector<uint32_t> list;
    std::vector<std::vector<uint32_t>> cellListStart;
    for (const SynTile& tl : s.tiles) {
        cellListStart.emplace_back();
        for (const SynCell& c : tl.cells) {
            cellListStart.back().push_back(uint32_t(list.size()));
            list.insert(list.end(), c.objects.begin(), c.objects.end());
        }
    }
    std::vector<uint8_t> packed;
    packBits(packed, list, width);
    const uint32_t ol = t.alloc(packed.size());
    std::memcpy(t.b.data() + ol, packed.data(), packed.size());
    t.u32(0x58, ol);
    t.u32(0x5C, uint32_t(list.size()));
    // Per-node tables.
    const uint32_t lod = t.alloc(nNodes * 4), pexp = t.alloc(nNodes * 4), cstart = t.alloc((nNodes + 1) * 4), table = t.alloc(nNodes * 4);
    t.u32(0x8C, leaves);
    t.u32(0x90, nNodes);
    t.u32(0x88, cstart);
    t.u32(0x9C, lod);
    t.u32(0xA0, table);
    t.u32(0x14C, pexp);
    uint32_t cellTotal = 0;
    for (uint32_t i = 0; i < nNodes; ++i) {
        t.u32(cstart + 4 * i, cellTotal);
        const int ti = s.tileOfNode[i];
        t.f32(lod + 4 * i, ti < 0 ? -1.0f : 1.0f);
        if (ti < 0) continue;
        const SynTile& tl = s.tiles[size_t(ti)];
        t.f32(pexp + 4 * i, tl.portalExpand);
        const uint32_t to = t.alloc(kUmbraTileSize);
        t.u32(table + 4 * i, to);
        for (int a = 0; a < 3; ++a) {
            t.f32(to + 4 * a, tl.mn[a]);
            t.f32(to + 12 + 4 * a, tl.mx[a]);
        }
        float ext = 0;
        for (int a = 0; a < 3; ++a) ext = std::max(ext, tl.mx[a] - tl.mn[a]);
        const float scale = ext / 65536.0f;
        t.f32(to + 0x44, scale);
        t.f32(to + 0x48, 1.0f / scale);
        for (int a = 0; a < 3; ++a) {
            t.u16(to + 0x4C + 4 * a, 0);
            t.u16(to + 0x4E + 4 * a, uint16_t(std::min(65535.0f, std::ceil((tl.mx[a] - tl.mn[a]) / scale) - 1)));
        }
        t.f32(to + 0x30, tl.portalExpand);
        t.u32(to + 0x34, uint32_t(tl.cells.size()));
        const uint32_t co = t.alloc(tl.cells.size() * kUmbraCellSize);
        t.u32(to + 0x38, co - to);
        uint32_t nPortals = 0;
        for (const SynCell& c : tl.cells) nPortals += uint32_t(c.portals.size());
        const uint32_t po = t.alloc(std::max<uint32_t>(nPortals, 1) * kUmbraPortalSize);
        t.u32(to + 0x3C, nPortals ? po - to : 0);
        uint32_t p = 0;
        for (size_t c = 0; c < tl.cells.size(); ++c) {
            const SynCell& cell = tl.cells[c];
            const uint32_t x = co + uint32_t(c) * kUmbraCellSize;
            t.u32(x + 0, p);
            t.u32(x + 4, uint32_t(cell.portals.size()));
            t.u32(x + 8, cellListStart[size_t(ti)][c]);
            t.u32(x + 12, uint32_t(cell.objects.size()));
            t.u32(x + 16, kUmbraNone);
            t.u32(x + 20, 0x80000401u);
            t.u32(x + 24, packPair(cell.qmin[0], cell.qmin[1]));
            t.u32(x + 28, packPair(cell.qmin[2], cell.qmax[0]));
            t.u32(x + 32, packPair(cell.qmax[1], cell.qmax[2]));
            for (const auto& w : cell.portals) {
                for (int k = 0; k < 4; ++k) t.u32(po + 16 * p + 4 * k, w[size_t(k)]);
                ++p;
            }
        }
        const uint32_t ci = t.alloc(tl.cellIndexList.size() * 4);
        for (size_t k = 0; k < tl.cellIndexList.size(); ++k) t.u32(ci + 4 * uint32_t(k), tl.cellIndexList[k]);
        t.u32(to + 0x40, ci - to);
        const auto vt = serializeTree(tl.viewCodes);
        const uint32_t vo = t.alloc(vt.size());
        std::memcpy(t.b.data() + vo, vt.data(), vt.size());
        unsigned mw = 1;
        while ((size_t(1) << mw) < tl.cellIndexList.size()) ++mw;
        std::vector<uint8_t> map;
        packBits(map, tl.leafMap, mw);
        const uint32_t mo = t.alloc(map.size());
        std::memcpy(t.b.data() + mo, map.data(), map.size());
        t.u32(to + 0x18, uint32_t(tl.viewCodes.size()) << 5 | mw);
        t.u32(to + 0x1C, vo - to);
        t.u32(to + 0x20, mo - to);
        t.alloc(0);
        t.u32(to + 0x2C, (uint32_t(t.b.size()) - to) << 8 | 5u);
        cellTotal += uint32_t(tl.cells.size());
    }
    t.u32(cstart + 4 * nNodes, cellTotal);
    t.alloc(0);
    t.u32(0x08, uint32_t(t.b.size()));
    t.u32(0x04, umbraCrc32c(t.b.data() + 8, t.b.size() - 8));
    return t.b;
}

// Two tiles side by side on x (A = [0,512], B = [512,1024], both 512 high and deep). Tile A has three cells: 0 (x < 256),
// 1 (x >= 256, y < 256) and 2 (x >= 256, y >= 256, sealed: no portals). A door joins cells 0 and 1 on x = 256 over
// y 64..128, z 0..128; cell 1 opens into tile B's single cell over the whole y < 256 half of x = 512.
// Objects: 0 surface in cell 0, 1 smodel in cell 1, 2 light in sealed cell 2, 3 probe high in cell 1 (out of the
// door's view from cell 0), 4 decal collection in tile B.
SynTome twoTileTome() {
    SynTome s;
    for (int a = 0; a < 3; ++a) {
        s.mn[a] = 0;
        s.mx[a] = a == 0 ? 1024.0f : 512.0f;
    }
    s.treeCodes = {0, 3, 3};
    s.splits = {512.0f, 0, 0};
    s.tileOfNode = {-1, 0, 1};
    SynTile A;
    for (int a = 0; a < 3; ++a) {
        A.mn[a] = 0;
        A.mx[a] = 512;
    }
    A.portalExpand = 2.0f;
    // tile quanta: 512 / 65536 = 1/128 unit
    const uint32_t doorY = packPair(64 * 128, 128 * 128 - 1), doorZ = packPair(0, 128 * 128 - 1);
    SynCell c0{{0, 0, 0}, {0x7FFF, 0xFFFF, 0xFFFF}, {0}, {}};
    c0.portals.push_back({1u << 29 | 1u << 25 | 0x0200u, 1u << 16 | 0x8000u, doorY, doorZ});
    SynCell c1{{0x8000, 0, 0}, {0xFFFF, 0x7FFF, 0xFFFF}, {1, 3}, {}};
    c1.portals.push_back({0u << 29 | 1u << 25 | 0x0200u, 0u << 16 | 0x8000u, doorY, doorZ});
    c1.portals.push_back({1u << 29 | 2u, 0u << 16 | 0x0200u, packPair(0, 0x7FFF), packPair(0, 0xFFFF)});  // to tile node 2
    SynCell c2{{0x8000, 0x8000, 0}, {0xFFFF, 0xFFFF, 0xFFFF}, {2}, {}};
    A.cells = {c0, c1, c2};
    A.viewCodes = {0, 3, 1, 3, 3};  // split x; left leaf; right split y; two leaves
    A.cellIndexList = {0, kUmbraNone, 1, kUmbraNone, 2, kUmbraNone};
    A.leafMap = {0, 2, 4};
    SynTile B;
    B.mn[0] = 512;
    B.mx[0] = 1024;
    for (int a = 1; a < 3; ++a) {
        B.mn[a] = 0;
        B.mx[a] = 512;
    }
    SynCell b0{{0, 0, 0}, {0xFFFF, 0xFFFF, 0xFFFF}, {4}, {}};
    b0.portals.push_back({0u << 29 | 1u, 1u << 16 | 0x0200u, packPair(0, 0x7FFF), packPair(0, 0xFFFF)});  // back to node 1, cell 1
    B.cells = {b0};
    B.viewCodes = {3};
    B.cellIndexList = {0, kUmbraNone};
    B.leafMap = {0};
    s.tiles = {A, B};
    auto obj = [](float x, float y, float z, uint32_t id) {
        SynObject o;
        o.mn[0] = x - 10;
        o.mn[1] = y - 10;
        o.mn[2] = z - 10;
        o.mx[0] = x + 10;
        o.mx[1] = y + 10;
        o.mx[2] = z + 10;
        o.userId = id;
        return o;
    };
    s.objects = {obj(100, 100, 50, 0x00000000u), obj(400, 100, 50, 0x10000005u), obj(400, 400, 50, 0x40000003u),
                 obj(400, 200, 400, 0x50000001u), obj(800, 100, 50, 0x60000002u)};
    s.objects[0].farSq = 60.0f * 60.0f;
    return s;
}

// Port of the converter's replayrender::BuildConservativeUmbraTome (x64-zt replay_render.cpp): one tile, one cell
// holding every object, every object spanning the whole box, a valid CRC32C.
std::vector<uint8_t> conservativeTome(const std::vector<uint32_t>& ids, const float mn[3], const float mx[3]) {
    const uint32_t n = uint32_t(ids.size());
    unsigned width = 1;
    while (width < 31 && (uint32_t(1) << width) < n) ++width;
    std::vector<uint32_t> seq(n);
    for (uint32_t i = 0; i < n; ++i) seq[i] = i;
    std::vector<uint8_t> packed;
    packBits(packed, seq, width);
    packed.resize((size_t(n) * width + 31) / 32 * 4);
    Buf t;
    t.alloc(kUmbraHeaderSize);
    t.u32(0x00, kUmbraTomeVersion);
    t.f32(0x0C, 128.0f);
    t.u32(0x40, n);
    t.u32(0x54, width);
    t.u32(0x5C, n);
    t.u32(0x8C, 1);
    t.u32(0x90, 1);
    for (int a = 0; a < 3; ++a) {
        t.f32(0x14 + 4 * a, mn[a]);
        t.f32(0x20 + 4 * a, mx[a]);
        t.f32(0x150 + 4 * a, mn[a]);
        t.f32(0x15C + 4 * a, mx[a]);
    }
    const uint32_t topTree = t.alloc(4);
    t.u32(topTree, 3);
    const uint32_t topMap = t.alloc(4);
    t.u32(0x2C, 33);
    t.u32(0x30, topTree);
    t.u32(0x34, topMap);
    const uint32_t ob = t.alloc(n * 24), od = t.alloc(n * 32);
    for (uint32_t o = 0; o < n; ++o)
        for (int a = 0; a < 3; ++a) {
            const float mid = (mn[a] + mx[a]) * 0.5f, half = (mx[a] - mn[a]) * 0.5f;
            t.f32(ob + 24 * o + 4 * a, mid - half);
            t.f32(ob + 24 * o + 12 + 4 * a, mid + half);
            t.f32(od + 32 * o + 4 * a, mid - half);
            t.f32(od + 32 * o + 16 + 4 * a, mid + half);
            t.f32(od + 32 * o + 28, std::numeric_limits<float>::infinity());
        }
    t.u32(0x44, ob);
    t.u32(0x48, od);
    const uint32_t uid = t.alloc(n * 4);
    for (uint32_t o = 0; o < n; ++o) t.u32(uid + 4 * o, ids[o]);
    t.u32(0x50, uid);
    const uint32_t ol = t.alloc(packed.size());
    std::memcpy(t.b.data() + ol, packed.data(), packed.size());
    t.u32(0x58, ol);
    const uint32_t cs = t.alloc(8);
    t.u32(cs + 4, 1);
    t.u32(0x88, cs);
    const uint32_t lod = t.alloc(4);
    t.f32(lod, 1.0f);
    t.u32(0x9C, lod);
    const uint32_t offs = t.alloc(4);
    t.u32(0xA0, offs);
    const uint32_t pe = t.alloc(4);
    t.u32(0x14C, pe);
    const uint32_t tile = t.alloc(kUmbraTileSize);
    for (int a = 0; a < 3; ++a) {
        t.f32(tile + 4 * a, mn[a]);
        t.f32(tile + 12 + 4 * a, mx[a]);
    }
    t.u32(tile + 0x18, 33);
    t.u32(tile + 0x34, 1);
    double ext = 0;
    for (int a = 0; a < 3; ++a) ext = std::max(ext, double(mx[a]) - mn[a]);
    float scale = float(ext / 65536.0);
    if (double(scale) < ext / 65536.0) scale = std::nextafter(scale, std::numeric_limits<float>::infinity());
    t.f32(tile + 0x44, scale);
    t.f32(tile + 0x48, 1.0f / scale);
    t.f32(0x168, scale);
    const uint32_t tt = t.alloc(4);
    t.u32(tt, 3);
    t.u32(tile + 0x1C, tt - tile);
    const uint32_t tm = t.alloc(8);
    t.u32(tile + 0x20, tm - tile);
    const uint32_t ci = t.alloc(8);
    t.u32(ci + 4, kUmbraNone);
    t.u32(tile + 0x40, ci - tile);
    const uint32_t cell = t.alloc(36);
    t.u32(cell + 0x0C, n);
    t.u32(cell + 0x10, kUmbraNone);
    t.u32(cell + 0x14, 0x80000000u);
    t.u32(cell + 0x1C, 0x0000FFFFu);
    t.u32(cell + 0x20, kUmbraNone);
    t.u32(tile + 0x38, cell - tile);
    t.alloc(0);
    t.u32(tile + 0x2C, (uint32_t(t.b.size()) - tile) << 8 | 3u);
    t.u32(offs, tile);
    t.u32(0x08, uint32_t(t.b.size()));
    t.u32(0x04, umbraCrc32c(t.b.data() + 8, t.b.size() - 8));
    return t.b;
}

bool contains(const std::vector<uint32_t>& v, uint32_t x) { return std::binary_search(v.begin(), v.end(), x); }

}  // namespace

TEST(umbra_crc32c) {
    // CRC-32C check value.
    const char* s = "123456789";
    CHECK(umbraCrc32c(reinterpret_cast<const uint8_t*>(s), 9) == 0xE3069283u);
    CHECK(std::string(umbraClassName(kUmbraStaticModel)) == "static model");
    CHECK(umbraUserIdClass(0x60000002u) == kUmbraDecalCollection && umbraUserIdIndex(0x60000002u) == 2);
}

TEST(umbra_conservative_tome) {
    const float mn[3] = {-1000, -2000, -300}, mx[3] = {3000, 1000, 700};
    const std::vector<uint32_t> ids = {0, 1, 2, 0x10000000u, 0x10000001u, 0x40000000u, 0x50000000u, 0x60000000u};
    const auto bytes = conservativeTome(ids, mn, mx);
    UmbraTome tome;
    std::string err;
    CHECK(tome.parse(bytes, &err));
    CHECK(err.empty());
    CHECK(tome.checksum() == UmbraChecksum::Valid);
    CHECK(tome.header().numTiles == 1 && tome.header().numLeafTiles == 1 && tome.objectCount() == 8);
    CHECK(tome.cells().size() == 1 && tome.portals().empty());
    const auto rep = tome.validate();
    CHECK(rep.ok);
    CHECK(rep.leafTiles == 1 && rep.lodTiles == 0 && rep.viewLeaves == 1 && rep.solidViewLeaves == 0);
    CHECK(rep.objectsReferenced == 8 && rep.userIdCount == 8);
    CHECK(rep.userIdsByClass[kUmbraSurface] == 3 && rep.userIdsByClass[kUmbraStaticModel] == 2);
    CHECK(tome.cellObjects(0).size() == 8 && tome.cellObjects(0)[7] == 7);
    // Every object is visible from anywhere inside, nothing from outside.
    const float in[3] = {0, 0, 0}, out[3] = {5000, 0, 0};
    const auto v = tome.queryPoint(in);
    CHECK(v.status == UmbraQueryStatus::Ok && v.objects.size() == 8 && v.userIds.size() == 8);
    CHECK(v.userIdsByClass[kUmbraSurface] == 3 && v.userIdsByClass[kUmbraDecalCollection] == 1);
    CHECK(tome.queryPoint(out).status == UmbraQueryStatus::OutsideTome);
    const float fwd[3] = {0, 1, 0}, up[3] = {0, 0, 1};
    CHECK(tome.queryView(in, fwd, up, 1.0f, 0.6f).objects.size() == 8);
}

TEST(umbra_two_tile_tome) {
    const SynTome s = twoTileTome();
    const auto bytes = buildTome(s);
    UmbraTome tome;
    std::string err;
    CHECK(tome.parse(bytes, &err));
    if (!err.empty()) std::printf("  umbra synthetic parse: %s\n", err.c_str());
    const auto rep = tome.validate();
    for (const auto& e : rep.errors) std::printf("  umbra synthetic: %s\n", e.c_str());
    CHECK(rep.ok);
    CHECK(rep.treeNodes == 3 && rep.treeLeaves == 2 && rep.leafTiles == 2 && rep.cells == 4);
    CHECK(rep.internalPortals == 2 && rep.finePortals == 2 && rep.unmatchedInternal == 0 && rep.unmatchedFine == 0);
    CHECK(rep.viewLeaves == 4 && rep.objectsReferenced == 5);
    CHECK(tome.tiles()[0].offset == 0 && tome.tiles()[1].cellBase == 0 && tome.tiles()[2].cellBase == 3);

    // Point lookups: tile tree (split x = 512), view tree (x then y at the tile's midpoints).
    const float p0[3] = {100, 100, 50}, p1[3] = {300, 100, 50}, p2[3] = {300, 300, 50}, pb[3] = {800, 100, 50};
    uint32_t tile = 0;
    CHECK(tome.cellsAt(p0, &tile) == std::vector<uint32_t>{0} && tile == 1);
    CHECK(tome.cellsAt(p1) == std::vector<uint32_t>{1});
    CHECK(tome.cellsAt(p2) == std::vector<uint32_t>{2});
    CHECK(tome.cellsAt(pb, &tile) == std::vector<uint32_t>{3} && tile == 2);
    float mn[3], mx[3];
    tome.cellBounds(1, mn, mx);
    CHECK(mn[0] == 256 && mx[0] == 512 && mn[1] == 0 && mx[1] == 256 && mx[2] == 512);
    float q[4][3];
    CHECK(tome.portalQuad(0, q));
    CHECK(q[0][0] == 256 && q[0][1] == 64 && q[2][1] == 128 && q[0][2] == 0 && q[2][2] == 128);
    CHECK(tome.portals()[2].targetCell == 3 && tome.portals()[3].targetCell == 1 && !tome.portals()[2].internal);

    // From cell 0: the door shows object 1 (and tile B's object 4 through it), not the sealed cell's object 2 and not
    // object 3 high above the door's view.
    UmbraQueryOptions opt;
    auto v = tome.queryPoint(p0, opt);
    CHECK(v.status == UmbraQueryStatus::Ok && v.startCells == std::vector<uint32_t>{0});
    CHECK(contains(v.objects, 0) && contains(v.objects, 1) && contains(v.objects, 4));
    CHECK(!contains(v.objects, 2) && !contains(v.objects, 3));
    CHECK(contains(v.cells, 0) && contains(v.cells, 1) && contains(v.cells, 3) && !contains(v.cells, 2));
    CHECK(v.userIdsByClass[kUmbraSurface] == 1 && v.userIdsByClass[kUmbraStaticModel] == 1 &&
          v.userIdsByClass[kUmbraDecalCollection] == 1 && v.userIdsByClass[kUmbraPrimaryLight] == 0);
    // Looking away from the door: only the object around the camera.
    const float back[3] = {-1, 0, 0}, up[3] = {0, 0, 1};
    v = tome.queryView(p0, back, up, 1.0f, 1.0f, opt);
    CHECK(v.objects == std::vector<uint32_t>{0});
    // The sealed cell sees only itself.
    v = tome.queryPoint(p2, opt);
    CHECK(v.objects == std::vector<uint32_t>{2} && v.cells == std::vector<uint32_t>{2});
    // From tile B: through the external portal and the door back to cell 0 (object 0 itself is beyond its 60-unit far
    // distance); object 3 is in view of the external portal.
    v = tome.queryPoint(pb, opt);
    CHECK(v.objects == (std::vector<uint32_t>{1, 3, 4}) && v.cells == (std::vector<uint32_t>{0, 1, 3}));
    opt.distanceCull = false;
    v = tome.queryPoint(pb, opt);
    opt.distanceCull = true;
    CHECK(v.objects == (std::vector<uint32_t>{0, 1, 3, 4}));
    if (v.objects != std::vector<uint32_t>{0, 1, 3, 4}) {
        std::printf("  umbra from tile B: objects");
        for (uint32_t o : v.objects) std::printf(" %u", o);
        std::printf(", cells");
        for (uint32_t c : v.cells) std::printf(" %u", c);
        std::printf("\n");
    }
    // Object 0's far distance (60) culls it from 130 units away unless distance culling is off.
    const float p0far[3] = {240, 100, 50};
    v = tome.queryPoint(p0far, opt);
    CHECK(!contains(v.objects, 0) && contains(v.objects, 1));
    opt.distanceCull = false;
    CHECK(contains(tome.queryPoint(p0far, opt).objects, 0));
    // Outside the tome box.
    const float outside[3] = {2000, 0, 0};
    CHECK(tome.queryPoint(outside).status == UmbraQueryStatus::OutsideTome);

    // Corruption: version, size field, truncation, checksum.
    {
        auto bad = bytes;
        bad[0] ^= 1;
        CHECK(!UmbraTome().parse(bad, &err) && !err.empty());
        bad = bytes;
        bad[8] ^= 0x10;
        CHECK(!UmbraTome().parse(bad));
        bad = bytes;
        bad.resize(bad.size() - 16);
        CHECK(!UmbraTome().parse(bad));
        bad = bytes;
        bad[bad.size() - 40] ^= 0x55;  // inside tile B's data
        UmbraTome t2;
        if (t2.parse(bad)) CHECK(t2.checksum() == UmbraChecksum::Invalid && !t2.validate().ok);
        bad = bytes;
        uint32_t disabled = kUmbraNoChecksum;
        std::memcpy(bad.data() + 4, &disabled, 4);
        CHECK(t2.parse(bad) && t2.checksum() == UmbraChecksum::Disabled && t2.validate().ok);
    }

    // The JSON cutter on an export-shaped text.
    std::string hex;
    static const char* digits = "0123456789abcdef";
    for (uint8_t b : bytes) {
        hex += digits[b >> 4];
        hex += digits[b & 15];
    }
    const std::string json = "{\"fields\":{\"numUmbraGates\":0,\"umbraGates\":null,\"umbraTomeSize\":" + std::to_string(bytes.size()) +
                             ",\"umbraTomeData\":{\"address\":\"0x1\",\"type\":\"unsigned __int8\",\"stride\":1,\"count\":" +
                             std::to_string(bytes.size()) + ",\"bytes\":\"" + hex + "\"},\"umbraTome\":null}}";
    std::vector<uint8_t> cut;
    CHECK(extractUmbraTomeFromGfxMapJsonText(json, cut, &err) && cut == bytes);
    CHECK(!extractUmbraTomeFromGfxMapJsonText("{\"umbraTomeData\":null}", cut, &err) && cut.empty());
    CHECK(!extractUmbraTomeFromGfxMapJsonText("{\"x\":1}", cut, &err));
    const fs::path tmp = fs::path(test::tmpDir()) / "umbra_cut.json";
    {
        std::FILE* f = std::fopen(tmp.string().c_str(), "wb");
        if (f) {
            std::fwrite(json.data(), 1, json.size(), f);
            std::fclose(f);
        }
    }
    CHECK(extractUmbraTomeFromGfxMapJson(tmp.string(), cut, &err) && cut == bytes);
}

// ---------------------------------------------------------------------------------------------------------------
// Stock tomes (world cache)
// ---------------------------------------------------------------------------------------------------------------

namespace {

fs::path worldCache() {
    const char* local = std::getenv("LOCALAPPDATA");
    return local ? fs::path(local) / "IW8 Radiant Suite" / "IW8 Radiant Suite" / "world_cache" : fs::path();
}

fs::path gfxMapJson(const std::string& zone) {
    const fs::path dir = worldCache() / zone / "assets" / "gfx_map";
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return {};
    for (const auto& e : fs::recursive_directory_iterator(dir, ec))
        if (e.is_regular_file() && e.path().extension() == ".json") return e.path();
    return {};
}

struct ZoneExpect {
    const char* zone;
    uint32_t size, crc, objects, tiles, leafTiles, clusters, gates;
};

// Fingerprints of the stock tomes (Replay 1.20 world cache exports, 2026-11-13).
const ZoneExpect kZones[] = {
    {"mp_shipment", 5614672, 0x810A311Fu, 10483, 4089, 2045, 3440, 0},
    {"mp_hideout", 2806416, 0x7C93B531u, 11575, 2397, 1199, 2638, 21},
    {"mp_aniyah_tac", 11530368, 0xF3D95891u, 51693, 6237, 3119, 6769, 0},
    {"mp_backlot2", 8441152, 0x1EDFBAE8u, 21418, 8021, 4011, 6964, 1},
    {"mp_rust", 1206208, 0x9DD48B7Cu, 7066, 957, 479, 913, 0},
    {"mp_village2", 2131040, 0x6F75D225u, 15118, 753, 377, 899, 0},
    {"mp_emporium", 4295056, 0x8AA2A450u, 17334, 3545, 1773, 3290, 0},
    {"mp_m_speedball", 1617776, 0x63B9E167u, 3528, 1645, 823, 1338, 0},
};

// Checks one stock tome; returns false when the zone is not on this machine.
bool checkStockZone(const ZoneExpect& z, bool printInfo) {
    const fs::path json = gfxMapJson(z.zone);
    if (json.empty()) return false;
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<uint8_t> bytes;
    std::string err;
    CHECK(extractUmbraTomeFromGfxMapJson(json.string(), bytes, &err));
    const auto t1 = std::chrono::steady_clock::now();
    CHECK(bytes.size() == z.size);
    const uint32_t crc = umbraCrc32c(bytes.data(), bytes.size());
    if (z.crc) CHECK(crc == z.crc);
    UmbraTome tome;
    CHECK(tome.parse(std::move(bytes), &err));
    if (!err.empty()) std::printf("  %s: %s\n", z.zone, err.c_str());
    const auto& h = tome.header();
    CHECK(h.version == kUmbraTomeVersion && h.numTomes == 0 && h.numMatchingTrees == 0 && h.numFaces == 0);
    if (z.objects) CHECK(h.numObjects == z.objects);
    if (z.tiles) CHECK(h.numTiles == z.tiles && h.numLeafTiles == z.leafTiles);
    if (z.clusters) CHECK(h.numClusters == z.clusters);
    CHECK(h.numGates == z.gates);
    const auto rep = tome.validate();
    const auto t2 = std::chrono::steady_clock::now();
    for (const auto& e : rep.errors) std::printf("  %s: %s\n", z.zone, e.c_str());
    CHECK(rep.ok);
    CHECK(rep.checksum == UmbraChecksum::Disabled);
    CHECK(rep.treeLeaves == h.numLeafTiles && rep.treeNodes == 2 * h.numLeafTiles - 1);
    CHECK(rep.slotPathsChecked == h.numTiles);
    CHECK(rep.objectsReferenced == h.numObjects);
    CHECK(rep.unmatchedInternal == 0 && rep.unmatchedFine == 0 && rep.unmatchedCoarse == 0 && rep.unmatchedClusterPortals == 0);
    CHECK(rep.finePortalsTouchingLod == 0 && rep.coarsePortalsBetweenLeafTiles == 0);
    CHECK(rep.userIdsByClass[7] == 0);
    CHECK(rep.gatePortalsWithBox == rep.gatePortals && rep.gateUserIdsOfClass2 == h.numGates);
    if (printInfo)
        std::printf("  umbra %s: %u bytes crc32c %08X, %u objects (%u ids: surf %u smodel %u gate %u vol %u light %u probe %u decal %u), "
                    "%u tiles (%u leaf, %u LOD, %u empty leaves), %u cells, %u portals (%u internal, %u fine, %u coarse, %u gate, %u outside; "
                    "unmatched %u/%u/%u), %u clusters / %u portals (unmatched %u), view leaves %u (solid %u, multi %u)\n",
                    z.zone, h.size, crc, h.numObjects, rep.userIdCount, rep.userIdsByClass[0], rep.userIdsByClass[1], rep.userIdsByClass[2],
                    rep.userIdsByClass[3], rep.userIdsByClass[4], rep.userIdsByClass[5], rep.userIdsByClass[6], h.numTiles, rep.leafTiles,
                    rep.lodTiles, rep.emptyLeaves, rep.cells, rep.portals, rep.internalPortals, rep.finePortals, rep.coarsePortals,
                    rep.gatePortals, rep.outsidePortals, rep.unmatchedInternal, rep.unmatchedFine, rep.unmatchedCoarse, h.numClusters,
                    rep.clusterPortals, rep.unmatchedClusterPortals, rep.viewLeaves, rep.solidViewLeaves, rep.multiCellViewLeaves);

    // Queries from the centres of leaf cells that list objects: the camera cell's own objects are all visible (no
    // distance culling), every reported object is listed by a visible cell, and on a stock map Umbra hides something.
    UmbraQueryOptions opt;
    opt.distanceCull = false;
    unsigned samples = 0, culled = 0;
    double visibleShare = 0;
    size_t steps = 0;
    const size_t stride = std::max<size_t>(1, tome.cells().size() / 7);
    for (size_t ci = 0; ci < tome.cells().size() && samples < 6; ci += stride) {
        const UmbraCell& c = tome.cells()[ci];
        if (!tome.tiles()[c.tile].hasViewTree() || !c.objectCount) continue;
        float mn[3], mx[3], p[3];
        tome.cellBounds(uint32_t(ci), mn, mx);
        for (int a = 0; a < 3; ++a) p[a] = 0.5f * (mn[a] + mx[a]);
        const auto at = tome.cellsAt(p);
        if (std::find(at.begin(), at.end(), uint32_t(ci)) == at.end()) continue;  // centre in another cell (concave)
        const auto v = tome.queryPoint(p, opt);
        ++samples;
        steps += v.steps;
        CHECK(v.status == UmbraQueryStatus::Ok && !v.truncated);
        bool ownVisible = true;
        for (uint32_t o : tome.cellObjects(uint32_t(ci))) ownVisible = ownVisible && contains(v.objects, o);
        CHECK(ownVisible);
        CHECK(contains(v.cells, uint32_t(ci)));
        std::vector<uint8_t> listed(h.numObjects, 0);
        for (uint32_t vc : v.cells)
            for (uint32_t o : tome.cellObjects(vc)) listed[o] = 1;
        bool allListed = true;
        for (uint32_t o : v.objects) allListed = allListed && listed[o];
        CHECK(allListed);
        if (v.objects.size() < h.numObjects) ++culled;
        visibleShare += double(v.objects.size()) / double(h.numObjects);
    }
    CHECK(samples > 0);
    CHECK(culled > 0);
    const auto t3 = std::chrono::steady_clock::now();
    auto msBetween = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    const double ms = msBetween(t0, t3);
    if (printInfo)
        std::printf("  umbra %s: %u sample queries (%u cull something, %.0f%% of objects visible on average), %zu steps; "
                    "cut %.0f ms, parse + validate %.0f ms, queries %.0f ms (%.0f ms total)\n",
                    z.zone, samples, culled, samples ? 100.0 * visibleShare / samples : 0.0, steps, msBetween(t0, t1),
                    msBetween(t1, t2), msBetween(t2, t3), ms);
    return true;
}

}  // namespace

TEST(umbra_stock_mp_tomes) {
    unsigned found = 0;
    for (const ZoneExpect& z : kZones) {
        if (!checkStockZone(z, true)) {
            test::skipf("  SKIP umbra: world_cache/%s gfx_map export not found\n", z.zone);
            continue;
        }
        ++found;
    }
    CHECK(found == 0 || found >= 6);
}

TEST(umbra_stock_verdansk) {
    // Verdansk's main zone: a 420 MB export with a 60 MB tome.
    const ZoneExpect z{"mp_donetsk", 60383968, 0xE91F7509u, 85477, 20627, 10314, 24465, 10};
    if (!checkStockZone(z, true)) test::skipf("  SKIP umbra: world_cache/mp_donetsk gfx_map export not found\n");
}
