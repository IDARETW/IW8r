import io

# Research 132 G1 (props): blended stock prop materials that are not decals (glass, visors, see-through plastics; sort
# keys 18 and up such as 25 and 30) are drawn see-through in a pass of their own after the other props: blended by
# their alpha, depth-tested without writing depth, casting no sun shadow. They were cut out like foliage and came out as
# solid or speckled panes.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('src/qtapp/camera_view.h', '''        // Research 132 G2: an `unlit_` technique set (TechsetFamily::UnlitEffect): drawn with its colour as is.
        bool unlit = false;''', '''        // Research 132 G2: an `unlit_` technique set (TechsetFamily::UnlitEffect): drawn with its colour as is.
        bool unlit = false;
        // Research 132 G1: a blended model material that is no decal (glass and other see-through surfaces).
        bool translucent = false;''')
patch('src/qtapp/main_window.cpp', '''        f.unlit = parseTechset(e->techset.section('/', -1).toStdString()).family == TechsetFamily::UnlitEffect;''',
      '''        f.unlit = parseTechset(e->techset.section('/', -1).toStdString()).family == TechsetFamily::UnlitEffect;
        // Blended model materials past the decal sort keys (glass 25, 30): see-through (research 132 G1).
        f.translucent = !e->techset.startsWith("tw") && (ts.contains("_bad_") || ts.contains("_bar_")) && f.propDecalSortKey == 0;''')
patch('src/qtapp/camera_view.cpp', '''                            renderer_.setReferenceInstDecal(k, f->propDecalSortKey), renderer_.setReferenceInstNoCast(k, f->noCastShadow);''',
      '''                            renderer_.setReferenceInstDecal(k, f->propDecalSortKey), renderer_.setReferenceInstNoCast(k, f->noCastShadow || f->translucent),
                                renderer_.setReferenceInstTranslucent(k, f->translucent);''')
patch('src/viewport/render.h', '''    void setReferenceInstDecal(size_t mesh, int sortKey) {
        if (mesh < refTexInst_.size()) refTexInst_[mesh].decal = sortKey;
    }''', '''    void setReferenceInstDecal(size_t mesh, int sortKey) {
        if (mesh < refTexInst_.size()) refTexInst_[mesh].decal = sortKey;
    }
    // A see-through prop mesh (glass): drawn after the others, blended by its alpha, without writing depth.
    void setReferenceInstTranslucent(size_t mesh, bool on) {
        if (mesh < refTexInst_.size()) refTexInst_[mesh].translucent = on;
    }''')
patch('src/viewport/render.h', '''        int decal = 0;              // setReferenceInstDecal: the material's decal sort key (0 = not a decal)''',
      '''        int decal = 0;              // setReferenceInstDecal: the material's decal sort key (0 = not a decal)
        bool translucent = false;   // setReferenceInstTranslucent''')
C = 'src/viewport/render.cpp'
patch(C, '''    for (int decalPass = 0; decalPass < 2; ++decalPass)
    for (const auto& g : refTexInst_) {
        if (!g.vb || !g.inst) continue;
        if ((g.decal > 0) != (decalPass == 1) || g.drawClass != 0) continue;
        if (decalPass == 1 && texPsOverride_) continue;''', '''    // Pass 0: the props; 1: their decal meshes; 2: see-through meshes (glass), blended by their alpha.
    bool glassStateSet = false;
    for (int decalPass = 0; decalPass < 3; ++decalPass)
    for (const auto& g : refTexInst_) {
        if (!g.vb || !g.inst) continue;
        const int passOf = g.translucent && g.decal == 0 ? 2 : g.decal > 0 ? 1 : 0;
        if (passOf != decalPass || g.drawClass != 0) continue;
        if (decalPass >= 1 && texPsOverride_) continue;
        if (decalPass == 2 && !glassStateSet) {
            glassStateSet = true;
            if (!decalStateSet) {
                decalStateSet = true;
                ctx_->RSGetState(&savedRs);
                ctx_->OMGetDepthStencilState(&savedDs, &savedRef);
            }
            if (savedRs) ctx_->RSSetState(savedRs);
            ctx_->OMSetDepthStencilState(dsTest_, 0);
            if (blend_) {
                const float bf[4] = {0, 0, 0, 0};
                ctx_->OMSetBlendState(blend_, bf, 0xffffffff);
            }
            if (cbBatch_) {
                const float flags[8] = {propNormals ? 1.0f : 0.0f, 1.0f, 0, 0, 0, 0, game_.skyIsStock ? 1.0f : 0.0f, 0};  // y = 1: blended
                writeBatchCb(flags);
            }
        }''')
print('ok')
