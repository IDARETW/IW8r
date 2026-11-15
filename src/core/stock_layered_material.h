// Stock Replay layered world materials (techset families tw4 = MTL_TYPE_WORLD_LAYERED and twc4 = the vertex-colour
// variant): the per-layer constants of a material, its texture slots per layer, and a CPU reference of the layer blend
// of the stock lit pixel shaders. Source and labels: docs/research/97_stock_layered_materials.md.
//
// A layered material of N layers (1 + 3N rows of 16 bytes in constantBufferTable[0].psData, the pixel shader's cb3):
//   row 0                 shared (x: float, = the layer count on the materials seen; y bit 0: reflection probes)
//   row 1 + 3L            layer L flags (x) and UV transform (y: half2 cos, sin; z: half2 scale; w: half2 offset)
//   row 2 + 3L            layer L tint (x bytes 0..2 = RGB / 255; x byte 3 = reveal sharpness k * 255); y: thermal
//   row 3 + 3L            layer L specular remap (x bytes 0, 1), gloss remap (y bytes 0, 1), normal strength (y byte 2)
// Texture slots of layer L (material textureTable index): colour L, normal+gloss 9 + L, alpha+reveal 27 + L (L >= 1:
// red = alpha, green = reveal), tint mask 59 + L.
// Layer 0 is the base; each layer L >= 1 is applied over the result of the layers below it, in order:
//   w  = vertex colour byte L / 255 (twc4; StockWorld::vertexColor), 1 on tw4 (no colour stream)
//   b  = (flags & 16 ? ramp(reveal, w, k) : w) * (flags & 8 ? alpha : 1)
//   ramp = saturate((reveal - lo) / (hi - lo)), lo = saturate(1 - w - k sqrt(w)), hi = saturate(1 - w + k sqrt(1 - w))
//   colour mode (flags >> 7) & 3: 0 lerp(below, layer, b), 1 below * lerp(1, layer, b), 2 and 3 below unchanged
//   normal mode (flags >> 9) & 3: 0 lerp, 1 add, 2 none; with flags 1 / 2 / 4 the normal / cavity / gloss blend by b.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace iw8r {

enum StockLayerFlags : uint32_t {
    kStockLayerBlendNormal = 1,   // the layer's normal blends in (normal mode)
    kStockLayerBlendCavity = 2,   // its cavity (normal map z) blends in
    kStockLayerBlendGloss = 4,    // its gloss blends in
    kStockLayerAlphaMask = 8,     // b is multiplied by the alpha+reveal map's red channel
    kStockLayerReveal = 16,       // b ramps across the alpha+reveal map's green channel (else b = w)
    kStockLayerTintMask = 64,     // the tint applies through the tint mask (slot 59 + L)
};

struct StockLayerConstants {
    uint32_t flags = 0;
    int colorMode = 0;   // (flags >> 7) & 3: 0 lerp, 1 multiply, 2 and 3 leave the colour below unchanged
    int normalMode = 0;  // (flags >> 9) & 3: 0 lerp, 1 add, 2 normal, cavity and gloss not blended
    float uvCos = 1, uvSin = 0;
    std::array<float, 2> uvScale{1, 1}, uvOffset{0, 0};
    std::array<float, 3> tint{1, 1, 1};
    float sharpness = 0;  // reveal ramp k in [0, 1] (unused on layer 0)
    // specular code s = alpha_of_colour * specScale + specBias; gloss = gloss_texel * glossScale + glossBias
    float specScale = 1, specBias = 0, glossScale = 1, glossBias = 0;
    float normalStrength = 1;
};

struct StockLayeredMaterial {
    int layers = 0;
    float row0x = 0;              // psData row 0 .x (the layer count on every material seen; meaning INFERRED)
    bool reflectionProbes = false;  // row 0 .y bit 0
    std::vector<StockLayerConstants> layer;
};

inline int stockLayerColorSlot(int layer) { return layer; }
inline int stockLayerNormalSlot(int layer) { return 9 + layer; }
inline int stockLayerRevealSlot(int layer) { return layer >= 1 ? 27 + layer : -1; }
inline int stockLayerTintMaskSlot(int layer) { return 59 + layer; }

// Decodes psData (at least 16 * (1 + 3 * layerCount) bytes). Fails on a short buffer or a layer count outside 1..8.
// Research 136 section 2: a model-layered technique set's layers from its name (`<family>/lit_3_lit_..._<hexB>_0_<N>_...`;
// layer L >= 1 holds the fields 18 + 8 (L - 1) onward: a, b, hex, colour mode, normal mode, c, 0, 0).
enum class ModelLayerKind { Standard, Overlay, Code };
struct ModelLayerTokens {
    int layers = 0;                       // N
    bool mask126 = false;                 // hexB starts with 3: slot 126 (R, G, B = layers 1, 2, 3) weights the layers
    std::vector<ModelLayerKind> kind;     // index L (0: the base, Standard)
    std::vector<bool> uv1;                // the layer samples the second UV set (hex bit 0x80000; a `2` family only)
};
// Empty (layers 0) for a name that is not a lit technique set with layer fields.
ModelLayerTokens parseModelLayerTokens(const std::string& techsetName);

bool decodeStockLayeredMaterial(const std::vector<uint8_t>& psData, int layerCount, StockLayeredMaterial& out,
                                std::string* err = nullptr);
// The same from the hex text of an ACTS material export (constantBufferTable.values[0].psData.bytes).
bool decodeStockLayeredMaterialHex(const std::string& hex, int layerCount, StockLayeredMaterial& out, std::string* err = nullptr);

// The texture coordinate the shader samples a layer with, from the STORED uv (v = 0 at the top of the image, i.e. before
// StockWorld's 1 - v flip): (c u su + s v sv, -s u su + c v sv) + offset.
std::array<float, 2> stockLayerUv(const StockLayerConstants& c, float u, float v);

// The blend factor b of a layer above 0 (header comment) from the vertex weight w, the alpha+reveal map texel (alpha =
// red, reveal = green; ignored unless flags 8 / 16) and the layer's constants. Exact sqrt and division; the shader
// uses fast approximations of both.
float stockLayerBlend(const StockLayerConstants& c, float weight, float alpha, float reveal);

// The colour after a layer: below and layer are linear RGB, layer already multiplied by its tint.
std::array<float, 3> stockLayerBlendColor(const StockLayerConstants& c, float blend, const std::array<float, 3>& below,
                                          const std::array<float, 3>& layer);

}  // namespace iw8r
