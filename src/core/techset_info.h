// IW8 Radiant - what a stock IW8 (Replay 1.20) material's technique set and flags say about how it draws.
// Research 66: a survey of 6,135 material bodies (techsets_mp_shipment + techsets_mp_aniyah_tac, ACTS) gives the name
// grammar and the flags. The goal is full coverage of the stock sets now and a place to describe custom sets later.
//
// Technique set name: "<prefix>/<family>_<blend>_<table>_<state hex>_<feature hex>_..."; families seen:
//   lit_3_lit            world / model surfaces (6,003 of 6,135)
//   unlit_6_effect       effects (sprites, sheets)
//   dist_10_distortion   heat haze / water ripple distortion (draws nothing of its own)
//   card_8_lit_card      lighting proxy cards (li_mtl_*_proxy): GI bake helpers, never drawn
//   sky_3_sky            sky box
//   ,cust_2_2d           2D / UI
//   ,null                info carrier (MTL_TYPE_INFO_CARRIER): carries data, never drawn
// Blend tokens: rpl replace (opaque), alt alpha test, bad alpha blend, bar alpha blend with reveal (glass).
// Table tokens: ta0, ta1, tca, tom.
#pragma once

#include <cstdint>
#include <string>

#include "material_blend.h"

namespace iw8r {

enum class TechsetFamily { Lit, UnlitEffect, Distortion, LightProxyCard, Sky, Ui2d, Null, Unknown };

struct TechsetInfo {
    TechsetFamily family = TechsetFamily::Unknown;
    std::string blendToken;  // "rpl", "alt", "bad", "bar" or empty
    std::string tableToken;  // "ta0", "ta1", "tca", "tom" or empty
    MaterialBlend blend = MaterialBlend::Opaque;
    bool glass = false;      // "bar": blended with reveal (vehicle glass, visors, windows)
};

TechsetInfo parseTechset(const std::string& techset);
const char* techsetFamilyName(TechsetFamily f);  // "lit", "unlit effect", "distortion", ...

// Material::surfaceFlags bits with a known meaning.
constexpr uint32_t kIw8SurfNoDraw = 0x80;  // shadow-only: every *_shadowcaster, nodraw, caulk_shadow (research 66)
// Casts no sun shadow: every *_noshadow material and the tree canopies' leaf cards (their trees cast from separate
// *_shadowcaster meshes); 109 of 16,272 cached stock materials, 2026-11-13.
constexpr uint32_t kIw8SurfNoCastShadow = 0x40000;

// How the editor's textured view draws a material.
enum class MaterialDraw {
    Draw,        // normal
    ShadowOnly,  // casts the sun shadow, invisible otherwise (foliage shadowcaster meshes)
    Hidden,      // nothing (info carriers, lighting proxy cards, distortion)
};

// surfaceFlags < 0 = unknown (older cache entries): the material name decides ("_shadowcaster" = shadow only).
MaterialDraw materialDrawFor(const std::string& material, const std::string& techset, int64_t surfaceFlags);
const char* materialDrawName(MaterialDraw d);

}  // namespace iw8r
