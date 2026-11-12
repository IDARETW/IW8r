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
s = s.replace('The sun direction stays (the baked sun shadow data holds it). Open: the sky of a stock map',
              'The sun direction stays (the baked sun shadow data holds it). Sky (2026-11-12): File > Relight Stock Map Sky uses another stock map\'s sky (worldspawn iw8r_sky_from): Rebuild Stock Zone carries tw/sky_<map> from techsets_<map> and points this map\'s sky surfaces at it (converter replace_materials "@sky"; tools/stock_sky_swap_test.py: mp_shipment with the sky of mp_backlot2, ACTS 0 failed). Open: the sun direction', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
rep('docs/features/FEATURES.md', [
 ('''- File > Relight Stock Map Sun sets''', '''- File > Relight Stock Map Sky gives a stock map the sky of another stock map (worldspawn `iw8r_sky_from`); File > Rebuild Stock Zone carries that sky material in and puts it on this map's sky. The baked lighting stays. (tested offline; not yet seen in game)
- File > Relight Stock Map Sun sets'''),
 ('''the sky and the sun direction of a stock map (row 11.6)''', '''the sun direction of a stock map (row 11.6)'''),
])
rep('docs/USER_GUIDE.md', [
 ('''**Sun of a stock map**:''', '''**Sky of a stock map**: File > Relight Stock Map Sky picks another stock map whose sky this map uses (worldspawn
`iw8r_sky_from`); File > Rebuild Stock Zone carries that sky in. The map's baked lighting stays as it was.

**Sun of a stock map**:'''),
])
rep('docs/KEYS_REFERENCE.md', [
 ('''| `iw8r_sun_color` |''', '''| `iw8r_sky_from` | a stock map id | `mp_backlot2` | Stock maps: use that map's sky (File > Relight Stock Map Sky); Rebuild Stock Zone carries it in. Not seen in game. |
| `iw8r_sun_color` |'''),
])
rep('CHANGELOG.md', [
 ('''- **Stock sun colour and intensity.**''', '''- **Stock map sky.** File > Relight Stock Map Sky: another stock map's sky, carried in and put on this map's sky
  surfaces (converter replace_materials). Not seen in game.
- **Stock sun colour and intensity.**'''),
])
s = io.open('tools/run_offline_proofs.py', encoding='utf-8').read()
s = s.replace("    ('dynent_move_test', ['--exe', exe]),", "    ('dynent_move_test', ['--exe', exe]),\n    ('stock_sky_swap_test', ['--exe', exe]),", 1)
io.open('tools/run_offline_proofs.py', 'w', encoding='utf-8').write(s)
print('ok')
