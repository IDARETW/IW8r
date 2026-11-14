import io

# Mount edges of a built map (research 125): the build finds them on the world's solid brushes (core mount_edges) and
# writes them into the collision file ("mount_edges"); the converter puts them into the map's edge list.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


C = 'src/core/compile_iw8.cpp'
patch(C, '''    bool ladder = false;        // write ladder_planes: the converter keeps the widest vertical faces (iw3_fastfile.cpp:7250-7284)
};''', '''    bool ladder = false;        // write ladder_planes: the converter keeps the widest vertical faces (iw3_fastfile.cpp:7250-7284)
    bool mountSource = false;   // a solid brush of the map itself: its open corners and ledge tops become mount edges
};''')
patch(C, '''        CollisionBrush cbr;
        cbr.contents = bc.collides ? bc.contents : 1u;
        cbr.ladder = bc.ladder;
''', '''        CollisionBrush cbr;
        cbr.contents = bc.collides ? bc.contents : 1u;
        cbr.ladder = bc.ladder;
        cbr.mountSource = (cbr.contents & kIw3ContentsSolid) != 0 && !bc.ladder;
''')
patch(C, '''    // Collision brush order: world brushes (submodel 0) first, then each brush model's (doc 15 §2.3).
    const size_t worldBrushCount = g.brushes.size();
''', '''    // Collision brush order: world brushes (submodel 0) first, then each brush model's (doc 15 §2.3).
    const size_t worldBrushCount = g.brushes.size();
    // Mount edges (research 125): the game lets a player rest a weapon only on edges of the map's edge list. They are
    // the open wall corners and ledge tops of the world's solid brushes; placed models' collision and clip brushes hide
    // edges but give none. Worldspawn iw8r_mount_edges 0 builds without them.
    std::string mountEdgesKey;
    {
        const std::string* off = worldspawn ? worldspawn->value(kMountEdgesKey) : nullptr;
        if (off && *off == "0") {
            warn.push_back("mount edges off (worldspawn iw8r_mount_edges 0): weapons cannot be mounted on this map");
        } else {
            std::vector<MountSolid> solids;
            solids.reserve(worldBrushCount);
            for (size_t i = 0; i < worldBrushCount; ++i) {
                const CollisionBrush& b = g.brushes[i];
                if (!(b.contents & (kIw3ContentsSolid | kIw3ContentsPlayerClip))) continue;
                solids.push_back({b.planes, b.bounds, b.mountSource});
            }
            MountEdgeStats st;
            const std::vector<MapEdge> edges = findMountEdges(solids, {}, &st);
            if (!edges.empty()) {
                mountEdgesKey = ",\\"mount_edges\\":" + mountEdgesJson(edges);
                warn.push_back("mount edges: " + std::to_string(st.corners) + " wall corner(s) and " + std::to_string(st.ledges) +
                               " ledge top(s) where a weapon can be mounted (from the world's solid brushes; " + std::to_string(st.hidden) +
                               " covered edge(s) left out" + (st.dropped ? ", " + std::to_string(st.dropped) + " over the limit dropped" : std::string()) +
                               "; not seen in game)");
            }
        }
    }
''')
patch(C, '''           (collisionTiles ? ",\\"collision_tiles\\":true" : "") +
''', '''           (collisionTiles ? ",\\"collision_tiles\\":true" : "") + mountEdgesKey +
''')
s = io.open(ROOT + C, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
inc = '#include "compile_iw8.h"'
assert s.count(inc) == 1
s = s.replace(inc, inc + nl + '#include "mount_edges.h"')
io.open(ROOT + C, 'w', encoding='utf-8', newline='').write(s)
print('ok')
