#include "techset_detail.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

// <repo>/tests/data/techset_sample.asset.json, found by walking up from the working directory.
std::string samplePath() {
    fs::path p = fs::current_path();
    for (int up = 0; up < 6; ++up, p = p.parent_path()) {
        const fs::path cand = p / "tests" / "data" / "techset_sample.asset.json";
        if (fs::exists(cand)) return cand.string();
    }
    return {};
}

std::string readText(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool has(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

}  // namespace

TEST(techset_detail_parses_real_export) {
    const std::string path = samplePath();
    CHECK(!path.empty());
    TechsetDetail d;
    std::string err;
    CHECK(parseTechsetExport(readText(path), d, &err));
    CHECK(err.empty());
    CHECK(d.name == "mo/lit_3_lit_rpl_ta1_802000_1002000000000010_0_1_1_0_0_13810015b_0_0_1_0_0");
    CHECK(d.materialType == "MTL_TYPE_MODEL_SELFVIS");
    // maskedTechniques has 12 slots, one record each (techniqueMask bits 0,3,6,...,28,34).
    CHECK(d.techniques.size() == 12);
    CHECK(d.techniques.front().typeName == "TECHNIQUE_FIRST");
    CHECK(d.techniques.back().typeName == "TECHNIQUE_LIT_FORWARDPLUS_BITMASK");
    CHECK(d.techniques.front().argumentCount == 2);
}

TEST(techset_detail_forward_technique_fields) {
    const std::string path = samplePath();
    TechsetDetail d;
    CHECK(parseTechsetExport(readText(path), d, nullptr));
    CHECK(d.techniques.size() == 12);
    const TechniqueDetail& fwd = d.techniques.back();
    CHECK(fwd.name == "TECHNIQUE_LIT_FORWARDPLUS_BITMASK_ed1fcfcfdcc7d56c");
    CHECK(fwd.argumentCount == 17);
    // Only vertex and pixel stages are set on the forward technique; hull and domain are null.
    CHECK(fwd.shaders.size() == 2);
    CHECK(fwd.shaders.size() == 2 && fwd.shaders[0] == ",c491d0203b90cf3d768d6f25650c1f67");
    CHECK(fwd.shaders.size() == 2 && fwd.shaders[1] == ",83cafbaec48e1ef140ed7c1895a5fed8");
    CHECK(has(fwd.stateSummary, "PSOs 1"));
    CHECK(has(fwd.stateSummary, "RTs 2"));
    CHECK(has(fwd.stateSummary, "blend 0 (GFXS_BLEND_STATE_BITS_EMPTY)"));
    CHECK(has(fwd.stateSummary, "other 2049"));
    CHECK(has(fwd.stateSummary, "layout MTL_CONSTANT_LAYOUT_XMODEL_LIT_PROBE_OBJ"));
}

TEST(techset_detail_null_pixel_shader_is_kept_as_named) {
    // The stencil technique has pixelShader ",null.hlsl" (exported name, not a file): it is still a set stage.
    const std::string path = samplePath();
    TechsetDetail d;
    CHECK(parseTechsetExport(readText(path), d, nullptr));
    bool found = false;
    for (const TechniqueDetail& t : d.techniques) {
        if (t.typeName == "TECHNIQUE_DEPTH_PREPASS_STENCIL") {
            found = true;
            CHECK(t.shaders.size() == 2 && t.shaders[0] == ",eda79c59363fd79b97cd32e3955849fc");
            CHECK(t.shaders.size() == 2 && t.shaders[1] == ",null.hlsl");
        }
    }
    CHECK(found);
}

TEST(techset_detail_rejects_malformed_json) {
    TechsetDetail d;
    std::string err;
    CHECK(!parseTechsetExport("{\"asset\": ", d, &err));
    CHECK(!err.empty());
    err.clear();
    CHECK(!parseTechsetExport("", d, &err));
    CHECK(!err.empty());
    err.clear();
    CHECK(!parseTechsetExport("[1,2,3]", d, &err));  // root is not an object
    CHECK(!err.empty());
}

TEST(techset_detail_missing_fields_are_tolerant) {
    TechsetDetail d;
    std::string err;
    CHECK(parseTechsetExport("{}", d, &err));
    CHECK(d.techniques.empty());
    CHECK(!d.notes.empty());

    CHECK(parseTechsetExport("{\"asset\":{\"type\":\"MaterialTechniqueSet\",\"fields\":{}}}", d, &err));
    CHECK(d.name.empty());
    CHECK(d.techniques.empty());
    CHECK(!d.notes.empty());

    // A technique record with no shaders, no args and no state.
    const char* sparse =
        "{\"asset\":{\"type\":\"MaterialTechniqueSet\",\"fields\":{\"name\":{\"string\":\"x/lit_3_lit_rpl_ta0\"},"
        "\"maskedTechniques\":{\"values\":[{\"values\":[{\"name\":{\"string\":\"TECHNIQUE_A\"},"
        "\"type\":{\"name\":\"TECHNIQUE_A\"},\"pixelShader\":null}]}]}}}}";
    CHECK(parseTechsetExport(sparse, d, &err));
    CHECK(d.techniques.size() == 1);
    CHECK(d.techniques[0].name == "TECHNIQUE_A");
    CHECK(d.techniques[0].shaders.empty());
    CHECK(d.techniques[0].argumentCount == 0);
    CHECK(has(d.techniques[0].stateSummary, "PSOs 0"));
}

TEST(techset_detail_wrong_asset_type_is_noted) {
    TechsetDetail d;
    CHECK(parseTechsetExport("{\"asset\":{\"type\":\"Material\",\"fields\":{\"name\":{\"string\":\"mo/x\"}}}}", d, nullptr));
    CHECK(d.name == "mo/x");
    bool noted = false;
    for (const std::string& n : d.notes) noted = noted || has(n, "expected MaterialTechniqueSet");
    CHECK(noted);
}

TEST(techset_detail_describes_names) {
    const std::string lit = describeTechsetName("mo/lit_3_lit_rpl_ta1_802000_1002000000000010_0_1_1_0_0_13810015b_0_0_1_0_0");
    CHECK(has(lit, "Family: lit"));
    CHECK(has(lit, "Blend: opaque"));
    CHECK(has(lit, "Table: ta1"));
    CHECK(has(lit, "State word: 802000 (not decoded)"));
    CHECK(has(lit, "Editor draws it: yes"));
    CHECK(!has(lit, "Glass:"));

    const std::string alt = describeTechsetName("tmco/lit_3_lit_alt_ta1_808810_1002000201000011_0_1_1_0_0_138f0035b");
    CHECK(has(alt, "Blend: alpha test"));

    const std::string glass = describeTechsetName("mo/lit_3_lit_bar_ta1_802_1002000000080011_0_1_1_0_0_13810035b_0");
    CHECK(has(glass, "Blend: blend"));
    CHECK(has(glass, "Glass: yes"));

    const std::string additive = describeTechsetName("elcq/unlit_6_effect_bad_tca_802_10a4_0_1_0_0_0_100060023_0_0_3_0_0");
    CHECK(has(additive, "Blend: additive"));
    CHECK(has(additive, "Family: unlit effect"));

    // Info carriers and proxy cards are never drawn.
    CHECK(has(describeTechsetName(",null"), "Editor draws it: no"));
    CHECK(has(describeTechsetName("tmo/card_8_lit_card_rpl_ta1_c08000_1002000000000011_0_"), "Editor draws it: no"));
    CHECK(describeTechsetName("") == "No technique set name.");
}
