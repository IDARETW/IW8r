#include <cmath>
// Build report (src/core/build_report.*): counts over a small fake extraction, DDS header reading, budget warnings, the
// text summary, the diff, JSON save / load, and the real showcase build when it is present.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "build_report.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

const char* kMap = "mp_rep_test";

void writeText(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f << text;
}

fs::path freshDir(const std::string& name) {
    const fs::path d = fs::path(test::tmpDir()) / "build_report" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

std::string findUp(const std::string& rel) {
    fs::path p = fs::current_path();
    for (int up = 0; up < 6; ++up, p = p.parent_path())
        if (fs::exists(p / rel)) return (p / rel).string();
    return "";
}

// A DDS of `payload` bytes whose header has the given size. `fourcc` is written when non-empty; `dxgi` (when `dx10`) goes in
// the DX10 extension; `cube` sets DDSCAPS2_CUBEMAP. With neither FourCC nor DX10, the pixel format is RGBA (uncompressed).
std::string makeDds(uint32_t w, uint32_t h, const std::string& fourcc, bool dx10, uint32_t dxgi, bool cube, size_t payload) {
    std::string b(std::max<size_t>(payload, 148), '\0');
    const auto put32 = [&](size_t o, uint32_t v) {
        for (int i = 0; i < 4; ++i) b[o + size_t(i)] = char((v >> (8 * i)) & 0xFF);
    };
    b.replace(0, 4, "DDS ");
    put32(4, 124);
    put32(12, h);
    put32(16, w);
    put32(76, 32);
    put32(80, dx10 || !fourcc.empty() ? 0x4u : 0x41u);  // DDPF_FOURCC, or DDPF_RGB | DDPF_ALPHAPIXELS
    if (dx10) b.replace(84, 4, "DX10");
    else if (!fourcc.empty()) b.replace(84, 4, fourcc);
    else put32(88, 32);
    put32(112, cube ? 0x200u : 0u);
    if (dx10) put32(128, dxgi);
    return b;
}

// Fake extraction: 2 materials, 2 images (one DXT5 512x256, one DX10 BC7 1024x1024), 2 models, a world with 2 surfaces,
// a collision json, a comworld json with 4 primary lights, an .ents with 4 blocks and a 100-byte script.
fs::path makeBuild(const std::string& name) {
    const fs::path ex = freshDir(name) / "extracted";
    writeText(ex / "materials" / "a" / "one.json", "{}");
    writeText(ex / "materials" / "two.json", "{}");
    writeText(ex / "materials" / "notes.txt", "not a material");
    writeText(ex / "images" / "small_c.dds", makeDds(512, 256, "DXT5", false, 0, false, 2000));
    writeText(ex / "images" / "bc7_c.dds", makeDds(1024, 1024, "", true, 98, false, 3000));
    writeText(ex / "xmodel" / "crate.json", R"({"collLod":0,"lods":[{"distance":0}],"type":"rigid"})");
    writeText(ex / "xmodel" / "crate.replay.json",
              R"({"schema":1,"name":"crate","lods":[{"distance":0,"surfaces":[)"
              R"({"material":"a","vertices":[{},{},{},{}],"indices":[0,1,2,0,2,3]},)"
              R"({"material":"b","vertices":[{},{},{}],"indices":[0,1,2]}]},)"
              R"({"distance":50,"surfaces":[{"material":"a","vertices":[{}],"indices":[0,0,0,0,0,0,0,0,0,0,0,0]}]}]})");
    writeText(ex / "xmodel" / "ball.json", R"({"collLod":0,"lods":[{"distance":0}],"type":"rigid"})");
    writeText(ex / "xmodel" / "ball.replay.json", R"({"schema":1,"name":"ball","lods":[{"distance":0,"surfaces":[]}]})");
    const fs::path map = ex / "maps" / "mp";
    writeText(map / (std::string(kMap) + ".d3dbsp.replay-world.json"),
              R"({"schema":1,"sun_primary_light_index":1,"reflection_probes":[{"image":"p"}],)"
              R"("models":[{"model":"crate"},{"model":"crate"},{"model":"ball"}],)"
              R"("surfaces":[{"material":"a","vertices":[{}],"indices":[0,1,2,0,1,2,0,1,2,0,1,2,0,1,2,0,1,2,0,1,2,0,1,2,0,1,2,0,1,2,0,1,2,0,1,2]},)"
              R"({"material":"b","vertices":[{}],"indices":[0,1,2,0,1,2,0,1,2,0,1,2]}]})");
    writeText(map / (std::string(kMap) + ".d3dbsp.replay-collision.json"),
              R"({"schema":1,"submodels":[{},{}],"brushes":[{},{},{},{},{}],"triangles":[]})");
    writeText(map / (std::string(kMap) + ".d3dbsp.replay-comworld.json"),
              R"({"schema":1,"primary_lights":[{"type":0},{"type":1},{"type":3},{"type":3}]})");
    writeText(map / (std::string(kMap) + ".d3dbsp.ents"),
              "{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"script_model\"\n}\n{\n\"classname\" \"script_model\"\n}\n{\n\"model\" \"x\"\n}\n");
    writeText(map / (std::string(kMap) + ".gsc"), std::string(100, 'x'));
    return ex;
}

BuildReport sample() {
    BuildReport r;
    r.mapId = "m";
    r.materials = 120;
    r.images = 134;
    r.imageBytes = 100ull * 1024 * 1024;
    r.worldTriangles = 500;
    r.entities = 10;
    r.entityClasses = {{"script_model", 6}, {"worldspawn", 1}};
    r.scriptBytes = 2048;
    return r;
}

}  // namespace

TEST(report_counts_fake_build) {
    const BuildReport r = makeBuildReport(makeBuild("counts").string(), kMap);
    CHECK(r.mapId == kMap);
    CHECK(r.materials == 2);  // notes.txt is not a material
    CHECK(r.images == 2);
    CHECK(r.imageBytes == 5000);
    CHECK(r.xmodels == 2);
    CHECK(r.xmodelVerticesLod0 == 4 + 3);  // LOD 0 only: the LOD 50 surface is not counted
    CHECK(r.xmodelTrianglesLod0 == 2 + 1);
    CHECK(r.worldSurfaces == 2);
    CHECK(r.worldTriangles == 12 + 4);  // 36 and 12 indices
    CHECK(r.worldModels == 3);
    CHECK(r.reflectionProbes == 1);
    CHECK(r.sunPrimaryLightIndex == 1);
    CHECK(r.collisionSubmodels == 2);
    CHECK(r.collisionBrushes == 5);
    CHECK(r.collisionTriangles == 0);
    CHECK(r.entities == 4);
    CHECK(r.primaryLights == 4);
    CHECK(r.scriptBytes == 100);
}

TEST(report_entity_classes_counted_per_classname) {
    const BuildReport r = makeBuildReport(makeBuild("classes").string(), kMap);
    // script_model 2; then "(none)" 1 and worldspawn 1 by name.
    CHECK(r.entityClasses.size() == 3);
    if (r.entityClasses.size() == 3) {
        CHECK(r.entityClasses[0].classname == "script_model" && r.entityClasses[0].count == 2);
        CHECK(r.entityClasses[1].classname == "(none)" && r.entityClasses[1].count == 1);
        CHECK(r.entityClasses[2].classname == "worldspawn" && r.entityClasses[2].count == 1);
    }
}

TEST(report_reads_dds_headers) {
    const BuildReport r = makeBuildReport(makeBuild("dds").string(), kMap);
    const ImageInfo* small = nullptr;
    const ImageInfo* bc7 = nullptr;
    for (const ImageInfo& i : r.largestImages) {
        if (i.name == "small_c") small = &i;
        if (i.name == "bc7_c") bc7 = &i;
    }
    CHECK(small && bc7);
    if (small) {
        CHECK(small->width == 512);
        CHECK(small->height == 256);
        CHECK(small->format == "DXT5");
        CHECK(!small->cubemap);
    }
    if (bc7) {
        CHECK(bc7->width == 1024);
        CHECK(bc7->height == 1024);
        CHECK(bc7->format == "DXGI 98");
    }
    CHECK(r.maxImageSide == 1024);
    CHECK(r.maxImageSideName == "bc7_c");
}

TEST(report_non_dds_file_counts_bytes_only) {
    const fs::path ex = makeBuild("nondds");
    writeText(ex / "images" / "junk_c.dds", "hello world");
    const BuildReport r = makeBuildReport(ex.string(), kMap);
    CHECK(r.images == 3);
    bool found = false;
    for (const ImageInfo& i : r.largestImages)
        if (i.name == "junk_c") {
            found = true;
            CHECK(i.width == 0 && i.height == 0);
            CHECK(i.format == "unknown");
            CHECK(i.bytes == 11);
        }
    CHECK(found);
}

TEST(report_largest_images_top_ten_by_size) {
    const fs::path ex = makeBuild("top10");
    for (int i = 0; i < 12; ++i)
        writeText(ex / "images" / ("extra_" + std::to_string(i) + ".dds"), makeDds(64, 64, "DXT1", false, 0, false, 1000 + size_t(i) * 100));
    const BuildReport r = makeBuildReport(ex.string(), kMap);
    CHECK(r.images == 14);
    CHECK(r.largestImages.size() == 10);
    bool sorted = true;
    for (size_t i = 1; i < r.largestImages.size(); ++i)
        if (r.largestImages[i - 1].bytes < r.largestImages[i].bytes) sorted = false;
    CHECK(sorted);
    CHECK(!r.largestImages.empty() && r.largestImages.front().name == "bc7_c");
}

TEST(report_missing_files_count_zero) {
    const fs::path ex = freshDir("empty") / "extracted";
    fs::create_directories(ex);
    const BuildReport r = makeBuildReport(ex.string(), kMap);
    CHECK(r.materials == 0 && r.images == 0 && r.xmodels == 0 && r.worldTriangles == 0);
    CHECK(r.entities == 0 && r.primaryLights == 0 && r.scriptBytes == 0 && r.collisionBrushes == 0);
    CHECK(budgetWarnings(r).empty());
}

TEST(budget_clean_report_has_no_warnings) {
    CHECK(budgetWarnings(BuildReport{}).empty());
}

TEST(budget_guidelines_fire_over_thresholds) {
    BuildReport r;
    r.imageBytes = 512ull * 1024 * 1024 + 1;
    r.worldTriangles = 2000001;
    r.entities = 4001;
    r.primaryLights = 251;
    r.scriptBytes = 512ull * 1024 + 1;
    const auto w = budgetWarnings(r);
    CHECK(w.size() == 5);
    for (const std::string& m : w) CHECK(m.rfind("guideline:", 0) == 0);
}

TEST(budget_guidelines_hold_at_threshold) {
    BuildReport r;
    r.imageBytes = 512ull * 1024 * 1024;
    r.worldTriangles = 2000000;
    r.entities = 4000;
    r.primaryLights = 250;
    r.scriptBytes = 512ull * 1024;
    CHECK(budgetWarnings(r).empty());
}

TEST(budget_image_side_native_and_guideline) {
    BuildReport r;
    ImageInfo flat;
    flat.name = "tall";
    flat.width = 9000;
    flat.height = 100;
    flat.format = "DXT1";
    ImageInfo cube;
    cube.name = "sky_cube";
    cube.width = 5000;
    cube.height = 5000;
    cube.format = "DXT5";
    cube.cubemap = true;
    ImageInfo cubeBc7 = cube;
    cubeBc7.name = "sky_bc7";
    cubeBc7.format = "DXGI 98";
    ImageInfo big = flat;
    big.name = "big_guideline";
    big.width = 4500;
    big.height = 4500;
    r.largeImages = {flat, cube, cubeBc7, big};
    const auto w = budgetWarnings(r);
    int nativeCount = 0, guidelineCount = 0;
    bool cubeNative = false, bc7Guideline = false;
    for (const std::string& m : w) {
        if (m.rfind("native limit:", 0) == 0) ++nativeCount;
        if (m.rfind("guideline:", 0) == 0) ++guidelineCount;
        if (m.find("sky_cube") != std::string::npos && m.rfind("native limit:", 0) == 0) cubeNative = true;
        if (m.find("sky_bc7") != std::string::npos && m.rfind("guideline:", 0) == 0) bc7Guideline = true;
    }
    CHECK(nativeCount == 2);      // 9000 px 2D image, and a 5000 px cube map (limit 4096)
    CHECK(guidelineCount == 2);   // the 4500 px image and the 5000 px BC7 cube (limit 8192 for BC7)
    CHECK(cubeNative);
    CHECK(bc7Guideline);
}

TEST(budget_native_sun_index_and_static_models) {
    BuildReport r;
    r.sunPrimaryLightIndex = 256;
    r.worldModels = 65537;
    const auto w = budgetWarnings(r);
    CHECK(w.size() == 2);
    for (const std::string& m : w) CHECK(m.rfind("native limit:", 0) == 0);
    BuildReport ok;
    ok.sunPrimaryLightIndex = 255;
    ok.worldModels = 65536;
    CHECK(budgetWarnings(ok).empty());
}

TEST(report_format_has_figures) {
    const std::string s = formatBuildReport(makeBuildReport(makeBuild("format").string(), kMap));
    CHECK(s.find("build report: " + std::string(kMap)) != std::string::npos);
    CHECK(s.find("materials") != std::string::npos);
    CHECK(s.find("images") != std::string::npos);
    CHECK(s.find("bc7_c") != std::string::npos);
    CHECK(s.find("script_model 2") != std::string::npos);
}

TEST(diff_identical_reports_no_changes) {
    CHECK(diffBuildReports(sample(), sample()) == "no changes\n");
}

TEST(diff_images_line_matches_format) {
    BuildReport before = sample(), after = sample();
    after.images = 134;
    before.images = 120;
    before.imageBytes = 100ull * 1024 * 1024;
    after.imageBytes = 110ull * 1024 * 1024;
    CHECK(diffBuildReports(before, after) == "images: 120 -> 134 (+14), 100.0 MB -> 110.0 MB\n");
}

TEST(diff_reports_decreases_and_classes) {
    BuildReport before = sample(), after = sample();
    after.worldTriangles = 400;
    after.entityClasses = {{"script_model", 3}, {"worldspawn", 1}};
    after.scriptBytes = 1024;
    const std::string d = diffBuildReports(before, after);
    CHECK(d.find("world triangles: 500 -> 400 (-100)\n") != std::string::npos);
    CHECK(d.find("entity class script_model: 6 -> 3 (-3)\n") != std::string::npos);
    CHECK(d.find("script: 2.0 KB -> 1.0 KB (-1.0 KB)\n") != std::string::npos);
    CHECK(d.find("materials") == std::string::npos);  // unchanged fields are not listed
}

TEST(report_save_load_round_trip) {
    const BuildReport r = makeBuildReport(makeBuild("roundtrip").string(), kMap);
    const std::string file = (freshDir("roundtrip_json") / "report.json").string();
    std::string err;
    CHECK(saveBuildReport(r, file, &err));
    BuildReport back;
    CHECK(loadBuildReport(file, back, &err));
    CHECK(diffBuildReports(r, back) == "no changes\n");
    CHECK(back.largestImages.size() == r.largestImages.size());
    CHECK(back.entityClasses.size() == r.entityClasses.size());
    CHECK(back.maxImageSideName == r.maxImageSideName);
}

TEST(report_load_rejects_bad_files) {
    const fs::path dir = freshDir("bad_json");
    std::string err;
    BuildReport out;
    writeText(dir / "no_schema.json", "{\"map\":\"x\"}");
    CHECK(!loadBuildReport((dir / "no_schema.json").string(), out, &err));
    writeText(dir / "broken.json", "{\"schema\":1,");
    CHECK(!loadBuildReport((dir / "broken.json").string(), out, &err));
    CHECK(!loadBuildReport((dir / "missing.json").string(), out, &err));
}

TEST(report_real_showcase_build) {
    const std::string ex = findUp("samples/dev/iw8build/mp_iw8r_dev_sho/extraction/extracted");
    if (ex.empty()) {
        test::skipf("  SKIP showcase build not found\n");
        return;
    }
    const BuildReport r = makeBuildReport(ex, "mp_iw8r_dev_sho");
    CHECK(r.materials > 0);
    CHECK(r.images > 0);
    CHECK(r.worldTriangles > 0);
    std::printf("%s", formatBuildReport(r).c_str());
    for (const std::string& w : budgetWarnings(r)) std::printf("  budget: %s\n", w.c_str());
}

TEST(build_report_budget_gauges) {
    BuildReport r;
    r.worldTriangles = 1000000;
    r.worldModels = 70000;
    r.imageBytes = 256ull * 1024 * 1024;
    const auto g = budgetGauges(r);
    CHECK(g.size() == 8);
    bool tri = false, models = false, img = false;
    for (const auto& x : g) {
        if (x.label == "World triangles") tri = std::abs(x.fraction() - 0.5) < 1e-9 && !x.native;
        if (x.label == "Static models") models = x.native && x.fraction() > 1.0;
        if (x.label.rfind("Image memory", 0) == 0) img = std::abs(x.value - 256) < 1e-9 && x.unit == "MB";
    }
    CHECK(tri && models && img);
}
