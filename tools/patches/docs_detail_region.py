import io

# Docs for detail brushes in the occlusion bake and the region outline (2026-11-14).

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

- **Occlusion bake: you choose the occluders per brush.** A detail brush (Brush > Make Detail, `contents detail;`)
  is never an occluder; structural brushes are, as before. That is the old Radiant rule (detail does not block
  sight), and it lets you keep small or doubtful brushes out of the bake. The build log counts them.
- **The region has an outline.** View > Region now draws the region's box: dashed in the 2D views, as a wire box in
  the camera view, so you can see where the hidden part of the map begins.

''')
patch('docs/PROGRESS.md', '''  The web list of plan 3.3 / 3.4 is done (the Vision Editor form closed it).''', '''  EdgeBrush::detail (set by compile_iw8.cpp from brushIsDetail; the occluder list skips detail brushes, test
  compile_occlusion_bake_leaves_detail_brushes_out). Region outline: SceneCache::regionLines (EditorContext::
  refreshRegion), drawn in camera_geometry's misc lines and in GridView::paintEvent.
  The web list of plan 3.3 / 3.4 is done (the Vision Editor form closed it).''')
patch('docs/USER_GUIDE.md', '''(so build thick
  walls from boxes; a thin slanted wall hides nothing).''', '''(so build thick
  walls from boxes; a thin slanted wall hides nothing). A detail brush (Brush > Make Detail) never hides anything:
  mark what should not count.''')
P = 'docs/PLAN_REMAINING.md'
s = read(P)
nl = '\r\n' if '\r\n' in s else '\n'
a = '| 2.4 thin slanted walls, terrain and models as occluders, a per-brush occluder key, gates, splitting merged surfaces | Opaque brushes (boxes, and other shapes by the box inside them) |'
assert s.count(a) == 1
s = s.replace(a, '| 2.4 thin slanted walls, terrain and models as occluders, gates, splitting merged surfaces | Opaque structural brushes (boxes, and other shapes by the box inside them); a detail brush is left out, which is the per-brush switch (2026-11-14) |')
a = '  - preview decode off the GUI thread, one process per validation run, the region outline: not built; none blocks a' + nl + '    workflow (decode is budgeted per frame, validation runs per zone, the region hides what is outside it).'
assert s.count(a) == 1
s = s.replace(a, '  - the region outline: done 2026-11-14 (2D and camera views).' + nl +
              '  - preview decode off the GUI thread, one process per validation run: not built; neither blocks a workflow' + nl +
              '    (decode is budgeted per frame, validation runs per zone).')
write(P, s)
patch('docs/research/118_occlusion_bake.md', '''- Thin slanted walls and roofs, terrain, and models as occluders''', '''- (Per-brush control exists since 2026-11-14: a detail brush is not an occluder.)
- Thin slanted walls and roofs, terrain, and models as occluders''')
print('ok')
