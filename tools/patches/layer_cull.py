import io
p = 'src/viewport/scene.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'


def rep(a, b):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, a[:80]
    s = s.replace(a, b)


rep('''            for (uint32_t k : {a, b, c}) {
                // ImportedMesh::uv is OBJ convention (v flipped); the textured camera uses the game's v.''',
    '''            // A layer whose weight is 0 at the three corners adds nothing to this triangle (the reveal ramp and the
            // plain weight both give b = 0 at w = 0): it is left out (Hideout drew 21 M triangles a frame with every
            // layer covering the whole terrain).
            bool layerOn[kStockMaxLayers] = {};
            for (int L = 1; L <= int(layerDst.size()); ++L)
                for (uint32_t k : {a, b, c})
                    if (stockLayerWeight(vcol ? (*vcol)[k] : 0xFFFFFFFFu, vcolHi ? (*vcolHi)[k] : 0xFFFFFFFFu, L) > 0.5f / 255.0f)
                        layerOn[L - 1] = true;
            for (uint32_t k : {a, b, c}) {
                // ImportedMesh::uv is OBJ convention (v flipped); the textured camera uses the game's v.''')
rep('''                for (int L = 1; L <= int(layerDst.size()); ++L) {
                    GpuTexVertex lv = v;''', '''                for (int L = 1; L <= int(layerDst.size()); ++L) {
                    if (!layerOn[L - 1]) continue;
                    GpuTexVertex lv = v;''')
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
