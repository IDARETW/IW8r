import io

# Docs 2026-11-14 late evening: the minimap fix, volumetric box masks, the owner's game results.

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

- **Fixed: the minimap was a quarter turn off in game** (owner's run of Cinder Rail Toon). The minimap picture was
  always drawn for a map whose north is +X, but since 2026-11-11 the build writes north = +Y (the top of the XY
  view), so the converter no longer turned it and the game showed it turned against the player. The picture is now
  drawn with the map's north up, for whatever `northyaw` the map has (a value that is no quarter turn gets a
  warning). Proof `tools/minimap_build_test.py`: two maps are built (north +Y, north +X), the picture is read back
  from each zone, and a block in the +X +Y corner is at the top right in both. Rebuild a map to get it.
- **Seen in game by the owner (evening):** Cinder Rail Toon without the occlusion bake is fine, and the map built
  again with the fixed occlusion bake works. The bake's build note and tooltip say so.
- **Volumetric boxes: mask pictures.** An `iw8r_volume` takes up to four masks (`mask`, `mask2`, `mask3`, `mask4`):
  `"<picture> [x|y|z] <sizeU> [<sizeV> [<scrollU> <scrollV> [<offsetU> <offsetV>]]]"`. A mask thins the box's fog by
  a picture projected through the box and repeated every size units; scroll makes it drift. This is how stock
  mp_village2 makes its moving mist. The editor ships two cloud pictures (`clouds_soft`, `clouds_hard`,
  `data/volume_masks`); a map's own go into `<map folder>/volume_masks`. Game Lighting shows the masks, moving.
  Proof `tools/volume_mask_build_test.py` (the masks and both pictures are read back from the built zone).
  Not seen in game.

''')

patch('docs/USER_GUIDE.md', '''- **Compass**: the compass north is the top of the XY view (+Y).''',
      '''- **Minimap**: drawn by the build from the map's brushes, with the map's north at the top, so the player arrow sits
  where the player stands (until 2026-11-14 it was a quarter turn off on built maps: rebuild).
- **Compass**: the compass north is the top of the XY view (+Y).''')

s = read('docs/KEYS_REFERENCE.md')
nl = '\r\n' if '\r\n' in s else '\n'
if '`mask`' not in s:
    hits = [i for i, l in enumerate(s.split(nl)) if 'height_half' in l and l.startswith('|')]
    lines = s.split(nl)
    row = ('| `mask`, `mask2`, `mask3`, `mask4` | `<picture> [x\\|y\\|z] <sizeU> [<sizeV> [<scrollU> <scrollV> [<offsetU> <offsetV>]]]` | '
           '`clouds_soft 800 800 50 0` | On `iw8r_volume`: a picture that thins the box\'s fog (white keeps it, black removes it), projected '
           'through the box along the axis (default z) and repeated every size units; scroll is in units per second. Pictures: '
           '`<map folder>/volume_masks/<picture>.png` (or TGA, JPG, BMP), or the editor\'s `clouds_soft` and `clouds_hard`. Up to four '
           'multiply. Game Lighting shows it. Not seen in game. |')
    if hits:
        lines.insert(hits[-1] + 1, row)
        s = nl.join(lines)
    else:
        # No table row for the box's keys yet: a short section before the counts.
        at = s.index('## Counts')
        s = s[:at] + ('## iw8r_volume (masks)' + nl + nl + '| Key | Value format | Example | What it does |' + nl + '|---|---|---|---|' + nl + row + nl + nl) + s[at:]
    write('docs/KEYS_REFERENCE.md', s)

s = read('docs/research/127_volumetric_boxes_translucent_fog.md') if False else None
import glob
note = glob.glob(ROOT + 'docs/research/127_*.md')[0].replace('\\', '/')[len(ROOT):]
s = read(note)
nl = '\r\n' if '\r\n' in s else '\n'
s = s.rstrip('\r\n') + nl + '''
## Masks on a built map (added 2026-11-14, main session)

Built, proven offline, not seen in game.

- Keys `mask`, `mask2`, `mask3`, `mask4` of an `iw8r_volume` (core `src/core/volume_mask.*`):
  `"<picture> [x|y|z] <sizeU> [<sizeV> [<scrollU> <scrollV> [<offsetU> <offsetV>]]]"`. The build writes them as the
  box's `masks` (type 2, the only kind the game draws; axis 0, 1 or 2; scale = the sizes in units; offset; scroll)
  and each picture as `images/iw8r_vmask_<picture>.dds`; the converter's existing volumetric path makes the picture
  a resident image of the map's zone.
- Pictures: `<map folder>/volume_masks`, then the editor's `data/volume_masks` (`clouds_soft`, `clouds_hard`: tileable
  noise made by `tools/gen_volume_masks.py`; stock mp_village2 uses its own `clouds_med_vol` / `clouds_hard_vol` at
  800, 400 and 200 units with scrolls of 50 to 75 units a second).
- The viewport (Game Lighting) samples them by the rule of A.2: `uv = (u / sizeU, -v / sizeV) + (offset + t * scroll)
  / size + 0.5`, u and v the two box axes other than the mask's, the red channel, repeated.
- Still not read from the exe (A.6): the sampler's address mode (taken as repeat: a scrolling picture needs it) and
  the unit of the scroll's time (taken as seconds).
- Proof `tools/volume_mask_build_test.py`: the built world's box has both masks with the keys' axes, sizes, offsets
  and scrolls, masks 2 and 3 unused; both images are in the map's zone, and a striped test picture comes back pixel
  for pixel.
'''.replace('\n', nl)
write(note, s)

p = 'docs/PROGRESS.md'
s = read(p)
nl = '\r\n' if '\r\n' in s else '\n'
a = '  ADDED 2026-11-14 18:10 (newest). MOUNT EDGES'
assert s.count(a) == 1
block = '''  ADDED 2026-11-14 20:40 (newest; read first).
  - OWNER, evening (their own game runs): Cinder Rail Toon without the occlusion bake "seemed to be fine", and
    "rebuilding with occlusion bake also seems to give a working map". New report: "the minimap doesn't seem aligned
    with the player correctly". New scope: "fill in any and all gaps that remain, including scripted object
    functionality and visibility in game/in the viewport, any rendering paths not yet accounted for, everything".
  - MINIMAP FIX: core minimap.* (renderMinimap(map, size, northQuarter); minimapQuarterForBuild; minimapPixel); the
    picture is drawn with the map's north up (the build's default northyaw 90: +Y up, +X right, not turned by the
    converter). Before, it was always drawn for northyaw 0 while the build wrote 90. Proof
    tools/minimap_build_test.py. The owner's built maps need a rebuild; nothing was deployed.
  - VOLUMETRIC BOX MASKS: core volume_mask.*, compile_iw8.cpp (masks in world.json, pictures as
    images/iw8r_vmask_*.dds), viewport (render.cpp: Texture2DArray t24, cbuffer Volume grown to 468 floats;
    camera_view.cpp loads the pictures and animates scrolling masks), data/volume_masks (tools/gen_volume_masks.py),
    package script, entity defs. Proof tools/volume_mask_build_test.py.
  - Converter 11eef2fc (ledger origins for mask pictures and the edge list), LIVE (old exe
    out/zt_backup/zonetool_7076f8da.exe).
  - Two Opus audits were started (docs/research/131 scripted objects, 132 rendering paths): their gap lists are the
    work queue that follows.
'''.replace('\n', nl)
s = s.replace(a, block + a.replace(' (newest)', ''))
write(p, s)
print('ok')
