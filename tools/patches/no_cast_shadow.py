import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


# techset_info.h, camera_view.h and main_window.cpp parts applied on the first run
RH = 'src/viewport/render.h'
patch(RH, '''        int drawClass = 0;
        int sortKey = 0;''', '''        int drawClass = 0;
        bool noCast = false;  // surfaceFlags 0x40000: drawn, casting no shadow
        int sortKey = 0;''')
patch(RH, '''        int drawClass = 0;          // setReferenceInstDrawClass: 1 shadow only, 2 never drawn''',
      '''        int drawClass = 0;          // setReferenceInstDrawClass: 1 shadow only, 2 never drawn
        bool noCast = false;        // setReferenceInstNoCast: drawn, casting no shadow (tree canopy leaf cards)''')
patch(RH, '''    void setReferenceInstDrawClass(size_t mesh, int cls) {''', '''    // A material that casts no sun shadow (surfaceFlags 0x40000: tree canopies, *_noshadow): drawn, left out of the
    // shadow maps. Owner 2026-11-13: shadows inside the trees' leaves were darker than in game.
    void setReferenceNoCast(size_t batch, bool on) {
        if (batch < refLayers_.size() && refLayers_[batch].noCast != on) refLayers_[batch].noCast = on, shadowDirty_ = nearDirty_ = true;
    }
    void setReferenceInstNoCast(size_t mesh, bool on) {
        if (mesh < refTexInst_.size() && refTexInst_[mesh].noCast != on) refTexInst_[mesh].noCast = on, shadowDirty_ = nearDirty_ = true;
    }
    void setReferenceInstDrawClass(size_t mesh, int cls) {''')
R = 'src/viewport/render.cpp'
patch(R, '''                if (k < refLayers_.size() && (refLayers_[k].layer > 0 || refLayers_[k].decal || refLayers_[k].drawClass == 2)) continue;''',
      '''                if (k < refLayers_.size() && (refLayers_[k].layer > 0 || refLayers_[k].decal || refLayers_[k].drawClass == 2 || refLayers_[k].noCast)) continue;''')
patch(R, '''        if (g.decal > 0 || g.drawClass == 2) continue;  // decals cast no shadow''',
      '''        if (g.decal > 0 || g.drawClass == 2 || g.noCast) continue;  // decals and no-cast materials cast no shadow''')
C = 'src/qtapp/camera_view.cpp'
patch(C, '''                    if (ctx_ && ctx_->scene.drawClassFor) renderer_.setReferenceDrawClass(k, ctx_->scene.drawClassFor(refTexMaterials_[k]));''',
      '''                    if (ctx_ && ctx_->scene.drawClassFor) renderer_.setReferenceDrawClass(k, ctx_->scene.drawClassFor(refTexMaterials_[k]));
                    if (factsLookup_)
                        if (const auto f = factsLookup_(refTexMaterials_[k])) renderer_.setReferenceNoCast(k, f->noCastShadow);''')
patch(C, '''                        if (const auto f = factsLookup_(texInstMaterials_[k])) renderer_.setReferenceInstDecal(k, f->propDecalSortKey);''',
      '''                        if (const auto f = factsLookup_(texInstMaterials_[k]))
                            renderer_.setReferenceInstDecal(k, f->propDecalSortKey), renderer_.setReferenceInstNoCast(k, f->noCastShadow);''')
print('ok')
