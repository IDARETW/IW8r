import io
import sys

# ACTS fork (replay-selfvis): the Replay base-surface GLB also carries each vertex's self-visibility word (XModelSurfs
# vertex +8, research 105 section 4) as the custom attribute _SELFVIS (VEC4 UNSIGNED_BYTE, the u32's bytes, not
# normalised), placed after the second UV and before the colour. IW8 Radiant decodes it for the static-model ambient
# occlusion (research 133 section 7).

ROOT = sys.argv[1] if len(sys.argv) > 1 else 'E:/mw124/acts-stt/'
P = ROOT + 'src/core/acts/tools/mw19/mw19_mesh.cpp'


def patch(a, b):
    s = io.open(P, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a2 = a.replace('\n', nl); b2 = b.replace('\n', nl)
    assert s.count(a2) == 1, a[:70]
    io.open(P, 'w', encoding='utf-8', newline='').write(s.replace(a2, b2))


patch('''            const size_t stride = 32 + (secondUv ? 8 : 0) + (colors ? 4 : 0);''',
      '''            // pos, normal, uv (32), second uv (8), self visibility (4: vertex +8, research 105 of IW8 Radiant), colour (4)
            const size_t selfVisAt = 32 + (secondUv ? 8 : 0);
            const size_t stride = selfVisAt + 4 + (colors ? 4 : 0);''')
patch('''                if (secondUv) {
                    Append(bin, Half(Get<uint16_t>(sharedData, uvOffset + v * 4)));
                    Append(bin, Half(Get<uint16_t>(sharedData, uvOffset + v * 4 + 2)));
                }
                if (colors)''', '''                if (secondUv) {
                    Append(bin, Half(Get<uint16_t>(sharedData, uvOffset + v * 4)));
                    Append(bin, Half(Get<uint16_t>(sharedData, uvOffset + v * 4 + 2)));
                }
                Append(bin, Get<uint32_t>(sharedData, at + 8));  // self visibility: octahedral bent normal, amount, cone
                if (colors)''')
patch('''            if (colors)
                attributes["COLOR_0"] =''', '''            attributes["_SELFVIS"] = accessor(start, vertexBytes, stride, selfVisAt, vertices, 5121, "VEC4", false, 34962);
            if (colors)
                attributes["COLOR_0"] =''')
print('ok')
