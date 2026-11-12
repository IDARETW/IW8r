import io


def load(p):
    s = io.open(p, encoding='utf-8', newline='').read()
    return s, ('\r\n' if '\r\n' in s else '\n')


def rep(p, pairs):
    s, nl = load(p)
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:80])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


p = 'docs/ROADMAP.md'
s, nl = load(p)
lines = s.split(nl)
for i, line in enumerate(lines):
    if line.startswith('| 11.6 |'):
        a = 'sun and sky of a stock map open (research 48)'
        assert a in line
        lines[i] = line.replace(a, 'sun colour and intensity (2026-11-12): File > Relight Stock Map Sun sets worldspawn iw8r_sun_color (linear) / iw8r_sun_intensity, Rebuild Stock Zone writes them into the ComWorld primary light the first stage names (x64-zt 5ecee331; tools/stock_sun_edit_test.py on mp_shipment: only that light changes, ACTS as stock). The sun direction stays (the baked sun shadow data holds it). Open: the sky of a stock map')
io.open(p, 'w', encoding='utf-8', newline='').write(nl.join(lines))

rep('docs/features/FEATURES.md', [
 ('''- File > Relight Stock Map (Fog / Vision) changes''', '''- File > Relight Stock Map Sun sets the colour and intensity of a stock map's sun (worldspawn `iw8r_sun_color`, linear, and `iw8r_sun_intensity`); File > Rebuild Stock Zone writes them. The direction stays: the stock map's baked sun shadows hold it. (tested offline; not yet seen in game)
- File > Relight Stock Map (Fog / Vision) changes'''),
 ('''sun and sky relight (row 11.6)''', '''the sky and the sun direction of a stock map (row 11.6)'''),
])
rep('CHANGELOG.md', [
 ('''- **Game mode presets**''', '''- **Stock sun colour and intensity.** File > Relight Stock Map Sun; Rebuild Stock Zone writes them into the stock
  sun light (converter 5ecee331). The direction stays (baked sun shadows). Not seen in game.
- **Game mode presets**'''),
])
rep('docs/KEYS_REFERENCE.md', [
 ('''| `iw8r_collision` |''', '''| `iw8r_sun_color` | linear `r g b`, 0 to 16 | `1 0.55 0.35` | Stock maps: the sun's new colour (File > Relight Stock Map Sun); Rebuild Stock Zone writes it. The direction stays. Not seen in game. |
| `iw8r_sun_intensity` | number 0 to 1000000 | `7.5` | Stock maps: the sun's new intensity (stock: mp_shipment 8.6, mp_br_tut 30.7). Not seen in game. |
| `iw8r_collision` |'''),
])
print('ok')
