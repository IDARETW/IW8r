import io

def load(p):
    s = io.open(p, encoding='utf-8', newline='').read()
    return s, ('\r\n' if '\r\n' in s else '\n')

def save(p, s):
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

p = 'docs/ROADMAP.md'
s, nl = load(p)
lines = s.split(nl)
hit = [i for i, l in enumerate(lines) if l.startswith('| 11.2 |')]
assert len(hit) == 1
cells = lines[hit[0]].split(' | ')
assert len(cells) == 5
text = ('Update (2026-11-12, later): the rebuild also writes patch files (zone .fp, fork 1ad5320d): the game applies a patch beside the stock zone of the same name, so an edited map installs as one or two small files (0.1 MB for the mp_shipment test) and the stock zones stay as shipped. '
        'Checked with ACTS, whose patch reader is its own code: a copy of the stock zone with the patch beside it loads with 0 failed and reads back as the edited entities and spawns. '
        'File, Install Edited Stock Map Into Game copies the patches of the last checked rebuild into the game zone folder after a confirmation that lists them (a stock patch of the same name is saved first), and File, Restore Stock Map In Game takes them out; core stock_install with tests on temporary folders. Only the owner runs these two. '
        'The edit command also runs on the Warzone maps (mp_br_tut 3 s, mp_br_quarry 5 s).')
assert '|' not in text
cells[3] = cells[3].rstrip() + ' ' + text
lines[hit[0]] = ' | '.join(cells)
save(p, nl.join(lines))

p = 'docs/USER_GUIDE.md'
s, nl = load(p)
a = ("5. The rebuilt files (`<map>.ff`, and `techsets_<map>.ff` when models were copied) are not installed. To play them" + nl +
     "   they replace the stock files of the same name in the game's `zone` folder: keep a copy of the originals first.")
assert s.count(a) == 1
b = ("5. File > Install Edited Stock Map Into Game puts the edit into the game. It copies small patch files (`<map>.fp`)" + nl +
     "   beside the stock zones after showing you the list; the stock zones themselves are not changed, and a stock" + nl +
     "   patch of the same name is saved first. Close the game before installing. File > Restore Stock Map In Game" + nl +
     "   takes the edit out again.")
s = s.replace(a, b)
save(p, s)

p = 'docs/TEST_CHECKLIST.md'
s, nl = load(p)
a = '## In game: camera view speed and prefab models (2026-11-11 late), editor only, not seen'
assert s.count(a) == 1
sec = [
 '## In game: an edited stock map (2026-11-12), not seen in game',
 '',
 '- [ ] File > Open IW8 Stock Map > mp_shipment. Map > Edit Compiled Spawns. Move one spawn a little, add a',
 '      `script_model` with a model of another map (for example `door_wooden_panel_03_painted_left_mp_nophysics`).',
 '- [ ] File > Rebuild Stock Zone: a few seconds, the status line ends with "checked (ACTS, 0 failed)".',
 '- [ ] Close the game. File > Install Edited Stock Map Into Game: the dialog lists `mp_shipment.fp` (and',
 '      `techsets_mp_shipment.fp`). Confirm.',
 '- [ ] Start Shipment in the game: say whether it loads and whether the added model and the moved spawn are there.',
 '- [ ] File > Restore Stock Map In Game puts the stock map back (do this before playing online or with others).',
 '',
]
s = s.replace(a, nl.join(sec) + nl + a)
save(p, s)

p = 'docs/research/88_stock_map_editing.md'
s, nl = load(p)
a = '3. Install and restore actions the owner runs (backup of the stock file first). The session never writes to the game' + nl + '   folder itself.'
assert s.count(a) == 1
b = ('3. Done 2026-11-12: the edit installs as patch files. `editzone` writes `<output>\\patch\\<zone>.fp` (fork 1ad5320d,' + nl +
     '   `write_patch`); the game applies `<zone>.fp` beside `<zone>.ff`, so the stock zones are never replaced. Proof: a' + nl +
     '   copy of the stock mp_shipment.ff with the 0.1 MB patch beside it loads in ACTS (its own patch reader) with 0' + nl +
     '   failed and reads back as the edited entities and spawns (`tools/stock_edit_test.py`, part 3). Editor: File >' + nl +
     '   Install Edited Stock Map Into Game / Restore Stock Map In Game (core `stock_install.*`, tests on temporary' + nl +
     '   folders; a stock patch of the same name is saved as `<name>.stock` in the app data folder `stock_backup\\<zone>`).' + nl +
     '   Only the owner runs them; the session never writes to the game folder itself. This replaces the "installed over' + nl +
     '   the stock file" note above.')
s = s.replace(a, b)
save(p, s)

p = 'docs/PROGRESS.md'
s, nl = load(p)
a = '  - Still open: impacts (F.12), the script-played loop of an own sound (F.6), reverb, probes, game modes, killstreak'
assert s.count(a) == 1
add = [
 '  - 2026-11-12 (stock map editing, continued): converter 1ad5320d live (old exe zonetool_3c6606d6.exe): editzone',
 '    writes .fp patches. Editor: File > Hide Stock Props In Selection / Show All Stock Props, File > Install Edited',
 '    Stock Map Into Game / Restore Stock Map In Game (src/core/stock_install.*, main_window_stock_edit.cpp:',
 '    lastStockRebuild_, stockBackupDir). The two install actions are for the owner only: never run them from a',
 '    session. Research 87 (Warzone survey, by a helper) is written; tools/warzone_*.py are its decoders.',
]
s = s.replace(a, nl.join(add) + nl + a)
save(p, s)
print('ok')
