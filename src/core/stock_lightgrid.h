// Stock static-model light grid volumes (LGV): how Replay 1.20 lights probe-lit placed props (docs/research/105).
//   - The box table: gfx_map_trzone fields.gpuLightGrid.staticModelVolumes (64 bytes each, research 105 section 1.2),
//     already in the ACTS export ("values" objects on small maps, hex "bytes" on big ones).
//   - The probe atlas: not exported by ACTS. It follows the box table in the decompressed zone (.ff.dec): a 240-byte
//     GfxLightGridVolumeAtlasDataTemp root, texture 0 (R11G11B10F, W x W x D), texture 1 (RGBA8, 2W x 3W x D, six
//     W x W tiles), LightGridVolumePackingParams[N] (32 B) and LightGridVolumeTransformParams[N] (64 B)
//     (research 105 section 1.3). extractStockLgvBlock* copies exactly those bytes; that copy is the cache "block".
//   - Sampling and evaluation follow the static-model vertex shader (research 105 section 1.4).
// Instance i of GfxWorld.smodels finds its box through StockModelInstance::lightingIndex when its model has the
// LIGHTPROBE flag (StockModelInstance::modelFlags & 1, stock_smodels.h).
// Read-only. Nothing here writes a file.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace iw8r {

// One static model light grid volume ("box") of gpuLightGrid.staticModelVolumes (64 bytes each, research 105 §1.2).
struct StockLgvBox {
    uint32_t firstProbe = 0;
    uint16_t size[3] = {1, 1, 1};
    uint16_t flags = 0;              // LGV_SMODEL_FLAGS: axis mask 3, thin push 4
    uint32_t singleProbe = 0xFFFFFFFFu;
    double row[3][4] = {};           // world -> box coordinate rows: coord = row * (p, 1), box = [0,1]^3
};

// From a gfx_map_trzone JSON text: fields.gpuLightGrid.staticModelVolumes (ACTS writes "values" (objects) or hex
// "bytes" with a "stride") and staticModelVolumeCount. Also returns the raw 64-byte records (for locating the atlas).
// Only the staticModelVolumes value is parsed (the rest of the 70+ MB export is skipped as text). A null
// staticModelVolumes (a zone without volumes) gives true and an empty list. Fails on a missing key, bad hex, a stride
// other than 64, or counts that disagree.
bool parseStockLgvBoxes(const std::string& trzoneJson, std::vector<StockLgvBox>& out, std::vector<uint8_t>* rawTable = nullptr,
                        std::string* err = nullptr);

// The probe atlas (research 105 §1.3): texture 0 R11G11B10F W x W x D, texture 1 RGBA8 (2W x 3W x D, six tiles), and
// per-volume packing (uvScale, uvBias).
struct StockLgvAtlas {
    int width = 0, depth = 0;
    std::vector<uint32_t> tex0, tex1;  // x fastest, then y, then z
    struct Pack { float uvScale[3] = {0, 0, 0}, uvBias[3] = {0, 0, 0}; };
    std::vector<Pack> pack;            // per volume
};

// Finds the atlas root in a decompressed zone (the bytes of a .ff.dec): right after the exported box table bytes
// (search for rawTable), or by the root's signature (u32 W, u32 depth 1..64, u32 numVolumes == count, texture 0
// {format 32, W x W x depth}, texture 1 {format 6, 2W x 3W x depth}, both pixel pointers and both parameter pointers
// -2). The signature is the fallback when rawTable is empty, not found, or not followed by a root (SMLGppZone not
// null). Copies the root + textures + packing + transform params into `block` (the cache file format: just those
// bytes, 240 + 28 W W D + 96 N).
bool extractStockLgvBlock(const std::string& decBytes, const std::vector<uint8_t>& rawTable, size_t numVolumes,
                          std::string& block, std::string* err = nullptr);
// Same, but streaming from a file path in 4 MB chunks (the .ff.dec can be 400 MB; it is never loaded whole).
bool extractStockLgvBlockFromFile(const std::string& decPath, const std::vector<uint8_t>& rawTable, size_t numVolumes,
                                  std::string& block, std::string* err = nullptr);

// Parses a block (exactly the bytes extractStockLgvBlock* produces). Non-finite packing values are read as 0.
bool parseStockLgvBlock(const std::string& block, StockLgvAtlas& out, std::string* err = nullptr);

// The SH of volume v at box coordinate `coord` (each clamped to [0,1]), trilinear like the GPU (research 105 §1.4):
// dc rgb and the 8 slots x 3 channels already multiplied by DC and the sqrt(3)/sqrt(5) factors.
// Slot order (the VS's): 0 z, 1 3z^2-1, 2 -y, 3 -x, 4 -xz, 5 -yz, 6 x^2-y^2, 7 xy. A volume out of range gives zeros.
// A texel whose DC decodes to inf / NaN counts as 0 (the stock data has none).
struct StockLgvSh { float dc[3] = {0, 0, 0}; float sh[8][3] = {}; };
StockLgvSh sampleStockLgv(const StockLgvAtlas& a, size_t volume, const double coord[3]);

// E(n) of research 105 §1.4 (band weights 1, 4/9, 1/16), clamped >= 0, before bakedLightScale. n: unit world normal.
void evalStockLgv(const StockLgvSh& s, const double n[3], double outRgb[3]);

// One R11G11B10F texel (texture 0) to linear rgb: R bits 0-10 (6-bit mantissa), G 11-21, B 22-31 (5-bit mantissa).
void decodeR11G11B10F(uint32_t packed, float outRgb[3]);

}  // namespace iw8r
