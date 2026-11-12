#include "collision_layout.h"
#include "test_harness.h"

using namespace iw8r;

namespace {
AABB box(double x0, double y0, double z0, double x1, double y1, double z1) {
    AABB b;
    b.add(Vec3{x0, y0, z0});
    b.add(Vec3{x1, y1, z1});
    return b;
}
}  // namespace

// 14.1 (research 90): one-piece collision or the game's streamed tiles.
TEST(collision_layout_parse_and_choice) {
    bool known = false;
    CHECK(parseCollisionLayout("", &known) == CollisionLayout::Auto && known);
    CHECK(parseCollisionLayout(" Auto ", &known) == CollisionLayout::Auto && known);
    CHECK(parseCollisionLayout("one", &known) == CollisionLayout::OnePiece && known);
    CHECK(parseCollisionLayout("FULL", &known) == CollisionLayout::OnePiece && known);
    CHECK(parseCollisionLayout("tiles", &known) == CollisionLayout::Tiles && known);
    CHECK(parseCollisionLayout("Streamed", &known) == CollisionLayout::Tiles && known);
    CHECK(parseCollisionLayout("maybe", &known) == CollisionLayout::Auto && !known);
    CHECK(std::string(collisionLayoutName(CollisionLayout::Tiles)) == "tiles" && std::string(collisionLayoutName(CollisionLayout::OnePiece)) == "one" &&
          std::string(collisionLayoutName(CollisionLayout::Auto)) == "auto");

    const AABB small = box(-2048, -2048, 0, 2048, 2048, 512);
    const AABB big = box(-30000, -8000, 0, 30000, 8000, 2000);
    // Auto: a small map stays in one piece without a word; a big one gets tiles with a note.
    CollisionLayoutChoice c = chooseCollisionLayout(CollisionLayout::Auto, small);
    CHECK(!c.tiles && c.notes.empty());
    c = chooseCollisionLayout(CollisionLayout::Auto, big);
    CHECK(c.tiles && c.notes.size() == 1 && c.notes[0].find("automatic") != std::string::npos && c.notes[0].find("60000 x 16000") != std::string::npos);
    // 60000 x 16000 around the origin touches 8 x 2 tiles.
    CHECK(c.notes[0].find("up to 16 of the 1,024 tiles") != std::string::npos);
    // Forced either way.
    c = chooseCollisionLayout(CollisionLayout::Tiles, small);
    CHECK(c.tiles && c.notes.size() == 1 && c.notes[0].find("asked for") != std::string::npos && c.notes[0].find("up to 4 of") != std::string::npos);
    c = chooseCollisionLayout(CollisionLayout::OnePiece, big);
    CHECK(!c.tiles && c.notes.empty());
    // Outside the grid: said so. No geometry: one piece.
    c = chooseCollisionLayout(CollisionLayout::Tiles, box(100000, 0, 0, 140000, 4000, 100));
    CHECK(c.tiles && c.notes.size() == 2 && c.notes[1].find("outside the tile grid") != std::string::npos);
    c = chooseCollisionLayout(CollisionLayout::Tiles, AABB{});
    CHECK(!c.tiles && c.notes.size() == 1);
    CHECK(!chooseCollisionLayout(CollisionLayout::Auto, AABB{}).tiles);
}

TEST(collision_layout_grid_and_names) {
    // The game's grid: 32 x 32 tiles of 8192 units around the origin; index = gridY * 32 + gridX. Stock tile 692 of
    // mp_br_quarry has grid (20, 21) and its centre at (36864, 45056).
    CHECK(kCollisionTileCount == 1024);
    CHECK(collisionTileOf(36864, 45056) == 692);
    CHECK(collisionTileOf(-131072, -131072) == 0 && collisionTileOf(131071, 131071) == 1023);
    CHECK(collisionTileOf(-1, -1) == 15 * 32 + 15 && collisionTileOf(0, 0) == 16 * 32 + 16 && collisionTileOf(-1, 1) == 16 * 32 + 15);
    CHECK(collisionTileOf(131072, 0) == -1 && collisionTileOf(0, -131073) == -1);

    CHECK(collisionTileZoneName("mp_big", 7) == "mp_big_cg_00007_tr" && collisionTileZoneName("mp_big", 1023) == "mp_big_cg_01023_tr");
    CHECK(collisionPackageZoneName("mp_big") == "mp_big_cg_ls_tr");
    const std::vector<std::string> names = collisionTileZoneNames("mp_big");
    CHECK(names.size() == 1025 && names.front() == "mp_big_cg_00000_tr" && names[1023] == "mp_big_cg_01023_tr" && names.back() == "mp_big_cg_ls_tr");
}

TEST(collision_layout_acts_streaming_info_exception) {
    const std::string passed = "{\"type\":\"col_map\",\"status\":\"passed\",\"name\":\"a\"}\n";
    const std::string streaming =
        "{\"type\":\"streaminginfo\",\"type_id\":67,\"status\":\"failed\",\"name\":\"mp_big\",\"field_issues\":[{\"path\":\"fields/x\","
        "\"reason\":\"Missing count context: TransientCosts\"}]}\n";
    const std::string other = "{\"type\":\"material\",\"status\":\"failed\",\"name\":\"m\",\"field_issues\":[{\"reason\":\"bad\"}]}\n";
    const std::string otherReason = "{\"type\":\"streaminginfo\",\"status\":\"failed\",\"name\":\"mp_big\",\"field_issues\":[{\"reason\":\"Truncated\"}]}\n";
    CHECK(actsOnlyStreamingInfoFailed(passed + streaming + passed));
    CHECK(!actsOnlyStreamingInfoFailed(passed + passed));          // nothing failed: not this case
    CHECK(!actsOnlyStreamingInfoFailed(passed + streaming + other));  // a real failure stays a failure
    CHECK(!actsOnlyStreamingInfoFailed(otherReason));
    CHECK(!actsOnlyStreamingInfoFailed(""));
}
