// Stock Replay blended world surfaces (decals: techsets "<family>/lit_3_lit_bad_*", material sortKey 10..17 on
// mp_m_speedball) and world displacement ("dn" layers, techset token "...0018"): the render state, the per-pixel
// opacity, the composite and the displacement offset of the shipped shaders, as plain functions.
// Source and VERIFIED / INFERRED labels: docs/research/98_stock_decals_displacement.md. Layer constants and the reveal
// ramp come from stock_layered_material.h (research 97); this file adds what differs for blended surfaces.
//
// Summary (research 98):
//   blend     ONE, INV_SRC_ALPHA, ADD for colour and alpha (premultiplied); no depth write; depth test LESSEQUAL;
//             polygon offset "decal" on most decal techsets; no depth prepass or shadow technique.
//   opacity   per layer b_L = stockLayerBlend(layer constants, w_L, alpha, reveal), alpha + reveal from slot 27 + L
//             (layer 0 included), w_L from stockDecalLayerWeight; the surface opacity is max over layers.
//   colour    the surface is lit exactly like an opaque one (research 95 / 97) with its own textures; the shader
//             outputs (lit * b, b), so the frame becomes lit * b + below * (1 - b).
//   displace  offset along the vertex normal = amp * (height * scale + bias), height = displacement map red at the
//             displaced layer's UV, (scale, bias) = the material's VS row (40, -20 on Speedball), amp per vertex.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "stock_layered_material.h"

namespace iw8r {

// GfxBlendStateBits factor codes (x64-zt src/games/iw8/structs.hpp GfxBlendStateBits): bits 0-3 src RGB, 4-7 dst RGB,
// 8-10 op RGB, 11-14 src alpha, 15-18 dst alpha, 19-21 op alpha, 32 + 4 i: render target i write mask, inverted.
enum StockBlendFactor : int {
    kStockBlendOne = 0, kStockBlendZero = 1, kStockBlendSrcColor = 2, kStockBlendInvSrcColor = 3,
    kStockBlendSrcAlpha = 4, kStockBlendInvSrcAlpha = 5, kStockBlendDestAlpha = 6, kStockBlendInvDestAlpha = 7,
    kStockBlendDestColor = 8, kStockBlendInvDestColor = 9,
};
enum StockBlendOp : int { kStockBlendOpAdd = 0, kStockBlendOpSubtract = 1, kStockBlendOpRevSubtract = 2,
                          kStockBlendOpMin = 3, kStockBlendOpMax = 4 };

struct StockBlendState {
    bool enabled = false;  // (bits & 0x3FFFFF) != 0, as R_HW_CreateBlendStateDesc
    int srcRgb = kStockBlendOne, dstRgb = kStockBlendOne, opRgb = kStockBlendOpAdd;
    int srcAlpha = kStockBlendOne, dstAlpha = kStockBlendOne, opAlpha = kStockBlendOpAdd;
    bool independent = false;            // any bit above 31: per render target write masks
    std::array<uint8_t, 8> writeMask{};  // RGBA bits 1, 2, 4, 8 per render target (all 0xF when !independent)
};
StockBlendState decodeStockBlendBits(uint64_t blendBits);

// GfxOtherStateBits (same structs.hpp; Load_DeriveMaterialPipelineState in the IW8 game-test decompile).
enum StockDepthTest : int { kStockDepthAlways = 0, kStockDepthGreaterEqual = 1, kStockDepthEqual = 2,
                            kStockDepthLessEqual = 3 };
enum StockPolygonOffset : int { kStockPolygonOffsetNone = 0, kStockPolygonOffsetDecal = 1,
                                kStockPolygonOffsetShadowMap = 2, kStockPolygonOffsetSuperTerrainShadowMap = 3 };
struct StockOtherState {
    int cull = 0;            // bits 0-1, index into the engine's cull table (1 on every world lit technique)
    bool wireframe = false;  // bit 2
    bool depthClipDisabled = false;  // bit 3
    int polygonOffset = kStockPolygonOffsetNone;  // bits 4-5
    bool depthWrite = false;  // bit 9
    bool depthEnabled = false;  // (bits & 0xE00) != 0
    int depthTest = kStockDepthAlways;  // bits 10-11 (names as the x64-zt enum; the D3D mapping is a table)
    bool stencil = false;  // bits 12-35 non-zero
};
StockOtherState decodeStockOtherBits(uint64_t otherBits);

// The world techset name tokens that matter for drawing, "<family>/lit_3_lit_<blend>_<x>_<hexA>_<hexB>_0_<n>_<n>_...".
// Rules VERIFIED on every world (tw, twc, tw4, twc4) lit techset of techsets_mp_m_speedball (research 98 section 4).
struct StockTechsetTraits {
    bool valid = false;
    std::string family;      // "tw", "twc", "tw4", "twc4", ...
    std::string blendToken;  // "rpl" opaque, "alt" alpha tested, "bad" premultiplied alpha blend
    bool blended = false;    // blendToken == "bad" (blendBits 0x28050)
    bool polygonOffset = false;  // hexA & 0x10000 on "bad" world techsets (otherBits 0x10)
    bool ssr = false;            // hexB bit 1 (last digit 2 or 3): screen-space reflection over the probe (research 104)
    bool displacement = false;   // hexB last digit 8: displacement (tessellation techniques + displaced VS)
    bool wrapDiffuse = false;    // hexB & 0x0008000000000000: one extra psData row, RGB wrap (stockWrapDiffuse)
    bool transmission = false;   // hexB bit 0 (last digit odd): texture slot 68 x constant 18 lets light through (research 107)
    // hexB bit 0x40: reactive motion (plants that bend around the player; every "..._reactive" material and no other, on
    // 7,281 lit sets of Hideout, Rust, Speedball and Shipment). The vertex colour of its models holds the motion's data
    // (red 1, green 0, blue a per-vertex value), not a colour: the game does not tint the albedo with it (Speedball's
    // arena grass is green in game; multiplied it drew crimson).
    bool reactiveMotion = false;
    bool uvAnim = false;         // hexC & 0x800000000: the technique binds constant 63, UV animation (research 133 section 1)
    int layers = 0;              // the first layer-count token
};
StockTechsetTraits parseStockTechsetName(const std::string& techsetName);

enum class StockDecalFamily { World, WorldVertCol, WorldLayered, WorldLayeredVertCol };
// tw -> World, twc -> WorldVertCol, tw4 -> WorldLayered, twc4 -> WorldLayeredVertCol (default World).
StockDecalFamily stockDecalFamilyFromTechset(const std::string& family);

// Weight w of layer L for the opacity ramp, from the surface's colour record (bytes 0..3, StockWorld::vertexColor;
// white when the surface has none):
//   twc  (single layer): byte 3 (alpha);  twc4: byte L (layer 0 = byte 0, red);  tw, tw4: 1 (compiled in).
float stockDecalLayerWeight(StockDecalFamily family, int layer, const std::array<uint8_t, 4>& color);

// Texture slot of layer L's alpha (red) + reveal (green) map on a blended surface: 27 + L, layer 0 included
// (stockLayerRevealSlot returns -1 for layer 0, which is right only for opaque surfaces).
inline int stockDecalRevealSlot(int layer) { return 27 + layer; }

// Surface opacity: max over the layer opacities (layers above 0 count only when b >= 1/255, as the shader skips
// them). Empty input gives 0.
float stockDecalCoverage(const std::vector<float>& layerOpacity);

// One frame pixel after the decal: lit * b + below * (1 - b) (ONE, INV_SRC_ALPHA on the shader's (lit * b, b)).
std::array<float, 3> stockDecalComposite(const std::array<float, 3>& below, const std::array<float, 3>& lit, float b);

// True when the shader discards the pixel: every output channel <= 1e-6 (lit * b black and b = 0).
bool stockDecalDiscards(const std::array<float, 3>& litTimesB, float b);

// Wrap diffuse of "100a" techsets (psData row 1 + 3N, xyz per colour channel; w clamped to >= 1e-6):
//   ndotl <  w: (ndotl + w)^2 * 3 / (4 w (w^2 + 3));   ndotl >= w: 3 ndotl / (w^2 + 3);   saturated.
// w -> 0 gives saturate(ndotl).
float stockWrapDiffuse(float ndotl, float wrap);

// Displacement ("dn" layers; GfxWorldSurfData.displacementOffset[L] != 0 for the displaced layer L).
// Per vertex record at displacementOffset[L] + 4 j: two binary16, lo (amplitude, artist painted) and hi (layer UV
// density; INFERRED meaning). The scene parameter (cb7[114].z) is r_tessellationHeightScale, times -1/256 when
// r_tessellationHeightAuto is on (game-test r_scene.cpp; the Replay defaults are not known statically).
float stockDisplacementSceneParam(float heightScale, bool heightAuto);
// amp = lo * |param| / (param >= 0 ? 1 : hi)
float stockDisplacementAmplitude(float lo, float hi, float sceneParam);
// Offset along the normalised vertex normal, inches: amp * (height * scale + bias); (scale, bias) is the material's
// VS constant (constantTable index 45 + L: (maxDisplacement, -maxDisplacement / 2) on Speedball).
float stockDisplacementOffset(float height, float amp, float scale, float bias);

}  // namespace iw8r
