import io
p = 'src/qtapp/main_window.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = '''        ImportedMesh m;
        for (const auto& [mat, tris] : it->second->tex)
            for (size_t i = 0; i + 2 < tris.size(); i += 3) {'''
b = '''        ImportedMesh m;
        for (const auto& [mat, tris] : it->second->tex) {
            // Cut-out and blended materials (foliage, cards, glass) receive no decal volume (research 99: their
            // techsets have no decal loop).
            if (const auto e = matCache_->lookup(qs(mat)); e && !e->failed &&
                (!e->maskDds.isEmpty() || materialBlendFromTechset(e->techset.toStdString()) != MaterialBlend::Opaque))
                continue;
            if (qs(mat).contains("foliage", Qt::CaseInsensitive)) continue;
            for (size_t i = 0; i + 2 < tris.size(); i += 3) {'''
assert s.count(a.replace('\n', nl)) == 1
s = s.replace(a.replace('\n', nl), b.replace('\n', nl))
a = '''                for (const Vec3& p : {a, b, c}) {
                    m.idx.push_back(uint32_t(m.pos.size()));
                    m.pos.push_back(p);
                    m.normal.push_back(n);
                    m.uv.push_back({0, 0});
                }
            }'''
b = a + nl + '            }'
assert s.count(a.replace('\n', nl)) == 1
s = s.replace(a.replace('\n', nl), b.replace('\n', nl))
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
