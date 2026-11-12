import io

def load(p):
    s = io.open(p, encoding='utf-8', newline='').read()
    return s, ('\r\n' if '\r\n' in s else '\n')

def save(p, s):
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

p = 'docs/ROADMAP.md'
s, nl = load(p)
lines = s.split(nl)
def cell(row):
    hit = [i for i, l in enumerate(lines) if l.startswith('| ' + row + ' |')]
    assert len(hit) == 1, row
    cells = lines[hit[0]].split(' | ')
    assert len(cells) in (4, 5), (row, len(cells))
    return hit[0], cells
def append(row, text):
    assert '|' not in text
    i, cells = cell(row)
    if len(cells) == 4:  # playtest rows: the status is the last cell and carries the closing bar
        cells[3] = cells[3].rstrip().rstrip('|').rstrip() + ' ' + text + ' |'
    else:
        cells[3] = cells[3].rstrip() + ' ' + text
    lines[i] = ' | '.join(cells)
def replace(row, text):
    assert '|' not in text
    i, cells = cell(row)
    cells[3] = text
    lines[i] = ' | '.join(cells)

append('F.14', 'Update (2026-11-12): stock parity for the map data the airstrike plane and the support helicopter read: the generated minimap function now also sets level.mapsize and level.mapcorners (the two minimap corner entities, placed and turned as the stock compass set-up does), fork db5914c2 in the live converter. '
       'Checked offline: the converted script compiles, and only the script differs from the previous build. NOT SEEN in game.')
append('6.21', 'Update (2026-11-12): level.mapsize and level.mapcorners are set by every built map script (row F.14).')
replace('5.11', 'partial: the light grid half is done (row 5.20: every build writes a light grid with ambient from the captured probes). Lightmaps are not built: the converter writes no lightmap data, so a built map is lit by the sun, the light grid and its dynamic lights (row 1.8). '
        'A lightmap baker needs a new writer in the converter and repeated looks in game; it is not part of this build.')
replace('5.10', 'one cell: every built map is a single visibility cell (extended upward for aircraft, row F.14). That is the right size for the small and medium maps this tool builds; portal and cell authoring, MDAO volumes and Umbra data are not built and would need converter work and looks in game.')
save(p, nl.join(lines))

p = 'docs/PROGRESS.md'
s, nl = load(p)
a = '  - Still open: impacts (F.12), the script-played loop of an own sound (F.6), reverb, probes, game modes, killstreak'
assert s.count(a) == 1
add = [
 '  - 2026-11-12 (later): converter db5914c2 (live): iw3compat_minimap() also sets level.mapsize / level.mapcorners.',
 '    Old live exes are in out/zt_backup (zonetool_<commit>.exe: 062ec6b4, 8ff480fa, 04995144, 02182e8a).',
 '    Rows 5.10 and 5.11 reworded to what is built (one cell; light grid yes, lightmaps no).',
 '    WHAT IS LEFT is of two kinds only: (1) looks in game (docs/TEST_CHECKLIST.md; the real stock door first),',
 '    (2) large features that need converter work plus game iteration and were not started: lightmap baking,',
 '    portal / cell visibility, editing stock maps (11.4, 11.5), Ground War scale (11.10).',
]
s = s.replace(a, nl.join(add) + nl + a)
save(p, s)
print('ok')
