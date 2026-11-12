// Streamed image catalogue (src/core/streamed_images.cpp): TSV parse, bad-row skipping, find, quality text.
// The catalogue format and the meaning of the columns: docs/research/52_streamed_images.md.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "streamed_images.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

std::string writeTsv(const std::string& name, const std::string& text) {
    const fs::path dir = fs::path(iw8r::test::tmpDir().empty() ? fs::temp_directory_path().string()
                                                               : iw8r::test::tmpDir()) /
                         "streamed_images_tests";
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path path = dir / name;
    std::ofstream out(path, std::ios::binary);
    out << text;
    out.close();
    return path.string();
}

const char* kGood =
    "# test catalogue: header comment\n"
    "# second comment line\n"
    "image\tfull_width\tfull_height\tresident_width\tresident_height\tstreamed\n"
    "plaster_painted_white_02_c&defaultspec~1\t2048\t2048\t256\t256\t1\n"
    "$outdoor\t16\t16\t16\t16\t0\n"
    "\n"
    "tm_wood_plank_01_c\t512\t512\t64\t64\t1\r\n";

}  // namespace

TEST(streamed_images_parses_rows_and_skips_comments) {
    const std::string path = writeTsv("good.tsv", kGood);
    StreamedImageCatalog cat;
    std::string err;
    CHECK(cat.load(path, &err));
    CHECK(err.empty());
    CHECK(cat.size() == 3);
    CHECK(cat.skippedRows() == 0);

    const StreamedImage* plaster = cat.find("plaster_painted_white_02_c&defaultspec~1");
    CHECK(plaster != nullptr);
    if (plaster) {
        CHECK(plaster->image == "plaster_painted_white_02_c&defaultspec~1");
        CHECK(plaster->fullW == 2048 && plaster->fullH == 2048);
        CHECK(plaster->residentW == 256 && plaster->residentH == 256);
        CHECK(plaster->streamed);
    }
    const StreamedImage* outdoor = cat.find("$outdoor");
    CHECK(outdoor != nullptr);
    if (outdoor) CHECK(!outdoor->streamed);
    // CRLF line endings are accepted.
    const StreamedImage* wood = cat.find("tm_wood_plank_01_c");
    CHECK(wood != nullptr);
    if (wood) CHECK(wood->streamed && wood->residentW == 64);
}

TEST(streamed_images_find_misses_and_is_exact) {
    StreamedImageCatalog cat;
    CHECK(cat.load(writeTsv("find.tsv", kGood)));
    CHECK(cat.find("does_not_exist") == nullptr);
    CHECK(cat.find("PLASTER_PAINTED_WHITE_02_C&DEFAULTSPEC~1") == nullptr);
    CHECK(cat.find("") == nullptr);
    CHECK(cat.find("plaster_painted_white_02_c") == nullptr);
}

TEST(streamed_images_skips_bad_rows) {
    const std::string text =
        "image\tfull_width\tfull_height\tresident_width\tresident_height\tstreamed\n"
        "ok_a\t1024\t1024\t128\t128\t1\n"
        "short_row\t1024\t1024\t128\n"                 // too few columns
        "extra_col\t1024\t1024\t128\t128\t1\t9\n"      // too many columns
        "no_name\t\t1024\t128\t128\t1\n"               // empty name
        "float_size\t10.5\t1024\t128\t128\t1\n"        // not an integer
        "signed_size\t+1024\t1024\t128\t128\t1\n"      // leading sign
        "zero_size\t0\t0\t0\t0\t0\n"                   // non-positive
        "resident_bigger\t512\t512\t1024\t128\t1\n"    // resident larger than full
        "bad_flag\t512\t512\t64\t64\t2\n"              // streamed not 0/1
        "ok_b\t512\t512\t64\t64\t0\n"
        "ok_a\t2048\t2048\t256\t256\t1\n"              // duplicate name, first one wins
        "ok_c\t512\t512\t64\t64\n";                    // missing streamed column
    StreamedImageCatalog cat;
    CHECK(cat.load(writeTsv("bad.tsv", text)));
    CHECK(cat.size() == 2);
    CHECK(cat.skippedRows() == 10);
    const StreamedImage* a = cat.find("ok_a");
    CHECK(a != nullptr);
    if (a) CHECK(a->fullW == 1024);  // the duplicate did not replace the first row
    CHECK(cat.find("ok_b") != nullptr);
    CHECK(cat.find("ok_c") == nullptr);
    CHECK(cat.find("zero_size") == nullptr);
}

TEST(streamed_images_missing_file_reports_error) {
    StreamedImageCatalog cat;
    std::string err;
    CHECK(!cat.load("E:/does/not/exist/iw8_streamed_images.tsv", &err));
    CHECK(!err.empty());
    CHECK(cat.size() == 0);
}

TEST(streamed_images_load_replaces_previous_contents) {
    StreamedImageCatalog cat;
    CHECK(cat.load(writeTsv("first.tsv", kGood)));
    CHECK(cat.size() == 3);
    CHECK(cat.load(writeTsv("second.tsv", "image\tfull_width\tfull_height\tresident_width\tresident_height\tstreamed\nonly\t64\t64\t64\t64\t0\n")));
    CHECK(cat.size() == 1);
    CHECK(cat.find("plaster_painted_white_02_c&defaultspec~1") == nullptr);
    CHECK(cat.find("only") != nullptr);
}

TEST(streamed_images_quality_text) {
    StreamedImage streamed;
    streamed.image = "a";
    streamed.fullW = streamed.fullH = 2048;
    streamed.residentW = streamed.residentH = 512;
    streamed.streamed = true;
    CHECK(streamedQualityText(streamed) == "streamed: editor gets 512x512 of 2048x2048");

    StreamedImage whole;
    whole.image = "b";
    whole.fullW = 1024;
    whole.fullH = 512;
    whole.residentW = 1024;
    whole.residentH = 512;
    whole.streamed = false;
    CHECK(streamedQualityText(whole) == "not streamed: full 1024x512");
}

TEST(streamed_images_shipped_catalogue_loads) {
    // Runs from the repo root (or a child folder); skipped when the catalogue is not found.
    fs::path p = fs::current_path();
    fs::path found;
    for (int i = 0; i < 6 && found.empty(); ++i) {
        const fs::path candidate = p / "data" / "asset_catalog" / "iw8_streamed_images.tsv";
        if (fs::exists(candidate)) found = candidate;
        if (!p.has_parent_path() || p.parent_path() == p) break;
        p = p.parent_path();
    }
    if (found.empty()) return;
    StreamedImageCatalog cat;
    std::string err;
    CHECK(cat.load(found.string(), &err));
    CHECK(cat.size() == 19396);  // 21 stock multiplayer zones (2026-11-11); the first catalogue had mp_backlot2 only (2671)
    CHECK(cat.skippedRows() == 0);
    const StreamedImage* plaster = cat.find("plaster_painted_white_02_c&defaultspec~12589240972894416292");
    CHECK(plaster != nullptr);
    if (plaster) {
        CHECK(plaster->streamed);
        CHECK(plaster->fullW == 2048 && plaster->fullH == 2048);
        CHECK(plaster->residentW == 256 && plaster->residentH == 256);
    }
}

// 4.25: the material cache names its DDS files after the image with '&' and '~' as '_'; the build reads DDS sizes.
TEST(streamed_images_cache_stem_and_dds_size) {
    StreamedImageCatalog cat;
    CHECK(cat.load(writeTsv("stem.tsv", kGood)));
    CHECK(streamedCacheStem("plaster_painted_white_02_c&defaultspec~1") == "plaster_painted_white_02_c_defaultspec_1");
    const StreamedImage* s = cat.findByCacheStem("plaster_painted_white_02_c_defaultspec_1");
    CHECK(s && s->fullW == 2048 && s->streamed);
    CHECK(cat.findByCacheStem("tm_wood_plank_01_c") != nullptr && cat.findByCacheStem("nope") == nullptr);
    // A minimal DDS header: "DDS ", size 124, flags, height 256 at 12, width 512 at 16.
    std::string dds = "DDS ";
    const unsigned vals[4] = {124, 0x1007, 256, 512};
    for (unsigned v : vals)
        for (int b = 0; b < 4; ++b) dds += char((v >> (8 * b)) & 0xff);
    dds += std::string(108, '\0');
    int w = 0, h = 0;
    CHECK(readDdsSize(writeTsv("size.dds", dds), w, h) && w == 512 && h == 256);
    CHECK(!readDdsSize(writeTsv("not.dds", "PNG..............................."), w, h));
}
