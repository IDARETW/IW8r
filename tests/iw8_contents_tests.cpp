// IW8 contents and surface types (src/core/iw8_contents.cpp): tool-material contents, the contents directives,
// IW3 surface type names and the surfaceFlags / surfaceTypeBits encodings, and the converter's ConvertContents.
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "brush_ops.h"
#include "iw8_contents.h"
#include "map_text.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

// Brush text (face lines plus braces, LF terminators) for a box with `mat` on every face.
// `directive` (e.g. "contents detail;") is written as the first body line.
std::string brushText(const AABB& box, const std::string& mat, const std::string& directive = "") {
    auto prim = makeBrushPrimitive(makeBox(box, " " + mat + " 128 128 0 0 0 0"), "\n");
    if (!prim) return {};
    if (!directive.empty()) prim->lines.insert(prim->lines.begin() + 1, TextLine{" " + directive, "\n"});
    return prim->text();
}

// Parses a one-entity map holding `brush` and returns its first brush primitive.
Primitive brushOf(const std::string& brush) {
    const std::string text = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n" + brush + "}\n";
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return {};
    for (const EntityItem& item : pr.map->entities[0].items)
        if (const Primitive* p = item.prim()) return *p;
    CHECK(false);
    return {};
}

const AABB kBox{{0, 0, 0}, {64, 64, 64}};

}  // namespace

// Every tool name the task lists, plus the converter bits it must produce.
TEST(iw8_contents_tool_materials) {
    struct Case {
        const char* mat;
        uint32_t contents;
        bool collides;
        bool rendered;
    };
    const Case cases[] = {
        {"clip", 0x1, true, false},
        {"clip_player", 0x10000, true, false},
        {"clip_weapon", 0x400, true, false},
        {"clip_nosight", 0x1000, true, false},
        {"clip_missile", 0x0, false, false},
        {"ladder", 0x1, true, false},
        {"water", 0x20, true, true},  // water bit, not solid (research 62)
        {"nodraw", 0x1, true, false},
        {"caulk", 0x1, true, false},
        {"trigger", kIw3TriggerContents, false, false},
        {"sky", 0x1, true, false},
        {"portal", 0x0, false, false},
        {"hint", 0x0, false, false},
        {"skip", 0x0, false, false},
        {"origin", 0x0, false, false},
    };
    for (const Case& c : cases) {
        const BrushContents bc = brushContents(brushOf(brushText(kBox, c.mat)));
        CHECK(bc.contents == c.contents);
        CHECK(bc.collides == c.collides);
        CHECK(bc.rendered == c.rendered);
        CHECK(!bc.reason.empty());
    }
}

// Match is on the last path component, case-insensitively, and whole names only.
TEST(iw8_contents_name_matching) {
    CHECK(brushContents(brushOf(brushText(kBox, "tm2co/clip_player"))).contents == 0x10000);
    CHECK(brushContents(brushOf(brushText(kBox, "TM2CO/CLIP_PLAYER"))).contents == 0x10000);
    CHECK(brushContents(brushOf(brushText(kBox, "tm2co/me_clipboard_01"))).rendered);      // not a tool
    CHECK(brushContents(brushOf(brushText(kBox, "tm2co/me_clipboard_01"))).collides);
    CHECK(brushContents(brushOf(brushText(kBox, "tm2co/me_curb_stone_01"))).contents == 0x1);
    CHECK(brushContents(brushOf(brushText(kBox, "tm2co/me_curb_stone_01"))).rendered);
    CHECK(brushContents(brushOf(brushText(kBox, "tm2co/me_trim_hint_glass"))).collides);  // not a whole hint name
}

// `contents detail;` keeps the brush solid and drawn; brushIsDetail reports it; it does not change the bits.
TEST(iw8_contents_detail_directive) {
    const Primitive detail = brushOf(brushText(kBox, "tm2co/me_plaster_barrier_fence_wall", "contents detail;"));
    const Primitive plain = brushOf(brushText(kBox, "tm2co/me_plaster_barrier_fence_wall"));
    CHECK(brushIsDetail(detail));
    CHECK(!brushIsDetail(plain));
    const BrushContents bc = brushContents(detail);
    CHECK(bc.collides);
    CHECK(bc.rendered);
    CHECK(bc.contents == 0x1);
    CHECK(bc.reason.find("detail") != std::string::npos);
}

// `contents nonColliding;` removes collision; `contents weaponClip;` adds the item clip bit.
TEST(iw8_contents_other_directives) {
    const BrushContents nc = brushContents(brushOf(brushText(kBox, "tm2co/me_plaster", "contents nonColliding;")));
    CHECK(!nc.collides);
    CHECK(nc.contents == 0);
    CHECK(nc.rendered);

    const BrushContents wc = brushContents(brushOf(brushText(kBox, "tm2co/me_plaster", "contents weaponClip;")));
    CHECK(wc.collides);
    CHECK(wc.contents == (0x1 | 0x400));

    const BrushContents unknown = brushContents(brushOf(brushText(kBox, "tm2co/me_plaster", "contents zzz;")));
    CHECK(unknown.collides);
    CHECK(unknown.contents == 0x1);
    CHECK(unknown.reason.find("unknown") != std::string::npos);
}

// Surface names from the task: concrete, wood, metal, glass; plus the keyword rules.
TEST(iw8_surface_type_names) {
    CHECK(iw3SurfaceTypeForMaterial("tm2co/ee_trim_beam_airport_concrete") == 5);
    CHECK(iw3SurfaceTypeForMaterial("concrete") == 5);
    CHECK(iw3SurfaceTypeForMaterial("tm/wood_plank_01") == 21);
    CHECK(iw3SurfaceTypeForMaterial("tm/me_metal_pipe_02") == 13);
    CHECK(iw3SurfaceTypeForMaterial("tm/glass_clear_unbreakable") == 9);
    CHECK(iw3SurfaceTypeForMaterial("tm2co/me_curb_stone_01") == 17);            // stone -> rock
    CHECK(iw3SurfaceTypeForMaterial("tm2co/me_plaster_barrier_fence_wall") == 16);
    CHECK(iw3SurfaceTypeForMaterial("tm2co/destruction_debris_plaster_concrete_edge_01") == 5);  // concrete wins
    CHECK(iw3SurfaceTypeForMaterial("tm/painted_metal_01") == 28);
    CHECK(iw3SurfaceTypeForMaterial("tm/ceramic_tile_02") == 23);
    CHECK(iw3SurfaceTypeForMaterial("tm/me_police_sign") == 0);                 // "ice" is not a token of "police"
    CHECK(iw3SurfaceTypeForMaterial("caulk") == 0);
    CHECK(iw3SurfaceTypeForMaterial("") == 0);

    // Every type name maps back to its own type.
    for (int t = 1; t <= 28; ++t) {
        const std::string name = iw3SurfaceTypeName(t);
        CHECK(!name.empty());
        CHECK(iw3SurfaceTypeForMaterial(name) == t);
    }
    CHECK(std::string(iw3SurfaceTypeName(0)) == "default");
    CHECK(std::string(iw3SurfaceTypeName(29)).empty());
}

// surfaceFlags carry the type in bits 20..24; surfaceTypeBits carry 1 << (type - 1); the converter maps through
// NativeSurfaceMaterial (x64-zt:6949-6986).
TEST(iw8_surface_flags_and_bits) {
    CHECK(iw3SurfaceFlagsForType(5) == 0x500000u);
    CHECK(((iw3SurfaceFlagsForType(28) >> 20) & 31u) == 28u);
    CHECK(iw3SurfaceFlagsForType(0) == 0u);
    CHECK(iw3SurfaceFlagsForType(29) == 0u);
    CHECK(iw3SurfaceTypeBits(1) == 1u);
    CHECK(iw3SurfaceTypeBits(5) == 16u);
    CHECK(iw3SurfaceTypeBits(14) == 8192u);   // mud, x64-zt:6969
    CHECK(iw3SurfaceTypeBits(28) == 134217728u);
    CHECK(iw3SurfaceTypeBits(0) == 0u);

    CHECK(replaySurfaceTypeForIw3(1) == 1);     // bark
    CHECK(replaySurfaceTypeForIw3(5) == 5);     // concrete
    CHECK(replaySurfaceTypeForIw3(14) == 15);   // mud
    CHECK(replaySurfaceTypeForIw3(17) == 18);   // rock
    CHECK(replaySurfaceTypeForIw3(25) == 26);   // rubber
    CHECK(replaySurfaceTypeForIw3(26) == 35);   // cushion
    CHECK(replaySurfaceTypeForIw3(28) == 28);   // painted metal
    CHECK(replaySurfaceTypeForIw3(0) == 0);
    // NativeSurfaceMaterial takes the single bit, so the two helpers agree.
    for (int t = 1; t <= 28; ++t) CHECK(replaySurfaceTypeForIw3(t) != 0);
}

// ConvertContents (x64-zt:6927-6947).
TEST(iw8_convert_contents_matches_converter) {
    CHECK(iw3ConvertContents(0x10) == 0x1);                      // glass becomes solid
    CHECK(iw3ConvertContents(0x1) == 0x1);
    CHECK(iw3ConvertContents(0x10000) == 0x10000);               // non-solid player clip stays a player clip
    CHECK(iw3ConvertContents(0x10000 | 0x1000) == 0x10000);      // 0x1000 is outside the 0x30202 clamp
    CHECK(iw3ConvertContents(0x40) == 0x0);                      // shoot-through bit is masked off
    CHECK(iw3ConvertContents(0x20) == 0x20);                     // water volume kept (fork 0639335b)
    CHECK(iw3ConvertContents(0x800) == 0x0);                     // sky is dropped
    CHECK(iw3ConvertContents(0x1 | 0x10000) == 0x10001);         // solid: the 0x30202 clamp does not apply
    CHECK(iw3ConvertContents(0x2) == 0x2);                       // foliage kept
}

// The shipped sample room: brush 1 is `contents detail;` with the fence material (plaster).
TEST(iw8_sample_room_contents) {
    fs::path p = fs::current_path();
    std::string sample;
    for (int up = 0; up < 6 && sample.empty(); ++up, p = p.parent_path())
        if (fs::exists(p / "tests" / "data" / "iw8_sample_room.map")) sample = (p / "tests" / "data" / "iw8_sample_room.map").string();
    CHECK(!sample.empty());
    if (sample.empty()) return;
    std::string text;
    CHECK(readFile(sample, text));
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const Entity& world = pr.map->entities[0];
    int brushIndex = 0;
    for (const EntityItem& item : world.items) {
        const Primitive* p2 = item.prim();
        if (!p2) continue;
        if (brushIndex == 1) {
            CHECK(brushIsDetail(*p2));
            CHECK(brushContents(*p2).collides);
            CHECK(iw3SurfaceTypeForMaterial(p2->faces[0].material()) == 16);  // plaster
        }
        if (brushIndex == 5) CHECK(!brushIsDetail(*p2));  // pillar
        ++brushIndex;
    }
    CHECK(brushIndex >= 6);
}

// Owner playtest 2026-11-11: the last material word of a stock name decides the surface.
TEST(iw8_surface_type_last_word_wins) {
    CHECK(iw3SurfaceTypeForMaterial("mo/ee_food_fruit_cardboard_box_01") == 15);     // paper, not fruit
    CHECK(iw3SurfaceTypeForMaterial("tm/container_barrel_plastic_01_closed") == 24); // plastic
    CHECK(iw3SurfaceTypeForMaterial("tm/building_horse_stall_wood_beam_128_01") == 21);
    CHECK(iw3SurfaceTypeForMaterial("mo/fruit_apple_01") == 27);
    CHECK(iw3SurfaceTypeForMaterial("tm2co/wall_painted_metal_01") == 28);
}

TEST(iw8_surface_type_from_stock_flags) {
    CHECK(iw3SurfaceTypeForStockSurfaceFlags(13107200) == 24);  // mo/me_traffic_cone_01_top: Replay 25, plastic
    CHECK(iw3SurfaceTypeForStockSurfaceFlags(9ll << 19) == 9);  // glass
    CHECK(iw3SurfaceTypeForStockSurfaceFlags(-1) == 0 && iw3SurfaceTypeForStockSurfaceFlags(0) == 0);
}
