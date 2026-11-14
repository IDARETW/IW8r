// Umbra 3 tome parser and visibility query (src/core/umbra_tome.h, docs/research/110_umbra_tome.md).
#include "umbra_tome.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace iw8r {

namespace {

constexpr uint32_t kTargetMask = 0x1FFFFFFu;  // d0 bits 0-24: target tile (DW 0x142003C0F: and eax, 0x1ffffff)

int popcount32(uint32_t v) {
    v = v - ((v >> 1) & 0x55555555u);
    v = (v & 0x33333333u) + ((v >> 2) & 0x33333333u);
    return int((((v + (v >> 4)) & 0x0F0F0F0Fu) * 0x01010101u) >> 24);
}

// Inner nodes (2-bit code != 3) among the first k fields of a tree word.
uint32_t innerInWord(uint32_t w, uint32_t k) {
    if (!k) return 0;
    const uint32_t leaves = (w & (w >> 1)) & 0x55555555u;
    const uint32_t mask = k >= 16 ? 0xFFFFFFFFu : ((1u << (2 * k)) - 1u);
    return k - uint32_t(popcount32(leaves & mask));
}

void setErr(std::string* err, const std::string& m) {
    if (err) *err = m;
}

std::string hex32(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%X", v);
    return b;
}

}  // namespace

const char* umbraClassName(unsigned cls) {
    switch (cls) {
        case kUmbraSurface: return "surface";
        case kUmbraStaticModel: return "static model";
        case kUmbraGate: return "gate";
        case kUmbraVolumetric: return "volumetric";
        case kUmbraPrimaryLight: return "primary light";
        case kUmbraReflectionProbe: return "reflection probe";
        case kUmbraDecalCollection: return "decal collection";
        default: return "class 7";
    }
}

uint32_t umbraCrc32c(const uint8_t* data, size_t size) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0x82F63B78u & (0u - (c & 1u)));
            t[i] = c;
        }
        return t;
    }();
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) crc = (crc >> 8) ^ table[(crc ^ data[i]) & 0xFFu];
    return ~crc;
}

// ---------------------------------------------------------------------------------------------------------------
// Decoding
// ---------------------------------------------------------------------------------------------------------------

namespace {

struct Reader {
    const std::vector<uint8_t>& d;
    bool has(uint64_t off, uint64_t size) const { return off <= d.size() && size <= d.size() - off; }
    uint32_t u32(uint64_t off) const {
        uint32_t v;
        std::memcpy(&v, d.data() + off, 4);
        return v;
    }
    float f32(uint64_t off) const {
        float v;
        std::memcpy(&v, d.data() + off, 4);
        return v;
    }
    uint16_t u16(uint64_t off) const {
        uint16_t v;
        std::memcpy(&v, d.data() + off, 2);
        return v;
    }
};

// Bytes of a serialized tree: the 2-bit codes, then the rank directory of inner-node counts: u32 per 65536-node
// block after the first, u16 per 256-node superblock that does not start a block (relative to its block, padded to 4
// bytes), u8 per tree word that does not start a superblock (relative to its superblock, padded to 4 bytes).
uint64_t treeBytes(uint32_t nodes) {
    if (!nodes) return 0;
    const uint64_t words = (uint64_t(nodes) * 2 + 31) / 32;
    const uint64_t supers = (words + 15) / 16;
    const uint64_t blocks = (words + 4095) / 4096;
    return words * 4 + (blocks - 1) * 4 + ((supers - blocks) * 2 + 3) / 4 * 4 + ((words - supers) + 3) / 4 * 4;
}

// Packs bounds the way cells and clusters store them: three u32 of (first << 16 | second) over
// minX, minY, minZ, maxX, maxY, maxZ.
void unpackBounds(const Reader& r, uint64_t off, uint16_t mn[3], uint16_t mx[3]) {
    const uint32_t a = r.u32(off), b = r.u32(off + 4), c = r.u32(off + 8);
    mn[0] = uint16_t(a >> 16);
    mn[1] = uint16_t(a & 0xFFFF);
    mn[2] = uint16_t(b >> 16);
    mx[0] = uint16_t(b & 0xFFFF);
    mx[1] = uint16_t(c >> 16);
    mx[2] = uint16_t(c & 0xFFFF);
}

}  // namespace

bool UmbraTome::parse(const uint8_t* data, size_t size, std::string* err) {
    return parse(std::vector<uint8_t>(data, data + size), err);
}

bool UmbraTome::parse(std::vector<uint8_t> bytes, std::string* err) {
    d_ = std::move(bytes);
    h_ = {};
    treeWords_.clear();
    treeRank_.clear();
    viewRank_.clear();
    tiles_.clear();
    cells_.clear();
    portals_.clear();
    clusters_.clear();
    clusterPortals_.clear();
    if (!decode(err)) {
        tiles_.clear();
        cells_.clear();
        portals_.clear();
        return false;
    }
    return true;
}

bool UmbraTome::decode(std::string* err) {
    const Reader r{d_};
    if (d_.size() < kUmbraHeaderSize) return setErr(err, "tome shorter than its header"), false;
    if (d_.size() > 0xFFFFFFF0u) return setErr(err, "tome larger than 4 GB"), false;
    auto& h = h_;
    h.version = r.u32(0x00);
    h.crc32 = r.u32(0x04);
    h.size = r.u32(0x08);
    h.lodBaseDistance = r.f32(0x0C);
    h.flags = r.u32(0x10);
    for (int a = 0; a < 3; ++a) {
        h.treeMin[a] = r.f32(0x14 + 4 * a);
        h.treeMax[a] = r.f32(0x20 + 4 * a);
        h.boundsMin[a] = r.f32(0x150 + 4 * a);
        h.boundsMax[a] = r.f32(0x15C + 4 * a);
    }
    h.tileTreeWord = r.u32(0x2C);
    h.tileTreeData = r.u32(0x30);
    h.tileTreeMap = r.u32(0x34);
    h.tileTreeNumSplits = r.u32(0x38);
    h.tileTreeSplits = r.u32(0x3C);
    h.numObjects = r.u32(0x40);
    h.objBounds = r.u32(0x44);
    h.objDistances = r.u32(0x48);
    h.userIdStarts = r.u32(0x4C);
    h.userIds = r.u32(0x50);
    h.listWidths = r.u32(0x54);
    h.objectLists = r.u32(0x58);
    h.objectListSize = r.u32(0x5C);
    h.clusterLists = r.u32(0x60);
    h.clusterListSize = r.u32(0x64);
    h.numGates = r.u32(0x68);
    h.gateIndexMap = r.u32(0x6C);
    h.gateVertices = r.u32(0x70);
    h.numGateVertices = r.u32(0x74);
    h.gateIndices = r.u32(0x78);
    h.numClusters = r.u32(0x7C);
    h.clusters = r.u32(0x80);
    h.clusterPortals = r.u32(0x84);
    h.cellStarts = r.u32(0x88);
    h.numLeafTiles = r.u32(0x8C);
    h.numTiles = r.u32(0x90);
    h.bitsPerSlotPath = r.u32(0x94);
    h.slotPaths = r.u32(0x98);
    h.tileLodLevels = r.u32(0x9C);
    h.tiles = r.u32(0xA0);
    h.tileMatchingData = r.u32(0xA4);
    h.matchingTrees = r.u32(0xA8);
    h.numMatchingTrees = r.u32(0xAC);
    h.numTomes = r.u32(0xB0);
    h.tomeClusterStarts = r.u32(0xB4);
    h.tomeClusterPortalStarts = r.u32(0xB8);
    {
        const char* s = reinterpret_cast<const char*>(d_.data() + 0xBC);
        h.computation.assign(s, strnlen(s, 128));
    }
    h.objectDepthmaps = r.u32(0x13C);
    h.depthmapFaces = r.u32(0x140);
    h.depthmapPalettes = r.u32(0x144);
    h.numFaces = r.u32(0x148);
    h.tilePortalExpands = r.u32(0x14C);
    h.clusterCoordScale = r.f32(0x168);

    if (h.version != kUmbraTomeVersion) return setErr(err, "not a Replay 1.20 Umbra tome (version " + hex32(h.version) + ")"), false;
    if (h.size != d_.size()) return setErr(err, "tome size field " + std::to_string(h.size) + " != " + std::to_string(d_.size())), false;
    if (!h.numTiles || h.numTiles > 0x1000000u) return setErr(err, "bad tile count"), false;
    if ((h.tileTreeWord >> 5) != h.numTiles) return setErr(err, "tile tree node count != m_numTiles"), false;
    if (h.numObjects > 0x1000000u) return setErr(err, "bad object count"), false;
    const unsigned width = h.objectListEntryWidth();
    if (h.numObjects && (h.objectIndexWidth() == 0 || (uint64_t(1) << h.objectIndexWidth()) < h.numObjects || width > 32))
        return setErr(err, "object index width too small"), false;

    // Tile tree.
    const uint32_t n = h.numTiles;
    const uint32_t words = (n * 2 + 31) / 32;
    if (!h.tileTreeData || !r.has(h.tileTreeData, treeBytes(n))) return setErr(err, "tile tree out of range"), false;
    treeWords_.resize(words);
    std::memcpy(treeWords_.data(), d_.data() + h.tileTreeData, size_t(words) * 4);
    treeRank_.resize(words + 1);
    uint32_t inner = 0;
    for (uint32_t w = 0; w < words; ++w) {
        treeRank_[w] = inner;
        const uint32_t k = std::min<uint32_t>(16, n - w * 16);
        inner += innerInWord(treeWords_[w], k);
    }
    treeRank_[words] = inner;
    if (2 * uint64_t(inner) + 1 != n) return setErr(err, "tile tree is not a full binary tree"), false;
    if (inner && (h.tileTreeNumSplits != n || !r.has(h.tileTreeSplits, uint64_t(n) * 4)))
        return setErr(err, "tile tree split values missing"), false;
    if (!h.tiles || !r.has(h.tiles, uint64_t(n) * 4)) return setErr(err, "tile table out of range"), false;
    if (h.tileLodLevels && !r.has(h.tileLodLevels, uint64_t(n) * 4)) return setErr(err, "tile LOD table out of range"), false;
    if (h.tilePortalExpands && !r.has(h.tilePortalExpands, uint64_t(n) * 4))
        return setErr(err, "tile portal expansion table out of range"), false;
    if (h.cellStarts && !r.has(h.cellStarts, uint64_t(n + 1) * 4)) return setErr(err, "cell starts out of range"), false;

    tiles_.assign(n, UmbraTile{});
    tiles_[0].depth = 0;
    for (uint32_t i = 0; i < n; ++i) {
        UmbraTile& t = tiles_[i];
        const uint32_t code = treeNodeCode(i);
        t.treeLeaf = code == 3;
        if (!t.treeLeaf) {
            uint32_t lo, hi;
            treeChildren(i, lo, hi);
            if (hi >= n) return setErr(err, "tile tree child out of range"), false;
            tiles_[lo].parent = tiles_[hi].parent = i;
            tiles_[lo].depth = tiles_[hi].depth = t.depth + 1;
        }
        t.offset = r.u32(h.tiles + 4ull * i);
        t.lodLevel = h.tileLodLevels ? r.f32(h.tileLodLevels + 4ull * i) : 0.0f;
    }

    // Tiles, cells and portals.
    uint32_t cellTotal = 0, portalTotal = 0;
    struct RawPortals {
        uint32_t tile, off, count;
    };
    std::vector<RawPortals> rawPortals;
    for (uint32_t i = 0; i < n; ++i) {
        UmbraTile& t = tiles_[i];
        if (!t.offset) {
            t.cellBase = cellTotal;
            if (h.cellStarts && r.u32(h.cellStarts + 4ull * i) != cellTotal) return setErr(err, "cell starts disagree with the tiles"), false;
            continue;
        }
        const uint64_t o = t.offset;
        if (!r.has(o, kUmbraTileSize)) return setErr(err, "tile " + std::to_string(i) + " out of range"), false;
        for (int a = 0; a < 3; ++a) {
            t.min[a] = r.f32(o + 4 * a);
            t.max[a] = r.f32(o + 12 + 4 * a);
        }
        const uint32_t viewWord = r.u32(o + 0x18);
        const uint32_t viewTreeRel = r.u32(o + 0x1C), viewMapRel = r.u32(o + 0x20);
        const uint32_t sizeAndFlags = r.u32(o + 0x2C);
        t.flags = sizeAndFlags & 0xFF;
        t.byteSize = sizeAndFlags >> 8;
        t.portalExpand = r.f32(o + 0x30);
        const uint32_t cellsAndClusters = r.u32(o + 0x34);
        t.cellCount = cellsAndClusters & 0xFFFF;
        t.clusterCount = cellsAndClusters >> 16;
        const uint32_t cellsRel = r.u32(o + 0x38), portalsRel = r.u32(o + 0x3C), cellIndicesRel = r.u32(o + 0x40);
        t.scale = r.f32(o + 0x44);
        t.invScale = r.f32(o + 0x48);
        for (int k = 0; k < 6; ++k) t.qbounds[k] = r.u16(o + 0x4C + 2 * k);
        if (!r.has(o, t.byteSize) || t.byteSize < kUmbraTileSize) return setErr(err, "tile " + std::to_string(i) + " size out of range"), false;
        if (!(t.scale > 0) || !std::isfinite(t.scale) || !(t.invScale > 0) || !std::isfinite(t.invScale))
            return setErr(err, "tile " + std::to_string(i) + " has no coordinate scale"), false;
        auto inTile = [&](uint64_t rel, uint64_t size) { return rel && rel + size <= t.byteSize; };
        if (t.cellCount && !inTile(cellsRel, uint64_t(t.cellCount) * kUmbraCellSize))
            return setErr(err, "tile " + std::to_string(i) + " cells out of range"), false;
        if (h.cellStarts && r.u32(h.cellStarts + 4ull * i) != cellTotal)
            return setErr(err, "cell starts disagree with the tiles at tile " + std::to_string(i)), false;
        t.cellBase = cellTotal;
        t.portalBase = portalTotal;
        uint32_t localPortals = 0;
        for (uint32_t c = 0; c < t.cellCount; ++c) {
            const uint64_t co = o + cellsRel + uint64_t(c) * kUmbraCellSize;
            UmbraCell cell;
            cell.tile = i;
            const uint32_t ps = r.u32(co), pc = r.u32(co + 4);
            if (ps != localPortals) return setErr(err, "tile " + std::to_string(i) + " portal ranges are not contiguous"), false;
            localPortals = ps + pc;
            cell.portalStart = portalTotal + ps;
            cell.portalCount = pc;
            cell.objectStart = r.u32(co + 8);
            cell.objectCount = r.u32(co + 12);
            cell.cluster = r.u32(co + 16);
            cell.flags = r.u32(co + 20);
            unpackBounds(r, co + 24, cell.qmin, cell.qmax);
            if (uint64_t(cell.objectStart) + cell.objectCount > h.objectListSize)
                return setErr(err, "cell object list out of range"), false;
            cells_.push_back(cell);
        }
        t.portalCount = localPortals;
        if (localPortals && !inTile(portalsRel, uint64_t(localPortals) * kUmbraPortalSize))
            return setErr(err, "tile " + std::to_string(i) + " portals out of range"), false;
        rawPortals.push_back({i, uint32_t(o + portalsRel), localPortals});
        cellTotal += t.cellCount;
        portalTotal += localPortals;
        // View tree (leaf tiles).
        if (t.hasViewTree()) {
            t.viewNodes = viewWord >> 5;
            t.viewMapWidth = viewWord & 31;
            if (!t.viewNodes || !inTile(viewTreeRel, treeBytes(t.viewNodes)) || !t.viewMapWidth)
                return setErr(err, "tile " + std::to_string(i) + " view tree out of range"), false;
            if (r.u32(o + 0x24) != 0) return setErr(err, "tile " + std::to_string(i) + " view tree with split values"), false;
            t.viewTree = uint32_t(o + viewTreeRel);
            const uint32_t vw = (t.viewNodes * 2 + 31) / 32;
            t.viewRank = uint32_t(viewRank_.size());
            uint32_t vinner = 0;
            for (uint32_t w = 0; w < vw; ++w) {
                viewRank_.push_back(vinner);
                const uint32_t k = std::min<uint32_t>(16, t.viewNodes - w * 16);
                vinner += innerInWord(r.u32(t.viewTree + 4ull * w), k);
            }
            viewRank_.push_back(vinner);
            if (2 * uint64_t(vinner) + 1 != t.viewNodes) return setErr(err, "tile " + std::to_string(i) + " view tree is not full"), false;
            const uint32_t leaves = t.viewNodes - vinner;
            if (!inTile(viewMapRel, (uint64_t(leaves) * t.viewMapWidth + 7) / 8))
                return setErr(err, "tile " + std::to_string(i) + " view map out of range"), false;
            t.viewMap = uint32_t(o + viewMapRel);
            if (!inTile(cellIndicesRel, 4)) return setErr(err, "tile " + std::to_string(i) + " cell index list out of range"), false;
            t.cellIndices = uint32_t(o + cellIndicesRel);
        }
    }
    if (h.cellStarts && r.u32(h.cellStarts + 4ull * n) != cellTotal) return setErr(err, "cell starts total != cells"), false;

    portals_.reserve(portalTotal);
    for (const RawPortals& rp : rawPortals) {
        const UmbraTile& t = tiles_[rp.tile];
        for (uint32_t p = 0; p < rp.count; ++p) {
            const uint64_t po = rp.off + uint64_t(p) * kUmbraPortalSize;
            UmbraPortal q;
            for (int k = 0; k < 4; ++k) q.raw[k] = r.u32(po + 4 * k);
            q.face = uint8_t(q.raw[0] >> 29);
            // The game's test (DW 0x14200382C): (d0 & 0x12000000) == 0x02000000 is a portal within the tile.
            q.internal = (q.raw[0] & 0x12000000u) == 0x02000000u;
            q.coarse = (q.raw[0] >> 26) & 1;
            q.gate = (q.raw[0] >> 27) & 1;
            q.outside = (q.raw[0] >> 28) & 1;
            if (q.face > 5) return setErr(err, "portal face out of range"), false;
            const uint32_t localCell = q.raw[1] >> 16;
            q.targetTile = q.internal ? rp.tile : (q.outside ? kUmbraNone : (q.raw[0] & kTargetMask));
            if (!q.gate) {
                q.plane = q.internal ? uint16_t(q.raw[1] & 0xFFFF) : uint16_t(0);
                // The portal box grows by this many tile quanta (DW 0x142003DB4: min - e, max + 1 + e).
                q.expand = uint16_t(q.internal ? (q.raw[0] & 0xFFFF) : (q.raw[1] & 0xFFFF));
                q.rect[0][0] = uint16_t(q.raw[2] >> 16);
                q.rect[0][1] = uint16_t(q.raw[2] & 0xFFFF);
                q.rect[1][0] = uint16_t(q.raw[3] >> 16);
                q.rect[1][1] = uint16_t(q.raw[3] & 0xFFFF);
            } else {
                // Gate geometry: a world box of 6 floats at m_gateVertices + 4 * (d3 >> 12) (DW 0x142003E06).
                const uint32_t at = q.raw[3] >> 12;
                if (h.gateVertices && uint64_t(at) + 6 <= h.numGateVertices && r.has(h.gateVertices + 4ull * at, 24)) q.gateBox = at;
            }
            if (q.targetTile != kUmbraNone) {
                if (q.targetTile >= n || !tiles_[q.targetTile].offset) return setErr(err, "portal to a missing tile"), false;
                const UmbraTile& tt = tiles_[q.targetTile];
                if (localCell >= tt.cellCount) return setErr(err, "portal to a missing cell"), false;
                q.targetCell = tt.cellBase + localCell;
            }
            portals_.push_back(q);
        }
        // Source cells.
        for (uint32_t c = 0; c < t.cellCount; ++c) {
            const UmbraCell& cell = cells_[t.cellBase + c];
            for (uint32_t p = 0; p < cell.portalCount; ++p) portals_[cell.portalStart + p].sourceCell = t.cellBase + c;
        }
    }

    // Clusters.
    if (h.numClusters) {
        if (!r.has(h.clusters, uint64_t(h.numClusters) * kUmbraClusterSize)) return setErr(err, "clusters out of range"), false;
        uint32_t cp = 0;
        clusters_.resize(h.numClusters);
        for (uint32_t c = 0; c < h.numClusters; ++c) {
            const uint64_t co = h.clusters + uint64_t(c) * kUmbraClusterSize;
            UmbraCluster& cl = clusters_[c];
            cl.portalStart = r.u32(co);
            cl.portalCount = r.u32(co + 4);
            unpackBounds(r, co + 8, cl.qmin, cl.qmax);
            if (cl.portalStart != cp) return setErr(err, "cluster portal ranges are not contiguous"), false;
            cp += cl.portalCount;
        }
        if (cp && !r.has(h.clusterPortals, uint64_t(cp) * kUmbraPortalSize)) return setErr(err, "cluster portals out of range"), false;
        clusterPortals_.resize(cp);
        for (uint32_t p = 0; p < cp; ++p) {
            const uint64_t po = h.clusterPortals + uint64_t(p) * kUmbraPortalSize;
            UmbraClusterPortal& q = clusterPortals_[p];
            for (int k = 0; k < 4; ++k) q.raw[k] = r.u32(po + 4 * k);
            q.face = uint8_t(q.raw[0] >> 29);
            q.gate = (q.raw[0] >> 27) & 1;
            q.target = q.raw[0] & 0xFFFF;
            q.plane = uint16_t(q.raw[1] & 0xFFFF);
            q.rect[0][0] = uint16_t(q.raw[2] >> 16);
            q.rect[0][1] = uint16_t(q.raw[2] & 0xFFFF);
            q.rect[1][0] = uint16_t(q.raw[3] >> 16);
            q.rect[1][1] = uint16_t(q.raw[3] & 0xFFFF);
            if (q.face > 5 || q.target >= h.numClusters) return setErr(err, "cluster portal target out of range"), false;
        }
    }
    for (const UmbraCell& c : cells_)
        if (c.cluster != kUmbraNone && c.cluster >= h.numClusters) return setErr(err, "cell cluster out of range"), false;

    // Objects.
    const uint64_t no = h.numObjects;
    if (no) {
        if (!r.has(h.objBounds, no * 24) || !r.has(h.objDistances, no * 32)) return setErr(err, "object bounds out of range"), false;
        if (h.userIdStarts) {
            if (!r.has(h.userIdStarts, (no + 1) * 4)) return setErr(err, "user ID starts out of range"), false;
            uint32_t prev = 0;
            for (uint64_t i = 0; i <= no; ++i) {
                const uint32_t s = r.u32(h.userIdStarts + 4 * i);
                if (s < prev || (i == 0 && s != 0)) return setErr(err, "user ID starts are not increasing"), false;
                prev = s;
            }
            if (!r.has(h.userIds, uint64_t(prev) * 4)) return setErr(err, "user IDs out of range"), false;
        } else if (!r.has(h.userIds, no * 4)) {
            return setErr(err, "user IDs out of range"), false;
        }
    }
    if (h.objectListSize && !r.has(h.objectLists, (uint64_t(h.objectListSize) * width + 7) / 8))
        return setErr(err, "object lists out of range"), false;
    return true;
}

UmbraChecksum UmbraTome::checksum() const {
    if (d_.size() < 8) return UmbraChecksum::Invalid;
    if (h_.crc32 == umbraCrc32c(d_.data() + 8, d_.size() - 8)) return UmbraChecksum::Valid;
    return h_.crc32 == kUmbraNoChecksum ? UmbraChecksum::Disabled : UmbraChecksum::Invalid;
}

uint32_t UmbraTome::treeNodeCode(uint32_t node) const { return (treeWords_[node >> 4] >> (2 * (node & 15))) & 3u; }

float UmbraTome::treeSplit(uint32_t node) const {
    if (!h_.tileTreeSplits || node >= h_.tileTreeNumSplits) return 0.0f;
    float v;
    std::memcpy(&v, d_.data() + h_.tileTreeSplits + 4ull * node, 4);
    return v;
}

void UmbraTome::treeChildren(uint32_t node, uint32_t& lo, uint32_t& hi) const {
    const uint32_t rank = treeRank_[node >> 4] + innerInWord(treeWords_[node >> 4], node & 15);
    lo = 2 * rank + 1;
    hi = 2 * rank + 2;
}

uint32_t UmbraTome::viewCode(const UmbraTile& t, uint32_t node) const {
    uint32_t w;
    std::memcpy(&w, d_.data() + t.viewTree + 4ull * (node >> 4), 4);
    return (w >> (2 * (node & 15))) & 3u;
}

uint32_t UmbraTome::viewInnerBefore(const UmbraTile& t, uint32_t node) const {
    uint32_t w;
    std::memcpy(&w, d_.data() + t.viewTree + 4ull * (node >> 4), 4);
    return viewRank_[t.viewRank + (node >> 4)] + innerInWord(w, node & 15);
}

void UmbraTome::objectBounds(uint32_t object, float mn[3], float mx[3]) const {
    const uint64_t o = h_.objBounds + 24ull * object;
    std::memcpy(mn, d_.data() + o, 12);
    std::memcpy(mx, d_.data() + o + 12, 12);
}

void UmbraTome::objectDistance(uint32_t object, float mn[3], float mx[3], float& nearSq, float& farSq) const {
    const uint64_t o = h_.objDistances + 32ull * object;
    std::memcpy(mn, d_.data() + o, 12);
    std::memcpy(&nearSq, d_.data() + o + 12, 4);
    std::memcpy(mx, d_.data() + o + 16, 12);
    std::memcpy(&farSq, d_.data() + o + 28, 4);
}

void UmbraTome::objectUserIds(uint32_t object, uint32_t& first, uint32_t& count) const {
    if (!h_.userIdStarts) {
        first = object;
        count = 1;
        return;
    }
    uint32_t a, b;
    std::memcpy(&a, d_.data() + h_.userIdStarts + 4ull * object, 4);
    std::memcpy(&b, d_.data() + h_.userIdStarts + 4ull * object + 4, 4);
    first = a;
    count = b - a;
}

uint32_t UmbraTome::userId(uint32_t index) const {
    uint32_t v;
    std::memcpy(&v, d_.data() + h_.userIds + 4ull * index, 4);
    return v;
}

std::vector<uint32_t> UmbraTome::gateUserIds() const {
    std::vector<uint32_t> out;
    if (!h_.gateIndexMap || uint64_t(h_.gateIndexMap) + 4ull * h_.numGates > d_.size()) return out;
    out.resize(h_.numGates);
    for (uint32_t g = 0; g < h_.numGates; ++g) std::memcpy(&out[g], d_.data() + h_.gateIndexMap + 4ull * g, 4);
    return out;
}

uint32_t UmbraTome::userIdTotal() const {
    if (!h_.userIdStarts) return h_.numObjects;
    uint32_t v;
    std::memcpy(&v, d_.data() + h_.userIdStarts + 4ull * h_.numObjects, 4);
    return v;
}

uint32_t UmbraTome::objectListEntry(uint32_t index) const {
    const unsigned width = h_.objectListEntryWidth();
    const uint64_t bit = uint64_t(index) * width;
    uint64_t v = 0;
    const uint64_t first = bit >> 3, last = (bit + width - 1) >> 3;
    for (uint64_t b = last + 1; b-- > first;) v = (v << 8) | d_[h_.objectLists + b];
    return uint32_t((v >> (bit & 7)) & ((uint64_t(1) << width) - 1));
}

void UmbraTome::objectListRun(uint32_t index, uint32_t& first, uint32_t& count) const {
    const uint32_t e = objectListEntry(index);
    const unsigned w = h_.objectIndexWidth();
    if (!h_.objectRunWidth()) {
        first = e;
        count = 1;
        return;
    }
    first = e & ((uint32_t(1) << w) - 1);
    count = e >> w;
}

std::vector<uint32_t> UmbraTome::cellObjects(uint32_t cell) const {
    const UmbraCell& c = cells_[cell];
    std::vector<uint32_t> out;
    out.reserve(c.objectCount);
    for (uint32_t k = 0; k < c.objectCount; ++k) {
        uint32_t first, count;
        objectListRun(c.objectStart + k, first, count);
        for (uint32_t o = first; o < first + count; ++o) out.push_back(o);
    }
    return out;
}

void UmbraTome::cellBounds(uint32_t cell, float mn[3], float mx[3]) const {
    const UmbraCell& c = cells_[cell];
    const UmbraTile& t = tiles_[c.tile];
    for (int a = 0; a < 3; ++a) {
        mn[a] = t.min[a] + float(c.qmin[a]) * t.scale;
        mx[a] = t.min[a] + float(uint32_t(c.qmax[a]) + 1) * t.scale;
    }
}

bool UmbraTome::portalQuad(uint32_t portal, float corners[4][3], bool expand) const {
    const UmbraPortal& p = portals_[portal];
    if (p.gate) return false;
    const UmbraTile& t = tiles_[cells_[p.sourceCell].tile];
    const unsigned a = p.axis(), b = (a + 1) % 3, c = (a + 2) % 3;
    const float e = expand ? float(p.expand) * t.scale : 0.0f;
    // Internal portals carry their plane (d1 bits 0-15); external ones sit on the tile bound of their face
    // (m_tileBounds[face], DW 0x142003D51), the inclusive max + 1 on the positive faces.
    const uint32_t q = p.internal ? p.plane : uint32_t(t.qbounds[p.face]) + (p.face & 1u);
    const float plane = t.min[a] + float(q) * t.scale;
    const float b0 = t.min[b] + float(p.rect[0][0]) * t.scale - e, b1 = t.min[b] + float(uint32_t(p.rect[0][1]) + 1) * t.scale + e;
    const float c0 = t.min[c] + float(p.rect[1][0]) * t.scale - e, c1 = t.min[c] + float(uint32_t(p.rect[1][1]) + 1) * t.scale + e;
    const float bs[4] = {b0, b1, b1, b0}, cs[4] = {c0, c0, c1, c1};
    for (int k = 0; k < 4; ++k) {
        corners[k][a] = plane;
        corners[k][b] = bs[k];
        corners[k][c] = cs[k];
    }
    return true;
}

bool UmbraTome::gateBounds(uint32_t portal, float mn[3], float mx[3]) const {
    const UmbraPortal& p = portals_[portal];
    if (!p.gate || p.gateBox == kUmbraNone) return false;
    std::memcpy(mn, d_.data() + h_.gateVertices + 4ull * p.gateBox, 12);
    std::memcpy(mx, d_.data() + h_.gateVertices + 4ull * p.gateBox + 12, 12);
    return true;
}

uint32_t UmbraTome::leafNodeAt(const float p[3]) const {
    if (tiles_.empty()) return kUmbraNone;
    for (int a = 0; a < 3; ++a)
        if (!(p[a] >= h_.treeMin[a] && p[a] <= h_.treeMax[a])) return kUmbraNone;
    uint32_t node = 0;
    while (true) {
        const uint32_t code = treeNodeCode(node);
        if (code == 3) return node;
        uint32_t lo, hi;
        treeChildren(node, lo, hi);
        node = p[code] < treeSplit(node) ? lo : hi;
    }
}

uint32_t UmbraTome::leafTileAt(const float p[3]) const {
    const uint32_t node = leafNodeAt(p);
    return node != kUmbraNone && tiles_[node].offset ? node : kUmbraNone;
}

uint32_t UmbraTome::viewLeafAt(const UmbraTile& t, const float q[3]) const {
    // The view tree splits the tile's quantized bounds (m_tileBounds, max exclusive) at midpoints.
    float mn[3] = {float(t.qbounds[0]), float(t.qbounds[2]), float(t.qbounds[4])};
    float mx[3] = {float(t.qbounds[1]) + 1.0f, float(t.qbounds[3]) + 1.0f, float(t.qbounds[5]) + 1.0f};
    uint32_t node = 0;
    while (true) {
        const uint32_t code = viewCode(t, node);
        if (code == 3) return node - viewInnerBefore(t, node);
        const float mid = 0.5f * (mn[code] + mx[code]);
        const uint32_t rank = viewInnerBefore(t, node);
        if (q[code] < mid) {
            mx[code] = mid;
            node = 2 * rank + 1;
        } else {
            mn[code] = mid;
            node = 2 * rank + 2;
        }
        if (node >= t.viewNodes) return kUmbraNone;
    }
}

std::vector<uint32_t> UmbraTome::cellsAt(const float p[3], uint32_t* tileOut) const {
    std::vector<uint32_t> out;
    const uint32_t ti = leafTileAt(p);
    if (tileOut) *tileOut = ti;
    if (ti == kUmbraNone) return out;
    const UmbraTile& t = tiles_[ti];
    if (!t.hasViewTree()) {
        for (uint32_t c = 0; c < t.cellCount; ++c) out.push_back(t.cellBase + c);
        return out;
    }
    float q[3];
    for (int a = 0; a < 3; ++a) q[a] = (p[a] - t.min[a]) * t.invScale;
    const uint32_t leaf = viewLeafAt(t, q);
    if (leaf == kUmbraNone) return out;
    // The map entry indexes the u32 cell index list; a run of cells ends at 0xFFFFFFFF.
    const uint64_t bit = uint64_t(leaf) * t.viewMapWidth;
    uint32_t v = 0;
    for (uint32_t b = 0; b < t.viewMapWidth; ++b) {
        const uint64_t at = bit + b;
        v |= uint32_t((d_[t.viewMap + (at >> 3)] >> (at & 7)) & 1u) << b;
    }
    // The tile's flag bit 2 says what the value is (research 129, the game's leaf reader): set, an offset into the cell
    // index list (every stock leaf tile); clear, the cell number itself (the converter's one-cell tome: value 0, cell 0;
    // a value with the top bit set points into a node tree no known tome has: no cell).
    if (!(t.flags & 4u)) {
        if (v < t.cellCount) out.push_back(t.cellBase + v);
        return out;
    }
    const uint64_t end = uint64_t(t.offset) + t.byteSize;
    for (uint64_t at = t.cellIndices + 4ull * v; at + 4 <= end; at += 4) {
        uint32_t c;
        std::memcpy(&c, d_.data() + at, 4);
        if (c == kUmbraNone || c >= t.cellCount) break;
        out.push_back(t.cellBase + c);
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------
// Checks
// ---------------------------------------------------------------------------------------------------------------

UmbraTomeReport UmbraTome::validate() const {
    UmbraTomeReport rep;
    auto fail = [&](const std::string& m) {
        rep.ok = false;
        if (rep.errors.size() < 16) rep.errors.push_back(m);
    };
    rep.checksum = checksum();
    if (rep.checksum == UmbraChecksum::Invalid) fail("checksum is neither valid nor disabled");
    const Reader r{d_};
    const uint32_t n = h_.numTiles;

    // Rank directories (tile tree and view trees) against the bitstreams.
    auto checkRank = [&](uint64_t base, uint32_t nodes, auto innerBefore) {
        const uint32_t words = (nodes * 2 + 31) / 32, supers = (words + 15) / 16, blocks = (words + 4095) / 4096;
        uint64_t o = base + 4ull * words;
        bool good = true;
        for (uint32_t b = 1; b < blocks; ++b)
            if (r.u32(o + 4ull * (b - 1)) != innerBefore(b * 65536)) good = false;
        o += 4ull * (blocks - 1);
        uint32_t k16 = 0;
        for (uint32_t s = 1; s < supers; ++s) {
            if (s % 256 == 0) continue;
            if (r.u16(o + 2ull * k16++) != innerBefore(s * 256) - innerBefore((s / 256) * 65536)) good = false;
        }
        o += (uint64_t(supers - blocks) * 2 + 3) / 4 * 4;
        uint32_t k = 0;
        for (uint32_t s = 0; s < supers; ++s)
            for (uint32_t w = s * 16 + 1; w < std::min(words, s * 16 + 16); ++w, ++k)
                if (d_[o + k] != uint8_t(innerBefore(w * 16) - innerBefore(s * 256))) good = false;
        ++rep.rankTablesChecked;
        return good;
    };
    if (!checkRank(h_.tileTreeData, n, [&](uint32_t node) {
            return treeRank_[node >> 4] + innerInWord(treeWords_[node >> 4], node & 15);
        }))
        fail("tile tree rank directory");

    // Tile tree boxes, tile kinds, slot paths.
    std::vector<std::array<float, 6>> box(n);
    for (int a = 0; a < 3; ++a) {
        box[0][a] = h_.treeMin[a];
        box[0][3 + a] = h_.treeMax[a];
    }
    rep.treeNodes = n;
    const uint32_t bitsPerPath = h_.bitsPerSlotPath;
    const bool slots = h_.slotPaths && bitsPerPath && bitsPerPath <= 62 && r.has(h_.slotPaths, (uint64_t(n) * bitsPerPath + 7) / 8);
    std::vector<uint64_t> path(n, 1);  // heap-style: 1, then one bit per step (1 = low child)
    for (uint32_t i = 0; i < n; ++i) {
        const UmbraTile& t = tiles_[i];
        const uint32_t code = treeNodeCode(i);
        if (code == 3) {
            ++rep.treeLeaves;
            if (!t.offset) ++rep.emptyLeaves;
        } else {
            uint32_t lo, hi;
            treeChildren(i, lo, hi);
            const float s = treeSplit(i);
            box[lo] = box[i];
            box[hi] = box[i];
            box[lo][3 + code] = s;
            box[hi][code] = s;
            path[lo] = (path[i] << 1) | 1;
            path[hi] = path[i] << 1;
        }
        if (t.offset) {
            for (int a = 0; a < 3; ++a)
                if (t.min[a] != box[i][a] || t.max[a] != box[i][3 + a]) {
                    fail("tile " + std::to_string(i) + " bounds != its tile-tree box");
                    break;
                }
            if (t.hasViewTree()) {
                ++rep.leafTiles;
                if (code != 3) fail("tile " + std::to_string(i) + " with a view tree on an inner node");
            } else {
                ++rep.lodTiles;
            }
            if (h_.tilePortalExpands && r.f32(h_.tilePortalExpands + 4ull * i) != t.portalExpand)
                fail("tile " + std::to_string(i) + " portal expansion != m_tilePortalExpands");
        }
        if (slots && t.depth < bitsPerPath) {
            // Stored MSB-aligned: bit (B-1-k) .. : the path bits deepest step first, then the sentinel 1.
            uint64_t want = 0;
            for (uint32_t k = 0; k <= t.depth; ++k)
                if ((path[i] >> k) & 1) want |= uint64_t(1) << (bitsPerPath - 1 - k);
            const uint64_t bit = uint64_t(i) * bitsPerPath;
            uint64_t got = 0;
            for (uint32_t b = 0; b < bitsPerPath; ++b) {
                const uint64_t at = bit + b;
                got |= uint64_t((d_[h_.slotPaths + (at >> 3)] >> (at & 7)) & 1u) << b;
            }
            if (got != want) fail("slot path of tile " + std::to_string(i));
            ++rep.slotPathsChecked;
        }
    }
    if (rep.treeLeaves != h_.numLeafTiles) fail("tile tree leaves != m_numLeafTiles");

    // Cells, view trees, portals.
    rep.cells = uint32_t(cells_.size());
    rep.portals = uint32_t(portals_.size());
    std::vector<uint8_t> referenced(h_.numObjects, 0);
    for (uint32_t ci = 0; ci < cells_.size(); ++ci) {
        const UmbraCell& c = cells_[ci];
        const UmbraTile& t = tiles_[c.tile];
        for (int a = 0; a < 3; ++a)
            if (c.qmin[a] > c.qmax[a]) {
                fail("cell " + std::to_string(ci) + " bounds inverted");
                break;
            }
        for (uint32_t k = 0; k < c.objectCount; ++k) {
            uint32_t first, count;
            objectListRun(c.objectStart + k, first, count);
            if (!count || uint64_t(first) + count > h_.numObjects) {
                fail("cell " + std::to_string(ci) + " lists a missing object");
                break;
            }
            for (uint32_t o = first; o < first + count; ++o) referenced[o] = 1;
        }
        if (t.clusterCount && c.cluster == kUmbraNone) fail("leaf cell without a cluster");
    }
    for (uint8_t v : referenced) rep.objectsReferenced += v;

    for (uint32_t i = 0; i < n; ++i) {
        const UmbraTile& t = tiles_[i];
        if (!t.offset || !t.hasViewTree()) continue;
        if (!checkRank(t.viewTree, t.viewNodes, [&](uint32_t node) { return viewInnerBefore(t, node); }))
            fail("view tree rank directory of tile " + std::to_string(i));
        const uint32_t leaves = t.viewNodes - (viewRank_[t.viewRank + (t.viewNodes * 2 + 31) / 32]);
        const uint64_t end = uint64_t(t.offset) + t.byteSize;
        for (uint32_t leaf = 0; leaf < leaves; ++leaf) {
            const uint64_t bit = uint64_t(leaf) * t.viewMapWidth;
            uint32_t v = 0;
            for (uint32_t b = 0; b < t.viewMapWidth; ++b) {
                const uint64_t at = bit + b;
                v |= uint32_t((d_[t.viewMap + (at >> 3)] >> (at & 7)) & 1u) << b;
            }
            if (!(t.flags & 4u)) {
                // Without flag bit 2 the game takes the value for the cell number (research 129).
                ++rep.viewLeaves;
                if (v >= t.cellCount) fail("view leaf of tile " + std::to_string(i) + " names cell " + std::to_string(v) + " of " +
                                           std::to_string(t.cellCount) + " (tile flags without bit 2: the value is the cell number)");
                continue;
            }
            uint32_t count = 0;
            bool terminated = false;
            for (uint64_t at = t.cellIndices + 4ull * v; at + 4 <= end; at += 4) {
                const uint32_t c = r.u32(at);
                if (c == kUmbraNone) {
                    terminated = true;
                    break;
                }
                if (c >= t.cellCount) break;
                ++count;
            }
            if (!terminated) fail("view leaf of tile " + std::to_string(i) + " points at a bad cell list");
            ++rep.viewLeaves;
            if (!count) ++rep.solidViewLeaves;
            if (count > 1) ++rep.multiCellViewLeaves;
        }
    }

    // Portals: kinds and reverse portals (same pair of cells, opposite face).
    std::unordered_set<uint64_t> keys;
    keys.reserve(portals_.size() * 2);
    auto key = [](uint32_t from, uint32_t to, unsigned face, unsigned kind) {
        return (uint64_t(from) << 36) ^ (uint64_t(to) << 6) ^ (uint64_t(face) << 2) ^ kind;
    };
    auto kindOf = [](const UmbraPortal& p) { return p.internal ? 0u : (p.coarse ? 2u : 1u); };
    for (const UmbraPortal& p : portals_)
        if (!p.gate && !p.outside && p.targetCell != kUmbraNone) keys.insert(key(p.sourceCell, p.targetCell, p.face, kindOf(p)));
    for (const UmbraPortal& p : portals_) {
        if (p.outside) {
            ++rep.outsidePortals;
            continue;
        }
        if (p.gate) {
            ++rep.gatePortals;
            rep.gatePortalsWithBox += p.gateBox != kUmbraNone;
            continue;
        }
        const unsigned kind = kindOf(p);
        const bool back = keys.count(key(p.targetCell, p.sourceCell, p.face ^ 1u, kind)) != 0;
        if (kind == 0) {
            ++rep.internalPortals;
            if (!back) ++rep.unmatchedInternal;
            if (cells_[p.targetCell].tile != cells_[p.sourceCell].tile) fail("internal portal leaves its tile");
        } else if (kind == 1) {
            ++rep.finePortals;
            if (!back) ++rep.unmatchedFine;
            if (!tiles_[cells_[p.sourceCell].tile].hasViewTree() || !tiles_[p.targetTile].hasViewTree()) ++rep.finePortalsTouchingLod;
        } else {
            ++rep.coarsePortals;
            if (!back) ++rep.unmatchedCoarse;
            if (tiles_[cells_[p.sourceCell].tile].hasViewTree() && tiles_[p.targetTile].hasViewTree()) ++rep.coarsePortalsBetweenLeafTiles;
        }
        const UmbraTile& t = tiles_[cells_[p.sourceCell].tile];
        if (!p.internal) {
            // External portals sit on the source tile's face.
            const unsigned a = p.axis();
            const UmbraTile& tt = tiles_[p.targetTile];
            const float here = (p.face & 1) ? t.max[a] : t.min[a];
            const float there = (p.face & 1) ? tt.min[a] : tt.max[a];
            if (here != there) fail("external portal between tiles that do not touch");
        }
    }

    // Cluster portals.
    std::unordered_set<uint64_t> ckeys;
    for (uint32_t c = 0; c < clusters_.size(); ++c)
        for (uint32_t k = 0; k < clusters_[c].portalCount; ++k) {
            const UmbraClusterPortal& p = clusterPortals_[clusters_[c].portalStart + k];
            if (!p.gate) ckeys.insert(key(c, p.target, p.face, 0));
        }
    for (uint32_t c = 0; c < clusters_.size(); ++c)
        for (uint32_t k = 0; k < clusters_[c].portalCount; ++k) {
            const UmbraClusterPortal& p = clusterPortals_[clusters_[c].portalStart + k];
            ++rep.clusterPortals;
            if (!p.gate && !ckeys.count(key(p.target, c, p.face ^ 1u, 0))) ++rep.unmatchedClusterPortals;
        }

    // User IDs.
    for (uint32_t id : gateUserIds()) rep.gateUserIdsOfClass2 += umbraUserIdClass(id) == kUmbraGate;
    rep.userIdCount = userIdTotal();
    for (uint32_t i = 0; i < rep.userIdCount; ++i) ++rep.userIdsByClass[umbraUserIdClass(userId(i))];
    return rep;
}

// ---------------------------------------------------------------------------------------------------------------
// Visibility query
// ---------------------------------------------------------------------------------------------------------------

struct UmbraTome::View {
    float pos[3];
    float f[3], r[3], u[3];
    float tx, ty;
};

namespace {

struct Rect {
    float x0, y0, x1, y1;
    bool empty() const { return !(x0 < x1 && y0 < y1); }
    bool contains(const Rect& o) const { return x0 <= o.x0 && y0 <= o.y0 && x1 >= o.x1 && y1 >= o.y1; }
    bool overlaps(const Rect& o) const { return x0 < o.x1 && o.x0 < x1 && y0 < o.y1 && o.y0 < y1; }
};

Rect intersect(const Rect& a, const Rect& b) {
    return {std::max(a.x0, b.x0), std::max(a.y0, b.y0), std::min(a.x1, b.x1), std::min(a.y1, b.y1)};
}
Rect unite(const Rect& a, const Rect& b) {
    return {std::min(a.x0, b.x0), std::min(a.y0, b.y0), std::max(a.x1, b.x1), std::max(a.y1, b.y1)};
}
// Outward to a 1/1024 grid, clamped to the frustum: rectangles can only grow a bounded number of times.
Rect snap(Rect a) {
    constexpr float g = 1024.0f;
    a.x0 = std::max(-1.0f, std::floor(a.x0 * g) / g);
    a.y0 = std::max(-1.0f, std::floor(a.y0 * g) / g);
    a.x1 = std::min(1.0f, std::ceil(a.x1 * g) / g);
    a.y1 = std::min(1.0f, std::ceil(a.y1 * g) / g);
    return a;
}

float dot3(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

void normalize(float v[3]) {
    const float l = std::sqrt(dot3(v, v));
    if (l > 0) {
        v[0] /= l;
        v[1] /= l;
        v[2] /= l;
    }
}

void cross(const float a[3], const float b[3], float o[3]) {
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}

struct CellRects {
    uint8_t n = 0;
    Rect r[8];
};

}  // namespace

namespace {

// Screen rectangle of a convex polygon (world corners), clipped to z >= zn. False when nothing is in front.
template <typename V>
bool projectPolygon(const V& v, const float* pts, int count, float zn, Rect& out) {
    float vs[16][3];
    int m = 0;
    for (int k = 0; k < count && k < 16; ++k) {
        const float* pk = pts + 3 * k;
        const float d[3] = {pk[0] - v.pos[0], pk[1] - v.pos[1], pk[2] - v.pos[2]};
        vs[m][0] = dot3(d, v.r);
        vs[m][1] = dot3(d, v.u);
        vs[m][2] = dot3(d, v.f);
        ++m;
    }
    float clipped[16][3];
    int c = 0;
    for (int k = 0; k < m; ++k) {
        const float* a = vs[k];
        const float* b = vs[(k + 1) % m];
        const bool ina = a[2] >= zn, inb = b[2] >= zn;
        if (ina && c < 16) {
            clipped[c][0] = a[0];
            clipped[c][1] = a[1];
            clipped[c][2] = a[2];
            ++c;
        }
        if (ina != inb && c < 16) {
            const float t = (zn - a[2]) / (b[2] - a[2]);
            clipped[c][0] = a[0] + (b[0] - a[0]) * t;
            clipped[c][1] = a[1] + (b[1] - a[1]) * t;
            clipped[c][2] = zn;
            ++c;
        }
    }
    if (!c) return false;
    out = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
           -std::numeric_limits<float>::max()};
    for (int k = 0; k < c; ++k) {
        const float x = clipped[k][0] / (clipped[k][2] * v.tx), y = clipped[k][1] / (clipped[k][2] * v.ty);
        out.x0 = std::min(out.x0, x);
        out.x1 = std::max(out.x1, x);
        out.y0 = std::min(out.y0, y);
        out.y1 = std::max(out.y1, y);
    }
    return true;
}

// Screen rectangle of a box; a box reaching behind the near plane counts as the whole view (conservative).
template <typename V>
bool projectBox(const V& v, const float mn[3], const float mx[3], float zn, Rect& out) {
    bool front = false, behind = false;
    out = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
           -std::numeric_limits<float>::max()};
    for (int k = 0; k < 8; ++k) {
        const float p[3] = {(k & 1) ? mx[0] : mn[0], (k & 2) ? mx[1] : mn[1], (k & 4) ? mx[2] : mn[2]};
        const float d[3] = {p[0] - v.pos[0], p[1] - v.pos[1], p[2] - v.pos[2]};
        const float z = dot3(d, v.f);
        if (z < zn) {
            behind = true;
            continue;
        }
        front = true;
        const float x = dot3(d, v.r) / (z * v.tx), y = dot3(d, v.u) / (z * v.ty);
        out.x0 = std::min(out.x0, x);
        out.x1 = std::max(out.x1, x);
        out.y0 = std::min(out.y0, y);
        out.y1 = std::max(out.y1, y);
    }
    if (!front) return false;
    if (behind) out = {-2.0f, -2.0f, 2.0f, 2.0f};
    return true;
}

float pointBoxDistSq(const float p[3], const float mn[3], const float mx[3]) {
    float s = 0;
    for (int a = 0; a < 3; ++a) {
        const float d = std::max({mn[a] - p[a], 0.0f, p[a] - mx[a]});
        s += d * d;
    }
    return s;
}

}  // namespace

void UmbraTome::runView(const View& v, const UmbraQueryOptions& opt, std::vector<uint8_t>& cellSeen,
                        std::vector<uint8_t>& objSeen, UmbraVisibility& out) const {
    const unsigned maxRects = std::clamp(opt.maxRectsPerCell, 1u, 8u);
    std::unordered_map<uint32_t, CellRects> state;
    std::vector<std::pair<uint32_t, Rect>> stack;
    const Rect full{-1.0f, -1.0f, 1.0f, 1.0f};
    for (uint32_t c : out.startCells) stack.push_back({c, full});
    const float r2 = opt.cameraRadius * opt.cameraRadius;
    while (!stack.empty()) {
        if (out.steps >= opt.maxSteps) {
            out.truncated = true;
            break;
        }
        ++out.steps;
        auto [cell, rect] = stack.back();
        stack.pop_back();
        CellRects& s = state[cell];
        bool covered = false;
        for (unsigned k = 0; k < s.n && !covered; ++k) covered = s.r[k].contains(rect);
        if (covered) continue;
        if (s.n < maxRects) {
            s.r[s.n++] = rect;
        } else {
            s.r[maxRects - 1] = unite(s.r[maxRects - 1], rect);
            rect = s.r[maxRects - 1];
        }
        const UmbraCell& c = cells_[cell];
        for (uint32_t pi = c.portalStart; pi < c.portalStart + c.portalCount; ++pi) {
            const UmbraPortal& p = portals_[pi];
            if (p.outside || p.targetCell == kUmbraNone) continue;
            // The game follows a portal only into the tiles active for the query (a per-query tile bit set, DW
            // 0x142003CEF). Accurate occlusion keeps every leaf tile active and no coarse LOD tile.
            if (!tiles_[cells_[p.targetCell].tile].hasViewTree()) continue;
            Rect next = rect;
            if (p.gate) {
                // Gates are open (gate states are not modelled); the portal is clipped to the gate box when present.
                float gmn[3], gmx[3];
                if (gateBounds(pi, gmn, gmx)) {
                    bool nearCamera = pointBoxDistSq(v.pos, gmn, gmx) <= r2;
                    Rect gr;
                    if (!nearCamera) {
                        if (!projectBox(v, gmn, gmx, opt.nearPlane, gr)) continue;
                        next = intersect(rect, gr);
                        if (next.empty()) continue;
                        next = snap(next);
                        if (next.empty()) continue;
                    }
                }
            } else {
                float q[4][3];
                portalQuad(pi, q, opt.portalExpand);
                const unsigned a = p.axis(), b = (a + 1) % 3, cc = (a + 2) % 3;
                const float plane = q[0][a];
                // q[0] = (b0, c0), q[2] = (b1, c1): distance from the camera to the rectangle.
                const float db = std::max({q[0][b] - v.pos[b], 0.0f, v.pos[b] - q[2][b]});
                const float dc = std::max({q[0][cc] - v.pos[cc], 0.0f, v.pos[cc] - q[2][cc]});
                const float da = v.pos[a] - plane;
                const bool nearCamera = da * da + db * db + dc * dc <= r2;
                if (!nearCamera) {
                    // Looking through the portal needs the camera on its source side.
                    if ((p.face & 1) ? v.pos[a] > plane : v.pos[a] < plane) continue;
                    // The expanded portal is a thin box around its plane (thickness 2e + 1 quanta): both faces.
                    const UmbraTile& st = tiles_[cells_[p.sourceCell].tile];
                    const float half = opt.portalExpand ? (float(p.expand) + 0.5f) * st.scale : 0.0f;
                    Rect pr{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
                            -std::numeric_limits<float>::max()};
                    bool any = false;
                    for (float side : {-half, half}) {
                        float qs[4][3];
                        std::memcpy(qs, q, sizeof qs);
                        for (int k = 0; k < 4; ++k) qs[k][a] += side;
                        Rect r1;
                        if (projectPolygon(v, &qs[0][0], 4, opt.nearPlane, r1)) {
                            pr = any ? unite(pr, r1) : r1;
                            any = true;
                        }
                    }
                    if (!any) continue;
                    next = intersect(rect, pr);
                    if (next.empty()) continue;
                    next = snap(next);
                    if (next.empty()) continue;
                }
            }
            stack.push_back({p.targetCell, next});
        }
    }
    // Objects of the visible cells, against the rectangles their cell was seen through.
    for (const auto& [cell, s] : state) {
        if (!s.n) continue;
        cellSeen[cell] = 1;
        for (uint32_t o : cellObjects(cell)) {
            if (o >= objSeen.size() || objSeen[o]) continue;
            if (opt.distanceCull) {
                float dmn[3], dmx[3], nearSq, farSq;
                objectDistance(o, dmn, dmx, nearSq, farSq);
                const float d2 = pointBoxDistSq(v.pos, dmn, dmx);
                if (d2 < nearSq || d2 >= farSq) continue;
            }
            float mn[3], mx[3];
            objectBounds(o, mn, mx);
            bool inside = true;
            for (int a = 0; a < 3; ++a) inside = inside && v.pos[a] >= mn[a] - opt.nearPlane && v.pos[a] <= mx[a] + opt.nearPlane;
            bool seen = inside;
            if (!seen) {
                Rect br;
                if (!projectBox(v, mn, mx, opt.nearPlane, br)) continue;
                for (unsigned k2 = 0; k2 < s.n && !seen; ++k2) seen = s.r[k2].overlaps(br);
            }
            if (seen) objSeen[o] = 1;
        }
    }
}

UmbraVisibility UmbraTome::finishQuery(const float pos[3], const UmbraQueryOptions& opt, const View* views,
                                       size_t count) const {
    UmbraVisibility out;
    const uint32_t node = leafNodeAt(pos);
    if (node == kUmbraNone) {
        out.status = UmbraQueryStatus::OutsideTome;
        return out;
    }
    uint32_t tile = kUmbraNone;
    out.startCells = cellsAt(pos, &tile);
    out.tile = tile;
    if (tile == kUmbraNone) {
        out.status = UmbraQueryStatus::NoTile;
        return out;
    }
    if (out.startCells.empty()) {
        // Inside solid voxels: the cells of this tile whose bounds come within the camera radius.
        const UmbraTile& t = tiles_[tile];
        for (uint32_t c = t.cellBase; c < t.cellBase + t.cellCount; ++c) {
            float mn[3], mx[3];
            cellBounds(c, mn, mx);
            if (pointBoxDistSq(pos, mn, mx) <= opt.cameraRadius * opt.cameraRadius) out.startCells.push_back(c);
        }
        if (out.startCells.empty()) {
            out.status = UmbraQueryStatus::InSolid;
            return out;
        }
    }
    std::vector<uint8_t> cellSeen(cells_.size(), 0), objSeen(h_.numObjects, 0);
    for (size_t k = 0; k < count; ++k) runView(views[k], opt, cellSeen, objSeen, out);
    for (uint32_t c = 0; c < cellSeen.size(); ++c)
        if (cellSeen[c]) out.cells.push_back(c);
    for (uint32_t o = 0; o < objSeen.size(); ++o) {
        if (!objSeen[o]) continue;
        out.objects.push_back(o);
        uint32_t first, n;
        objectUserIds(o, first, n);
        for (uint32_t k = 0; k < n; ++k) out.userIds.push_back(userId(first + k));
    }
    std::sort(out.userIds.begin(), out.userIds.end());
    out.userIds.erase(std::unique(out.userIds.begin(), out.userIds.end()), out.userIds.end());
    for (uint32_t id : out.userIds) ++out.userIdsByClass[umbraUserIdClass(id)];
    return out;
}

UmbraVisibility UmbraTome::queryView(const float pos[3], const float forward[3], const float up[3], float tanHalfX,
                                     float tanHalfY, const UmbraQueryOptions& opt) const {
    View v;
    for (int a = 0; a < 3; ++a) {
        v.pos[a] = pos[a];
        v.f[a] = forward[a];
    }
    normalize(v.f);
    cross(v.f, up, v.r);
    normalize(v.r);
    cross(v.r, v.f, v.u);
    normalize(v.u);
    v.tx = tanHalfX > 0 ? tanHalfX : 1.0f;
    v.ty = tanHalfY > 0 ? tanHalfY : 1.0f;
    return finishQuery(pos, opt, &v, 1);
}

UmbraVisibility UmbraTome::queryPoint(const float pos[3], const UmbraQueryOptions& opt) const {
    static const float dirs[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    static const float ups[6][3] = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {1, 0, 0}, {1, 0, 0}};
    View views[6];
    for (int k = 0; k < 6; ++k) {
        View& v = views[k];
        for (int a = 0; a < 3; ++a) {
            v.pos[a] = pos[a];
            v.f[a] = dirs[k][a];
        }
        cross(v.f, ups[k], v.r);
        normalize(v.r);
        cross(v.r, v.f, v.u);
        normalize(v.u);
        v.tx = v.ty = 1.0f + 1.0f / 512.0f;  // a hair over 90 degrees so the faces overlap
    }
    return finishQuery(pos, opt, views, 6);
}

// ---------------------------------------------------------------------------------------------------------------
// Extraction from a gfx_map JSON export
// ---------------------------------------------------------------------------------------------------------------

namespace {

// Streams JSON text: finds "umbraTomeData", then its "bytes":"<hex>" and decodes the hex.
class TomeHexScanner {
public:
    explicit TomeHexScanner(std::vector<uint8_t>& out) : out_(out) {}
    // Returns false once finished (done) or failed.
    bool feed(const char* p, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            const char c = p[i];
            switch (phase_) {
                case 0:
                    if (step(c, kKey, keyAt_)) phase_ = 1, scanned_ = 0;
                    break;
                case 1:  // ':' then '{' or null
                    if (c == ':' || c == ' ' || c == '\t' || c == '\r' || c == '\n') break;
                    if (c == '{') {
                        phase_ = 2;
                        keyAt_ = 0;
                        scanned_ = 0;
                        break;
                    }
                    error_ = c == 'n' ? "umbraTomeData is null" : "umbraTomeData is not an object";
                    phase_ = 9;
                    return false;
                case 2:
                    if (++scanned_ > 4096) {
                        error_ = "umbraTomeData has no \"bytes\"";
                        phase_ = 9;
                        return false;
                    }
                    if (step(c, kBytes, keyAt_)) phase_ = 3;
                    break;
                case 3: {
                    if (c == '"') {
                        if (half_) {
                            error_ = "odd hex length";
                            phase_ = 9;
                            return false;
                        }
                        phase_ = 8;
                        return false;
                    }
                    int v;
                    if (c >= '0' && c <= '9') v = c - '0';
                    else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
                    else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
                    else {
                        error_ = "bad hex digit";
                        phase_ = 9;
                        return false;
                    }
                    if (half_) {
                        out_.push_back(uint8_t(hi_ << 4 | v));
                        half_ = false;
                    } else {
                        hi_ = v;
                        half_ = true;
                    }
                    break;
                }
                default: return false;
            }
        }
        return true;
    }
    bool done() const { return phase_ == 8; }
    const std::string& error() const { return error_; }
    int phase() const { return phase_; }

private:
    static constexpr const char* kKey = "\"umbraTomeData\"";
    static constexpr const char* kBytes = "\"bytes\":\"";
    // Rolling match of a key whose only self-overlap is its leading quote.
    static bool step(char c, const char* key, size_t& at) {
        if (c == key[at]) {
            ++at;
            if (!key[at]) {
                at = 0;
                return true;
            }
        } else {
            at = c == key[0] ? 1 : 0;
        }
        return false;
    }
    std::vector<uint8_t>& out_;
    int phase_ = 0;
    size_t keyAt_ = 0, scanned_ = 0;
    bool half_ = false;
    int hi_ = 0;
    std::string error_;
};

}  // namespace

bool extractUmbraTomeFromGfxMapJsonText(const std::string& text, std::vector<uint8_t>& tome, std::string* err) {
    tome.clear();
    TomeHexScanner s(tome);
    s.feed(text.data(), text.size());
    if (s.done()) return true;
    setErr(err, !s.error().empty() ? s.error() : (s.phase() == 0 ? "no umbraTomeData member" : "umbraTomeData is cut short"));
    tome.clear();
    return false;
}

bool extractUmbraTomeFromGfxMapJson(const std::string& path, std::vector<uint8_t>& tome, std::string* err) {
    tome.clear();
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return setErr(err, "cannot open " + path), false;
    TomeHexScanner s(tome);
    std::vector<char> buf(size_t(8) << 20);
    while (true) {
        const size_t got = std::fread(buf.data(), 1, buf.size(), f);
        if (!got) break;
        if (!s.feed(buf.data(), got)) break;
    }
    std::fclose(f);
    if (s.done()) return true;
    setErr(err, !s.error().empty() ? s.error() : (s.phase() == 0 ? "no umbraTomeData member" : "umbraTomeData is cut short"));
    tome.clear();
    return false;
}

}  // namespace iw8r
