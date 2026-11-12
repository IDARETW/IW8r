// Zone dependency view (src/core/zone_deps.*): a small fake extraction with tiny fake catalogs in a temp folder (carried
// counts, stock copies and their zones, by-name refs, the per-zone summary, missing inputs), the always-loaded rule, and the
// real showcase build when it is present.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "test_harness.h"
#include "zone_deps.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

const char* kMap = "m";

void writeText(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f << text;
}

fs::path freshDir(const std::string& name) {
    const fs::path d = fs::path(test::tmpDir()) / "zone_deps" / name;
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

const KindTotals& kindOf(const ZoneDepReport& r, DepKind k) { return r.kinds[size_t(k)]; }

const StockCopy* findCopy(const ZoneDepReport& r, const std::string& copyName) {
    for (const StockCopy& c : r.stockCopies)
        if (c.copyName == copyName) return &c;
    return nullptr;
}

const ZoneUsage* findZone(const ZoneDepReport& r, const std::string& zone) {
    for (const ZoneUsage& z : r.zones)
        if (z.zone == zone) return &z;
    return nullptr;
}

const ByNameRef* findRef(const ZoneDepReport& r, DepKind k, const std::string& name) {
    for (const ByNameRef& b : r.byName)
        if (b.kind == k && b.name == name) return &b;
    return nullptr;
}

const std::string kMatCrate =
    R"({"techniqueSet":"iw8rcm1","textures":[{"name":"colorMap","semantic":"colorMap","image":"iw8rm_iw8r_stock_m2o_crate_01_1a2b3c4d_c"},)"
    R"({"name":"normalMap","semantic":"normalMap","image":"iw8rm_iw8r_stock_m2o_crate_01_1a2b3c4d_n"}]})";
const std::string kMatUnknown =
    R"({"textures":[{"name":"colorMap","semantic":"colorMap","image":"iw8rm_iw8r_stock_zz_unknown_9f9f9f9f_c"}]})";
const std::string kMatMine = R"({"textures":[{"name":"colorMap","semantic":"colorMap","image":"$black"}]})";
const std::string kMatGrey = R"({"textures":[{"name":"normalMap","semantic":"normalMap","image":"$identitynormalmap"}]})";
const std::string kReplayJunk =
    R"({"schema":1,"name":"tm_com_junktire","lods":[{"distance":0,"surfaces":[{"material":"$default","vertices":[],"indices":[]}]}]})";
const std::string kModelJunk = R"({"collLod":0,"lods":[{"distance":0}],"type":"rigid"})";
const std::string kWorld =
    R"({"schema":1,"sky":"$black","reflection_probes":[{"image":"probe_img"}],)"
    R"("surfaces":[{"material":"iw8r_stock/m2o/crate_01","vertices":[],"indices":[]},{"material":"$default","vertices":[],"indices":[]}]})";

// The fake extraction (materials, images, models, map script, entities, world, a stock sound list) and the fake catalogs.
// Returns the extraction folder; the catalog folder goes in `catalogOut`.
fs::path makeFake(const std::string& name, fs::path& catalogOut) {
    const fs::path base = freshDir(name);
    const fs::path ex = base / "extracted";
    const fs::path cat = base / "catalog";
    catalogOut = cat;

    writeText(cat / "iw8_materials.tsv",
              "# fake stock materials\n"
              "m2o/crate_01\tmp_hideout,mp_rust\n"
              "tm/bin_01\tcommon_mp\n"
              "#tm/old_one\tmp_hideout\n");
    writeText(cat / "iw8_images.tsv",
              "# fake stock images\n"
              "crate_01_c&crate_01_s~111\tmp_hideout\n"
              "crate_01_n&crate_01_g~222\tmp_hideout,techsets_mp_donetsk\n"
              "$black\tcode_post_gfx\n");
    writeText(cat / "iw8_models.tsv", "# fake stock models\ntm/com_junktire\tmp_hideout\n");
    writeText(cat / "iw8_common_vfx.tsv", "# fake always-loaded effects\nvfx/code/spark\tcommon_mp\n");
    writeText(cat / "iw8_common_sounds.tsv", "# fake sounds\n2pop\t0\t25000\tcommon_mp\n");

    writeText(ex / "materials" / "iw8r_stock" / "m2o" / "crate_01.json", kMatCrate);
    writeText(ex / "materials" / "iw8r_stock" / "zz" / "unknown.json", kMatUnknown);
    writeText(ex / "materials" / "custom" / "mine.json", kMatMine);
    writeText(ex / "materials" / "iw8r" / "grey.json", kMatGrey);
    writeText(ex / "images" / "iw8rm_iw8r_stock_m2o_crate_01_1a2b3c4d_c.dds", std::string(2000, 'c'));
    writeText(ex / "images" / "iw8rm_iw8r_stock_m2o_crate_01_1a2b3c4d_n.dds", std::string(3000, 'n'));
    writeText(ex / "images" / "iw8rm_custom_mine_0_c.dds", std::string(500, 'm'));
    writeText(ex / "xmodel" / "iw8r_stock_tm_com_junktire.json", kModelJunk + std::string(100 - kModelJunk.size(), ' '));  // 100 bytes
    writeText(ex / "xmodel" / "iw8r_stock_tm_com_junktire.replay.json", kReplayJunk + std::string(400 - kReplayJunk.size(), ' '));
    writeText(ex / "xmodel" / "iw8r_dev_crate.json", std::string(50, 'd'));

    const fs::path map = ex / "maps" / "mp";
    writeText(map / (std::string(kMap) + ".gsc"),
              "// loadfx(\"vfx/commented_out\") is not a reference\n"
              "main() {\n"
              "    level.fx = loadfx(\"vfx/code/spark\");\n"
              "    level.fx2 = loadfx(\"fx_local\");\n"
              "    playsound(\"2pop\");\n"
              "    playsound(self, \"snd_local\");\n"
              "}\n");
    writeText(map / (std::string(kMap) + ".d3dbsp.ents"),
              "{\n\"classname\" \"script_model\"\n\"fx\" \"fx_local\"\n\"soundalias\" \"snd_local\"\n}\n"
              "{\n\"classname\" \"worldspawn\"\n}\n");
    writeText(map / (std::string(kMap) + ".d3dbsp.replay-world.json"), kWorld);
    writeText(ex / "sound" / (std::string(kMap) + ".stock_sounds.txt"), "2pop 0\nlisted_alias\n");
    return ex;
}

}  // namespace

TEST(zone_always_loaded_rule) {
    CHECK(zoneIsAlwaysLoaded("common_mp"));
    CHECK(zoneIsAlwaysLoaded("common_br_mp"));
    CHECK(zoneIsAlwaysLoaded("techsets_common"));
    CHECK(zoneIsAlwaysLoaded("techsets_common_ui"));
    CHECK(zoneIsAlwaysLoaded("code_post_gfx"));
    CHECK(zoneIsAlwaysLoaded("mp_patch_01"));
    CHECK(!zoneIsAlwaysLoaded("mp_hideout"));
    CHECK(!zoneIsAlwaysLoaded("techsets_mp_donetsk"));
    CHECK(!zoneIsAlwaysLoaded("global_mp"));
    CHECK(!zoneIsAlwaysLoaded("techsets_global_stream_mp"));
}

TEST(zone_fake_extraction_carried_and_copies) {
    fs::path cat;
    const fs::path ex = makeFake("carried", cat);
    const ZoneDepReport r = makeZoneDepReport(ex.string(), kMap, cat.string());
    CHECK(r.notes.empty());
    CHECK(r.kinds.size() == 5);

    const KindTotals& mat = kindOf(r, DepKind::Material);
    CHECK(mat.carried == 4);
    CHECK(mat.carriedBytes == kMatCrate.size() + kMatUnknown.size() + kMatMine.size() + kMatGrey.size());
    CHECK(mat.stockCopies == 2);
    CHECK(mat.referenceable == 1);

    const KindTotals& img = kindOf(r, DepKind::Image);
    CHECK(img.carried == 3);
    CHECK(img.carriedBytes == 2000 + 3000 + 500);
    CHECK(img.stockCopies == 2);
    CHECK(img.stockCopyBytes == 5000);
    CHECK(img.referenceable == 2);
    CHECK(img.referenceableBytes == 5000);

    const KindTotals& mdl = kindOf(r, DepKind::Model);
    CHECK(mdl.carried == 2);
    CHECK(mdl.carriedBytes == 100 + 400 + 50);  // json + replay json of each model
    CHECK(mdl.stockCopies == 1);
    CHECK(mdl.referenceableBytes == 500);

    const StockCopy* crate = findCopy(r, "iw8r_stock/m2o/crate_01");
    CHECK(crate != nullptr);
    if (crate) {
        CHECK(crate->kind == DepKind::Material);
        CHECK(crate->stockName == "m2o/crate_01");
        CHECK(crate->bytes == kMatCrate.size());
        CHECK(crate->stockZones == (std::vector<std::string>{"mp_hideout", "mp_rust"}));
    }
    const StockCopy* unknown = findCopy(r, "iw8r_stock/zz/unknown");
    CHECK(unknown != nullptr);
    if (unknown) CHECK(unknown->stockZones.empty());  // not in the catalogs: a copy, not a candidate

    const StockCopy* colour = findCopy(r, "iw8rm_iw8r_stock_m2o_crate_01_1a2b3c4d_c");
    CHECK(colour != nullptr);
    if (colour) {
        CHECK(colour->kind == DepKind::Image);
        CHECK(colour->stockName == "crate_01_c");
        CHECK(colour->stockZones == (std::vector<std::string>{"mp_hideout"}));
    }
    const StockCopy* normal = findCopy(r, "iw8rm_iw8r_stock_m2o_crate_01_1a2b3c4d_n");
    CHECK(normal != nullptr);
    if (normal) CHECK(normal->stockZones == (std::vector<std::string>{"mp_hideout", "techsets_mp_donetsk"}));

    const StockCopy* model = findCopy(r, "iw8r_stock_tm_com_junktire");
    CHECK(model != nullptr);
    if (model) {
        CHECK(model->kind == DepKind::Model);
        CHECK(model->stockName == "tm/com_junktire");
        CHECK(model->bytes == 500);
        CHECK(model->stockZones == (std::vector<std::string>{"mp_hideout"}));
    }
    // A custom image (iw8rm_custom_...) and a non-stock model are carried but are not copies.
    CHECK(findCopy(r, "iw8rm_custom_mine_0_c") == nullptr);
    CHECK(findCopy(r, "iw8r_dev_crate") == nullptr);
}

TEST(zone_fake_extraction_by_name_refs) {
    fs::path cat;
    const fs::path ex = makeFake("byname", cat);
    const ZoneDepReport r = makeZoneDepReport(ex.string(), kMap, cat.string());

    const ByNameRef* spark = findRef(r, DepKind::Effect, "vfx/code/spark");
    CHECK(spark != nullptr);
    if (spark) CHECK(spark->zones == (std::vector<std::string>{"common_mp"}));
    CHECK(findRef(r, DepKind::Effect, "vfx/commented_out") == nullptr);  // comments are not references
    const ByNameRef* fx = findRef(r, DepKind::Effect, "fx_local");
    CHECK(fx != nullptr);
    CHECK(fx && fx->zones.empty());  // named by the script and the entity, not in the fake effect catalog
    CHECK(kindOf(r, DepKind::Effect).byName == 2);

    const ByNameRef* twoPop = findRef(r, DepKind::Sound, "2pop");
    CHECK(twoPop != nullptr);
    CHECK(twoPop && twoPop->zones == (std::vector<std::string>{"common_mp"}));
    CHECK(findRef(r, DepKind::Sound, "snd_local") != nullptr);
    CHECK(findRef(r, DepKind::Sound, "listed_alias") != nullptr);
    CHECK(kindOf(r, DepKind::Sound).byName == 3);
    CHECK(kindOf(r, DepKind::Sound).byNameInZones == 1);

    const ByNameRef* black = findRef(r, DepKind::Image, "$black");
    CHECK(black != nullptr);
    CHECK(black && black->zones == (std::vector<std::string>{"code_post_gfx"}));
    CHECK(findRef(r, DepKind::Image, "$identitynormalmap") != nullptr);
    CHECK(findRef(r, DepKind::Image, "probe_img") == nullptr);  // not an engine name
    CHECK(kindOf(r, DepKind::Image).byName == 2);
    CHECK(kindOf(r, DepKind::Image).byNameInZones == 1);

    CHECK(findRef(r, DepKind::Material, "$default") != nullptr);
}

TEST(zone_fake_extraction_zone_summary) {
    fs::path cat;
    const fs::path ex = makeFake("zones", cat);
    const ZoneDepReport r = makeZoneDepReport(ex.string(), kMap, cat.string());

    // mp_hideout: crate material (shared with mp_rust), crate colour, crate normal (shared with techsets), model. The fx_local
    // and snd_local names are not in the fake effect or sound catalogs, so they have no zone and count nowhere.
    CHECK(!r.zones.empty());
    if (!r.zones.empty()) CHECK(r.zones.front().zone == "mp_hideout");
    const ZoneUsage* hideout = findZone(r, "mp_hideout");
    CHECK(hideout != nullptr);
    if (hideout) {
        CHECK(hideout->assets == 4);
        CHECK(hideout->soleSource == 2);  // the colour image and the model are listed in no other zone
        CHECK(!hideout->alwaysLoaded);
    }
    const ZoneUsage* rust = findZone(r, "mp_rust");
    CHECK(rust != nullptr);
    if (rust) {
        CHECK(rust->assets == 1);
        CHECK(rust->soleSource == 0);
    }
    const ZoneUsage* common = findZone(r, "common_mp");
    CHECK(common != nullptr);
    if (common) {
        CHECK(common->alwaysLoaded);
        CHECK(common->assets == 2);  // spark effect and 2pop sound
        CHECK(common->soleSource == 2);
    }
    const ZoneUsage* post = findZone(r, "code_post_gfx");
    CHECK(post != nullptr);
    if (post) CHECK(post->alwaysLoaded);
    CHECK(findZone(r, "techsets_mp_donetsk") != nullptr);
}

TEST(zone_fake_extraction_format) {
    fs::path cat;
    const fs::path ex = makeFake("format", cat);
    const ZoneDepReport r = makeZoneDepReport(ex.string(), kMap, cat.string());
    const std::string text = formatZoneDepReport(r);
    CHECK(text.find("zone dependencies: m") != std::string::npos);
    CHECK(text.find("mp_hideout") != std::string::npos);
    CHECK(text.find("always") != std::string::npos);
    CHECK(text.find("reference instead: 4 carried copies") != std::string::npos);  // crate, colour, normal, model
    CHECK(text.find("top 4 candidates by bytes") != std::string::npos);
    CHECK(text.find("iw8r_stock/m2o/crate_01") != std::string::npos);
    // Candidate bytes sorted: the normal image (3000 bytes) comes before the colour image (2000).
    CHECK(text.find("iw8rm_iw8r_stock_m2o_crate_01_1a2b3c4d_n") < text.find("iw8rm_iw8r_stock_m2o_crate_01_1a2b3c4d_c"));
    CHECK(text.find("iw8r_stock/zz/unknown") == std::string::npos);  // not a candidate
}

TEST(zone_missing_inputs_count_nothing) {
    fs::path cat;
    const fs::path ex = makeFake("missing", cat);
    // No catalogs: every carried file still counts, nothing is zoned, and the missing catalogs are noted.
    const ZoneDepReport noCat = makeZoneDepReport(ex.string(), kMap, (cat / "nowhere").string());
    CHECK(noCat.notes.size() == 5);
    CHECK(kindOf(noCat, DepKind::Material).carried == 4);
    CHECK(kindOf(noCat, DepKind::Material).referenceable == 0);
    CHECK(noCat.zones.empty());
    CHECK(formatZoneDepReport(noCat).find("reference instead: 0") != std::string::npos);

    // No extraction folder: all counts are zero and the folder is noted.
    const ZoneDepReport noEx = makeZoneDepReport((freshDir("missing_ex") / "nope").string(), kMap, cat.string());
    CHECK(!noEx.notes.empty());
    CHECK(kindOf(noEx, DepKind::Material).carried == 0);
    CHECK(noEx.stockCopies.empty());
    CHECK(noEx.byName.empty());
}

TEST(zone_real_showcase_build) {
    const std::string ex = findUp("samples/dev/iw8build/mp_iw8r_dev_sho/extraction/extracted");
    const std::string cat = findUp("data/asset_catalog/iw8_materials.tsv");
    if (ex.empty() || cat.empty()) {
        test::skipf("  SKIP showcase build or catalogs not found\n");
        return;
    }
    const fs::path catDir = fs::path(cat).parent_path();
    const ZoneDepReport r = makeZoneDepReport(ex, "mp_iw8r_dev_sho", catDir.string());
    CHECK(r.notes.empty());

    size_t zoned = 0;
    uint64_t zonedBytes = 0;
    for (const StockCopy& c : r.stockCopies)
        if (!c.stockZones.empty()) {
            ++zoned;
            zonedBytes += c.bytes;
        }
    CHECK(zoned > 0);
    CHECK(kindOf(r, DepKind::Material).referenceable > 0);
    CHECK(kindOf(r, DepKind::Model).referenceable > 0);
    CHECK(zonedBytes > 0);
    CHECK(!r.zones.empty());
    std::printf("%s", formatZoneDepReport(r).c_str());
}
