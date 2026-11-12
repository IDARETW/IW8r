// Overview image and PNG writer tests (src/core/overview_image.cpp).
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "brush_ops.h"
#include "custom_materials.h"
#include "map_text.h"
#include "overview_image.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

const char* kFloorSuffix = " tm2co/me_curb_stone_01 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0";
const char* kCaulkSuffix = " caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0";

std::string brushText(const AABB& box, const std::string& suffix) {
    const std::optional<Primitive> p = makeBrushPrimitive(makeBox(box, suffix), "\n");
    return p ? p->text() : std::string();
}

// One worldspawn holding `brushes`, then one point entity per `pointEntities` text (key lines, no braces).
std::optional<MapFile> makeMap(const std::vector<std::string>& brushes, const std::vector<std::string>& pointEntities) {
    std::string text = "iwmap 4\n// entity 0\n{\n\"classname\" \"worldspawn\"\n";
    for (const std::string& b : brushes) text += b;
    text += "}\n";
    int n = 1;
    for (const std::string& e : pointEntities) text += "// entity " + std::to_string(n++) + "\n{\n" + e + "}\n";
    Id next = 1;
    ParseResult result = parseMap(text, next);
    return result.map;
}

std::string pointEntity(const std::string& classname, const std::string& origin, const std::string& angles,
                        const std::string& extra = "") {
    std::string s = "\"classname\" \"" + classname + "\"\n\"origin\" \"" + origin + "\"\n";
    if (!angles.empty()) s += "\"angles\" \"" + angles + "\"\n";
    return s + extra;
}

// Floor whose top is at z = 0 over [0, 1024] squared.
std::string floorBrush() { return brushText(AABB{{0, 0, -64}, {1024, 1024, 0}}, kFloorSuffix); }

std::array<int, 3> pixel(const std::vector<uint8_t>& rgba, int w, int x, int y) {
    const size_t i = (size_t(y) * size_t(w) + size_t(x)) * 4;
    return {rgba[i], rgba[i + 1], rgba[i + 2]};
}

bool same(const std::array<int, 3>& p, int r, int g, int b) { return p[0] == r && p[1] == g && p[2] == b; }

int lum(const std::array<int, 3>& p) { return p[0] + p[1] + p[2]; }

}  // namespace

TEST(overview_png_round_trip_decodes_to_same_pixels) {
    // 301 x 97 RGBA is about 117 KB of raw data, so the stream spans several stored deflate blocks.
    const int w = 301, h = 97;
    std::vector<uint8_t> rgba(size_t(w) * size_t(h) * 4);
    for (size_t i = 0; i < rgba.size(); ++i) rgba[i] = static_cast<uint8_t>((i * 31) ^ (i / 7) ^ (i >> 5));
    const std::string path = (fs::temp_directory_path() / "iw8r_overview_roundtrip.png").string();
    std::string err;
    CHECK(writePng(path, w, h, rgba, &err));

    // The PNG signature and the IHDR of an RGBA8 image.
    std::ifstream in(path, std::ios::binary);
    std::vector<uint8_t> head(33);
    in.read(reinterpret_cast<char*>(head.data()), 33);
    CHECK(head[0] == 0x89 && head[1] == 'P' && head[2] == 'N' && head[3] == 'G');
    CHECK(head[12] == 'I' && head[13] == 'H' && head[14] == 'D' && head[15] == 'R');
    CHECK(head[24] == 8 && head[25] == 6);
    in.close();

    int dw = 0, dh = 0;
    std::vector<uint8_t> back;
    CHECK(decodeImageFile(path, dw, dh, back, &err));
    CHECK(dw == w && dh == h);
    CHECK(back == rgba);
    fs::remove(path);
}

TEST(overview_png_export_returns_file_size) {
    const std::optional<MapFile> map = makeMap({floorBrush()}, {});
    CHECK(map.has_value());
    if (!map) return;
    OverviewOptions opt;
    opt.width = 64;
    const std::string path = (fs::temp_directory_path() / "iw8r_overview_export.png").string();
    std::string err;
    const size_t bytes = exportOverviewPng(*map, opt, path, &err);
    CHECK(bytes > 0);
    CHECK(static_cast<size_t>(fs::file_size(path)) == bytes);
    int dw = 0, dh = 0;
    std::vector<uint8_t> px;
    CHECK(decodeImageFile(path, dw, dh, px, &err));
    CHECK(dw == 64 && dh == 64);
    fs::remove(path);
}

TEST(overview_rejects_maps_with_nothing_to_draw) {
    const std::optional<MapFile> map = makeMap({}, {});
    CHECK(map.has_value());
    if (!map) return;
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    std::string err;
    CHECK(!renderOverview(*map, OverviewOptions{}, rgba, w, h, &err));
    CHECK(!err.empty());
    const std::vector<uint8_t> shortBuffer{1, 2, 3};
    CHECK(encodePng(2, 2, shortBuffer).empty());
}

TEST(overview_two_level_box_is_lighter_than_floor) {
    const std::string box = brushText(AABB{{256, 256, 0}, {768, 768, 128}}, kFloorSuffix);
    const std::optional<MapFile> map = makeMap({floorBrush(), box}, {});
    CHECK(map.has_value());
    if (!map) return;
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    std::string err;
    CHECK(renderOverview(*map, OverviewOptions{}, rgba, w, h, &err));
    CHECK(w == 2048 && h == 2048);
    if (w != 2048 || h != 2048) return;

    // Scale 2 px per 1024 units at width 2048 is 2 px/unit. The box centre (512, 512) is at pixel (1024, 1024); the floor
    // point (100, 100) is at (200, 1848). Both are far from the box edges, so no edge darkening applies.
    const std::array<int, 3> top = pixel(rgba, w, 1024, 1024);
    const std::array<int, 3> floorPx = pixel(rgba, w, 200, 1848);
    CHECK(lum(top) > lum(floorPx));
    CHECK(lum(floorPx) > 0);
}

TEST(overview_spawn_pixels_have_team_colour_and_yaw) {
    // Scale 0.5 px per unit at width 512 over 1024 units. Triangle radius is 40 units = 20 px.
    const std::string floor = floorBrush();
    const std::optional<MapFile> map = makeMap(
        {floor},
        {pointEntity("mp_tdm_spawn_allies_start", "200 200 0", "0 0 0"),
         pointEntity("mp_tdm_spawn_axis_start", "800 800 0", "0 180 0"),
         pointEntity("mp_dm_spawn", "500 500 0", "0 0 0")});
    CHECK(map.has_value());
    if (!map) return;
    OverviewOptions opt;
    opt.width = 512;
    opt.drawObjectives = false;
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    std::string err;
    CHECK(renderOverview(*map, opt, rgba, w, h, &err));
    CHECK(w == 512 && h == 512);
    if (w != 512 || h != 512) return;

    // Allies at world (200, 200) -> pixel centre (100, 412), yaw 0 so the tip points to +X (right).
    CHECK(same(pixel(rgba, w, 100, 412), 50, 120, 255));
    CHECK(same(pixel(rgba, w, 112, 412), 50, 120, 255));  // on the tip side
    CHECK(!same(pixel(rgba, w, 100, 400), 50, 120, 255));  // off the axis, beside the body

    // Axis at world (800, 800) -> pixel (400, 112), yaw 180 so the tip points to -X (left).
    CHECK(same(pixel(rgba, w, 400, 112), 235, 50, 45));
    CHECK(same(pixel(rgba, w, 385, 112), 235, 50, 45));  // on the tip side (left of the centre)
    CHECK(!same(pixel(rgba, w, 415, 112), 235, 50, 45));  // behind the centre, outside the triangle

    // FFA spawn (no team) at world (500, 500) -> pixel (250, 262).
    CHECK(same(pixel(rgba, w, 250, 262), 60, 210, 80));
}

TEST(overview_tool_faces_do_not_draw) {
    // A caulk box raised to z = 128 over a non-tool floor. Its top is a tool face and must not show, so its centre
    // pixel is the flat floor colour, the same as a far floor pixel.
    const std::string caulkBox = brushText(AABB{{256, 256, 0}, {768, 768, 128}}, kCaulkSuffix);
    const std::optional<MapFile> map = makeMap({floorBrush(), caulkBox}, {});
    CHECK(map.has_value());
    if (!map) return;
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    std::string err;
    CHECK(renderOverview(*map, OverviewOptions{}, rgba, w, h, &err));
    if (w != 2048) return;
    CHECK(pixel(rgba, w, 1024, 1024) == pixel(rgba, w, 200, 1848));
}

TEST(overview_aspect_follows_bounds) {
    // Fit mode: width is the requested width, height follows the bounds (2048 x 512 units -> 512 x 128 px).
    const std::string wide = brushText(AABB{{0, 0, -64}, {2048, 512, 0}}, kFloorSuffix);
    const std::optional<MapFile> map = makeMap({wide}, {});
    CHECK(map.has_value());
    if (!map) return;
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    std::string err;
    OverviewOptions opt;
    opt.width = 512;
    CHECK(renderOverview(*map, opt, rgba, w, h, &err));
    CHECK(w == 512 && h == 128);
    CHECK(rgba.size() == size_t(w) * size_t(h) * 4);

    // Fixed scale: 8 units per pixel gives 256 x 64.
    opt.unitsPerPixel = 8;
    CHECK(renderOverview(*map, opt, rgba, w, h, &err));
    CHECK(w == 256 && h == 64);

    // Tall bounds (300 x 900 units) at width 256: 256 x 768 px.
    const std::string tall = brushText(AABB{{0, 0, -64}, {300, 900, 0}}, kFloorSuffix);
    const std::optional<MapFile> tallMap = makeMap({tall}, {});
    CHECK(tallMap.has_value());
    if (!tallMap) return;
    opt = OverviewOptions{};
    opt.width = 256;
    CHECK(renderOverview(*tallMap, opt, rgba, w, h, &err));
    CHECK(w == 256 && h == 768);
}

TEST(overview_objective_marker_is_drawn) {
    // A Domination flag (script_gameobjectname "dom onslaught", label _a) at world (300, 300) -> pixel (150, 362).
    // The disc has radius 6 px at width 512 and the letter A sits in its middle, so (154, 362) is disc colour.
    const std::string flag = pointEntity("trigger_radius", "300 300 32", "", "\"script_gameobjectname\" \"dom onslaught\"\n\"script_label\" \"_a\"\n");
    const std::optional<MapFile> map = makeMap({floorBrush()}, {flag});
    CHECK(map.has_value());
    if (!map) return;
    OverviewOptions opt;
    opt.width = 512;
    opt.drawSpawns = false;
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    std::string err;
    CHECK(renderOverview(*map, opt, rgba, w, h, &err));
    if (w != 512) return;
    CHECK(same(pixel(rgba, w, 154, 362), 255, 210, 40));
}

TEST(overview_grid_and_labels_draw) {
    const std::optional<MapFile> map = makeMap({floorBrush()}, {});
    CHECK(map.has_value());
    if (!map) return;
    OverviewOptions opt;
    opt.width = 512;
    opt.drawGrid = true;
    opt.gridStep = 256;  // lines at world x = 0, 256, 512, ... -> columns 0, 128, 256, ...
    opt.labels.push_back({"A", AABB{{100, 100, 0}, {100, 100, 0}}});
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    std::string err;
    CHECK(renderOverview(*map, opt, rgba, w, h, &err));
    CHECK(w == 512 && h == 512);
    if (w != 512) return;
    // Grid column 128 is lighter than the neighbouring floor column 129 at row 200 (no horizontal line there).
    CHECK(lum(pixel(rgba, w, 128, 200)) > lum(pixel(rgba, w, 129, 200)));
    // The label A is centred on world (100, 100) -> pixel (50, 462); its middle row is fully set, so the centre is white.
    CHECK(same(pixel(rgba, w, 50, 462), 255, 255, 255));
}
