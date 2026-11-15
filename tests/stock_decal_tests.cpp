// Stock blended world surfaces and displacement (src/core/stock_decal.h, docs/research/98).
#include <cmath>
#include <string>
#include <vector>

#include "stock_decal.h"
#include "stock_layered_material.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

// psData of mp_m_speedball's twc/wall_stain_01_dec (techsets_mp_m_speedball ACTS export): 1 layer, flags 0x1F
// (alpha and reveal), sharpness 0xBF.
const char* kWallStain =
    "0000803f000000004e36d93f4e36d93f"
    "1f000000003c0080003c003c00000000"
    "ffffffbfbb2fff0000bc000000000000"
    "09000000ff00ff000000804f00000000";

// twc4/16n_94n (sortKey 17): layer 0 flags 0x1F sharpness 6, layer 1 flags 0x0F (alpha only).
const char* kLayeredDecal =
    "00000040000000004e36d93f4e36d93f"
    "1f000000003c0080003c003c00000000"
    "ffffff06bb2fff0000bc000000000000"
    "09000000b400d6000000804f00000000"
    "0f000000003c0080003c003c00000000"
    "ffffffffad2fff0000bc000000000000"
    "09000000ff00ff000000804f00000000";

}  // namespace

TEST(stock_decal_state_bits) {
    // Every Speedball "bad" lit technique: blendBits 0x28050, otherBits 0xC11 (0xC01 without polygon offset).
    const StockBlendState b = decodeStockBlendBits(0x28050);
    CHECK(b.enabled && !b.independent);
    CHECK(b.srcRgb == kStockBlendOne && b.dstRgb == kStockBlendInvSrcAlpha && b.opRgb == kStockBlendOpAdd);
    CHECK(b.srcAlpha == kStockBlendOne && b.dstAlpha == kStockBlendInvSrcAlpha && b.opAlpha == kStockBlendOpAdd);
    CHECK(b.writeMask[0] == 0xF && b.writeMask[7] == 0xF);
    const StockBlendState opaque = decodeStockBlendBits(0);
    CHECK(!opaque.enabled);
    // Depth prepass: colour writes off on every target (0xffffff0f00000000 keeps target 0 RGBA off too).
    const StockBlendState prepass = decodeStockBlendBits(0xffffffff00000000ull);
    CHECK(prepass.independent && prepass.writeMask[0] == 0 && prepass.writeMask[3] == 0);

    const StockOtherState d = decodeStockOtherBits(0xC11);
    CHECK(d.cull == 1 && d.polygonOffset == kStockPolygonOffsetDecal && !d.depthWrite && d.depthEnabled);
    CHECK(d.depthTest == kStockDepthLessEqual && !d.stencil && !d.wireframe);
    const StockOtherState lit = decodeStockOtherBits(0x801);  // opaque lit after the prepass
    CHECK(lit.depthTest == kStockDepthEqual && !lit.depthWrite && lit.polygonOffset == kStockPolygonOffsetNone);
    const StockOtherState first = decodeStockOtherBits(0xE01);  // depth prepass
    CHECK(first.depthWrite && first.depthTest == kStockDepthLessEqual);
    const StockOtherState shadow = decodeStockOtherBits(0xE21);
    CHECK(shadow.polygonOffset == kStockPolygonOffsetShadowMap);
}

TEST(stock_decal_techset_names) {
    StockTechsetTraits t = parseStockTechsetName(
        "twc/lit_3_lit_bad_tca_812002_1002000000000010_0_1_1_0_0_13810015b_0_0_3_0_0");
    CHECK(t.valid && t.family == "twc" && t.blended && t.polygonOffset && !t.ssr && !t.displacement);
    CHECK(!t.wrapDiffuse && t.layers == 1);
    t = parseStockTechsetName("tw/lit_3_lit_bad_tca_802002_1002000000000010_0_1_1_0_0_13810015b_0_0_3_0_0");
    CHECK(t.valid && t.blended && !t.polygonOffset && !t.uvAnim);
    // UV animation (research 133 section 1): hexC bit 0x800000000 (the fan blade, the Shipment water).
    t = parseStockTechsetName("mo/lit_3_lit_bad_tca_802_1002000000000010_0_1_1_0_0_938100158_0_0_3_0_0");
    CHECK(t.valid && t.uvAnim);
    t = parseStockTechsetName("twc/lit_3_lit_rpl_ta1_801010_1002000000010012_0_1_1_0_0_193810015b_0_0_3_0_0");
    CHECK(t.valid && t.uvAnim && t.ssr);
    t = parseStockTechsetName("twc/lit_3_lit_bad_tca_812002_100a000000000010_0_1_1_0_0_13810015b_0_0_3_0_0");
    CHECK(t.valid && t.wrapDiffuse && !t.ssr);
    t = parseStockTechsetName("tw/lit_3_lit_bad_ta1_815002_1002000000000012_0_1_1_0_0_113810015b_0_0_1_0_0");
    CHECK(t.valid && t.blended && t.ssr && !t.displacement);
    // The glass set ending in 3 traces too (research 104 section 5); 8 (displacement) does not.
    t = parseStockTechsetName("tw/lit_3_lit_bad_ta1_815002_1002000000000013_0_1_1_0_0_113810015b_0_0_1_0_0");
    CHECK(t.valid && t.ssr);
    t = parseStockTechsetName("tw/lit_3_lit_bad_ta1_815002_1002000000000018_0_1_1_0_0_113810015b_0_0_1_0_0");
    CHECK(t.valid && !t.ssr && t.displacement && !t.transmission);
    // Transmission: hexB bit 0 (Speedball's grass, research 107 section 1.1); the "...3" glass set has it too.
    t = parseStockTechsetName("tmco/lit_3_lit_alt_ta1_848810_1012000201000051_0_1_1_0_0_13810015b_0_0_1_0_0");
    CHECK(t.valid && t.transmission && t.blendToken == "alt" && !t.ssr);
    t = parseStockTechsetName("tw/lit_3_lit_bad_ta1_815002_1002000000000013_0_1_1_0_0_113810015b_0_0_1_0_0");
    CHECK(t.transmission && t.ssr);
    t = parseStockTechsetName(
        "twc4/lit_3_lit_rpl_ta1_802000_1002000000000018_0_3_3_0_0_13810015b_0_0_1_0_0_1_0_13811015b_0_0_3_0_0_2_0_"
        "13810015b_0_0_3_0_0");
    CHECK(t.valid && !t.blended && t.displacement && t.layers == 3 && t.family == "twc4");
    CHECK(stockDecalFamilyFromTechset(t.family) == StockDecalFamily::WorldLayeredVertCol);
    CHECK(!parseStockTechsetName("tw/sky_3_sky_rpl_ta1_0_16_0_0_0").valid);
    CHECK(!parseStockTechsetName("nonsense").valid);
}

TEST(stock_decal_weights_and_coverage) {
    const std::array<uint8_t, 4> c{63, 91, 0, 200};
    CHECK(near(stockDecalLayerWeight(StockDecalFamily::WorldVertCol, 0, c), 200.0f / 255.0f));
    CHECK(near(stockDecalLayerWeight(StockDecalFamily::WorldLayeredVertCol, 0, c), 63.0f / 255.0f));
    CHECK(near(stockDecalLayerWeight(StockDecalFamily::WorldLayeredVertCol, 1, c), 91.0f / 255.0f));
    CHECK(stockDecalLayerWeight(StockDecalFamily::World, 0, c) == 1.0f);
    CHECK(stockDecalLayerWeight(StockDecalFamily::WorldLayered, 1, c) == 1.0f);
    CHECK(stockDecalRevealSlot(0) == 27 && stockDecalRevealSlot(1) == 28);

    // Single-layer wall stain: b = ramp(reveal, vertex alpha, k = 191/255) * alpha.
    StockLayeredMaterial m;
    CHECK(decodeStockLayeredMaterialHex(kWallStain, 1, m));
    if (m.layer.size() != 1) return;
    const StockLayerConstants& l0 = m.layer[0];
    CHECK(l0.flags == 0x1F && near(l0.sharpness, 191.0f / 255.0f));
    CHECK(stockLayerBlend(l0, 0.0f, 1.0f, 1.0f) == 0.0f);  // vertex alpha 0 hides the decal even at full reveal
    const float w = 18.0f / 255.0f;  // the stain's average vertex alpha
    const float k = l0.sharpness;
    const float lo = std::fmax(0.0f, std::fmin(1.0f, 1 - w - k * std::sqrt(w)));
    const float hi = std::fmax(0.0f, std::fmin(1.0f, 1 - w + k * std::sqrt(1 - w)));
    const float reveal = 0.9f, alpha = 0.5f;
    const float expect = std::fmax(0.0f, std::fmin(1.0f, (reveal - lo) / (hi - lo))) * alpha;
    CHECK(near(stockLayerBlend(l0, w, alpha, reveal), expect));
    CHECK(stockDecalCoverage({expect}) == expect);

    // Two-layer decal: coverage is the max of the layer opacities; a layer under 1/255 does not count.
    CHECK(decodeStockLayeredMaterialHex(kLayeredDecal, 2, m));
    if (m.layer.size() != 2) return;
    CHECK(m.layer[1].flags == 0x0F);
    const float b0 = stockLayerBlend(m.layer[0], 63.0f / 255.0f, 1.0f, 0.95f);
    const float b1 = stockLayerBlend(m.layer[1], 91.0f / 255.0f, 0.8f, 0.0f);  // no reveal flag: w * alpha
    CHECK(near(b1, 91.0f / 255.0f * 0.8f));
    CHECK(stockDecalCoverage({b0, b1}) == std::fmax(b0, b1));
    CHECK(stockDecalCoverage({0.3f, 0.001f}) == 0.3f);
    CHECK(stockDecalCoverage({}) == 0.0f);

    // tw (no vertex colour): w = 1, the ramp is a step at reveal > 0.
    StockLayerConstants step;
    step.flags = 0x17;
    step.sharpness = 0.2f;
    CHECK(stockLayerBlend(step, 1.0f, 1.0f, 0.0f) == 0.0f);
    CHECK(stockLayerBlend(step, 1.0f, 1.0f, 0.01f) == 1.0f);
}

TEST(stock_decal_composite_and_wrap) {
    const std::array<float, 3> below{0.2f, 0.4f, 0.6f}, lit{1.0f, 0.5f, 0.0f};
    const std::array<float, 3> out = stockDecalComposite(below, lit, 0.25f);
    CHECK(near(out[0], 0.4f) && near(out[1], 0.425f) && near(out[2], 0.45f));
    const std::array<float, 3> none = stockDecalComposite(below, lit, 0.0f);
    CHECK(none == below);
    CHECK(stockDecalDiscards({0, 0, 0}, 0.0f));
    CHECK(!stockDecalDiscards({0, 0, 0}, 0.01f));
    CHECK(!stockDecalDiscards({0.1f, 0, 0}, 0.0f));

    // Wrap diffuse: continuous at ndotl = w, saturate(ndotl) as w -> 0, light past the terminator for w > 0.
    const float w = 0.69f;
    const float left = stockWrapDiffuse(w - 1e-4f, w), right = stockWrapDiffuse(w, w);
    CHECK(near(left, right, 1e-3f));
    CHECK(near(right, 3 * w / (w * w + 3)));
    CHECK(near(stockWrapDiffuse(0.5f, 0.0f), 0.5f, 1e-4f));
    CHECK(stockWrapDiffuse(-0.3f, 0.0f) == 0.0f);
    CHECK(stockWrapDiffuse(-0.3f, w) > 0.0f);
    CHECK(stockWrapDiffuse(-1.0f, w) == 0.0f);
    CHECK(stockWrapDiffuse(1.0f, w) <= 1.0f);
}

TEST(stock_decal_displacement) {
    // Speedball twc4/14n_15dn_16n: VS row (40, -20); surface 313 vertex 1: lo 0.4158, hi 0.0195.
    CHECK(near(stockDisplacementSceneParam(1.0f, true), -1.0f / 256.0f));
    CHECK(stockDisplacementSceneParam(2.0f, false) == 2.0f);
    const float lo = 0.4158f, hi = 0.0195f;
    const float ampAuto = stockDisplacementAmplitude(lo, hi, stockDisplacementSceneParam(1.0f, true));
    CHECK(near(ampAuto, lo / hi / 256.0f));
    const float ampPlain = stockDisplacementAmplitude(lo, hi, 1.0f);
    CHECK(near(ampPlain, lo));
    CHECK(near(stockDisplacementOffset(0.5f, ampPlain, 40.0f, -20.0f), 0.0f));  // mid grey is the base surface
    CHECK(near(stockDisplacementOffset(1.0f, ampPlain, 40.0f, -20.0f), 20.0f * lo));
    CHECK(near(stockDisplacementOffset(0.0f, ampPlain, 40.0f, -20.0f), -20.0f * lo));
    CHECK(stockDisplacementAmplitude(lo, 0.0f, -1.0f) == 0.0f);
}
