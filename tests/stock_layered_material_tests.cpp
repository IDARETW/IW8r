// Stock layered material constants and blend (src/core/stock_layered_material.h, docs/research/97).
#include <cmath>
#include <string>

#include "stock_layered_material.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

// psData of mp_br_tut's twc4/12n_13n_35n (techsets_mp_br_tut, ACTS material export, constantBufferTable.values[0]):
// 3 layers, 160 bytes.
const char* kTerrainPsData =
    "00004040010000004e36d93f4e36d93f"  // row 0
    "07000000003c0080003c003c00000000"  // layer 0 flags 7, identity UV transform
    "ffffffffbb2fff0000bc000000000000"  // layer 0 tint white
    "09000000de21d8000000804f00000000"  // layer 0 spec (9, 0), gloss (222, 33), normal strength 216
    "1f000000003c0080003c003c00000000"  // layer 1 flags 0x1F
    "ffffff26bb2fff0000bc000000000000"  // layer 1 tint white, sharpness 0x26
    "090000002315cc000000804f00000000"  // layer 1 spec, gloss (35, 21), normal 204
    "17000000003c0080003c003c00000000"  // layer 2 flags 0x17
    "ffffff33bb2fff0000bc000000000000"  // layer 2 sharpness 0x33
    "09000000ff00ff000000804f00000000";  // layer 2 gloss (255, 0), normal 255

bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

}  // namespace

TEST(stock_layered_material_decode_terrain) {
    StockLayeredMaterial m;
    std::string err;
    CHECK(decodeStockLayeredMaterialHex(kTerrainPsData, 3, m, &err));
    CHECK(err.empty());
    CHECK(m.layers == 3 && m.layer.size() == 3);
    if (m.layer.size() != 3) return;
    CHECK(m.row0x == 3.0f);
    CHECK(m.reflectionProbes);
    const StockLayerConstants& l0 = m.layer[0];
    CHECK(l0.flags == 7 && l0.colorMode == 0 && l0.normalMode == 0);
    CHECK(l0.uvCos == 1.0f && l0.uvSin == 0.0f && l0.uvScale[0] == 1.0f && l0.uvScale[1] == 1.0f);
    CHECK(l0.uvOffset[0] == 0.0f && l0.uvOffset[1] == 0.0f);
    CHECK(l0.tint[0] == 1.0f && l0.tint[1] == 1.0f && l0.tint[2] == 1.0f);
    CHECK(near(l0.specScale, 10.0f / 256.0f) && l0.specBias == 0.0f);
    CHECK(near(l0.glossScale, 223.0f / 256.0f) && near(l0.glossBias, 33.0f / 256.0f));
    CHECK(near(l0.normalStrength, 217.0f / 256.0f));
    const StockLayerConstants& l1 = m.layer[1];
    CHECK(l1.flags == 0x1F && l1.colorMode == 0 && l1.normalMode == 0);
    CHECK((l1.flags & kStockLayerReveal) && (l1.flags & kStockLayerAlphaMask));
    CHECK(near(l1.sharpness, 38.0f / 255.0f));
    CHECK(near(l1.glossScale, 36.0f / 256.0f) && near(l1.glossBias, 21.0f / 256.0f));
    const StockLayerConstants& l2 = m.layer[2];
    CHECK(l2.flags == 0x17 && (l2.flags & kStockLayerReveal) && !(l2.flags & kStockLayerAlphaMask));
    CHECK(near(l2.sharpness, 51.0f / 255.0f));
    CHECK(near(l2.normalStrength, 1.0f));

    // Short buffers and bad counts fail.
    CHECK(!decodeStockLayeredMaterialHex(kTerrainPsData, 4, m, &err));
    CHECK(!decodeStockLayeredMaterialHex(kTerrainPsData, 0, m, &err));
    CHECK(!decodeStockLayeredMaterialHex("0g", 1, m, &err));
    CHECK(!decodeStockLayeredMaterialHex("000", 1, m, &err));
}

TEST(stock_layered_material_slots_and_uv) {
    CHECK(stockLayerColorSlot(0) == 0 && stockLayerColorSlot(3) == 3);
    CHECK(stockLayerNormalSlot(0) == 9 && stockLayerNormalSlot(7) == 16);
    CHECK(stockLayerRevealSlot(0) == -1 && stockLayerRevealSlot(1) == 28 && stockLayerRevealSlot(7) == 34);
    CHECK(stockLayerTintMaskSlot(0) == 59 && stockLayerTintMaskSlot(1) == 60);

    StockLayerConstants c;
    auto uv = stockLayerUv(c, 0.25f, -3.0f);
    CHECK(uv[0] == 0.25f && uv[1] == -3.0f);
    // Scale (2, 4), a quarter turn (cos 0, sin 1), offset (0.5, 0): (s v sv, -s u su) + offset.
    c.uvScale = {2, 4};
    c.uvCos = 0;
    c.uvSin = 1;
    c.uvOffset = {0.5f, 0};
    uv = stockLayerUv(c, 1.0f, 1.0f);
    CHECK(near(uv[0], 4.5f) && near(uv[1], -2.0f));
}

TEST(stock_layered_material_blend) {
    StockLayerConstants c;
    // No reveal, no alpha: b = w.
    CHECK(near(stockLayerBlend(c, 0.3f, 0.0f, 0.0f), 0.3f));
    // Alpha mask: b = w * alpha.
    c.flags = kStockLayerAlphaMask;
    CHECK(near(stockLayerBlend(c, 0.5f, 0.5f, 0.0f), 0.25f));
    // Reveal: the ramp between lo = sat(1 - w - k sqrt w) and hi = sat(1 - w + k sqrt(1 - w)).
    c.flags = kStockLayerReveal;
    c.sharpness = 0.2f;
    const float w = 0.5f, lo = 0.5f - 0.2f * std::sqrt(0.5f), hi = 0.5f + 0.2f * std::sqrt(0.5f);
    CHECK(near(stockLayerBlend(c, w, 0, lo), 0.0f));
    CHECK(near(stockLayerBlend(c, w, 0, hi), 1.0f));
    CHECK(near(stockLayerBlend(c, w, 0, 0.5f), 0.5f));
    CHECK(near(stockLayerBlend(c, w, 0, 0.0f), 0.0f) && near(stockLayerBlend(c, w, 0, 1.0f), 1.0f));
    // w = 0 hides the layer, w = 1 shows it wherever the reveal map is above 0 (tw4: no colour stream, w = 1).
    CHECK(stockLayerBlend(c, 0.0f, 0, 0.99f) == 0.0f);
    CHECK(stockLayerBlend(c, 1.0f, 0, 0.01f) == 1.0f);
    CHECK(stockLayerBlend(c, 1.0f, 0, 0.0f) == 0.0f);
    // A sharpness of 0: a step at 1 - w.
    c.sharpness = 0;
    CHECK(stockLayerBlend(c, 0.3f, 0, 0.71f) == 1.0f && stockLayerBlend(c, 0.3f, 0, 0.69f) == 0.0f);
    // Reveal and alpha together.
    c.flags = kStockLayerReveal | kStockLayerAlphaMask;
    CHECK(near(stockLayerBlend(c, 1.0f, 0.4f, 0.5f), 0.4f));

    // Colour modes: lerp, multiply, unchanged; below 1/255 the layer is skipped.
    const std::array<float, 3> below{0.2f, 0.4f, 0.6f}, layer{1.0f, 0.5f, 0.0f};
    StockLayerConstants m;
    auto out = stockLayerBlendColor(m, 0.5f, below, layer);
    CHECK(near(out[0], 0.6f) && near(out[1], 0.45f) && near(out[2], 0.3f));
    m.colorMode = 1;
    out = stockLayerBlendColor(m, 0.5f, below, layer);
    CHECK(near(out[0], 0.2f) && near(out[1], 0.3f) && near(out[2], 0.3f));
    m.colorMode = 2;
    out = stockLayerBlendColor(m, 1.0f, below, layer);
    CHECK(out == below);
    m.colorMode = 0;
    out = stockLayerBlendColor(m, 0.5f / 255.0f, below, layer);
    CHECK(out == below);
}

// Research 136 section 2: the layer kinds and UV sets from technique set names (M2 of research 107, overlays, a UV1
// layer of a `2` family, the slot-126 mask by hexB, code layers).
TEST(model_layer_tokens_from_techset_names) {
    // M2: tmo, 2 layers, layer 1 standard (3810015f, colour mode 2), UV0, no 126 mask (hexB 1...).
    const ModelLayerTokens m2 = parseModelLayerTokens("tmo/lit_3_lit_rpl_ta1_800800_1002000000000010_0_2_1_0_0_13810015b_0_0_1_0_0_0_0_3810015f_2_1_2_0_0");
    CHECK(m2.layers == 2 && !m2.mask126 && m2.kind.size() == 2 && m2.kind[1] == ModelLayerKind::Standard && !m2.uv1[1]);
    // An overlay layer (410005b) and a standard UV1 layer (3818015f) on a tm2co set masked by slot 126 (hexB 3...).
    const ModelLayerTokens o = parseModelLayerTokens(
        "tm2co/lit_3_lit_rpl_ta1_800800_3002000000000010_0_3_1_0_0_13810015b_0_0_1_0_0_0_0_410005b_0_0_1_0_0_0_0_3818015f_0_0_2_0_0");
    CHECK(o.layers == 3 && o.mask126);
    if (o.kind.size() == 3) {
        CHECK(o.kind[1] == ModelLayerKind::Overlay && !o.uv1[1]);
        CHECK(o.kind[2] == ModelLayerKind::Standard && o.uv1[2]);
    }
    // The same UV1 bit in a family without a second UV set: UV0. A code layer (b = 1).
    const ModelLayerTokens c = parseModelLayerTokens("tmo/lit_3_lit_rpl_ta1_800800_1002000000000010_0_3_1_0_0_13810015b_0_0_1_0_0_0_0_3818015f_0_0_2_0_0_0_1_180153_0_0_1_0_0");
    CHECK(c.layers == 3 && !c.uv1[1] && c.kind[2] == ModelLayerKind::Code);
    // Not a lit technique set.
    CHECK(parseModelLayerTokens("tw/dist_10_distortion_rpl").layers == 0);
    CHECK(parseModelLayerTokens("no_slash").layers == 0);
}
