import io
p = 'src/qtapp/main_window.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'


def rep(a, b):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, a[:80]
    s = s.replace(a, b)


rep('''    for (auto [m, shades] : {std::pair{&boxMesh, boxShades}, std::pair{&waitMesh, waitShades}, std::pair{&failMesh, failShades}})
        if (!m->rows.empty()) {''', '''    for (auto [m, shades] : {std::pair{&boxMesh, boxShades}, std::pair{&waitMesh, waitShades}, std::pair{&failMesh, failShades}})
        if (!m->rows.empty()) {
            // In the lit preview and Game Lighting (a game-like view) the stand-in boxes are left out as the other
            // placeholders are: on Verdansk they covered the sky and walls in tan.
            if (m == &boxMesh && (ctx_.scene.litPreview || ctx_.scene.gameLook)) continue;''')
rep('''                if (s.counts.missing + s.counts.failed > 0) QTimer::singleShot(0, this, [this] { rebuildReferenceProps(); });''',
    '''                if (s.counts.missing + s.counts.failed > 0 || refPropBoxed_ > 0) QTimer::singleShot(0, this, [this] { rebuildReferenceProps(); });''')
rep('''    refPropPlaceholders_ = (waitMesh.rows.size() + failMesh.rows.size()) / 12;''',
    '''    refPropPlaceholders_ = (waitMesh.rows.size() + failMesh.rows.size()) / 12;
    refPropBoxed_ = boxMesh.rows.size() / 12;''')
io.open(p, 'w', encoding='utf-8', newline='').write(s)
p = 'src/qtapp/main_window.h'
s = io.open(p, encoding='utf-8', newline='').read()
a = '    std::map<std::string, std::shared_ptr<const RefPropMesh>> refPropBuilt_;'
assert s.count(a) == 1
s = s.replace(a, a + nl + '    size_t refPropBoxed_ = 0;  // models drawn as stand-in boxes (past the triangle budget) in the last rebuild', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
