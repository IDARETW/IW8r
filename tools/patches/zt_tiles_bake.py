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

# ---- the shape list takes slots without a shape (the collision tiles of a big map)
patch('src/games/iw8/havok/physics.hpp', [
 ('''	struct shape_list_entry
	{
		std::uint32_t shape{};
''', '''	// One slot of a HavokPhysicsShapeList. A slot without a shape (shape 0) is a collision tile of a big map: the
	// tile's own zone holds the shape, the list keeps its tile index, counts, bounds and contents (IW8 Radiant
	// research 90). Only slots with a shape are in the list's shapes and shapeNames.
	struct shape_list_entry
	{
		std::uint32_t shape{};
'''),
])
patch('src/games/iw8/havok/physics.cpp', [
 ('''			const auto count = static_cast<std::uint32_t>(entries.size());
			const auto shapes = builder.array("T*<hknpShape>", count, std::vector<std::uint8_t>(count * 8));
''', '''			const auto count = static_cast<std::uint32_t>(entries.size());
			const auto shape_count = static_cast<std::uint32_t>(std::ranges::count_if(entries, [](const shape_list_entry& entry)
			{
				return entry.shape != 0;
			}));
			const auto shapes = builder.array("T*<hknpShape>", shape_count, std::vector<std::uint8_t>(shape_count * 8));
			std::uint32_t shape_slot = 0;
'''),
 ('''				builder.pointer(shapes, i * 8, pointer_type, entry.shape);
				names.push_back(add_string(builder, entry.name));
''', '''				if (entry.shape)
				{
					builder.pointer(shapes, shape_slot++ * 8, pointer_type, entry.shape);
					names.push_back(add_string(builder, entry.name));
				}
'''),
])

# ---- the bake
patch('src/games/iw8/common/replay_havok.h', [
 ('''struct BakeInput
{
    std::filesystem::path replayExecutable;
    std::filesystem::path collision;
    std::filesystem::path footsteps;
};
''', '''struct BakeInput
{
    std::filesystem::path replayExecutable;
    std::filesystem::path collision;
    std::filesystem::path footsteps;
    // Cut the world collision into the 32 x 32 grid of streamed collision tiles (IW8 Radiant research 90).
    bool tiles{};
};

// The grid of collision tiles is fixed by the game: 32 x 32 tiles of 8192 units, centred on the origin.
inline constexpr int kCollisionTileGrid = 32;
inline constexpr float kCollisionTileSize = 8192.0f;
inline constexpr std::uint32_t kCollisionTileCount = 1024;

// The Havok shape of one collision tile (CollisionTile.havokTileShapeData) and its slot in the world shape list
// (CollisionTile.worldCollisionShapeIdx). Only tiles with collision have one.
struct CollisionTileShape
{
    std::uint32_t index{};
    std::uint32_t slot{};
    std::vector<std::uint8_t> data;
};
'''),
 ('''    std::vector<LadderFace> ladders;
};
''', '''    std::vector<LadderFace> ladders;
    // With BakeInput::tiles: `world` is the list with one slot per tile below, and these are the tile shapes.
    bool tiled{};
    std::vector<CollisionTileShape> tiles;
};
'''),
])
patch('src/games/iw8/common/replay_havok.cpp', [
 ('''                                               const std::vector<ShapeTag> &seedTags = {},
                                               std::vector<ShapeTag> *builtTags = nullptr)
{
    namespace hk = zonetool::iw8::havok;
    hk::object_builder builder(*g_nativeCompendium);
''', '''                                               const std::vector<ShapeTag> &seedTags = {},
                                               std::vector<ShapeTag> *builtTags = nullptr,
                                               const std::vector<zonetool::iw8::havok::shape_list_entry> *slots = nullptr,
                                               zonetool::iw8::havok::shape_list_entry *rootShape = nullptr)
{
    namespace hk = zonetool::iw8::havok;
    hk::object_builder builder(*g_nativeCompendium);
'''),
 ('''    std::vector<hk::shape_tag> shapeTags;
    for (const auto &tag : tags)
        shapeTags.push_back({tag.contents, tag.hash, tag.collisionFilterInfo, tag.userData});
    const auto list = hk::add_shape_list(builder, entries, shapeTags);
''', '''    if (rootShape)
    {
        // A collision tile: the shape itself is the root of the data; its tags are in the world list.
        if (entries.size() != 1)
            throw std::runtime_error("A collision tile shape takes one shape group");
        *rootShape = entries.front();
        auto tile = hk::write_self_describing_tagfile(builder.finish("20180200", {}, entries.front().shape),
                                                      *g_nativeCompendium);
        if (builtTags)
            *builtTags = tags;
        return tile;
    }
    if (slots)
        entries.insert(entries.end(), slots->begin(), slots->end());

    std::vector<hk::shape_tag> shapeTags;
    for (const auto &tag : tags)
        shapeTags.push_back({tag.contents, tag.hash, tag.collisionFilterInfo, tag.userData});
    const auto list = hk::add_shape_list(builder, entries, shapeTags);
'''),
 ('''    std::vector<ShapeTag> worldTags;
    if (g_nativeCompendium)
    {
        const std::vector<ShapeGroup> worldGroups{std::move(world)};
        std::vector<ShapeTag> entityTags;
        result.world = BuildShapeListNative(collision.hulls, collision.meshes, worldGroups, floors, true, {},
                                            &worldTags);
''', '''    std::vector<ShapeTag> worldTags;
    if (input.tiles)
    {
        // Streamed collision tiles (research 90): every world hull and mesh triangle goes to the tile of its centre;
        // what is wider than a tile, or outside the grid, stays in the always-loaded shape of slot 0. Each tile with
        // collision gets its own Havok data (rooted at the shape) and one slot of the world list, ascending by tile
        // index. The tag table is shared.
        if (!g_nativeCompendium)
            throw std::runtime_error("Collision tiles need the native Havok builder");
        namespace hk = zonetool::iw8::havok;
        const float gridMin = -kCollisionTileSize * kCollisionTileGrid * 0.5f;
        const auto tileOf = [&](const float x, const float y) -> int {
            const int gx = static_cast<int>(std::floor((x - gridMin) / kCollisionTileSize));
            const int gy = static_cast<int>(std::floor((y - gridMin) / kCollisionTileSize));
            if (gx < 0 || gy < 0 || gx >= kCollisionTileGrid || gy >= kCollisionTileGrid)
                return -1;
            return gy * kCollisionTileGrid + gx;
        };
        std::vector<Mesh> meshes = collision.meshes;
        ShapeGroup always{world.name};
        std::map<int, ShapeGroup> tileGroups;
        for (const auto index : world.hulls)
        {
            const auto &hull = result.hulls[index];
            const bool wide = hull.maximum[0] - hull.minimum[0] > kCollisionTileSize ||
                              hull.maximum[1] - hull.minimum[1] > kCollisionTileSize;
            const int tile = wide ? -1
                                  : tileOf((hull.minimum[0] + hull.maximum[0]) * 0.5f,
                                           (hull.minimum[1] + hull.maximum[1]) * 0.5f);
            (tile < 0 ? always : tileGroups[tile]).hulls.push_back(index);
        }
        // Mesh triangles: one merged mesh per tile, and one for the triangles that stay in slot 0.
        std::map<int, Mesh> tileMeshes;
        std::map<int, std::map<std::pair<std::size_t, std::uint32_t>, std::uint32_t>> vertexOf;
        for (const auto meshIndex : world.meshes)
        {
            const auto &source = collision.meshes[meshIndex];
            for (const auto &triangle : source.triangles)
            {
                std::array<float, 2> low{std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()};
                std::array<float, 2> high{-std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
                std::array<float, 2> centre{};
                for (const auto vertex : triangle.indices)
                {
                    if (vertex >= source.vertices.size())
                        throw std::runtime_error("Collision mesh triangle has an invalid vertex");
                    for (std::size_t axis = 0; axis < 2; ++axis)
                    {
                        low[axis] = (std::min)(low[axis], source.vertices[vertex][axis]);
                        high[axis] = (std::max)(high[axis], source.vertices[vertex][axis]);
                        centre[axis] += source.vertices[vertex][axis] / 3.0f;
                    }
                }
                const bool wide = high[0] - low[0] > kCollisionTileSize || high[1] - low[1] > kCollisionTileSize;
                const int tile = wide ? -1 : tileOf(centre[0], centre[1]);
                auto &target = tileMeshes[tile];
                auto &remap = vertexOf[tile];
                MeshTriangle copy = triangle;
                for (auto &vertex : copy.indices)
                {
                    const auto [slot, added] = remap.try_emplace({meshIndex, vertex},
                                                                 static_cast<std::uint32_t>(target.vertices.size()));
                    if (added)
                        target.vertices.push_back(source.vertices[vertex]);
                    vertex = slot->second;
                }
                target.triangles.push_back(copy);
            }
        }
        for (auto &[tile, mesh] : tileMeshes)
        {
            (tile < 0 ? always : tileGroups[tile]).meshes.push_back(meshes.size());
            meshes.push_back(std::move(mesh));
        }
        if (always.hulls.empty() && always.meshes.empty() && !tileGroups.empty())
        {
            // The list needs a shape in slot 0: the first tile's collision stays always loaded.
            always.hulls = std::move(tileGroups.begin()->second.hulls);
            always.meshes = std::move(tileGroups.begin()->second.meshes);
            tileGroups.erase(tileGroups.begin());
        }
        if (always.hulls.empty() && always.meshes.empty())
            throw std::runtime_error("Collision tiles: the map has no world collision");

        std::vector<ShapeTag> tags;
        std::vector<hk::shape_list_entry> slots;
        std::size_t tileBytes = 0;
        for (const auto &[tile, group] : tileGroups)
        {
            hk::shape_list_entry entry;
            CollisionTileShape shape;
            shape.index = static_cast<std::uint32_t>(tile);
            shape.slot = static_cast<std::uint32_t>(slots.size() + 1);
            ShapeGroup named = group;
            named.name = "collisiontile" + std::to_string(tile);
            shape.data = BuildShapeListNative(collision.hulls, meshes, {named}, floors, true, tags, &tags, nullptr, &entry);
            tileBytes += shape.data.size();
            entry.shape = 0;
            entry.name.clear();
            entry.index = tile;
            slots.push_back(std::move(entry));
            result.tiles.push_back(std::move(shape));
        }
        const std::vector<ShapeGroup> worldGroups{std::move(always)};
        std::vector<ShapeTag> entityTags;
        result.world = BuildShapeListNative(collision.hulls, meshes, worldGroups, floors, true, tags, &worldTags, &slots);
        result.entities = BuildShapeListNative(collision.hulls, collision.meshes, entities, floors, false, worldTags,
                                               &entityTags);
        result.tiled = true;
        zt::info("collision: %zu collision tiles with collision (%zu bytes of tile shapes), %zu slots in the world list",
                 result.tiles.size(), tileBytes, slots.size() + 1);
    }
    else if (g_nativeCompendium)
    {
        const std::vector<ShapeGroup> worldGroups{std::move(world)};
        std::vector<ShapeTag> entityTags;
        result.world = BuildShapeListNative(collision.hulls, collision.meshes, worldGroups, floors, true, {},
                                            &worldTags);
'''),
])
patch('src/convert/iw3_iw8/convert.cpp', [
 ('''    std::string collisionPath;
''', '''    std::string collisionPath;
    // The world collision is cut into streamed collision tiles (research 90).
    bool collisionTiles = false;
'''),
 ('''        return iw8::havok::BakeCollision(
            {args.replayExecutable, args.collisionPath, args.footstepsPath});
''', '''        return iw8::havok::BakeCollision(
            {args.replayExecutable, args.collisionPath, args.footstepsPath, args.collisionTiles});
'''),
])
print('ok')
