import io

# Docs for the fitted occluder boxes of the occlusion bake (2026-11-14).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('CHANGELOG.md', '''## 2026-11-14

''', '''## 2026-11-14

- **Occlusion bake: slanted and many-sided brushes hide things too.** An opaque brush that is not an axis-aligned
  box (a round tower, a wedge, a turned block) now gives the bake the box found inside it; before, only exact boxes
  counted. The box lies wholly inside the brush, so it hides at most what the brush hides. Thin slanted walls hold no
  such box and still hide nothing. The build log says how many boxes were fitted. Core tests: the box is inside the
  brush for boxes, prisms of 3 to 24 sides and 300 random cut solids; a compile with a box, a prism and a sliver
  writes two occluders.

''')
patch('docs/PROGRESS.md', '''  The web list of plan 3.3 / 3.4 is done except:''', '''  Occlusion bake: core occluder_box.h inscribedBox (grow from the corners' average: all sides together, then each
  side alone, three rounds); compile_iw8.cpp uses it for opaque brushes that are not axis-aligned boxes (at least 8
  units a side; the converter's minOccluderSide 64 still applies). Tests: tests/occluder_box_tests.cpp and
  compile_occlusion_bake_fits_boxes_into_slanted_brushes in custom_materials_tests.cpp. The converter is unchanged.
  The web list of plan 3.3 / 3.4 is done except:''')
patch('docs/HANDOVER.md', '''- Occlusion: occluders that are not boxes; local updates of a stock map's occlusion data (removing stock geometry
  opens the whole map's occlusion).''', '''- Occlusion: thin slanted walls, terrain and models as occluders (other slanted and many-sided brushes hide by the
  box inside them); local updates of a stock map's occlusion data (removing stock geometry opens the whole map's
  occlusion).''')
patch('docs/USER_GUIDE.md', '''rooms, corridors and closed streets; an open map gains little. Only brushes that are axis-aligned boxes with opaque
  materials hide anything.''', '''rooms, corridors and closed streets; an open map gains little. Only brushes with opaque materials hide anything:
  an axis-aligned box as it is, a slanted or many-sided brush by the largest box that fits inside it (so build thick
  walls from boxes; a thin slanted wall hides nothing).''')
patch('docs/research/118_occlusion_bake.md', '''- **Occluders are boxes.** Only opaque brushes that are axis-aligned boxes count (a portal is an axis-aligned
  rectangle in tile quanta).''', '''- **Occluders are boxes.** An opaque brush that is an axis-aligned box counts as it is; any other opaque convex
  brush counts by a box found inside it (core `occluder_box.h`, 2026-11-14: grown from the average of the brush's
  corners, all sides together and then each side alone; at least 8 units a side). A box inside the brush hides at
  most what the brush hides, so this keeps the bake conservative (a portal is an axis-aligned rectangle in tile
  quanta).''')
patch('docs/research/118_occlusion_bake.md', '''- Occluders that are not boxes (slanted roofs, terrain), and models as occluders.''',
      '''- Thin slanted walls and roofs, terrain, and models as occluders (thick slanted brushes are covered by their inner
  box since 2026-11-14).''')
patch('docs/features/FEATURES.md', '''real occlusion cells and portals from the map's opaque box brushes,''',
      '''real occlusion cells and portals from the map's opaque brushes (boxes as they are, other shapes by the box inside them),''')
print('ok')
