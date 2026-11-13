import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


RH = 'src/viewport/render.h'
patch(RH, '''        bool decal = false;  // a decal material's batch (research 98): drawn after every opaque batch and layer''',
      '''        bool decal = false;  // a decal material's batch (research 98): drawn after every opaque batch and layer
        // Research 66 (materialDrawFor): 1 = shadow only (tw/shadowcaster: its "Shadow" image showed on mp_rust's
        // walls and floors), 2 = never drawn.
        int drawClass = 0;''')
patch(RH, '''        int decal = 0;              // setReferenceInstDecal: the material's decal sort key (0 = not a decal)''',
      '''        int decal = 0;              // setReferenceInstDecal: the material's decal sort key (0 = not a decal)
        int drawClass = 0;          // setReferenceInstDrawClass: 1 shadow only, 2 never drawn''')
patch(RH, '''    void setReferenceInstDecal(size_t mesh, int sortKey) {
        if (mesh < refTexInst_.size()) refTexInst_[mesh].decal = sortKey;
    }''', '''    void setReferenceInstDecal(size_t mesh, int sortKey) {
        if (mesh < refTexInst_.size()) refTexInst_[mesh].decal = sortKey;
    }
    // A stock material's draw class (materialDrawFor: 0 drawn, 1 shadow only, 2 never) for a reference batch / prop mesh.
    void setReferenceDrawClass(size_t batch, int cls) {
        if (batch < refLayers_.size() && refLayers_[batch].drawClass != cls) refLayers_[batch].drawClass = cls, shadowDirty_ = nearDirty_ = true;
    }
    void setReferenceInstDrawClass(size_t mesh, int cls) {
        if (mesh < refTexInst_.size() && refTexInst_[mesh].drawClass != cls) refTexInst_[mesh].drawClass = cls, shadowDirty_ = nearDirty_ = true;
    }''')

R = 'src/viewport/render.cpp'
# shadow pass of the world: never-drawn ones out
patch(R, '''                if (k < refLayers_.size() && (refLayers_[k].layer > 0 || refLayers_[k].decal)) continue;
                const int id = refTexIds_[k] >= 0 ? refTexIds_[k] : whiteTex_;
                if (id < 0 || size_t(id) >= textures_.size()) continue;
                if (cull && refExtraOutside(k, sf)) continue;''', '''                if (k < refLayers_.size() && (refLayers_[k].layer > 0 || refLayers_[k].decal || refLayers_[k].drawClass == 2)) continue;
                const int id = refTexIds_[k] >= 0 ? refTexIds_[k] : whiteTex_;
                if (id < 0 || size_t(id) >= textures_.size()) continue;
                if (cull && refExtraOutside(k, sf)) continue;''')
patch(R, '''                if (k < refLayers_.size() && (refLayers_[k].layer > 0 || refLayers_[k].decal)) continue;  // layers, decals: later''',
      '''                if (k < refLayers_.size() && (refLayers_[k].layer > 0 || refLayers_[k].decal || refLayers_[k].drawClass != 0)) continue;  // layers, decals: later''')
patch(R, '''            if (k < refLayers_.size() && (refLayers_[k].layer > 0 || refLayers_[k].decal)) continue;  // layers, decals: below''',
      '''            if (k < refLayers_.size() && (refLayers_[k].layer > 0 || refLayers_[k].decal || refLayers_[k].drawClass != 0)) continue;  // layers, decals: below''')
patch(R, '''                const RefLayer& L = refLayers_[k];
                if (!L.ready || refTexIds_[k] < 0 || size_t(refTexIds_[k]) >= textures_.size()) continue;''', '''                const RefLayer& L = refLayers_[k];
                if (!L.ready || L.drawClass != 0 || refTexIds_[k] < 0 || size_t(refTexIds_[k]) >= textures_.size()) continue;''')
# props
patch(R, '''        if ((g.decal > 0) != (decalPass == 1)) continue;''', '''        if ((g.decal > 0) != (decalPass == 1) || g.drawClass != 0) continue;''')
patch(R, '''        if (g.decal > 0) continue;  // decals cast no shadow''', '''        if (g.decal > 0 || g.drawClass == 2) continue;  // decals cast no shadow''')

C = 'src/qtapp/camera_view.cpp'
patch(C, '''                    if (nid >= 0) renderer_.setReferenceNormal(k, nid);
                    setBaseMaterialConstants(k);''', '''                    if (nid >= 0) renderer_.setReferenceNormal(k, nid);
                    if (ctx_ && ctx_->scene.drawClassFor) renderer_.setReferenceDrawClass(k, ctx_->scene.drawClassFor(refTexMaterials_[k]));
                    setBaseMaterialConstants(k);''')
patch(C, '''                    if (factsLookup_)
                        if (const auto f = factsLookup_(texInstMaterials_[k])) renderer_.setReferenceInstDecal(k, f->propDecalSortKey);''', '''                    if (factsLookup_)
                        if (const auto f = factsLookup_(texInstMaterials_[k])) renderer_.setReferenceInstDecal(k, f->propDecalSortKey);
                    if (ctx_ && ctx_->scene.drawClassFor) renderer_.setReferenceInstDrawClass(k, ctx_->scene.drawClassFor(texInstMaterials_[k]));''')
print('ok')
