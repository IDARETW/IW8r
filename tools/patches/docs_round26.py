import io

def load(p):
    s = io.open(p, encoding='utf-8', newline='').read()
    return s, ('\r\n' if '\r\n' in s else '\n')

def save(p, s):
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

p = 'docs/ROADMAP.md'
s, nl = load(p)
lines = s.split(nl)
hit = [i for i, l in enumerate(lines) if l.startswith('| 11.5 |')]
assert len(hit) == 1
cells = lines[hit[0]].split(' | ')
assert len(cells) == 5
old = 'No editor command picks the instances yet.'
assert cells[3].count(old) == 1
cells[3] = cells[3].replace(old, 'Editor (same day): File, Hide Stock Props In Selection adds every stock prop whose origin lies inside the selection bounds to the list and takes it out of the reference display; File, Show All Stock Props clears the list. Each reference prop carries its row number (core stock_smodels index).')
if cells[3].startswith('open:'):
    cells[3] = 'partial (hide built offline 2026-11-12, see the update; removal and collision open):' + cells[3][len('open:'):]
lines[hit[0]] = ' | '.join(cells)
for row in ('11.4',):
    hit = [i for i, l in enumerate(lines) if l.startswith('| ' + row + ' |')]
    cells = lines[hit[0]].split(' | ')
    if cells[3].startswith('open:'):
        cells[3] = 'partial (models from other stock maps built offline 2026-11-12, see the update; brushes open):' + cells[3][len('open:'):]
        lines[hit[0]] = ' | '.join(cells)
save(p, nl.join(lines))

p = 'docs/USER_GUIDE.md'
s, nl = load(p)
a = ("Not possible yet: new brushes inside a stock map, and choosing stock props to hide from the editor. Not seen in" + nl + "game.")
assert s.count(a) == 1
b = ("6. To hide stock props: draw a brush around them, select it and use File > Hide Stock Props In Selection. The props" + nl +
     "   leave the view and the rebuilt map draws them at size zero; their collision stays. File > Show All Stock Props" + nl +
     "   brings them back. Delete the helper brush afterwards (brushes are not built into a stock map)." + nl + nl +
     "Not possible yet: new brushes inside a stock map. Not seen in game.")
s = s.replace(a, b)
save(p, s)

p = 'docs/research/88_stock_map_editing.md'
s, nl = load(p)
a = '2. Editor: hide stock props inside a selection (needs the instance index of each reference prop).'
assert s.count(a) == 1
s = s.replace(a, '2. Done 2026-11-12: File > Hide Stock Props In Selection / Show All Stock Props (`StockModelInstance::index`,' + nl +
              '   worldspawn `iw8r_hide_smodels`, the reference display skips hidden rows). Not covered by a GUI check.')
save(p, s)
print('ok')
