// Baked lightmap of a STOCK Replay 1.20 map, read from the decompressed zone (docs/research/94_stock_lightmaps.md).
//
// A stock map's lightmap is not an image asset: its pixels are in the world's transient zone
// (GfxWorldTransientZone.tempLightmapData), which ACTS's gfx_map_trzone export leaves null. They are read from the
// decompressed zone (acts ... fastfile -d --noAssetDump, which writes <zone>.ff.dec):
//   - Root: GfxLightmapAtlasDataTemp, 0xF10 bytes: u32 transientZone, numLightmaps, firstLightmapIndex, lightmapType,
//     then 32 x GfxLightmapRaw { GfxImageRaw textures[3] }. GfxImageRaw is 0x28 bytes: GfxImageDesc (u32 format,
//     u32 flags, u32 totalSize, u16 width, height, depth, numElements, u8 levelCount, padding) at 0x00, u32 textureId
//     at 0x18, u64 pixels at 0x20 (-2: the pixels follow in the stream). x64-zt structs.hpp:12493-12523.
//   - The root is found by its signature (research 94 section 5): tz < 1536, 1 <= num <= 32, first < 512, type <= 2,
//     then textures[0]: format 1..49, totalSize > 0, width and height 1..16384, depth 1, levelCount 1..16,
//     pixels -2. A candidate is then checked in full (every used texture, and the pixels fit in the file).
//     Exactly one root on each map seen (mp_shipment at 0x88BC494, mp_backlot2 at 0x6C0827E: not 4-aligned).
//   - The pixels follow the root at root + 0xF10, plane after plane, lightmap after lightmap, with no padding.
//   - Planes: 0 BC4 (39) ambient fraction f; 1 R11G11B10F (32) irradiance C, linear, no scale; 2 BC5 (40) octahedral
//     direction, or R8G8B8A8 (6) on AB_LIGHTING maps (R, G the direction; B, A not decoded, meaning unknown).
//     Type 0 (REGULAR, the Warzone maps): plane 1 is BC6H unsigned (42), the others BC4 and BC5 as above.
// Warzone (research 94 section "Warzone (type 0)"): GfxWorldDraw lightmapCount N, one lightmap per transient zone:
// lightmap i is in transient zone lightmapTransientIndex[i] (zone 0 is the map's main .ff, zone k > 0 the streamed
// <map>_<k, 5 digits>_tr.ff), whose root has numLightmaps 1 and firstLightmapIndex i. Only that zone's surfaces use it
// (GfxSurface.lightmapIndex i, transientZone k; checked on every surface of mp_donetsk and mp_br_quarry).
// extractStockLightmapsFromDecompressedZone returns every lightmap of a zone with its global index; the older
// single-lightmap extractStockLightmapFromDecompressedZone stays for the MP maps (one root, one lightmap).
//
// Shading (research 94 section 1.4, INFERRED from the converter's comment on the stock shader):
//   light = (1 - f) * C / max(D.z, 0.001) * sat(N . D) + f * C       (N, D tangent space)
// With a flat normal this is light = C, so a viewport without normal maps draws albedo x C.
// Lightmap UVs per vertex: StockWorld::lightmapUv (stock_world.h). No V flip: v = 0 is row 0 of these arrays.
//
// Cache file (one lightmap per file: "lightmap.iw8rlm", or "lightmaps/<index>.iw8rlm" for the many lightmaps of a
// Warzone map, see world_cache.h; little endian; about 18 MB for 2048x1024):
//   0   char[8]  "IW8RLM\0\1"
//   8   u32      version (1)
//   12  u32      type (GfxWorldDraw lightmapType)
//   16  u32      width
//   20  u32      height
//   24  u32      planes: bit 0 irradiance, bit 1 ambient fraction, bit 2 direction
//   28  u32      lightmap index (GfxSurface.lightmapIndex; 0 in files written before 2026-11-12, which were all the
//                only lightmap of their map, index 0)
//   32  irradiance        width * height * 3 IEEE binary16 (R, G, B), row 0 first
//       ambient fraction  width * height uint8 (round(f * 255))
//       direction         width * height * 2 uint8, the octahedral bytes the game stores (decodeOctahedralDirection)
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace iw8r {

struct StockLightmap {
    int index = 0;           // global lightmap index (GfxSurface.lightmapIndex): the root's firstLightmapIndex + i
    int transientZone = -1;  // the root's transientZone (not kept in the cache file: -1 after a read)
    int type = 0;  // GfxWorldDraw lightmapType: 0 REGULAR (Warzone, BC6H C), 1 REGULAR_UNCOMPRESSED, 2 AB_LIGHTING
    int width = 0, height = 0;
    std::vector<float> irradiance;       // C, 3 floats per texel (R, G, B), linear, row 0 first (v = 0)
    std::vector<float> ambientFraction;  // f, 1 per texel, 0..1
    std::vector<float> direction;        // tangent-space unit direction, 3 per texel, decoded octahedral (z >= 0)
};

// GfxPixelFormat values used by the lightmap planes (x64-zt structs.hpp:3231).
enum : uint32_t {
    kLmFormatRGBA8 = 6,
    kLmFormatRGBA8Srgb = 7,
    kLmFormatR11G11B10F = 32,
    kLmFormatBC4 = 39,
    kLmFormatBC5 = 40,
    kLmFormatBC6H = 42,  // unsigned
    kLmFormatBC6HS = 43,
};

// --- Texel decoders (level 0, row 0 first). Each fails (returns false, out untouched) when `bytes` is too small. ---
// BC4 unorm: 1 value per texel, 0..1.
bool decodeBC4(const uint8_t* data, size_t bytes, int width, int height, std::vector<float>& out);
// BC5 unorm: 2 values per texel (red, green), 0..1.
bool decodeBC5(const uint8_t* data, size_t bytes, int width, int height, std::vector<float>& out);
// DXGI R11G11B10_FLOAT: 3 floats per texel. Exponent 31 (inf / NaN) decodes as 0.
bool decodeR11G11B10F(const uint8_t* data, size_t bytes, int width, int height, std::vector<float>& out);
// R8G8B8A8 unorm: 4 values per texel, 0..1.
bool decodeRGBA8(const uint8_t* data, size_t bytes, int width, int height, std::vector<float>& out);
// One DXGI unsigned small float (5-bit exponent, bias 15, `mantissaBits` 6 or 5). Exponent 31 decodes as 0.
float unpackUnsignedSmallFloat(uint32_t bits, int mantissaBits);
// Research 94 section 1.3: x, y = the stored bytes / 255; a = 2x - 1, b = 2y - 1; o = ((a + b) / 2, (a - b) / 2,
// 1 - |ox| - |oy|), normalised. The result always has z >= 0 (|ox| + |oy| = max(|a|, |b|) <= 1).
std::array<float, 3> decodeOctahedralDirection(float x, float y);
// The inverse (x64-zt EncodeOctDirection): a direction with z < 0 is first put on the horizon. Returns x, y in 0..1.
std::array<float, 2> encodeOctahedralDirection(float dx, float dy, float dz);

// --- Root search ---
// Offsets of the GfxLightmapAtlasDataTemp roots in a decompressed zone, each checked in full. The file is streamed in
// chunks of `chunkBytes` (0: 16 MiB) that overlap by the signature length, so a root across a chunk boundary is found
// and the file is never loaded whole. Fails only when the file cannot be read.
bool findStockLightmapRoots(const std::string& decPath, std::vector<uint64_t>& roots, std::string* err = nullptr,
                            size_t chunkBytes = 0);

// Decodes EVERY lightmap of every root in a decompressed zone (a map's main <zone>.ff.dec, or a streamed
// <map>_<NNNNN>_tr.ff.dec), in file order, each with its global index (StockLightmap::index) and its root's transient
// zone. Plane formats: 0 BC4; 1 R11G11B10F or BC6H (42 unsigned, 43 signed); 2 BC5 or R8G8B8A8. Fails with a clear
// message when the file cannot be read, has no root, two lightmaps claim one index, or a plane has another format,
// another size than plane 0, or runs past the end of the file. `rootOffsets` (optional) receives the roots' offsets.
bool extractStockLightmapsFromDecompressedZone(const std::string& decPath, std::vector<StockLightmap>& out,
                                               std::string* err = nullptr, std::vector<uint64_t>* rootOffsets = nullptr);

// Finds the one root in a decompressed zone (<zone>.ff.dec) and decodes its one lightmap (any type; the MP maps).
// Fails with a clear message when there is no root, more than one, more than one lightmap ("N lightmaps in one root:
// ...") or a plane cannot be decoded. `rootOffset` (optional) receives the root's offset.
bool extractStockLightmapFromDecompressedZone(const std::string& decPath, StockLightmap& out, std::string* err = nullptr,
                                              uint64_t* rootOffset = nullptr);

// --- Cache file (format above) ---
bool writeStockLightmapCache(const std::string& path, const StockLightmap& lm, std::string* err = nullptr);
bool readStockLightmapCache(const std::string& path, StockLightmap& out, std::string* err = nullptr);
// Only the header (type, size, index; planes left empty): for listings.
bool readStockLightmapCacheHeader(const std::string& path, StockLightmap& out, std::string* err = nullptr);

// IEEE binary16 conversions used by the cache (round to nearest even; values over 65504 become 65504, NaN 0).
uint16_t floatToHalf(float f);
float halfToFloat(uint16_t h);

}  // namespace iw8r
