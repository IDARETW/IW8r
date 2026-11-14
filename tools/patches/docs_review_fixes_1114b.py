import io
import sys

# Docs for the second review's fixes and the script check fix (2026-11-14). Argument: the converter commit.

ROOT = 'E:/IW8-Radiant/'
ZT = sys.argv[1]


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

- **The build's script check no longer rejects ordinary scripts.** A map script that called one of its own
  functions on an entity (`player give_kit();`, everyday GSC) was stopped before the build with "unknown method".
  Only a name that is neither a game function nor defined in the script is an error now.
- **Fixes after a second independent review** (eight defects in the HUD pictures, the map text and the earlier
  fixes):
  - A HUD picture or a text entry you removed from the map stayed in every later build (the build folder kept the
    old files). The build now clears what it does not write again.
  - Opening the same stock map twice in quick succession could delete the folder the first read was still writing.
  - The shadow passes no longer leave out props by the camera's distance. Shadow maps are kept over several frames,
    so a prop could appear without its shadow after the camera came closer. A prop the view has dropped casts its
    shadow again, as before today.
  - The build's "not one of this map's HUD images" warning could be wrong for pictures whose file names sort
    differently from their HUD names.
  - A picture whose file name uses characters outside the system's character set ended the program; it is left out
    with a warning. A text file that cannot be read is said.
  - A text file saved with a byte-order mark lost its first entry.
  - Names in comments or inside ordinary strings of the script were read as text names.
  - A tool face is hidden under paint meshes only when they cover all of it (a face with a gap between two meshes
    was hidden in the editor).
  - Converter (x64-zt `''' + ZT + '''`, deployed): HUD pictures are taken from the build's own images folder only,
    one file per name.

''')
patch('docs/PROGRESS.md', '''  Review fixes (tools/patches/review_fixes_1114.py)''', '''  Second review (tools/patches/review_fixes_1114b.py): compile_iw8.cpp deletes stale extracted/images/hud_*.dds
  and localize/strings.tsv before writing; rep.hudImages sorted; openStockZone checks exportingMapEnts_ first; the
  shadow cell skip of the first review is gone again (InstCell::maxScale, cellCullOn_ removed); listHudImages
  catches name conversions; parseMapStrings skips a BOM; stringRefsInScript is a small lexer; PatchCovers::covered
  also needs the patch triangles to hold the face's area (clippedArea). build_preflight.cpp: a script-defined
  function called on an entity is not an unknown method. Converter ''' + ZT + ''': CopyHudImages reads
  extracted/images only.
  Review fixes (tools/patches/review_fixes_1114.py)''')
patch('docs/research/119_lod_distance_rule.md', '''- Shadows of left-out props: the viewport's sun and lamp shadow passes skip a cell of placements (2,048 units) when
  all of them are past their cull distance; inside a nearer cell a left-out prop can still cast one. The game's own
  shadow passes compute their levels separately (`shadowCutoffLod` is not read here).''',
      '''- Shadows of left-out props: the viewport still draws every placement into its shadow maps. The maps are kept
  over several frames (a lamp's until a caster changes), so leaving casters out by the camera's distance lost
  shadows after the camera moved (tried and taken out on 2026-11-14). The game's own shadow passes compute their
  levels separately (`shadowCutoffLod` is not read here).''')
print('ok')
