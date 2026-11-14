import io
import sys

# Converter (x64-zt worktree): mount edges in a built map's edge list (IW8 Radiant research 125).
#   - src/games/iw8/common/map_edges.hpp: IW8 Radiant's src/core/map_edges.h, copied (the edge record, the octree
#     builder, the checks).
#   - the collision JSON's "mount_edges" ([[ax,ay,az,bx,by,bz,adjacentNormal,openAngle,flags],...]) goes into a file
#     beside the native collision (collision.native.edges); BakeCollision reads it into BakeResult::mountEdges.
#   - emitMapEdgeListBody writes the ladders' edges (bucket 2, one leaf, as before) and the mount edges (bucket 0, a
#     real octree with node sets); the map entities name the buckets in edgeListUsedQueryTypes.
# Argument: the converter checkout (default the IW8 Radiant worktree).

ROOT = (sys.argv[1] if len(sys.argv) > 1 else 'C:/zsw-iw8r/scriptables').rstrip('/') + '/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b, n=1):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    write(p, s.replace(a, b))


src = io.open('E:/IW8-Radiant/src/core/map_edges.h', encoding='utf-8').read()
write('src/games/iw8/common/map_edges.hpp',
      '// Copied from IW8 Radiant (src/core/map_edges.h) by tools/patches/zt_map_edges.py: do not edit here.\n' + src)
if '--header-only' in sys.argv:
    print('header ok')
    sys.exit(0)

H = 'src/games/iw8/common/replay_havok.h'
patch(H, '''#include <vector>
''', '''#include <vector>

#include "map_edges.hpp"
''')
patch(H, '''    std::vector<LadderFace> ladders;
    // With BakeInput::tiles''', '''    std::vector<LadderFace> ladders;
    // Mount edges of the map (IW8 Radiant research 125), from the file <collision>.edges when it is there.
    std::vector<iw8r::MapEdge> mountEdges;
    // With BakeInput::tiles''')

C = 'src/games/iw8/common/replay_havok.cpp'
patch(C, '''    const auto collision = ReadCollision(input.collision);
    const FloorMaterials floors(input.footsteps);

    BakeResult result;
''', '''    const auto collision = ReadCollision(input.collision);
    const FloorMaterials floors(input.footsteps);

    BakeResult result;
    {
        // The mount edges beside the native collision: "MWEDGE01", a count, then 32 bytes per edge (two end points,
        // the adjacent normal's angle, the open angle as a half float, the flags, one unused byte).
        auto edgesPath = input.collision;
        edgesPath += ".edges";
        if (std::filesystem::exists(edgesPath))
        {
            const auto bytes = ReadFile(edgesPath);
            if (bytes.size() < 12 || std::memcmp(bytes.data(), "MWEDGE01", 8) != 0)
                throw std::runtime_error("Mount edge file is not MWEDGE01");
            std::uint32_t count{};
            std::memcpy(&count, bytes.data() + 8, 4);
            if (bytes.size() != 12 + std::size_t{count} * 32)
                throw std::runtime_error("Mount edge file has the wrong size");
            result.mountEdges.resize(count);
            for (std::uint32_t index = 0; index < count; ++index)
            {
                const auto *record = bytes.data() + 12 + std::size_t{index} * 32;
                auto &edge = result.mountEdges[index];
                std::memcpy(edge.a, record, 12);
                std::memcpy(edge.b, record + 12, 12);
                std::memcpy(&edge.adjacentNormal, record + 24, 4);
                std::memcpy(&edge.openAngle, record + 28, 2);
                edge.flags = record[30];
            }
        }
    }
''')

F = 'src/convert/iw3_iw8/iw3_fastfile.cpp'
patch(F, '''        if (result.collisionTiles)
            zt::info("iw3: the world collision is built as streamed collision tiles");
    }
''', '''        if (result.collisionTiles)
            zt::info("iw3: the world collision is built as streamed collision tiles");
    }
    // "mount_edges" (IW8 Radiant research 125): the edges a weapon can be mounted on, each
    // [ax, ay, az, bx, by, bz, adjacentNormal, openAngle, flags] (the angle in radians, the open angle as the stored
    // half float, flags with bit 0). They travel beside the native collision as collision.native.edges.
    if (collision.contains("mount_edges"))
    {
        const auto &list = collision.at("mount_edges");
        if (!list.is_array() || list.size() > 200000)
            throw std::runtime_error("mount_edges must be a list of at most 200000 edges");
        std::vector<std::uint8_t> bytes(12 + list.size() * 32);
        std::memcpy(bytes.data(), "MWEDGE01", 8);
        const auto count = static_cast<std::uint32_t>(list.size());
        std::memcpy(bytes.data() + 8, &count, 4);
        for (std::size_t index = 0; index < list.size(); ++index)
        {
            const auto &edge = list[index];
            if (!edge.is_array() || edge.size() != 9)
                throw std::runtime_error("mount_edges[" + std::to_string(index) + "] must hold 9 numbers");
            auto *record = bytes.data() + 12 + index * 32;
            for (std::size_t part = 0; part < 7; ++part)
            {
                const float value = edge[part].get<float>();
                if (!std::isfinite(value))
                    throw std::runtime_error("mount_edges[" + std::to_string(index) + "] has a number that is not finite");
                std::memcpy(record + part * 4, &value, 4);
            }
            const auto open = edge[7].get<std::uint32_t>();
            const auto flags = edge[8].get<std::uint32_t>();
            if (open > 0xFFFF || !(flags & 1) || (flags & ~0xBu))
                throw std::runtime_error("mount_edges[" + std::to_string(index) + "] has a bad open angle or flags");
            const auto open16 = static_cast<std::uint16_t>(open);
            std::memcpy(record + 28, &open16, 2);
            record[30] = static_cast<std::uint8_t>(flags);
        }
        const auto edgesPath = result.scratch / "collision.native.edges";
        std::ofstream out(edgesPath, std::ios::binary);
        out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out)
            throw std::runtime_error("Cannot write " + edgesPath.string());
        zt::info("iw3: %zu mount edges for the map's edge list", list.size());
    }
''')

Z = 'src/games/iw8/zone/map_zone.h'
s = read(Z)
nl = '\r\n' if '\r\n' in s else '\n'
start = s.index('inline void emitMapEdgeListBody(ZoneBuffer &zb, const std::vector<havok::LadderFace> &ladders)')
end = s.index('// Emit one MapEnts asset body: struct(0x408)->TEMP_PRELOAD(1)')
body = '''// The map's edge list (IW8 Radiant research 125): the ladders' centre lines (bucket 2, one leaf, as it has always been
// written) and the mount edges (bucket 0, an octree with node sets as stock maps have it). The trees come from
// map_edges.hpp, which also checks them.
inline void emitMapEdgeListBody(ZoneBuffer &zb, const std::vector<havok::LadderFace> &ladders,
                                const std::vector<iw8r::MapEdge> &mountEdges)
{
    if (ladders.size() > 512)
        throw std::runtime_error("Replay ladder edge list count is invalid");
    if (ladders.empty() && mountEdges.empty())
        throw std::runtime_error("Replay edge list has no edges");
    if (mountEdges.size() > 200000)
        throw std::runtime_error("Replay edge list has too many mount edges");

    std::vector<iw8r::MapEdge> all;
    all.reserve(ladders.size() * 2 + mountEdges.size());
    for (const auto &ladder : ladders)
    {
        iw8r::MapEdge center{};
        std::copy_n(ladder.top.data(), 3, center.a);
        std::copy_n(ladder.bottom.data(), 3, center.b);
        // Replay EdgeAdjacentFaceNormalUnpack (13F04E0) rotates world X
        // around the edge direction. Our centerline runs downwards (-Z),
        // so the reference angle is the negative world yaw. BG_GetLadderInfo
        // (CC4470 -> 13EC520) averages the two adjacent normals; a synthetic
        // centerline has one authored facing, so both normals must coincide.
        center.adjacentNormal = -std::atan2(ladder.normal[1], ladder.normal[0]);
        center.openAngle = 0;
        center.flags = iw8r::kEdgeFlagLadder;
        all.push_back(center);

        iw8r::MapEdge width{};
        const float tangent[2]{-ladder.normal[1], ladder.normal[0]};
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            width.a[axis] = ladder.top[axis];
            width.b[axis] = ladder.top[axis];
        }
        width.a[0] += tangent[0] * ladder.width * 0.5f;
        width.a[1] += tangent[1] * ladder.width * 0.5f;
        width.b[0] -= tangent[0] * ladder.width * 0.5f;
        width.b[1] -= tangent[1] * ladder.width * 0.5f;
        width.adjacentNormal = 0.0f;
        width.openAngle = 15944;
        width.flags = iw8r::kEdgeFlagLadder;
        all.push_back(width);
    }
    all.insert(all.end(), mountEdges.begin(), mountEdges.end());

    // The ladder tree stays one leaf (bit 2 of the flat buckets).
    const iw8r::MapEdgeList list = iw8r::buildMapEdgeList(std::move(all), 8, 10, 4u);
    if (const char *wrong = iw8r::validateMapEdgeList(list); *wrong)
        throw std::runtime_error(std::string("Replay edge list: ") + wrong);
    static_assert(sizeof(iw8r::MapEdgeNode) == 16);

    struct Edge
    {
        float endpoint[2][4];
    };
    struct Metadata
    {
        float adjacentNormal;
        uint16_t openAngle;
        uint8_t flags;
        uint8_t pad;
    };
    static_assert(sizeof(Edge) == 32);
    static_assert(sizeof(Metadata) == 8);
    std::vector<Edge> edges(list.edges.size());
    std::vector<Metadata> metadata(list.edges.size());
    for (std::size_t index = 0; index < list.edges.size(); ++index)
    {
        const auto &source = list.edges[index];
        edges[index] = {};
        std::copy_n(source.a, 3, edges[index].endpoint[0]);
        std::copy_n(source.b, 3, edges[index].endpoint[1]);
        metadata[index] = {source.adjacentNormal, source.openAngle, source.flags, 0};
    }

    uint8_t body[iw8sz::EDGELIST]{};
    stamp64(body, 0x00, PTR_FOLLOWS);
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        stampf(body, 0x08 + axis * 4, list.mins[axis]);
        stampf(body, 0x14 + axis * 4, list.maxs[axis]);
    }
    stamp32(body, 0x24, list.queryTypes);
    stamp32(body, 0x28, list.queryTypes);
    stamp32(body, 0x30, static_cast<uint32_t>(edges.size()));
    body[0x34] = 1;
    stamp32(body, 0x38, static_cast<uint32_t>(edges.size()));
    stamp64(body, 0x40, PTR_FOLLOWS);
    stamp64(body, 0x48, PTR_FOLLOWS);
    stamp32(body, 0x50, static_cast<uint32_t>(list.trees.size()));
    stamp64(body, 0x58, PTR_FOLLOWS);
    if (!list.nodeSets.empty())
    {
        stamp32(body, 0x60, static_cast<uint32_t>(list.nodeSets.size()));
        stamp64(body, 0x68, PTR_FOLLOWS);
    }
    stamp32(body, 0x70, static_cast<uint32_t>(list.indices.size()));
    stamp64(body, 0x78, PTR_FOLLOWS);

    zb.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
    zb.align(7);
    zb.write(body, sizeof(body));
    zb.pushStream(XFILE_BLOCK_VIRTUAL);
    zb.writeStr("mapedgelist_00000");
    zb.align(31); // Replay AllocLoad_float4x2 (0xDF1B70).
    zb.write(edges.data(), edges.size() * sizeof(Edge));
    zb.align(3);
    zb.write(metadata.data(), metadata.size() * sizeof(Metadata));

    zb.align(15);
    for (const auto &tree : list.trees)
    {
        uint8_t octree[0x170]{};
        for (std::size_t level = 0; level < 16; ++level)
        {
            const float levelHalfWidth = std::ldexp(tree.halfWidth, -static_cast<int>(level));
            for (std::size_t axis = 0; axis < 3; ++axis)
                stampf(octree, level * 16 + axis * 4, levelHalfWidth);
            stampf(octree, 0x124 + level * 4, 3.0f * levelHalfWidth * levelHalfWidth);
        }
        for (std::size_t axis = 0; axis < 3; ++axis)
            stampf(octree, 0x100 + axis * 4, tree.minExtent[axis]);
        stampf(octree, 0x10C, -tree.halfWidth);
        std::memcpy(octree + 0x110, &tree.root, 16);
        stamp32(octree, 0x120, tree.nodeSetCount);
        octree[0x164] = static_cast<uint8_t>(tree.bucket);
        octree[0x165] = 3;
        zb.write(octree, sizeof(octree));
    }
    if (!list.nodeSets.empty())
    {
        zb.align(127); // the game's loader aligns a node set to its own size (asset_streams.cpp stream_mapedgelist)
        zb.write(list.nodeSets.data(), list.nodeSets.size() * sizeof(list.nodeSets[0]));
    }
    zb.align(3);
    zb.write(list.indices.data(), list.indices.size() * sizeof(uint32_t));
    zb.popStream();
    zb.popStream();
}

'''.replace('\n', nl)
s = s[:start] + body + s[end:]
write(Z, s)

patch(Z, '''        if (!collision->ladders.empty())
        {
            if (!edgeListAlias)
                throw std::runtime_error("Replay ladder edge list alias is missing");
            stamp32(me, kME_edgeListUsedQueryTypes, 4);''', '''        if (!collision->ladders.empty() || !collision->mountEdges.empty())
        {
            if (!edgeListAlias)
                throw std::runtime_error("Replay ladder edge list alias is missing");
            // The buckets with edges: 4 ladder centre lines, 1 mount edges. Bit 1 (mantle edges) stays clear: the game
            // then finds a mantle ledge with a trace (IW8 Radiant research 125 section 2.1).
            stamp32(me, kME_edgeListUsedQueryTypes,
                    (collision->ladders.empty() ? 0u : 4u) | (collision->mountEdges.empty() ? 0u : 1u));''')
patch(Z, '''    if (collision && !collision->ladders.empty())
    {
        zb.align(7);
        zb.writeT(edgeListAlias);''', '''    if (collision && (!collision->ladders.empty() || !collision->mountEdges.empty()))
    {
        zb.align(7);
        zb.writeT(edgeListAlias);''')
patch(Z, '''    const bool hasLadders = !collision.ladders.empty();
''', '''    // "Ladders" here: the map has an edge list (ladder centre lines, mount edges or both).
    const bool hasLadders = !collision.ladders.empty() || !collision.mountEdges.empty();
''')
patch(Z, '''            emitMapEdgeListBody(output.buffer(), collision.ladders);''',
      '''            emitMapEdgeListBody(output.buffer(), collision.ladders, collision.mountEdges);''')
print('ok')
