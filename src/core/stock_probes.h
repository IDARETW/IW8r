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
    std::string arrayImageName;    // "*reflection_probe_array_octahedron"
};

constexpr uint32_t kProbeFormatBC6H = 42;  // GFX_PF_BC6H (unsigned)

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
// "reflectionProbeData" object). Fills size 0, no images.
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

// --- Cache file (format above) ---
bool writeStockProbeCache(const std::string& path, const StockProbeSet& set, std::string* err = nullptr);
// `decode`: also fill each probe's mips (else only bc6h).
bool readStockProbeCache(const std::string& path, StockProbeSet& out, std::string* err = nullptr, bool decode = true);

}  // namespace iw8r
