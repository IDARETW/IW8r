// Minimap render and DDS writer tests (src/core/minimap.cpp). Frame and mapping: see minimap.h.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "brush_ops.h"
#include "map_text.h"
#include "minimap.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

const char* kFloorSuffix = " tm2co/me_curb_stone_01 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0";
const char* kWallSuffix = " tm2co/me_plaster_barrier_fence_wall 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0";
const char* kCaulkSuffix = " caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0";

std::string sampleRoomPath() {
    fs::path p = fs::current_path();
    for (int i = 0; i < 6; ++i) {
        const fs::path candidate = p / "tests" / "data" / "iw8_sample_room.map";
        if (fs::exists(candidate)) return candidate.string();
        if (!p.has_parent_path() || p.parent_path() == p) break;
        p = p.parent_path();
    }
    return {};
}

std::optional<MapFile> parseText(const std::string& text) {
    Id next = 1;
    ParseResult result = parseMap(text, next);
    return result.map;
}

// A map of one worldspawn holding one brush per box (made by makeBox with the matching suffix).
std::optional<MapFile> mapFromBoxes(const std::vector<AABB>& boxes, const std::vector<std::string>& suffixes) {
    std::string text = "iwmap 4\n// entity 0\n{\n\"classname\" \"worldspawn\"\n";
    for (size_t i = 0; i < boxes.size(); ++i) {
        const std::optional<Primitive> brush = makeBrushPrimitive(makeBox(boxes[i], suffixes[i]), "\n");
        if (!brush) return std::nullopt;
        text += brush->text();
    }
    text += "}\n";
    return parseText(text);
}

struct Px {
    int r = 0, g = 0, b = 0, a = 0;
    int lum() const { return r + g + b; }
};

// Pixel of a world point, from the mapping in minimap.h (source frame: column from Y, row from X).
bool pixelOf(const MinimapImage& img, double x, double y, int& px, int& py) {
    const double span = img.worldRect.maxs.x - img.worldRect.mins.x;
    px = static_cast<int>(std::floor((img.worldRect.maxs.y - y) / span * img.width));
    py = static_cast<int>(std::floor((img.worldRect.maxs.x - x) / span * img.height));
    return px >= 0 && py >= 0 && px < img.width && py < img.height;
}

Px pixelAt(const MinimapImage& img, int px, int py) {
    const size_t i = (size_t(py) * size_t(img.width) + size_t(px)) * 4;
    return {img.rgba[i], img.rgba[i + 1], img.rgba[i + 2], img.rgba[i + 3]};
}

Px pixelWorld(const MinimapImage& img, double x, double y) {
    int px = 0, py = 0;
    if (!pixelOf(img, x, y, px, py)) return {};
    return pixelAt(img, px, py);
}

uint32_t readU32(const std::vector<uint8_t>& bytes, size_t offset) {
    uint32_t v = 0;
    for (size_t i = 0; i < 4; ++i) v |= uint32_t(bytes[offset + i]) << (8 * i);
    return v;
}

}  // namespace

// The room of tests/data/iw8_sample_room.map: floor top z=0, walls to z=128 on +-256..272, a 64x64 pillar to z=96
// at the origin and a box to z=64 at x -96..-32, y 64..128.
TEST(minimap_sample_room_floor_and_walls) {
    const std::string path = sampleRoomPath();
    CHECK(!path.empty());
    if (path.empty()) return;
    std::string text;
    CHECK(readFile(path, text));
    const std::optional<MapFile> map = parseText(text);
    CHECK(map.has_value());
    if (!map) return;

    const MinimapImage img = renderMinimap(*map, 512);
    CHECK(img.width == 512 && img.height == 512);
    CHECK(img.rgba.size() == size_t(512) * 512 * 4);
    CHECK(std::abs(img.worldRect.mins.x + 272.0) < 1e-6 && std::abs(img.worldRect.maxs.x - 272.0) < 1e-6);
    CHECK(std::abs(img.worldRect.mins.y + 272.0) < 1e-6 && std::abs(img.worldRect.maxs.y - 272.0) < 1e-6);

    size_t opaque = 0;
    for (size_t i = 3; i < img.rgba.size(); i += 4) opaque += img.rgba[i] == 255 ? 1 : 0;
    CHECK(opaque > size_t(512) * 512 / 2);

    const Px floorA = pixelWorld(img, 128, 128);
    const Px floorB = pixelWorld(img, -128, -128);
    CHECK(floorA.a == 255);
    CHECK(floorA.r == floorB.r && floorA.g == floorB.g && floorA.b == floorB.b);
    CHECK(floorA.r > floorA.b);  // warm albedo

    const Px pillarTop = pixelWorld(img, 0, 0);  // top at z=96
    const Px wallCap = pixelWorld(img, -264, 0);  // wall top at z=128
    CHECK(pillarTop.a == 255 && wallCap.a == 255);
    CHECK(floorA.lum() < pillarTop.lum());
    CHECK(pillarTop.lum() < wallCap.lum());

    // The four image corners lie outside the room walls: transparent.
    CHECK(pixelAt(img, 0, 0).a == 0);
    CHECK(pixelAt(img, 511, 0).a == 0);
    CHECK(pixelAt(img, 0, 511).a == 0);
    CHECK(pixelAt(img, 511, 511).a == 0);
}

// A brush at +X is drawn on the top side of the converter's source frame (the converter then turns it 90 degrees
// clockwise, so it ends on the right in game); a brush at +Y is drawn on the left. The map is an east box at
// x 100..200, y -20..20 and a north box at x -20..20, y 100..200. The covered region is x,y in -20..200.
TEST(minimap_orientation_east_is_top_north_is_left) {
    const std::optional<MapFile> map = mapFromBoxes(
        {AABB{{100, -20, 0}, {200, 20, 16}}, AABB{{-20, 100, 0}, {20, 200, 16}}}, {kWallSuffix, kWallSuffix});
    CHECK(map.has_value());
    if (!map) return;
    const MinimapImage img = renderMinimap(*map, 512);
    CHECK(std::abs(img.worldRect.mins.x + 20.0) < 1e-6 && std::abs(img.worldRect.maxs.x - 200.0) < 1e-6);
    CHECK(std::abs(img.worldRect.mins.y + 20.0) < 1e-6 && std::abs(img.worldRect.maxs.y - 200.0) < 1e-6);

    int px = 0, py = 0;
    CHECK(pixelOf(img, 150, 0, px, py));
    CHECK(pixelAt(img, px, py).a == 255);  // inside the east box
    CHECK(py < img.height / 2);            // +X is the top half
    CHECK(px > img.width / 2);             // y = 0 sits on the right half (-Y side is right)

    CHECK(pixelOf(img, 150, 150, px, py));
    CHECK(pixelAt(img, px, py).a == 0);    // north-east corner is empty

    CHECK(pixelOf(img, 0, 150, px, py));
    CHECK(pixelAt(img, px, py).a == 255);  // inside the north box
    CHECK(px < img.width / 2);             // +Y is the left half
    CHECK(py > img.height / 2);            // x = 0 sits in the lower half of the source frame

    CHECK(pixelOf(img, 0, 0, px, py));
    CHECK(pixelAt(img, px, py).a == 0);    // between the boxes
}

TEST(minimap_tool_materials_are_not_drawn) {
    const std::optional<MapFile> map =
        mapFromBoxes({AABB{{-50, -50, 0}, {50, 50, 32}}, AABB{{60, 60, 0}, {80, 80, 16}}}, {kCaulkSuffix, kWallSuffix});
    CHECK(map.has_value());
    if (!map) return;
    const MinimapImage img = renderMinimap(*map, 64);
    // Only the wall box (x 60..80) is drawn, so the covered region starts at its edge, not at the caulk box (-50).
    CHECK(std::abs(img.worldRect.mins.x - 60.0) < 1e-6);
    CHECK(pixelWorld(img, 70, 70).a == 255);
    CHECK(pixelWorld(img, 0, 0).a == 0);
}

TEST(minimap_empty_map_is_transparent) {
    const std::optional<MapFile> map = parseText("iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n");
    CHECK(map.has_value());
    if (!map) return;
    const MinimapImage img = renderMinimap(*map, 32);
    CHECK(img.width == 32 && img.height == 32);
    bool any = false;
    for (uint8_t v : img.rgba) any = any || v != 0;
    CHECK(!any);
}

// Tiny BC3 decoder for the round trip (the palettes and bit order of the BC3 format).
void decodeBc3Level(const std::vector<uint8_t>& bytes, size_t offset, int w, int h, std::vector<Px>& out) {
    const int bw = std::max(1, (w + 3) / 4);
    out.assign(size_t(w) * size_t(h), Px{});
    for (int by = 0; by < (h + 3) / 4; ++by)
        for (int bx = 0; bx < bw; ++bx) {
            const uint8_t* b = &bytes[offset + (size_t(by) * size_t(bw) + size_t(bx)) * 16];
            int alphaPal[8];
            alphaPal[0] = b[0];
            alphaPal[1] = b[1];
            for (int k = 2; k < 8; ++k) alphaPal[k] = ((8 - k) * b[0] + (k - 1) * b[1] + 3) / 7;
            uint64_t alphaBits = 0;
            for (int i = 0; i < 6; ++i) alphaBits |= uint64_t(b[2 + i]) << (8 * i);
            const int c0 = b[8] | (b[9] << 8), c1 = b[10] | (b[11] << 8);
            const auto expand = [](int c) {
                const int r5 = (c >> 11) & 31, g6 = (c >> 5) & 63, b5 = c & 31;
                return std::array<int, 3>{(r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2)};
            };
            const auto e0 = expand(c0), e1 = expand(c1);
            std::array<int, 3> pal[4];
            pal[0] = e0;
            pal[1] = e1;
            for (int c = 0; c < 3; ++c) {
                pal[2][c] = (2 * e0[c] + e1[c]) / 3;
                pal[3][c] = (e0[c] + 2 * e1[c]) / 3;
            }
            uint32_t colourBits = uint32_t(b[12]) | (uint32_t(b[13]) << 8) | (uint32_t(b[14]) << 16) | (uint32_t(b[15]) << 24);
            for (int j = 0; j < 4; ++j)
                for (int i = 0; i < 4; ++i) {
                    const int x = bx * 4 + i, y = by * 4 + j;
                    const int t = j * 4 + i;
                    if (x >= w || y >= h) continue;
                    const int ai = int((alphaBits >> (3 * t)) & 7);
                    const int ci = int((colourBits >> (2 * t)) & 3);
                    out[size_t(y) * size_t(w) + size_t(x)] = {pal[ci][0], pal[ci][1], pal[ci][2], alphaPal[ai]};
                }
        }
}

TEST(minimap_dds_bc3_header_mips_and_roundtrip) {
    const std::string path = sampleRoomPath();
    CHECK(!path.empty());
    if (path.empty()) return;
    std::string text;
    CHECK(readFile(path, text));
    const std::optional<MapFile> map = parseText(text);
    CHECK(map.has_value());
    if (!map) return;
    const MinimapImage img = renderMinimap(*map, 512);

    const fs::path out = fs::temp_directory_path() / "iw8r_minimap_test.dds";
    std::string err;
    CHECK(writeMinimapDds(img, out.string(), &err));
    std::vector<uint8_t> bytes;
    {
        std::ifstream in(out, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    CHECK(bytes.size() >= 128);
    if (bytes.size() < 128) return;
    CHECK(std::memcmp(bytes.data(), "DDS ", 4) == 0);
    CHECK(readU32(bytes, 4) == 124);                 // header size
    CHECK(readU32(bytes, 8) & 0x80000);              // DDSD_LINEARSIZE
    CHECK(readU32(bytes, 12) == 512 && readU32(bytes, 16) == 512);
    CHECK(readU32(bytes, 20) == 512 / 4 * 512 / 4 * 16);  // top level: 16 bytes per 4x4 block
    CHECK(readU32(bytes, 28) == 10);                 // 512 down to 1x1
    CHECK(readU32(bytes, 76) == 32);                 // pixel format size
    CHECK(readU32(bytes, 80) == 0x4);                // DDPF_FOURCC only
    CHECK(readU32(bytes, 84) == 0x35545844);         // 'DXT5'
    CHECK((readU32(bytes, 108) & 0x400000) != 0);    // DDSCAPS_MIPMAP
    // Exact size: header plus one 16-byte block per 4x4 texels of each level, 1x1..512x512 (padded below 4x4).
    size_t expected = 128;
    for (int s = 512; s >= 1; s >>= 1) expected += size_t(std::max(1, (s + 3) / 4)) * size_t(std::max(1, (s + 3) / 4)) * 16;
    CHECK(expected == 349680);
    CHECK(bytes.size() == expected);

    // Round trip: the decoded top level matches the rendered image. Alpha is exact (the image holds only 0 and 255),
    // and the floor centre colour survives BC3 within a tolerance.
    std::vector<Px> decoded;
    decodeBc3Level(bytes, 128, 512, 512, decoded);
    size_t alphaMismatch = 0;
    for (size_t i = 0; i < decoded.size(); ++i)
        if (decoded[i].a != img.rgba[i * 4 + 3]) ++alphaMismatch;
    CHECK(alphaMismatch == 0);
    const int tolerance = 12;
    int px = 0, py = 0;
    CHECK(pixelOf(img, 128, 128, px, py));
    const Px want = pixelAt(img, px, py);
    const Px got = decoded[size_t(py) * 512 + size_t(px)];
    CHECK(want.a == 255 && got.a == 255);
    CHECK(std::abs(got.r - want.r) <= tolerance);
    CHECK(std::abs(got.g - want.g) <= tolerance);
    CHECK(std::abs(got.b - want.b) <= tolerance);
    // Transparent background stays transparent after the round trip.
    CHECK(decoded[0].a == 0 && decoded[511].a == 0 && decoded[size_t(511) * 512].a == 0);
    fs::remove(out);

    // A wrong buffer or a non power-of-two side is refused.
    MinimapImage bad = img;
    bad.width = bad.height = 3;
    CHECK(!writeMinimapDds(bad, out.string(), &err));
    CHECK(!err.empty());
    MinimapImage shortBuffer = img;
    shortBuffer.rgba.resize(10);
    CHECK(!writeMinimapDds(shortBuffer, out.string(), &err));
}

TEST(minimap_size_is_clamped_to_power_of_two) {
    const std::optional<MapFile> map = parseText("iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n");
    CHECK(map.has_value());
    if (!map) return;
    CHECK(renderMinimap(*map, 100).width == 128);
    CHECK(renderMinimap(*map, 1).width == 4);
    CHECK(renderMinimap(*map, 64).height == 64);
}
