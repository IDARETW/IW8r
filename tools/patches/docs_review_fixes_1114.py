import io

# Docs for the review fixes of 2026-11-14 (tools/patches/review_fixes_1114.py).

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    write(p, s.replace(a, b))


patch('CHANGELOG.md', '''## 2026-11-14

''', '''## 2026-11-14

- **Fixes after a second pair of eyes read the day's changes** (an independent review of the code; nine defects, none
  of them a crash in normal use):
  - A stock prop made of several materials could lose one part before another at a distance (each part was measured
    on its own). The whole model is measured now. Models with untextured parts are not dropped by distance at all,
    so they stay whole.
  - A prop the view had dropped still cast its sun and lamp shadow. The shadow passes now leave out the areas whose
    props are all past their distance (inside such an area a small prop near its limit can still cast one).
  - A screenshot without markers dropped no props at all, so it showed more than the view. It follows the view now.
    Its vignette used the window's shape instead of the picture's.
  - A paint mesh on a face with five or more corners still flickered against the caulk under it; the test of what a
    mesh covers is exact now (by its triangles), which also stops a slanted mesh from hiding a tool face beside it.
  - Cancelling "Open IW8 Stock Map" while it read the entities could leave a half-written file that the next open
    read. The next open reads the map again. A second open cannot start inside the first.
  - When the texture memory limit was reached during a load, finished textures of the old size blocked the queue for
    some seconds. A decoder that runs out of memory now gives a missing texture instead of ending the program.
  - A nonsense white balance value in a map's vision overrides is ignored safely.

''')
patch('docs/PROGRESS.md', '''  Volumetric fog measured (research 109 section 2.7)''', '''  Review fixes (tools/patches/review_fixes_1114.py): viewport/patch_covers.{h,cpp} (PatchCovers: find / covered by
  the patch triangles; used by camera_geometry.cpp and scene.cpp buildCameraGeometryImpl), InstancedTexMesh::
  hasModelBox / modelBox (RefPropMesh::box), InstCell::maxScale + cellCullOn_ / cellCullFov_ (shadow passes skip
  cells past the cull distance), instCull_ also for clean captures, RefPropMesh.lodOut = 0 when the model has plain
  parts, KeyedJobPool catches job exceptions, decodeSide_ clears the pool, <cache>/export.incomplete marker and
  exportingMapEnts_ in exportStockMapEnts.
  Volumetric fog measured (research 109 section 2.7)''')
patch('docs/research/119_lod_distance_rule.md', '''- Sun shadows of left-out props: the viewport still draws them into the shadow map (`shadowCutoffLod` is not read).''',
      '''- Shadows of left-out props: the viewport's sun and lamp shadow passes skip a cell of placements (2,048 units) when
  all of them are past their cull distance; inside a nearer cell a left-out prop can still cast one. The game's own
  shadow passes compute their levels separately (`shadowCutoffLod` is not read here).
- A model with untextured parts in the viewport (a material that did not resolve) is never left out by distance, so
  that it does not lose only its textured parts.''')
print('ok')
