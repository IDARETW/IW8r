import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


# Second half of prop_shared_verts.py (its first half is applied; its `tm.tris = tris;` step matched both sites).
M = 'src/qtapp/main_window.cpp'
patch(M, '''tm.tris = tris;''', '''tm.shared = tris;''', 2)
patch(M, '''        for (const auto& [mat, tris] : it->second->tex)
            for (const auto& v : tris) pb.add(place(v));''', '''        for (const auto& [mat, tris] : it->second->tex)
            for (const auto& v : *tris) pb.add(place(v));''')
patch(M, '''        for (const auto& [mat, tris] : it->second->tex) {
            // Cut-out and blended materials (foliage, cards, glass) receive no decal volume (research 99: their''',
      '''        for (const auto& [mat, trisPtr] : it->second->tex) {
            const std::vector<app::GpuTexVertex>& tris = *trisPtr;
            // Cut-out and blended materials (foliage, cards, glass) receive no decal volume (research 99: their''')
print('ok')
