import io

def load(p):
    s = io.open(p, encoding='utf-8', newline='').read()
    return s, ('\r\n' if '\r\n' in s else '\n')

def save(p, s):
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

def rep(s, nl, a, b, p):
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    return s.replace(a, b)

p = 'docs/research/88_stock_map_editing.md'
s, nl = load(p)
if 'Done 2026-11-12: added brushes' not in s:
  s = s
else:
  raise SystemExit('88 already updated: run the rest')
s = rep(s, nl, '''- New world geometry (brushes) inside the stock world. The route that fits this design: build the added brushes as a
  custom model with collision through the normal map build, load that small zone with the fork's loader and add its
  assets to the stock zone the same way stock assets are carried. Not done.
''', '''- New world geometry inside the stock world itself. Added brushes are built as models instead (done 2026-11-12,
  next step 4 below), which draws and collides the same for a player.
''', p)
s = rep(s, nl, '''4. Added brushes as a carried custom model.
''', '''4. Done 2026-11-12: added brushes as a carried custom model. File > Rebuild Stock Zone turns the visible brushes and
   patches of the worldspawn and of each func_group into one Y-up OBJ model each (core `stockOverlayModels`; tool-only
   brushes such as a caulk helper and other brush entities are left out with a note), writes a helper map
   (`mp_iw8rovl`: a room, the spawn set a build needs, one targeted `script_model` per model so it stays a live model
   of its own name with its physics) and builds it with the editor's own headless build (stock textures, collision).
   Then `editzone` carries each model from the helper package (carry entry with `folder`, fork 9f1472de) and a
   `script_model` places it at the brushes' centre. Proof: `tools/stock_overlay_test.py` on mp_br_tut (the model and
   its physics asset are in the zone, in the level model table, placed in the entity text, ACTS tests the zone like
   the stock one, the techsets zone loads with 0 failed) and a GUI check of the plan.
''', p)
save(p, s)

p = 'docs/ROADMAP.md'
s, nl = load(p)
lines = s.split(nl)
for i, line in enumerate(lines):
    if line.startswith('| 11.4 |'):
        a = 'partial (models from other stock maps built offline 2026-11-12, see the update; brushes open)'
        assert a in line
        lines[i] = line.replace(a, 'done offline (2026-11-12): models from other stock maps, and added brushes built as models of a helper map and carried in (research 88 next step 4, `tools/stock_overlay_test.py`); lights and effects as entities. Not seen in game. Earlier note')
save(p, nl.join(lines))

p = 'docs/USER_GUIDE.md'
s, nl = load(p)
s = rep(s, nl, '''Not possible yet: new brushes inside a stock map. Not seen in game.
''', '''10. New brushes: draw brushes and patches as usual (in the worldspawn or grouped in func_groups). File > Rebuild
    Stock Zone builds the visible ones as one model per group (with collision) and places it where you drew it; the
    first part of the rebuild is a normal build of a small helper map, so it takes a minute or two. Brushes made only
    of tool materials (caulk, clip, nodraw), such as the helper brush for hiding props, are not built. Not seen in
    game.
''', p)
save(p, s)

p = 'docs/TEST_CHECKLIST.md'
s, nl = load(p)
s = rep(s, nl, '''- [ ] Start Shipment in the game: say whether it loads and whether the added model and the moved spawn are there.
''', '''- [ ] Draw a box brush with a stock material somewhere open on Shipment before the rebuild. The Console says "added
      brushes (entity 0): 12 triangles ... built as model iw8r_ovl_...".
- [ ] Start Shipment in the game: say whether it loads and whether the added model, the box (can you stand on it?)
      and the moved spawn are there.
''', p)
save(p, s)

p = 'docs/PROGRESS.md'
s, nl = load(p)
s = rep(s, nl, '''    REPOSITORY: the project is in the private GitHub repo IDARETW/IW8r''', '''    ADDED BRUSHES ON STOCK MAPS (research 88 step 4): converter live at 9f1472de (old exe zonetool_94ac6fea.exe):
    carry entries may name a "folder". Editor: core stockOverlayModels (compile_iw8.cpp), main_window_stock_edit.cpp
    StockOverlayPlan / overlayMapText / runStockEdit (the helper map mp_iw8rovl is built with IW8Radiant.exe
    --compile, then edits.json gets its package folder). Proof tools/stock_overlay_test.py; GUI check in
    qt_gui_test_loot.cpp.
    REPOSITORY: the project is in the private GitHub repo IDARETW/IW8r''', p)
save(p, s)
print('ok')
