// The Umbra 3 occlusion tome of Replay 1.20 stock maps (GfxWorld.umbraTomeData, ImpTome version 0xD6000014):
// parser, structural checks and an offline visibility query (camera point -> visible Umbra objects).
// Layout, evidence (VERIFIED / INFERRED) and the way to reproduce it: docs/research/110_umbra_tome.md.
//
// What a tome holds (offsets are byte offsets from the tome start, all sections 16-byte aligned):
//   - the header (0x170 bytes, the IW7 Umbra::ImpTome field names);
//   - the tile tree: a k-d tree over the tome box, one tile per tree node (inner nodes may carry a coarse "LOD" tile,
//     leaf nodes carry the leaf tiles the camera lives in);
//   - per tile (Umbra::ImpTile, 0x60 bytes): its cells (36 bytes each), its portals (16 bytes each), a view tree that
//     maps a point to its cell(s) (leaf tiles only) and the cell index lists that tree points into;
//   - clusters (groups of cells, 20 bytes each) joined by cluster portals (16 bytes each);
//   - the objects: bounds, near / far distances, user IDs (class in bits 28-30) and the bit-packed object lists of the
//     cells.
// The query walks the leaf-tile cell graph from the camera's cell through the portals, clipping the view to each
// portal's screen rectangle (a rectangle-based stand-in for Umbra's portal rasterizer: it can only show more, never
// less, than an exact portal walk), and reports the objects listed by the visible cells.
// Read-only. Nothing here writes a file.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace iw8r {

constexpr uint32_t kUmbraTomeVersion = 0xD6000014u;  // ImpTome::m_versionMagic of Replay 1.20
constexpr uint32_t kUmbraNoChecksum = 0xD15AB1EDu;   // m_crc32 of every stock tome ("disabled")
constexpr uint32_t kUmbraNone = 0xFFFFFFFFu;
constexpr size_t kUmbraHeaderSize = 0x170;
constexpr size_t kUmbraTileSize = 0x60;
constexpr size_t kUmbraCellSize = 36;
constexpr size_t kUmbraPortalSize = 16;
constexpr size_t kUmbraClusterSize = 20;

// User-ID classes (bits 28-30), as the Replay query consumer (0x18CCA80) reads them.
enum UmbraObjectClass : unsigned {
    kUmbraSurface = 0,          // GfxWorld surface index
    kUmbraStaticModel = 1,      // static model placement (smodel) index
    kUmbraGate = 2,             // gate (door) user ID (GfxWorld.umbraGates; tome m_gateIndexMap)
    kUmbraVolumetric = 3,       // world volumetric
    kUmbraPrimaryLight = 4,     // primary light index
    kUmbraReflectionProbe = 5,  // reflection probe instance
    kUmbraDecalCollection = 6,  // decal volume collection
    kUmbraClassCount = 8,
};
inline unsigned umbraUserIdClass(uint32_t id) { return (id >> 28) & 7u; }
inline uint32_t umbraUserIdIndex(uint32_t id) { return id & 0x0FFFFFFFu; }
const char* umbraClassName(unsigned cls);

// CRC32C (Castagnoli, reflected 0x82F63B78), the tome checksum over bytes [8, size).
uint32_t umbraCrc32c(const uint8_t* data, size_t size);

// Umbra::ImpTome (0x170 bytes). Offsets (DataPtr) are absolute byte offsets in the tome, 0 = absent.
struct UmbraTomeHeader {
    uint32_t version = 0, crc32 = 0, size = 0;
    float lodBaseDistance = 0;
    uint32_t flags = 0;
    float treeMin[3] = {}, treeMax[3] = {};
    // m_tileTree (SerializedTreeData): node count << 5 | map width, tree bits, map, split value count, split values
    uint32_t tileTreeWord = 0, tileTreeData = 0, tileTreeMap = 0, tileTreeNumSplits = 0, tileTreeSplits = 0;
    uint32_t numObjects = 0, objBounds = 0, objDistances = 0, userIdStarts = 0, userIds = 0;
    uint32_t listWidths = 0, objectLists = 0, objectListSize = 0, clusterLists = 0, clusterListSize = 0;
    uint32_t numGates = 0, gateIndexMap = 0, gateVertices = 0, numGateVertices = 0, gateIndices = 0;
    uint32_t numClusters = 0, clusters = 0, clusterPortals = 0, cellStarts = 0;
    uint32_t numLeafTiles = 0, numTiles = 0, bitsPerSlotPath = 0, slotPaths = 0, tileLodLevels = 0, tiles = 0;
    uint32_t tileMatchingData = 0, matchingTrees = 0, numMatchingTrees = 0;
    uint32_t numTomes = 0, tomeClusterStarts = 0, tomeClusterPortalStarts = 0;
    std::string computation;  // m_computationString, e.g. "SO 24.0 SH 4.000 BF 100 F 0" (often empty)
    uint32_t objectDepthmaps = 0, depthmapFaces = 0, depthmapPalettes = 0, numFaces = 0, tilePortalExpands = 0;
    float boundsMin[3] = {}, boundsMax[3] = {};
    float clusterCoordScale = 0;  // cluster coordinates: treeMin + q * clusterCoordScale

    // m_listWidths: bits 0-4 object index width, bits 5-9 run-length width (0 on most maps; 5 on the mp_m_* maps,
    // whose object list entries are (first object | count << index width)).
    unsigned objectIndexWidth() const { return listWidths & 31u; }
    unsigned objectRunWidth() const { return (listWidths >> 5) & 31u; }
    unsigned objectListEntryWidth() const { return objectIndexWidth() + objectRunWidth(); }
};

// Umbra::ImpTile plus where its records landed in the decoded arrays. Index = tile-tree node.
struct UmbraTile {
    uint32_t offset = 0;              // byte offset of the ImpTile; 0 = this tree node has no tile
    uint32_t parent = kUmbraNone;     // tile-tree parent node
    uint32_t depth = 0;               // tile-tree depth (root 0)
    bool treeLeaf = false;            // a leaf of the tile tree (else an inner node, whose tile is a coarse LOD tile)
    uint32_t flags = 0;               // m_sizeAndFlags & 0xFF: stock 5 (leaf tile, view tree), 4 (LOD tile); converter 3
    uint32_t byteSize = 0;            // m_sizeAndFlags >> 8
    float min[3] = {}, max[3] = {};   // m_treeMin / m_treeMax: the tile-tree box
    float scale = 0, invScale = 0;    // world = min + q * scale (16-bit tile quanta)
    uint16_t qbounds[6] = {};         // m_tileBounds: minX, maxX, minY, maxY, minZ, maxZ (inclusive quanta)
    float portalExpand = 0;           // m_portalExpand (= m_tilePortalExpands[tile])
    float lodLevel = 0;               // m_tileLodLevels[tile] (-1 for nodes without a tile)
    uint32_t cellBase = 0, cellCount = 0, clusterCount = 0;  // global cells [cellBase, cellBase + cellCount)
    uint32_t portalBase = 0, portalCount = 0;                // global portals of this tile
    uint32_t viewNodes = 0, viewMapWidth = 0;                // view tree: node count, bits per leaf map entry
    uint32_t viewTree = 0, viewMap = 0, cellIndices = 0;     // absolute offsets (0 = none)
    uint32_t viewRank = 0;                                    // first entry of this tile's rank table (internal)
    bool hasViewTree() const { return (flags & 1u) != 0; }
};

// One cell (36 bytes). Bounds are tile quanta, inclusive: world [min + q * scale, min + (q + 1) * scale].
struct UmbraCell {
    uint32_t tile = 0;
    uint32_t portalStart = 0, portalCount = 0;  // global portal range
    uint32_t objectStart = 0, objectCount = 0;  // entries of the bit-packed object lists (an entry may be a run)
    uint32_t cluster = kUmbraNone;              // global cluster (LOD-tile cells have none)
    uint32_t flags = 0;                         // 0x80000000 | one-hot (0x10..0x2000) | bit 0 (leaf tile); INFERRED
    uint16_t qmin[3] = {}, qmax[3] = {};
};

// One portal (16 bytes) of a tile cell.
//   d0: bits 29-31 face (axis = face / 2; even = towards -axis, odd = towards +axis), bit 28 leads outside the tome
//       (target 0x1FFFFFF), bit 27 gate (its state is looked up per query), bit 26 coarse (an LOD-tile link),
//       bit 25 internal (the game tests (d0 & 0x12000000) == 0x02000000), bits 0-24 target tile (external and gate
//       portals; a one-hot value on plain internal portals)
//   d1: bits 16-31 target cell (in the target tile), bits 0-15 plane (internal portals: the boundary in tile quanta;
//       external portals sit on m_tileBounds[face] and carry a one-hot value here)
//   d2, d3: the rectangle, (min << 16 | max) tile quanta on axes (axis + 1) % 3 and (axis + 2) % 3
//   Gate portals: d3 >> 12 indexes a 6-float world box in m_gateVertices; the query treats every gate as open.
struct UmbraPortal {
    uint32_t raw[4] = {};
    uint8_t face = 0;
    bool internal = false, coarse = false, gate = false, outside = false;
    uint32_t sourceCell = 0;                  // global
    uint32_t targetTile = kUmbraNone;
    uint32_t targetCell = kUmbraNone;         // global (kUmbraNone: outside / unresolved)
    uint16_t plane = 0;                       // internal non-gate portals
    uint16_t expand = 0;                      // growth in tile quanta (d0 bits 0-15 internal, d1 bits 0-15 external)
    uint16_t rect[2][2] = {};                 // [axis b / c][min, max], inclusive quanta
    uint32_t gateBox = kUmbraNone;            // gate portals: first float of their box in m_gateVertices
    unsigned axis() const { return face >> 1; }
};

// Cluster (20 bytes): portal range + bounds in cluster coordinates; and a cluster portal (16 bytes, the tile portal
// layout with d0 bits 0-15 = target cluster and the plane / rectangle in cluster coordinates).
struct UmbraCluster {
    uint32_t portalStart = 0, portalCount = 0;
    uint16_t qmin[3] = {}, qmax[3] = {};
};
struct UmbraClusterPortal {
    uint32_t raw[4] = {};
    uint8_t face = 0;
    bool gate = false;
    uint32_t target = kUmbraNone;
    uint16_t plane = 0;
    uint16_t rect[2][2] = {};
};

enum class UmbraChecksum { Valid, Disabled, Invalid };

// validate(): structure and self-consistency counts. `ok` is false on any structural error (errors lists the first
// few); the counts are for tests and the research note.
struct UmbraTomeReport {
    bool ok = true;
    std::vector<std::string> errors;
    UmbraChecksum checksum = UmbraChecksum::Invalid;
    uint32_t treeNodes = 0, treeLeaves = 0, leafTiles = 0, lodTiles = 0, emptyLeaves = 0;
    uint32_t cells = 0, portals = 0, internalPortals = 0, finePortals = 0, coarsePortals = 0, gatePortals = 0,
             outsidePortals = 0;
    uint32_t unmatchedInternal = 0, unmatchedFine = 0, unmatchedCoarse = 0;  // no reverse portal (non-gate only)
    uint32_t finePortalsTouchingLod = 0, coarsePortalsBetweenLeafTiles = 0;    // expected 0: fine = leaf <-> leaf
    uint32_t gatePortalsWithBox = 0, gateUserIdsOfClass2 = 0;
    uint32_t clusterPortals = 0, unmatchedClusterPortals = 0;
    uint32_t viewLeaves = 0, solidViewLeaves = 0, multiCellViewLeaves = 0;
    uint32_t rankTablesChecked = 0, slotPathsChecked = 0;
    uint32_t objectsReferenced = 0;  // objects listed by at least one cell
    uint32_t userIdCount = 0;
    uint32_t userIdsByClass[kUmbraClassCount] = {};
};

// Options of the visibility query. Defaults follow the stock data's own conservative settings.
struct UmbraQueryOptions {
    bool distanceCull = true;     // drop objects outside their [near, far) distance (m_objDistances, squared)
    bool portalExpand = true;     // grow each portal by its own expansion (tile quanta), as the game does
    float nearPlane = 0.5f;       // view-space near plane for portal / object projection (world units)
    float cameraRadius = 2.0f;    // portals within this distance of the camera pass unclipped; also the slop for
                                  // finding the start cell when the point is inside solid voxels
    unsigned maxRectsPerCell = 4; // screen rectangles kept per cell and face before they are merged
    size_t maxSteps = 20000000;   // traversal step limit per query (visibility.truncated when reached)
};

enum class UmbraQueryStatus {
    Ok,
    OutsideTome,  // the point is outside the tome box
    NoTile,       // a tile-tree leaf without a tile (empty space Umbra did not compute)
    InSolid,      // inside solid voxels: no cell found, nothing reported
};

struct UmbraVisibility {
    UmbraQueryStatus status = UmbraQueryStatus::Ok;
    bool truncated = false;
    uint32_t tile = kUmbraNone;
    std::vector<uint32_t> startCells;  // global
    std::vector<uint32_t> cells;       // visible global cells, sorted
    std::vector<uint32_t> objects;     // visible object indices, sorted
    std::vector<uint32_t> userIds;     // their user IDs, sorted, unique
    uint32_t userIdsByClass[kUmbraClassCount] = {};
    size_t steps = 0;
};

class UmbraTome {
public:
    // Parses and decodes a tome (copies the bytes). Fails on a wrong version, a size field that disagrees, offsets or
    // counts outside the buffer, or tables that do not decode. Gate geometry and tome collections (m_numTomes) are
    // kept raw; depthmaps are not used by any stock tome.
    bool parse(const uint8_t* data, size_t size, std::string* err = nullptr);
    bool parse(std::vector<uint8_t> bytes, std::string* err = nullptr);

    const UmbraTomeHeader& header() const { return h_; }
    const std::vector<uint8_t>& bytes() const { return d_; }
    UmbraChecksum checksum() const;

    const std::vector<UmbraTile>& tiles() const { return tiles_; }
    const std::vector<UmbraCell>& cells() const { return cells_; }
    const std::vector<UmbraPortal>& portals() const { return portals_; }
    const std::vector<UmbraCluster>& clusters() const { return clusters_; }
    const std::vector<UmbraClusterPortal>& clusterPortals() const { return clusterPortals_; }

    // Tile tree: 2 bits per node in breadth-first order (3 = leaf, else the split axis), children of the k-th inner
    // node at 2k + 1 and 2k + 2, one split value per node.
    uint32_t treeNodeCode(uint32_t node) const;
    float treeSplit(uint32_t node) const;
    void treeChildren(uint32_t node, uint32_t& lo, uint32_t& hi) const;

    // Objects.
    uint32_t objectCount() const { return h_.numObjects; }
    void objectBounds(uint32_t object, float mn[3], float mx[3]) const;
    // m_objDistances: a box (usually the bounds) and squared near / far distances (far = +inf: never culled).
    void objectDistance(uint32_t object, float mn[3], float mx[3], float& nearSq, float& farSq) const;
    // The user IDs of an object (one, or a range when m_userIDStarts is present).
    void objectUserIds(uint32_t object, uint32_t& first, uint32_t& count) const;
    uint32_t userId(uint32_t index) const;
    // m_gateIndexMap: the user ID of each gate (class 2, GfxWorld.umbraGates[i].objID), m_numGates entries.
    std::vector<uint32_t> gateUserIds() const;
    uint32_t userIdTotal() const;
    uint32_t objectListEntry(uint32_t index) const;  // raw entry of the bit-packed object lists
    void objectListRun(uint32_t index, uint32_t& first, uint32_t& count) const;  // an entry as objects [first, +count)
    std::vector<uint32_t> cellObjects(uint32_t cell) const;  // the objects a cell lists (runs expanded)

    // Geometry in world units.
    void cellBounds(uint32_t cell, float mn[3], float mx[3]) const;
    // The portal rectangle as a quad in its plane (corners in order), optionally grown by its expansion. Gate portals
    // have a box instead (gateBounds): portalQuad returns false for them.
    bool portalQuad(uint32_t portal, float corners[4][3], bool expand = false) const;
    bool gateBounds(uint32_t portal, float mn[3], float mx[3]) const;

    // Point lookups.
    uint32_t leafNodeAt(const float p[3]) const;  // tile-tree leaf node containing p (kUmbraNone outside)
    uint32_t leafTileAt(const float p[3]) const;  // same, kUmbraNone when that node has no tile
    // Global cells of the view-tree leaf containing p (empty: solid voxels or no tile); optional tile out.
    std::vector<uint32_t> cellsAt(const float p[3], uint32_t* tileOut = nullptr) const;

    UmbraTomeReport validate() const;

    // Visibility from a point in all directions (six 90-degree faces).
    UmbraVisibility queryPoint(const float pos[3], const UmbraQueryOptions& opt = {}) const;
    // Visibility for one view: forward / up (need not be unit or exactly orthogonal), tangents of the half angles.
    UmbraVisibility queryView(const float pos[3], const float forward[3], const float up[3], float tanHalfX,
                              float tanHalfY, const UmbraQueryOptions& opt = {}) const;

private:
    struct View;
    bool decode(std::string* err);
    uint32_t viewLeafAt(const UmbraTile& t, const float q[3]) const;
    void runView(const View& v, const UmbraQueryOptions& opt, std::vector<uint8_t>& cellSeen,
                 std::vector<uint8_t>& objSeen, UmbraVisibility& out) const;
    UmbraVisibility finishQuery(const float pos[3], const UmbraQueryOptions& opt, const View* views, size_t count) const;
    uint32_t viewCode(const UmbraTile& t, uint32_t node) const;
    uint32_t viewInnerBefore(const UmbraTile& t, uint32_t node) const;

    std::vector<uint8_t> d_;
    UmbraTomeHeader h_;
    std::vector<uint32_t> treeWords_;     // tile tree bits
    std::vector<uint32_t> treeRank_;      // inner nodes before each tree word
    std::vector<uint32_t> viewRank_;      // view trees: inner nodes before each tree word, all tiles back to back
    std::vector<UmbraTile> tiles_;
    std::vector<UmbraCell> cells_;
    std::vector<UmbraPortal> portals_;
    std::vector<UmbraCluster> clusters_;
    std::vector<UmbraClusterPortal> clusterPortals_;
};

// Cuts GfxWorld.umbraTomeData out of a gfx_map JSON export (the world cache's
// assets/gfx_map/.../<map>.d3dbsp.*.asset.json): the "bytes" hex of the "umbraTomeData" member. The file is read in
// chunks (the exports reach 420 MB); only the tome's own bytes are kept. False when the member is missing or null.
bool extractUmbraTomeFromGfxMapJson(const std::string& path, std::vector<uint8_t>& tome, std::string* err = nullptr);
bool extractUmbraTomeFromGfxMapJsonText(const std::string& text, std::vector<uint8_t>& tome, std::string* err = nullptr);

}  // namespace iw8r
