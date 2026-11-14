import io
import subprocess
import sys

# Converter (x64-zt worktree): the mount adjacency tables of a built map's edge list (IW8 Radiant research 125 section
# 11). The header is copied again (it now builds the tables) and emitMapEdgeListBody writes them: one word per edge
# and the records, after the index list.
# Argument: the converter checkout (default the IW8 Radiant worktree).

ROOT = (sys.argv[1] if len(sys.argv) > 1 else 'C:/zsw-iw8r/scriptables').rstrip('/') + '/'
subprocess.check_call([sys.executable, '-I', 'E:/IW8-Radiant/tools/patches/zt_map_edges.py', ROOT, '--header-only'])

Z = ROOT + 'src/games/iw8/zone/map_zone.h'
s = io.open(Z, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'


def rep(a, b):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (s.count(a), a[:70])
    s = s.replace(a, b)


rep('''    stamp32(body, 0x70, static_cast<uint32_t>(list.indices.size()));
    stamp64(body, 0x78, PTR_FOLLOWS);
''', '''    stamp32(body, 0x70, static_cast<uint32_t>(list.indices.size()));
    stamp64(body, 0x78, PTR_FOLLOWS);
    // Mount adjacency (the edges a mounted player can move between): one word per edge and the records, or neither.
    static_assert(sizeof(iw8r::MapEdgeAdjacency) == 12);
    if (!list.adjacencyMeta.empty())
    {
        stamp32(body, 0x80, static_cast<uint32_t>(list.adjacencyMeta.size()));
        stamp64(body, 0x88, PTR_FOLLOWS);
        stamp32(body, 0x90, static_cast<uint32_t>(list.adjacency.size()));
        stamp64(body, 0x98, PTR_FOLLOWS);
    }
''')
rep('''    zb.align(3);
    zb.write(list.indices.data(), list.indices.size() * sizeof(uint32_t));
    zb.popStream();''', '''    zb.align(3);
    zb.write(list.indices.data(), list.indices.size() * sizeof(uint32_t));
    if (!list.adjacencyMeta.empty())
    {
        zb.align(3);
        zb.write(list.adjacencyMeta.data(), list.adjacencyMeta.size() * sizeof(uint32_t));
        zb.align(3);
        zb.write(list.adjacency.data(), list.adjacency.size() * sizeof(iw8r::MapEdgeAdjacency));
    }
    zb.popStream();''')
io.open(Z, 'w', encoding='utf-8', newline='').write(s)
print('ok')
