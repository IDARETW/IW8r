// Reflection probes of a STOCK Replay 1.20 map (positions, influence boxes and their HDR octahedral images), for the
// viewport's "Game Lighting" mode. Data side only; the shader is elsewhere (docs/research/95_lit_shader_math.md,
// "Indirect specular: probes").
//
// Where the data is (VERIFIED on mp_shipment and mp_backlot2, 2026-11-12):
//   - Records: the gfx_map export (ACTS JSON, <zone cache>/assets/gfx_map/**.json), draw.reflectionProbeData:
//       reflectionProbes[reflectionProbeCount]       GfxReflectionProbe (48 B): livePath, origin, angles,
//                                                    probeInstances[] (u16 indexes into reflectionProbeInstances),
//                                                    probeRelightingIndex (65535 none), probeImageIndirection (0)
//       reflectionProbeInstances[instanceCount]       GfxReflectionProbeInstance (144 B): probePosition, probeImageIndex
//                                                    (the array slice = the probe), lodLevel, flags (1: the fallback /
//                                                    global box), probeRotation (quaternion), volumeObb {center,
//                                                    x/y/zAxis, halfSize}, priority, feather, expandProjectionNeg/Pos
//       reflectionProbeSH[4 * probeCount]            GfxSH3Vec (9 floats): four SH3 vectors per probe
//       reflectionProbeArrayImage_octahedron         the image asset name ("*reflection_probe_array_octahedron")
//     reflectionProbeImages / _octahedron are null: every probe is one slice of the array image.
//   - Pixels: the array image is an ordinary GfxImage (0xE8 bytes) in the zone, followed in the decompressed zone
//     (<zone>.ff.dec, ACTS -d) by its name string and then its pixels (pointer -2 = they follow):
//       format 42 GFX_PF_BC6H (unsigned), 256 x 256, numElements = probe count, 6 levels (256 .. 8), flags 0x20001,
//       totalSize = count * 87360. Level-major: level 0 of every slice, then level 1 of every slice, ...
//     The images are octahedral in WORLD space (Z up), the probe's angles not applied: for a world direction d,
//     o = d.xy / (|d.x| + |d.y| + |d.z|), folded for d.z < 0 (o = (1 - |o.yx|) * sign(o.xy)); column = x, row = y,
//     texel = (o * 0.5 + 0.5) * (S - 8) + 4 at a level of S texels. VERIFIED: the brightest texel of every outdoor
//     mp_shipment probe decodes to (0.90, -0.27, 0.33), the map's sunDirection is (0.904, -0.249, 0.346).
//     Gutter: 4 texels on each side; top row r < 4 holds row 7 - r at column S - 1 - x (the octahedral wrap),
//     VERIFIED on levels 0-3 (BC6H error aside). Levels 4 (16 px) and 5 (8 px) are near-uniform blurs that do not
//     follow it; the shader's (4 << m) / 256 gutter maths samples level 5 at its centre.
//     Levels are prefiltered (blurrier than a box downsample): roughness levels for mip = 5 - 5 * gloss.
//     Brightness: Rec. 709 luminance averaged over the sphere (solid-angle weighted, gutter skipped) is 0.58-1.02 on
//     mp_shipment and 0.11-0.99 (all but 3 of 63 in 0.75-0.99) on mp_backlot2, sun texels up to 9576 included: the
//     images look luminance-normalised (INFERRED; indoor and outdoor probes have the same level).
//
// Cache file ("probes.iw8rprb", little endian). The images are kept as the game's BC6H blocks (bit exact, 87360
// bytes per probe: about 1.5 MB for mp_shipment, 5.5 MB for mp_backlot2; half-float RGB would be 6x that) and are
// decoded to half RGB when read.
//   0    char[8] "IW8RPRB\1"
//   8    u32     version (1)
//   12   u32     probe count P
//   16   u32     image size (256)
//   20   u32     mip count (6)
//   24   u32     box count B
//   28   u32     image format (42 = BC6H unsigned)
//   32   P probe records, 208 bytes each:
//          f32 origin[3], f32 angles[3], f32 mins[3], f32 maxs[3], i32 relightingIndex, u32 imageIndirection,
//          f32 sh[4][9], u32 firstBox, u32 boxCount                         (boxCount <= 64)
//        then (sum of boxCount) u32 box indexes
//   ...  B box records, 140 bytes each: f32 position[3], u32 imageIndex, u32 lodLevel, u32 flags, f32 rotation[4],
//          f32 center[3], f32 axes[3][3], f32 halfSize[3], f32 priority, f32 feather[3], f32 expandNeg[3],
//          f32 expandPos[3]
//   ...  P x bc6hBytesPerProbe() bytes: each probe's BC6H blocks, level 0 first, blocks row-major
//
// Warzone maps (mp_donetsk, mp_br_quarry; docs/research/108_warzone_reflection_probes.md, VERIFIED 2026-11-13) keep
// every record in the main zone but have NO array image: reflectionProbeArrayImage_octahedron is null and each
// probe's image is found through its probeImageIndirection (stockProbeZone / Bucket / Element below):
//   - zone 0xFFF: a SHARED probe; its image is the streamed GfxImage reflectionProbeImages_octahedron[element]
//     ("*reflection_probe_octahedron_<probe index>"), pixels in the XPaks: ACTS image export -> DDS
//     (readStockProbeDds).
//   - zone 0: the main zone's own transient zone (the fallback and the map-sized boxes); pixels in the main .ff.dec.
//   - zone z: an EXCLUSIVE probe of tile <map>_<z %05d>_tr; pixels in that tile's .ff.dec.
//   Zones 0 and z keep their images in GfxReflectionProbeTransientData (the reflectionProbes of the zone's
//   gfx_map_trzone export): bucket b is a level-major texture array; the probe's element is its slice
//   (extractStockProbeZoneImages). Every image has the MP format (BC6H unsigned, 256 x 256, 6 levels, 87360 bytes).
//
// Cache file version 2 (v1 stays readable and means "K = P, identity"; MP maps keep writing v1):
//   0    char[8] "IW8RPRB\1"
//   8    u32     version (2)
//   12   u32     probe count P (0 in a tile file)
//   16   u32     image size (256)
//   20   u32     mip count (6)
//   24   u32     box count B (0 in a tile file)
//   28   u32     image format (42)
//   32   u32     image count K (the images this file holds)
//   36   u32     zone: 0xFFFFFFFF = the main file, else the tile's transient zone index
//   40   P probe records (v1), the box index lists (v1), B box records (v1)
//   ...  K x u32 probe index (ascending)
//   ...  tile file only: u32 N, N x u32 box index (the tile's reflectionProbeInstances)
//   ...  K x bc6hBytesPerProbe() bytes (v1 order)
// Main file (<zone>/probes.iw8rprb): all records + the images of the shared and zone-0 probes. Tile file
// (<zone>/streamed/<tile>/probes.iw8rprb): that tile's exclusive images; mergeStockProbeImages adds them to a set.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace iw8r {

using ProbeVec3 = std::array<float, 3>;

// One influence box (GfxReflectionProbeInstance). The box is oriented: a point p is inside when
// |dot(p - center, axes[k])| <= halfSize[k] for k = 0..2. On stock maps the axes are a permutation of the world axes
// with signs (mp_shipment: x = (0,-1,0), y = (0,0,-1), z = (1,0,0)), so halfSize is not in world x, y, z order.
struct StockProbeBox {
    ProbeVec3 position{};          // probePosition (the probe's capture origin)
    int imageIndex = 0;            // probeImageIndex: the probe (array slice) this box shows
    int lodLevel = 0;
    int flags = 0;                 // bit 0 on the fallback box and on some ordinary boxes (meaning not established)
    std::array<float, 4> rotation{0, 0, 0, 1};  // probeRotation (x, y, z, w); identity on every stock box seen
    ProbeVec3 center{};
    std::array<ProbeVec3, 3> axes{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    ProbeVec3 halfSize{};
    float priority = 0;            // higher first; the shader blends front to back (the game's buffer order)
    ProbeVec3 feather{};           // fade width in units along each box axis (8 on every stock box seen)
    ProbeVec3 expandNeg{}, expandPos{};  // expandProjectionNeg / Pos (0 on every stock box seen)
};

struct StockProbe {
    ProbeVec3 origin{};            // capture position, Z-up inches (the editor's world space)
    ProbeVec3 angles{};            // pitch, yaw, roll in degrees (yaw 90 / 180 on some shipment probes)
    ProbeVec3 mins{}, maxs{};      // world AABB of this probe's boxes, the fallback box excluded (origin when none)
    int relightingIndex = -1;      // probeRelightingIndex, -1 when none (65535)
    uint32_t imageIndirection = 0;
    std::array<std::array<float, 9>, 4> sh{};  // reflectionProbeSH[4 * i + k]
    std::vector<int> boxes;        // indexes into StockProbeSet::boxes (probeInstances)
    // The probe's octahedral image: the game's BC6H blocks (all levels, level 0 first; bc6hBytesPerProbe bytes) and,
    // after decodeStockProbeImages / readStockProbeCache(decode = true), mips[m] = (size >> m)^2 x 3 half floats
    // (IEEE binary16 bits, R G B), row 0 first.
    std::vector<uint8_t> bc6h;
    std::vector<std::vector<uint16_t>> mips;
};

struct StockProbeSet {
    int size = 0;                  // level-0 width = height (256)
    int mipCount = 0;              // 6: 256, 128, 64, 32, 16, 8
    std::vector<StockProbe> probes;
    std::vector<StockProbeBox> boxes;  // every GfxReflectionProbeInstance, in the game's order
    std::string arrayImageName;    // "*reflection_probe_array_octahedron" ("" on a streamed set)
    // The streamed layout (Warzone): no array image, the images found through each probe's imageIndirection. A
    // probe whose image no file has supplied yet keeps bc6h empty.
    bool streamed = false;
    int sharedCount = 0;                        // sharedReflectionProbeCount
    std::vector<std::string> sharedImageNames;  // reflectionProbeImages_octahedron[k].name, k = a shared probe's
                                                // element (from the gfx_map export; not kept in the cache file)
};

constexpr uint32_t kProbeFormatBC6H = 42;  // GFX_PF_BC6H (unsigned)

// --- probeImageIndirection (research 108 section 0) ---
constexpr uint32_t kStockProbeSharedZone = 0xFFF;       // stockProbeZone of a shared probe
constexpr uint32_t kStockProbeMainFile = 0xFFFFFFFFu;   // StockProbeImages::zone / cache header zone of a main file
constexpr int kStockProbeImageSize = 256;               // every Warzone probe image: 256 x 256, 6 levels, BC6H
constexpr int kStockProbeImageMips = 6;
inline uint32_t stockProbeZone(uint32_t indirection) { return indirection >> 20; }
inline uint32_t stockProbeBucket(uint32_t indirection) { return (indirection >> 16) & 0xFu; }
inline uint32_t stockProbeElement(uint32_t indirection) { return indirection & 0xFFFFu; }
inline bool stockProbeIsShared(uint32_t indirection) { return stockProbeZone(indirection) == kStockProbeSharedZone; }

// The fallback box (instance 0 on stock maps): priority -FLT_MAX, centred on its probe, half size 131072 (the whole
// map), feather 8. It is what a point outside every other box gets.
bool stockProbeBoxIsFallback(const StockProbeBox& b);

// BC6H blocks of one probe for `size` x `size` and `mipCount` levels (87360 for 256 and 6).
size_t bc6hBytesPerProbe(int size, int mipCount);

// --- BC6H ---
// Decodes BC6H (unsigned when `isSigned` is false) to half floats, 3 per texel (R, G, B), row 0 first. Width and
// height need not be multiples of 4. Fails (out untouched) when `bytes` is too small.
bool decodeBC6H(const uint8_t* data, size_t bytes, int width, int height, bool isSigned, std::vector<uint16_t>& out);

// --- Records ---
// Parses draw.reflectionProbeData from a gfx_map JSON text (the whole export or any text that contains the
// "reflectionProbeData" object). Fills size 0, no images. Up to 65535 probes ("too many" beyond). Without an array
// image the set is the streamed layout when sharedReflectionProbeCount > 0 or any probeImageIndirection != 0 (else
// an error); reflectionProbeSH may be "values" ({coeffs}) or hex "bytes" (36 bytes = 9 little-endian floats each).
bool parseStockProbeRecords(const std::string& jsonText, StockProbeSet& out, std::string* err = nullptr);
// The same from the gfx_map export file: only the reflectionProbeData object is read (streamed, the file is 100+ MB).
bool readStockProbeRecords(const std::string& gfxMapJsonPath, StockProbeSet& out, std::string* err = nullptr);
// The gfx_map JSON under a cached zone folder (<zoneDir>/assets/gfx_map/**/*.json), "" when there is not exactly one.
std::string findStockGfxMapJson(const std::string& zoneDir);

// --- Images ---
// Finds the array image `set.arrayImageName` in a decompressed zone (streamed in chunks) and fills each probe's bc6h
// (slice i = probe i). Checks the header against the records: format 42, square, numElements = probe count,
// totalSize = count * bc6hBytesPerProbe. Sets size and mipCount. `imageOffset` (optional) receives the GfxImage's
// offset. Does not decode (decodeStockProbeImages).
bool extractStockProbeImages(const std::string& decPath, StockProbeSet& set, std::string* err = nullptr,
                             uint64_t* imageOffset = nullptr, size_t chunkBytes = 0);
// Decodes every probe's bc6h into mips.
bool decodeStockProbeImages(StockProbeSet& set, std::string* err = nullptr);
// Records + images: readStockProbeRecords(gfxMapJsonPath) then extractStockProbeImages(decPath). Not decoded.
bool extractStockProbes(const std::string& gfxMapJsonPath, const std::string& decPath, StockProbeSet& out,
                        std::string* err = nullptr);

// One texel of a decoded probe image (linear RGB). No bounds check beyond returning zero.
ProbeVec3 stockProbeTexel(const StockProbe& p, int size, int mip, int x, int y);
// Mean Rec. 709 luminance of a decoded level, the 4-texel gutter excluded when `skipGutter` (and the level > 8).
double stockProbeMeanLuminance(const StockProbe& p, int size, int mip, bool skipGutter);

// The GfxImage name of a shared probe's image: sharedImageNames[element] when the set has it, else
// "*reflection_probe_octahedron_<probeIndex>" (the rule on every Verdansk and Quarry record). "" when the probe is
// not shared or out of range.
std::string stockProbeSharedImageName(const StockProbeSet& set, int probeIndex);

// --- Streamed layout: transient zones (research 108 sections 2.1, 2.3, 4.2) ---
// GfxReflectionProbeTransientData of one transient zone, from its gfx_map_trzone export ("reflectionProbes" values[0]).
struct StockProbeTransientZone {
    int zoneIndex = -1;                  // transientZoneIndex (0 = the main zone, z = tile <map>_<z %05d>_tr)
    bool hasProbes = false;              // reflectionProbes not null
    uint32_t exclusiveCount = 0;         // probes whose image only this zone holds
    std::vector<uint16_t> instances;     // reflectionProbeInstances: the boxes this zone switches on
    std::vector<uint32_t> bucketCounts;  // exclusiveProbeBuckets[b].elementCount (64/32/../1, largest first)
};
// From the export text (the bucket pixel hex is never parsed: ACTS exports it wrongly, research 108 section 2.4).
// A zone whose reflectionProbes is null parses with hasProbes false.
bool parseStockProbeTransientZone(const std::string& trzoneJsonText, StockProbeTransientZone& out, std::string* err = nullptr);
// The same from the export file, streamed (the main zone's export is 30 MB, the probes near its end).
bool readStockProbeTransientZone(const std::string& trzoneJsonPath, StockProbeTransientZone& out, std::string* err = nullptr);

// Images of some probes of a set, keyed by probe index: what a zone's extraction found, or a v2 tile file.
struct StockProbeImages {
    uint32_t zone = kStockProbeMainFile;     // the transient zone index (kStockProbeMainFile: none / a main file)
    int size = 0, mipCount = 0;
    std::vector<int> probes;                 // probe indexes, ascending
    std::vector<std::vector<uint8_t>> bc6h;  // bc6h[k]: the image of probes[k] (bc6hBytesPerProbe bytes, level 0 first)
    std::vector<int> boxes;                  // the zone's reflectionProbeInstances (box indexes it switches on)
};

// Finds zone `tz`'s GfxReflectionProbeTransientData in its decompressed zone (the main .ff.dec for zone 0, the
// tile's .ff.dec for a tile; read in chunks, never whole) by the exact needle of research 108 section 4.2 (u16
// instanceCount, u16 bucketCount, u32 exclusiveCount, u64 -2, u64 -2, the u16 instances, one {u32 count * 87360,
// u32 count, u64 0, u64 -2} per bucket; exactly one hit required), reads the buckets that follow and splits them
// level-major into per-probe images, keyed by the probe index of every record of `records` whose imageIndirection
// names zone tz.zoneIndex (bucket b, element e = slice e of bucket b). Checks that the records and the export agree
// (count, buckets, slots). A zone without exclusive images gives an empty result without reading the file.
// `headerOffset` (optional) receives the needle's offset.
bool extractStockProbeZoneImages(const std::string& decPath, const StockProbeTransientZone& tz, const StockProbeSet& records,
                                 StockProbeImages& out, std::string* err = nullptr, uint64_t* headerOffset = nullptr,
                                 size_t chunkBytes = 0);

// A shared probe's image from an ACTS image export (DDS: DX10 header, DXGI 95 BC6H_UF16, 256 x 256, 6 mips, one
// 2D slice, nothing after the data): the 87360 bytes after the 148-byte header. Anything else is refused.
bool parseStockProbeDds(const uint8_t* data, size_t bytes, std::vector<uint8_t>& bc6h, std::string* err = nullptr);
bool readStockProbeDds(const std::string& ddsPath, std::vector<uint8_t>& bc6h, std::string* err = nullptr);

// Copies `images` into set.probes[probe].bc6h by probe index; a set without an image size takes the images'. Fails
// (set untouched) when an index is out of range or the image size differs.
bool mergeStockProbeImages(StockProbeSet& set, const StockProbeImages& images, std::string* err = nullptr);

// --- Camera-local subset (research 108 section 4.4) ---
// A set the renderer can upload as it is (every probe has its image; at most 128 boxes and slices): the fallback box
// plus the `maxBoxes` (at most 127) boxes nearest to `camera` (distance to their world AABB, 0 inside) whose probe
// image is present, ordered by lodLevel ascending then priority descending (the game's order), the fallback last.
// Probes are re-indexed in box order (boxes' imageIndex and probes' box lists follow). Empty when the fallback's
// image is missing and no other box has one.
StockProbeSet buildStockProbeSubset(const StockProbeSet& full, const ProbeVec3& camera, int maxBoxes = 127);

// Verdansk's images are 160+ MB: the editor keeps the records and this index of where each probe's image lies in the
// cache files, and reads only the subset's images.
struct StockProbeImageIndex {
    int size = 0, mipCount = 0;
    std::vector<std::string> files;
    std::vector<int> fileOf;         // per probe: index into files, -1 = no image anywhere
    std::vector<uint64_t> offsetOf;  // per probe: byte offset of its image in that file
};
// Adds the images of one cache file (v1, v2 main or v2 tile) to `idx`, sized to `probeCount` probes on first use.
// A later file wins for a probe two files hold. Fails on a file whose image size differs from the index's.
bool indexStockProbeCacheImages(const std::string& path, size_t probeCount, StockProbeImageIndex& idx, std::string* err = nullptr);
// buildStockProbeSubset for records whose images stay on disk: the same boxes and order, the chosen probes' images read
// from their files. Fails (empty set) when a chosen image cannot be read.
StockProbeSet buildStockProbeSubset(const StockProbeSet& records, const StockProbeImageIndex& idx, const ProbeVec3& camera,
                                    int maxBoxes = 127, std::string* err = nullptr);

// --- Cache file (format above) ---
// An array-image set (MP) is written as v1 (every probe needs its image). A streamed set is written as a v2 main
// file: every record plus the images present (shared and zone 0).
bool writeStockProbeCache(const std::string& path, const StockProbeSet& set, std::string* err = nullptr);
// Reads a v1 file or a v2 main file (a tile file is refused: readStockProbeTileCache). `decode`: also fill each
// probe's mips, for v1 files only; a v2 file's images stay BC6H (Verdansk would need 1 GB of halves; decode a subset
// with decodeStockProbeImages).
bool readStockProbeCache(const std::string& path, StockProbeSet& out, std::string* err = nullptr, bool decode = true);
// The records of a v1 / v2 main file without reading its images (every bc6h empty).
bool readStockProbeCacheRecords(const std::string& path, StockProbeSet& out, std::string* err = nullptr);
// A v2 tile file: `images.zone` must name the tile (not kStockProbeMainFile); its K images and box list.
bool writeStockProbeTileCache(const std::string& path, const StockProbeImages& images, std::string* err = nullptr);
bool readStockProbeTileCache(const std::string& path, StockProbeImages& out, std::string* err = nullptr);

}  // namespace iw8r
