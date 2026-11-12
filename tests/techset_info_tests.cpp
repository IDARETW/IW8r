#include "material_slots.h"
#include "techset_info.h"
#include "test_harness.h"

using namespace iw8r;

TEST(techset_info_families_and_blend) {
    // Real names from the research 66 survey.
    auto t = parseTechset("tmo/lit_3_lit_alt_ta1_808800_1002000201000011_0_1_1_0_0_138f0035b_0_0_1_0_0");
    CHECK(t.family == TechsetFamily::Lit && t.blendToken == "alt" && t.tableToken == "ta1" && t.blend == MaterialBlend::AlphaTest);
    t = parseTechset("mo/lit_3_lit_bar_ta1_802_1002000000080011_0_1_1_0_0_13810035b_0");
    CHECK(t.glass && t.blend == MaterialBlend::Blend);
    CHECK(materialBlendFromTechset("tw/lit_3_lit_bar_tca_4002_1002000000080010_0_1_1_0_0_13810015b_") == MaterialBlend::Blend);
    t = parseTechset("mco/lit_3_lit_alt_tom_808810_1002000000000010_0_1_1_0_");
    CHECK(t.blend == MaterialBlend::AlphaTest && t.tableToken == "tom");
    CHECK(parseTechset("elcq/dist_10_distortion_rpl_ta0_0_2_0_0_0").family == TechsetFamily::Distortion);
    CHECK(parseTechset("tmo/card_8_lit_card_rpl_ta1_c08000_1002000000000011_0_").family == TechsetFamily::LightProxyCard);
    CHECK(parseTechset(",null").family == TechsetFamily::Null);
    CHECK(parseTechset(",cust_2_2d_rpl_ta0_0_0_0").family == TechsetFamily::Ui2d);
    CHECK(parseTechset("tm/sky_3_sky_rpl_ta1_0").family == TechsetFamily::Sky);
    CHECK(parseTechset("elcq/unlit_6_effect_bad_tca_802_10a4_0_1_0_0_0_100060023_0_0_3_0_0").blend == MaterialBlend::Additive);
    CHECK(parseTechset("#tmco/lit_3_lit_alt_ta1_808814_1002").blend == MaterialBlend::AlphaTest);
    CHECK(parseTechset("").family == TechsetFamily::Unknown);
    CHECK(std::string(techsetFamilyName(TechsetFamily::LightProxyCard)) == "lighting proxy card");
}

TEST(techset_info_draw_class) {
    const std::string alt = "tmco/lit_3_lit_alt_ta1_808810_1002000201000011_0_1_1_0_0_138f0035b_0_0_1_0_0";
    // surfaceFlags from the alder bush pair: 0x80 (nodraw) only on the shadowcaster.
    CHECK(materialDrawFor("tmco/foliage_alder_bush_large_01_flag", alt, 138477616) == MaterialDraw::Draw);
    CHECK(materialDrawFor("tmco/foliage_alder_bush_large_01_shadowcaster_flag", alt, 134283440) == MaterialDraw::ShadowOnly);
    // No flags recorded: the name decides.
    CHECK(materialDrawFor("tmo/me_foliage_tree_olive_canopy_shadowcaster", alt, -1) == MaterialDraw::ShadowOnly);
    CHECK(materialDrawFor("tmo/foliage_tree_beech_shadow_caster", alt, -1) == MaterialDraw::ShadowOnly);
    CHECK(materialDrawFor("tmo/me_foliage_tree_olive_canopy", alt, -1) == MaterialDraw::Draw);
    CHECK(materialDrawFor("x", "tmo/card_8_lit_card_rpl_ta1_c08000_1002", 0) == MaterialDraw::Hidden);
    CHECK(materialDrawFor("x", ",null", -1) == MaterialDraw::Hidden);
    CHECK(materialDrawFor("x", "twc/dist_10_distortion_rpl_ta0_8000_3_0_0_0", 0) == MaterialDraw::Hidden);
    CHECK(std::string(materialDrawName(MaterialDraw::ShadowOnly)) == "shadow only");
}

TEST(texture_slot_roles) {
    CHECK(iw8TextureSlotName(27).find("opacity") != std::string::npos);
    CHECK(iw8TextureSlotName(10) == "normal + gloss, layer 2");
    CHECK(iw8TextureSlotName(2) == "colour, layer 3");
    CHECK(iw8TextureSlotName(59) == "tint");
    CHECK(iw8TextureSlotName(68).find("translucency") == 0);
    CHECK(iw8TextureSlotName(163) == "impostor depth");
    CHECK(iw8TextureSlotName(200) == "slot 200");
    CHECK(iw8TextureSlotIsBuilt(27) && !iw8TextureSlotIsBuilt(59));
}
