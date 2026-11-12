import io, sys
root = sys.argv[1]

def patch(rel, pairs):
    p = root + '/' + rel
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (rel, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

patch('src/convert/iw3_iw8/convert.cpp', [
 # ---- the package may hold the collision tile zones
 ('''    if (actual != expectedFiles(map))
    {
        err("validate: output must contain exactly five map fastfiles and map.json "
            "(and optionally preview.rgba)");
        return false;
    }
''', '''    // A map with streamed collision (research 90) also has its 1,024 collision tile zones and the combined package:
    // all of them or none.
    std::vector<std::string> tileFiles;
    for (std::uint32_t tile = 0; tile < iw8::havok::kCollisionTileCount; ++tile)
        tileFiles.push_back(collisionTileZone(map, tile) + ".ff");
    tileFiles.push_back(collisionPackageZone(map) + ".ff");
    std::sort(tileFiles.begin(), tileFiles.end());
    std::vector<std::string> presentTiles;
    std::set_intersection(actual.begin(), actual.end(), tileFiles.begin(), tileFiles.end(), std::back_inserter(presentTiles));
    if (!presentTiles.empty())
    {
        if (presentTiles != tileFiles)
        {
            err("validate: the package holds %zu of the %zu collision tile zones; a tiled map has all of them",
                presentTiles.size(), tileFiles.size());
            return false;
        }
        std::vector<std::string> rest;
        std::set_difference(actual.begin(), actual.end(), tileFiles.begin(), tileFiles.end(), std::back_inserter(rest));
        actual = std::move(rest);
    }
    if (actual != expectedFiles(map))
    {
        err("validate: output must contain exactly five map fastfiles and map.json "
            "(and optionally preview.rgba, and the collision tile zones of a tiled map)");
        return false;
    }
'''),
 ('''std::vector<std::string> expectedFiles(const std::string &map)
{
''', '''// The zone of one streamed collision tile, and of the combined collision package (the names the game reads from the
// map's streaming info: <map>_cg_%05u_tr and <map>_cg_ls_tr).
std::string collisionTileZone(const std::string &map, const std::uint32_t tile)
{
    char number[16];
    std::snprintf(number, sizeof(number), "%05u", tile);
    return map + "_cg_" + number + "_tr";
}

std::string collisionPackageZone(const std::string &map)
{
    return map + "_cg_ls_tr";
}

std::vector<std::string> expectedFiles(const std::string &map)
{
'''),
 # ---- tile zones and the streaming info, before the main zone is written
 ('''        // Carried scriptable definitions join the zone here, and each instance record gets its definition.
        const ZonePrepare mapPrepare = carry.empty() ? ZonePrepare{} : [&](zonetool::iw8::loaded_zone &zone) {
            auto additions = NewAdditions(zone, carry.assets);
            std::size_t pointed = 0;
''', '''        // Streamed collision (research 90): the tile zones and the combined package are written first, because the
        // streaming info of the main zone states what each of them costs to load.
        struct TiledCollision
        {
            std::deque<std::string> names;
            std::vector<zonetool::iw8::CollisionTile> tiles;
            std::vector<zonetool::iw8::TransientFileReference> files;
            std::vector<zonetool::iw8::TransientAssetReference> assets;
            std::vector<std::uint16_t> residentPages, tempPages, contentCounts;
            std::vector<zonetool::iw8::StreamingItemContents> contents;
            zonetool::iw8::TransientInfo transient{};
            zonetool::iw8::StreamingInfo info{};
        } tiled;
        if (collision.tiled)
        {
            namespace zi = zonetool::iw8;
            const auto keep = [&](std::string text) -> const char * {
                tiled.names.push_back(std::move(text));
                return tiled.names.back().c_str();
            };
            const float gridMin = -iw8::havok::kCollisionTileSize * iw8::havok::kCollisionTileGrid * 0.5f;
            tiled.tiles.resize(iw8::havok::kCollisionTileCount);
            for (std::uint32_t index = 0; index < iw8::havok::kCollisionTileCount; ++index)
            {
                auto &tile = tiled.tiles[index];
                const auto gridX = index % iw8::havok::kCollisionTileGrid, gridY = index / iw8::havok::kCollisionTileGrid;
                tile.name = keep("collisiontile" + std::to_string(index));
                tile.index = index;
                tile.gridId[0] = static_cast<std::uint8_t>(gridX);
                tile.gridId[1] = static_cast<std::uint8_t>(gridY);
                // The centre of the tile's square.
                tile.origin.v[0] = gridMin + (static_cast<float>(gridX) + 0.5f) * iw8::havok::kCollisionTileSize;
                tile.origin.v[1] = gridMin + (static_cast<float>(gridY) + 0.5f) * iw8::havok::kCollisionTileSize;
                tile.origin.v[2] = 0.0f;
            }
            for (auto &shape : collision.tiles)
            {
                if (shape.index >= tiled.tiles.size() || shape.data.empty())
                    throw std::runtime_error("collision tile shape is invalid");
                auto &tile = tiled.tiles[shape.index];
                tile.havokTileShapeDataSize = static_cast<std::uint32_t>(shape.data.size());
                tile.havokTileShapeData = reinterpret_cast<char *>(shape.data.data());
                tile.worldCollisionShapeIdx = shape.slot;
            }
            const auto pages = [](const std::uint64_t bytes) {
                return static_cast<std::uint16_t>(std::clamp<std::uint64_t>((bytes + 0xFFFFu) / 0x10000u, 1, 0xFFFFu));
            };
            // One zone from tile structures: a collision zone has transient file type 3 and xfile version 0xFF3, as
            // the stock tiles. Returns its resident and temporary cost in 64 KiB pages (block 5; blocks 1 and 2).
            const auto writeTiles = [&](const std::string &zoneName, const std::span<const zi::zone_asset> list) {
                auto written = zi::write_zone(list, list, {}, {});
                zonetool::xfile::fastfile file{};
                file.payload = std::move(written.data);
                file.block_sizes = written.block_sizes;
                file.xfile_version = 0xFF3;
                file.transient = true;
                file.transient_type = 3;
                file.preload_walk_size = written.preload_walk_size;
                const std::pair<std::uint16_t, std::uint16_t> cost{pages(file.block_sizes.at(5)),
                                                                   pages(file.block_sizes.at(1) + file.block_sizes.at(2))};
                zonetool::xfile::write(path_join(outputDirectory, zoneName + ".ff"), zi::definition().format, file);
                return cost;
            };
            // The files of the streaming info: the map, every tile, the combined package.
            tiled.files.push_back({keep(map), 0});
            tiled.residentPages.push_back(0);
            tiled.tempPages.push_back(0);
            std::vector<zi::zone_asset> all;
            for (std::uint32_t index = 0; index < iw8::havok::kCollisionTileCount; ++index)
            {
                const zi::zone_asset asset{zi::ASSET_TYPE_COLLISION_TILE, &tiled.tiles[index], false};
                all.push_back(asset);
                const auto zoneName = collisionTileZone(map, index);
                const auto cost = writeTiles(zoneName, std::span<const zi::zone_asset>(&asset, 1));
                tiled.files.push_back({keep(zoneName), 3});
                tiled.residentPages.push_back(cost.first);
                tiled.tempPages.push_back(cost.second);
                tiled.assets.push_back({tiled.tiles[index].name, static_cast<std::uint8_t>(zi::ASSET_TYPE_COLLISION_TILE), 0xFFFFFFFFu});
            }
            const auto packageCost = writeTiles(collisionPackageZone(map), all);
            tiled.files.push_back({keep(collisionPackageZone(map)), 3});
            tiled.residentPages.push_back(packageCost.first);
            tiled.tempPages.push_back(packageCost.second);
            tiled.contentCounts.assign(tiled.files.size(), 0);
            tiled.contents.assign(tiled.files.size(), zi::StreamingItemContents{});

            auto &transient = tiled.transient;
            transient.transientFiles = tiled.files.data();
            transient.transientFileCount = static_cast<std::uint32_t>(tiled.files.size());
            transient.transientAssets = tiled.assets.data();
            transient.transientAssetCount = static_cast<std::uint32_t>(tiled.assets.size());
            transient.transientCollisionTileCount = iw8::havok::kCollisionTileCount;
            transient.transientCosts.streamContents.contentCounts = tiled.contentCounts.data();
            transient.transientCosts.streamContents.contents = tiled.contents.data();
            transient.transientCosts.residentPageSizes = tiled.residentPages.data();
            transient.transientCosts.tempPageSizes = tiled.tempPages.data();
            transient.transientCosts.transientCostCount = static_cast<std::uint32_t>(tiled.files.size());
            tiled.info.name = keep(map);
            tiled.info.transientInfo = &transient;
            info("collision tiles: wrote %u tile zones (%zu with collision) and %s (%u resident pages); the main zone "
                 "gets a streaming info with %zu files",
                 iw8::havok::kCollisionTileCount, collision.tiles.size(), collisionPackageZone(map).c_str(),
                 static_cast<unsigned>(packageCost.first), tiled.files.size());
            timing.lap("collision tile zones");
        }

        // Carried scriptable definitions join the zone here, and each instance record gets its definition.
        const ZonePrepare mapPrepare = carry.empty() && !collision.tiled ? ZonePrepare{} : [&](zonetool::iw8::loaded_zone &zone) {
            auto additions = NewAdditions(zone, carry.assets);
            if (collision.tiled)
                additions.push_back({zonetool::iw8::ASSET_TYPE_STREAMING_INFO, &tiled.info, false});
            if (carry.empty())
                return additions;
            std::size_t pointed = 0;
'''),
 ('''        const bool wantInventory = !native.empty() || !carry.empty();
''', '''        const bool wantInventory = !native.empty() || !carry.empty() || collision.tiled;
'''),
 ('''    build.collisionPath = prepared.collision.string();
''', '''    build.collisionPath = prepared.collision.string();
    build.collisionTiles = prepared.collisionTiles;
'''),
])
patch('src/convert/iw3_iw8/iw3_fastfile.h', [
 ('''    // Loot points of the map (collision JSON "loot_points").
''', '''    // The world collision is built as streamed collision tiles (collision JSON "collision_tiles": true).
    bool collisionTiles = false;
    // Loot points of the map (collision JSON "loot_points").
'''),
])
patch('src/convert/iw3_iw8/iw3_fastfile.cpp', [
 ('''    , lootPoints(std::move(other.lootPoints))
''', '''    , collisionTiles(other.collisionTiles)
    , lootPoints(std::move(other.lootPoints))
'''),
 ('''        lootPoints = std::move(other.lootPoints);
''', '''        lootPoints = std::move(other.lootPoints);
        collisionTiles = other.collisionTiles;
'''),
 ('''    result.lootPoints = ReadLootPoints(collision);
''', '''    if (collision.contains("collision_tiles"))
    {
        if (!collision.at("collision_tiles").is_boolean())
            throw std::runtime_error("collision_tiles is not true or false");
        result.collisionTiles = collision.at("collision_tiles").get<bool>();
        if (result.collisionTiles)
            zt::info("iw3: the world collision is built as streamed collision tiles");
    }
    result.lootPoints = ReadLootPoints(collision);
'''),
])
print('ok')
