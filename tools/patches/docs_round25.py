import io

def load(p):
    s = io.open(p, encoding='utf-8', newline='').read()
    return s, ('\r\n' if '\r\n' in s else '\n')

def save(p, s):
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

p = 'docs/ROADMAP.md'
s, nl = load(p)
lines = s.split(nl)
def append(row, text):
    assert '|' not in text
    hit = [i for i, l in enumerate(lines) if l.startswith('| ' + row + ' |')]
    assert len(hit) == 1, row
    cells = lines[hit[0]].split(' | ')
    assert len(cells) == 5, (row, len(cells))
    cells[3] = cells[3].rstrip() + ' ' + text
    lines[hit[0]] = ' | '.join(cells)
append('11.2', 'Update (2026-11-12, research 88; owner scope decision: stock map editing is part of the finished product): File, Rebuild Stock Zone now runs the converter\'s new editzone command (fork 3c6606d6, in the live converter): the stock zone is loaded, changed in memory and written again in a few seconds (the dump script took minutes and stays as "Rebuild Stock Zone (dump script, slow)"). '
       'The entity text carries the spawn entities; the converter compiles them into the spawn list and takes them out of the text. Checked: the files the editor writes for an unchanged mp_shipment rebuild to the stock entity string and the stock 611 spawns (names included), and ACTS loads the zone like the stock one (tools/stock_edit_editor_check.py, tools/stock_edit_test.py). The ACTS loader test runs after every rebuild. NOT SEEN in game; installing the zone stays the owner\'s step.')
append('11.4', 'Update (2026-11-12, research 88): models from other stock maps can be added to a stock map. A script_model placed in the stock document whose model the zone does not hold is carried from the first stock zone that holds it (model, surfaces, images; materials and technique sets into the techsets companion) and named in the level model table; the Console says which models are carried. '
       'Checked on mp_shipment with a door model of mp_backlot2: both rebuilt zones load in ACTS with 0 failed, the model and the table entry read back. New brushes inside a stock world are still not possible directly; the planned route is to build them as a model with collision and carry that. NOT SEEN in game.')
append('11.5', 'Update (2026-11-12, research 88): the converter\'s editzone sets the scale of chosen static model instances to 0 (worldspawn key iw8r_hide_smodels with instance numbers; the rebuild passes it on). Collision is not changed, and whether scale 0 hides a model in game is not known. No editor command picks the instances yet.')
save(p, nl.join(lines))

p = 'docs/PROGRESS.md'
s, nl = load(p)
a = '  - Still open: impacts (F.12), the script-played loop of an own sound (F.6), reverb, probes, game modes, killstreak'
assert s.count(a) == 1
add = [
 '  - 2026-11-12, OWNER DECISIONS (asked with two questions): (1) the finished product must include "Stock map',
 '    editing, warzone-specific editing and map/mode creation" (not lightmaps, not Ground War scale); (2) real stock',
 '    scripted objects: "Turn it on by default" (done: build/realScriptables defaults to on; GUI tests force it off',
 '    and switch it on where they test it).',
 '    STOCK MAP EDITING (research 88, working record): converter 3c6606d6 (live; old exe',
 '    out/zt_backup/zonetool_db5914c2.exe) adds `zonetool editzone <zone> <edits.json> <out>`: entity text with spawn',
 '    entities compiled into the spawn list, hide_static_models, carry of assets from other stock zones. Editor:',
 '    src/core/stock_zone_edit.*, src/qtapp/main_window_stock_edit.cpp (prepareStockEdit, rebuildStockZoneFast),',
 '    File > Rebuild Stock Zone. Proofs: tools/stock_edit_test.py, tools/stock_edit_editor_check.py.',
 '    Next for stock maps: a command to pick props to hide, install / restore actions the owner runs, added brushes',
 '    as a carried model, the same for the Warzone maps.',
 '    WARZONE: research 87 (survey by a helper agent) is the starting point; nothing built yet.',
]
s = s.replace(a, nl.join(add) + nl + a)
save(p, s)

p = 'docs/USER_GUIDE.md'
s, nl = load(p)
marker = '### Stock scripted objects (scriptables)'
assert s.count(marker) == 1
sec = [
 '### Editing a stock map',
 '',
 '1. File > Open IW8 Stock Map opens a stock map: its entities are the document, its world and props are shown as a',
 '   reference.',
 '2. Map > Edit Compiled Spawns turns the spawn points into entities you can move, add and delete.',
 '3. Add things: a `script_model` with any stock model works even when the model belongs to another stock map. The',
 '   rebuild copies that model with its materials and textures from the map that has it, and the Console lists what',
 '   is copied.',
 '4. File > Rebuild Stock Zone writes the changed map in a few seconds and checks it with the game\'s own loaders',
 '   (ACTS). The Console shows the result and the folder. The game folder is only read.',
 '5. The rebuilt files (`<map>.ff`, and `techsets_<map>.ff` when models were copied) are not installed. To play them',
 '   they replace the stock files of the same name in the game\'s `zone` folder: keep a copy of the originals first.',
 '',
 'Not possible yet: new brushes inside a stock map, and choosing stock props to hide from the editor. Not seen in',
 'game.',
 '',
]
s = s.replace(marker, nl.join(sec) + nl + marker)
save(p, s)

p = 'docs/research/88_stock_map_editing.md'
s, nl = load(p)
a = '## Next steps'
assert s.count(a) == 1
add = ('''## Editor side (2026-11-12)

- `src/core/stock_zone_edit.*`: the model catalogue (`data/asset_catalog/iw8_models.tsv`), the carry list (models the
  document places that neither the edited zone nor an always-loaded zone holds; from the first other `mp_` zone), the
  hidden prop list (worldspawn `iw8r_hide_smodels`), and the edits JSON. Core tests `stock_zone_edit_*`.
- `src/qtapp/main_window_stock_edit.cpp`: `prepareStockEdit` writes `edit.ents` (the entity text; with the spawn
  entities when Edit Compiled Spawns was used) and `edits.json`; `rebuildStockZoneFast` runs the converter, then the
  ACTS loader test on each zone written, and reports in the status line and the Console. Menu: File > Rebuild Stock
  Zone. The dump script path stays as "Rebuild Stock Zone (dump script, slow)".
- End to end (`tools/stock_edit_editor_check.py` on the files the GUI test leaves): an unchanged mp_shipment document
  with its 611 spawns as entities rebuilds to the stock entity string and the stock spawn list, ACTS 0 failed.

''').replace('\n', nl)
s = s.replace(a, add + a)
save(p, s)
print('ok')
