#include "material_blend.h"
#include "test_harness.h"

using namespace iw8r;

TEST(material_blend_from_stock_techsets) {
    // Real names from the editor's material cache and the converter's effect sets.
    CHECK(materialBlendFromTechset("tmo/lit_3_lit_rpl_ta1_800800_1002000000000010_0_1_1_0_0_13810015b_0_0_1_0_0") == MaterialBlend::Opaque);
    CHECK(materialBlendFromTechset("#tmco/lit_3_lit_alt_ta1_808814_1002000201000011_0_1_1_0_0_138f0035b_0_0_1_0_0") == MaterialBlend::AlphaTest);
    CHECK(materialBlendFromTechset("tmo/lit_3_lit_bad_tca_a90802_1002000000000010_0_1_1_0_0_13810015b_0_0_3_0_0") == MaterialBlend::Blend);
    CHECK(materialBlendFromTechset("tmo/lit_3_lit_bad_ta1_9002_1002000000000011_0_1_1_0_0_13810035b_0_0_1_0_0") == MaterialBlend::Blend);
    CHECK(materialBlendFromTechset("elcq/unlit_6_effect_bad_tca_802_10a4_0_1_0_0_0_100060023_0_0_3_0_0") == MaterialBlend::Additive);
    CHECK(materialBlendFromTechset("elcq/unlit_6_effect_bad_ta0_802_1004_0_1_0_0_0_100000023_0_0_2_0_0") == MaterialBlend::Blend);
    CHECK(materialBlendFromTechset(",cust_2_2d_rpl_ta0_0_0_0_0_0") == MaterialBlend::Opaque);
    CHECK(materialBlendFromTechset("") == MaterialBlend::Opaque);
    CHECK(materialBlendFromTechset(",null") == MaterialBlend::Opaque);
    CHECK(materialBlendFromTechset("badger_alt") == MaterialBlend::Opaque);  // no table token after the word
    CHECK(std::string(materialBlendName(MaterialBlend::Additive)) == "additive");
    // A renamed set (an installed stock edit's iw8rf_<hash>) gives no blend: the cache then takes slot 27 as the mask.
    CHECK(!materialBlendKnown("tmo/iw8rf_e4dd71cccd61a1abc624d4ba"));
    CHECK(materialBlendKnown("#tmco/lit_3_lit_alt_ta1_808814_1002000201000011_0_1_1_0_0_138f0035b_0_0_1_0_0"));
    CHECK(materialBlendKnown("tmo/lit_3_lit_rpl_ta1_800800_1002000000000010_0_1_1_0_0_13810015b_0_0_1_0_0"));
}
