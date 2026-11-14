import io

# Plan 1.13, the last open point: the placement's own lodScale on the cull distance, and the game's cull of every
# placed model past its last LOD distance (research 119 section 1, game-test source, INFERRED for Replay):
#   lod = XModelGetDesiredLodForDist(model, dist x invFovScale, dist x min(invFovScale, 1) / lodCullScale)
#   lod 6 (past the last LOD distance) = not drawn (R_GetStaticModelCollectionLODMaskInternal returns 0)
#   dist        = from the camera to the nearest point of the placement's bounds (0 inside)
#   lodCullScale = g_lodDistIndexToScale[instanceFlags[firstInstance] & 0xF] for a collection of one instance that is
#                  not clutter (collection flag 0x20); 1 for the others
# The export has smodels.instanceFlags (one byte a placement). The viewport culled only foliage, by the distance to
# the model's centre, with scale 1.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, n=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


# ---- core: the scale table, public
patch('src/core/smodel_flags.h', '''// Parses a ground_lighting colour''', '''// The game's LOD distance scale for index 0..15 (the low four bits of a placement's instance flags; doc 54). 6 is 1.
// An index out of range gives 1.
double smodelLodScaleOfIndex(unsigned index);

// Parses a ground_lighting colour''')
patch('src/core/smodel_flags.cpp', '''bool parseGroundColor(''', '''double smodelLodScaleOfIndex(unsigned index) {
    return index < kLodScales.size() ? kLodScales[index] : 1.0;
}

bool parseGroundColor(''')

# ---- core: the reader
H = 'src/core/stock_smodels.h'
patch(H, '''//     smodels.collections.bytes               GfxStaticModelCollection, 16 bytes each: u32 firstInstance,
//                                             u32 instanceCount, u32 smodelIndex, u32 flags''',
      '''//     smodels.collections.bytes               GfxStaticModelCollection, 16 bytes each: u32 firstInstance,
//                                             u32 instanceCount, u16 smodelIndex, u16 transientGfxWorldPlaced,
//                                             u16 clutterIndex, u8 flags (0x20 clutter, 0x80 occluder), u8 pad
//     smodels.instanceFlags.bytes             one byte per instance row; the low four bits are the placement's LOD
//                                             distance scale index (smodelLodScaleOfIndex; research 119)''')
patch(H, '''    uint32_t modelFlags = 0;                             // smodels.models[collection.smodelIndex].flags''',
      '''    uint32_t modelFlags = 0;                             // smodels.models[collection.smodelIndex].flags
    // The scale on the distance where the game stops drawing this placement (research 119): the placement's own
    // lodScale when its collection holds this one instance and is not clutter, else 1 (the game's rule). 1 when the
    // export has no instanceFlags.
    float lodCullScale = 1.0f;''')
C = 'src/core/stock_smodels.cpp'
patch(C, '''            put32(coll, uint64_t(num(c.get("smodelIndex"))));
            put32(coll, 0);''', '''            put32(coll, uint64_t(num(c.get("smodelIndex"))) & 0xFFFF);
            const Value* fl = c.get("flags");  // {"value": n, "name": "..."} or a plain number
            const Value* fv = fl && fl->type == Value::Type::Object ? fl->get("value") : fl;
            put32(coll, (uint64_t(num(fv)) & 0xFF) << 16);  // byte 14''')
patch(C, '''    const uint64_t indirTotal = indir.size() / kIndirectionBytes;
''', '''    const uint64_t indirTotal = indir.size() / kIndirectionBytes;

    // The placements' flag bytes (optional, like the indirection): hex bytes, or an array of numbers.
    std::vector<uint8_t> instFlags;
    if (const json::View fl = sm.get("instanceFlags"); fl.valid()) {
        const json::View fb = fl.get("bytes");
        const json::View fv = fl.get("values");
        if (stringOf(fb, scratch, hex)) {
            if (!unhex(hex, instFlags)) instFlags.clear();
        } else if (fv.type() == Value::Type::Array) {
            forEachRecord(fv, [&](const Value& r) { instFlags.push_back(uint8_t(uint32_t(num(&r)) & 0xFF)); });
        }
    }
''')
patch(C, '''        const uint64_t model = u32At(coll, c * kCollectionBytes + 8);
        if (model >= names.size()) {''', '''        const uint64_t model = u16At(coll, c * kCollectionBytes + 8);
        const uint8_t collFlags = coll[c * kCollectionBytes + 14];
        if (model >= names.size()) {''')
patch(C, '''            s.modelFlags = modelFlags[size_t(model)];
            s.surfaceMaterials = skins[size_t(model)];''', '''            s.modelFlags = modelFlags[size_t(model)];
            if (count == 1 && !(collFlags & 0x20) && k < instFlags.size())
                s.lodCullScale = float(smodelLodScaleOfIndex(instFlags[size_t(k)] & 0xFu));
            s.surfaceMaterials = skins[size_t(model)];''')
s = io.open(ROOT + C, encoding='utf-8', newline='').read()
if '#include "smodel_flags.h"' not in s:
    nl = '\r\n' if '\r\n' in s else '\n'
    a = '#include "stock_smodels.h"' + nl
    assert s.count(a) == 1
    s = s.replace(a, a + nl + '#include "smodel_flags.h"' + nl, 1)
    io.open(ROOT + C, 'w', encoding='utf-8', newline='').write(s)

# ---- viewport: per placement scale, the model's box, the game's distance
R = 'src/viewport/render.h'
patch(R, '''    // Stock foliage: the model's last LOD distance (inches). The game stops drawing a placement beyond it (the grass
    // dissolves there with a dither); 0 = always drawn.
    float lodOut = 0;''', '''    // Stock props: the model's last LOD distance (inches). The game stops drawing a placement beyond it (the grass
    // dissolves there with a dither); 0 = always drawn.
    float lodOut = 0;
    // Per instance, the scale on lodOut (the placement's lodScale, research 119); 0 = this placement is never left out
    // by distance. Empty: 1 for every instance.
    std::vector<float> cullScale;''')
patch(R, '''        float lodOut = 0;  // InstancedTexMesh::lodOut''', '''        float lodOut = 0;  // InstancedTexMesh::lodOut
        std::vector<float> cullScale;  // InstancedTexMesh::cullScale in GPU order (empty: 1 for all)
        float boxMid[3] = {0, 0, 0}, boxHalf[3] = {0, 0, 0};  // the mesh's box in model space''')
P = 'src/viewport/render.cpp'
patch(P, '''                if (m.ids.size() == count) {
                    g.ids.resize(count);
                    for (size_t i = 0; i < count; ++i) g.ids[i] = m.ids[g.order[i]];
                }''', '''                if (m.ids.size() == count) {
                    g.ids.resize(count);
                    for (size_t i = 0; i < count; ++i) g.ids[i] = m.ids[g.order[i]];
                }
                if (m.cullScale.size() == count &&
                    std::any_of(m.cullScale.begin(), m.cullScale.end(), [](float v) { return v != 1.0f; })) {
                    g.cullScale.resize(count);
                    for (size_t i = 0; i < count; ++i) g.cullScale[i] = m.cullScale[g.order[i]];
                }''')
io.open(ROOT + 'out/agent/_patch_ok.txt', 'w').write('core+header ok')
print('ok')
