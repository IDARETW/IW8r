import io

def load(p):
    s = io.open(p, encoding='utf-8', newline='').read()
    return s, ('\r\n' if '\r\n' in s else '\n')

def save(p, s):
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

p = 'docs/ROADMAP.md'
s, nl = load(p)
lines = s.split(nl)
def append(row, text, ncells):
    assert '|' not in text
    hit = [i for i, l in enumerate(lines) if l.startswith('| ' + row + ' |')]
    assert len(hit) == 1, row
    cells = lines[hit[0]].split(' | ')
    assert len(cells) == ncells, (row, len(cells))
    cells[3] = cells[3].rstrip() + ' ' + text
    lines[hit[0]] = ' | '.join(cells)
append('6.14', 'Research 83 (2026-11-11, after the owner asked for a real stock door): a stock door is not an entity but one 72-byte scriptable instance (definition name, origin, angles) in the map\'s scriptable block, for example scriptable_door_wooden_panel_03_painted_mp; mp_backlot2 has 19, mp_aniyah_tac 52, mp_shipment and mp_rust none. '
       'Its definition has one part with 18 states (closed, open left / right in steps, bash, explode), two models (drawn and collision), stock sounds from the always-loaded common_mp bank, and the native prompt strings MP/DOOR_USE_OPEN and MP/DOOR_USE_CLOSE ("Open Door" / "Close Door"); the script scripts/engine/scriptable_door drives it after its system_init is called. '
       'Not possible in an editor-built map today: the converter writes the scriptable block empty, cannot write a scriptable definition, the door definitions and models live only in stock map zones, and editor maps never call system_init. Converter steps in order: F1 allow the call (small), F2 the instance block (medium to large), F3 a native definition writer (large), F4 the door collision model (medium). '
       'The always-loaded hardpoint_chevron scriptable is the cheap game test for F2.', 5)
append('6.24', 'Research 83: the owner wants the door to be a real stock door delivered as a prefab bundle. The real door waits on converter steps F1 to F4 (row 6.14). Until then a door made of stock parts (a copy of the stock door model, stock door sounds, stock timing, the "Open Door" text) driven by generated script is possible; it is an imitation, and whether the prompt shows on a usable script_model in a built map is unknown.', 5)
save(p, nl.join(lines))

p = 'docs/PROGRESS.md'
s, nl = load(p)
a = '  - Still open: impacts (F.12), the script-played loop of an own sound (F.6), reverb, probes, game modes, killstreak'
assert s.count(a) == 1
add = [
 '  - RESEARCH 83 (stock door + prefab bundles): a real stock door is a native scriptable instance; needs converter',
 '    steps F1-F4 (rows 6.14 / 6.24). Bundle format designed: folder <name>.iw8prefab with prefab.json, prefab.map,',
 '    thumb.png, script.gsc, fallback.*, materials/ models/ sounds/ effects/; per-placement iw8r_pid and name',
 '    scoping; script included once per prefab kind. Editor work list with sizes in section B of the document.',
 '    AGENTS RUNNING at the time of writing (2026-11-11 21:00): showcase 2 (tools/make_dev_showcase2.py,',
 '    docs/DEV_SHOWCASE.md), decal producer (worktree C:\\zsw-iw8r\\decals, research 82), stock prop loading fix',
 '    (research 84; edits model_cache / acts_runner / two functions of main_window.cpp), viewport optimisation',
 '    (research 85; owns src/viewport and camera_view), prefab bundle core (see below).',
]
s = s.replace(a, nl.join(add) + nl + a)
save(p, s)
print('ok')
