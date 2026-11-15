import io
import sys

# ACTS fork: the Replay base-surface GLB also carries a lightmapped static model's per-vertex lightmap coordinates
# (XSurface.lmapCoords, the pointer at +80; PackedLmapCoords = u16 u, u16 v over 65535, research 105 section 3.2 of IW8
# Radiant) as the custom attribute _LMAPUV (VEC2 UNSIGNED_SHORT, normalised), after the self visibility and before the
# colour, on surfaces that have them.

ROOT = sys.argv[1] if len(sys.argv) > 1 else 'E:/mw124/acts-stt/'
P = ROOT + 'src/core/acts/tools/mw19/mw19_mesh.cpp'


def patch(a, b):
    s = io.open(P, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a2 = a.replace('\n', nl); b2 = b.replace('\n', nl)
    assert s.count(a2) == 1, a[:70]
    io.open(P, 'w', encoding='utf-8', newline='').write(s.replace(a2, b2))


patch('''            const size_t selfVisAt = 32 + (secondUv ? 8 : 0);
            const size_t stride = selfVisAt + 4 + (colors ? 4 : 0);''',
      '''            const size_t selfVisAt = 32 + (secondUv ? 8 : 0);
            // Lightmapped static models: XSurface.lmapCoords (+80), one PackedLmapCoords (u16 u, u16 v) per vertex,
            // streamed with the zone (not in the shared buffer).
            const auto lmapAddress = Get<uint64_t>(h, 80);
            std::vector<uint8_t> lmap;
            if (lmapAddress)
                lmap = bytes(lmapAddress, vertices * 4);
            const size_t lmapAt = selfVisAt + 4;
            const size_t stride = lmapAt + (lmap.empty() ? 0 : 4) + (colors ? 4 : 0);''')
patch('''                Append(bin, Get<uint32_t>(sharedData, at + 8));  // self visibility: octahedral bent normal, amount, cone
                if (colors)''', '''                Append(bin, Get<uint32_t>(sharedData, at + 8));  // self visibility: octahedral bent normal, amount, cone
                if (!lmap.empty())
                    Append(bin, Get<uint32_t>(lmap, v * 4));  // lightmap u, v as unorm16
                if (colors)''')
patch('''            attributes["_SELFVIS"] = accessor(start, vertexBytes, stride, selfVisAt, vertices, 5121, "VEC4", false, 34962);''',
      '''            attributes["_SELFVIS"] = accessor(start, vertexBytes, stride, selfVisAt, vertices, 5121, "VEC4", false, 34962);
            if (!lmap.empty())
                attributes["_LMAPUV"] = accessor(start, vertexBytes, stride, lmapAt, vertices, 5123, "VEC2", true, 34962);''')
print('ok')
