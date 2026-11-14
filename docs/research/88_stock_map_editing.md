# 88. Stock map editing: working plan and notes

> **2026-11-13:** the patch files (`<zone>.fp`) this note installs do not load in the game on its signed stock
> zones. Edits are installed as whole unsigned zones now: [research 116](116_stock_zone_install.md).

Date: 2026-11-12. Owner decision (asked which large features the finished product must include): "Stock map editing,
warzone-specific editing and map/mode creation". This file is the working record of the stock map half; the Warzone
survey is research 87. It replaces the limits listed in research 48 where they differ.

Nothing in this file has been seen in game.

## What changed since research 48

Research 48 assumed a stock zone can only be changed through the dump files (`dumpzone`, patch a record, `buildzone`),
which takes minutes and limits edits to fixed-size patches. Two facts found on 2026-11-12 change that:

1. The fork loads a stock map zone into memory in seconds and writes it again. With the zone's pointer markers kept the
   written data equals the stock data byte for byte (`IW8_REWRITE=1 zonetool verifyzone mp_shipment`: "the written zone
   data is equal to the zone data", 4 s; mp_br_tut 2 s).
2. The same writer takes additional definitions that are in memory, and a definition's dependencies can be found by
   writing a scratch zone (`asset_closure`, research 86 step F3). So assets of any other stock zone can be added.

## The converter command (fork commit 3c6606d6, branch `iw8radiant/scriptables`, worktree `C:\zsw-iw8r\scriptables`)

`zonetool -game iw8 -path <game folder> editzone <zone> <edits.json> <output folder>`

Reads `<game>\zone\<zone>.ff` (and `techsets_<zone>.ff` when needed), never writes there. Writes `<output>\<zone>.ff`,
`<output>\techsets_<zone>.ff` when materials were added, and `<zone>.edit-report.json`.

Edits (all optional):

| Key | Meaning |
|---|---|
| `entities` | Path of an entity text in the game's own form (numeric keys). Replaces the entity string. |
| spawn entities | A stock map keeps its spawn points only in a compiled list. When the entity text has `mp_*_spawn*` entities (classname key 212, origin 709, angles 80, target 1070, script_noteworthy 845) they become the compiled list in file order and are taken out of the text. With none in the text the stock list stays. `"rebuild_spawns": false` keeps the stock list always. |
| `hide_static_models` | List of indices into `GfxWorld.smodels.smodelInstanceData`; each gets scale 0. Collision is not changed. Whether scale 0 hides the model in game is not known. |
| `carry` | List of `{"zone", "type", "name"}`: assets of other stock zones, added with everything they use. Materials and technique sets go to the techsets companion. A carried `xmodel` is added to the level's model table (`ncs_mdl_level`, NetConstStrings type 0), so a `script_model` entity can use it. |

The edited zone is written "fresh" (the writer's own layout, as the converter's custom maps are), not with the stock
pointer layout: a stock zone aliases data by absolute offset, and any edit that changes a size moves the data. A first
try that kept the layout and added assets gave 16 failed assets in the ACTS loader test.

## Proof (`tools/stock_edit_test.py <work folder>`, on mp_shipment; 15 checks pass)

- The stock entity text plus the 611 stock spawns written as entities goes through `editzone`: the entity string read
  back equals the stock one and the compiled spawn list equals the stock list, names included. A spawn without a
  target holds the empty string, not the null string.
- One spawn moved by 16 units, one `script_model` entity added that uses a door model of mp_backlot2, that model
  carried (12 assets into the map zone: the model, 4 surface sets, 7 images; 3 into the techsets zone: 2 materials,
  1 technique set), two static model instances hidden: ACTS loads the map zone and the techsets zone with 0 failed
  (the same "1 unavailable" as the stock zone), the entity text and the moved spawn read back, the model is in the
  zone and in the level model table.
- Time: a few seconds per run. The old dump path took minutes.

## What an edited stock map still cannot do

- New world geometry inside the stock world itself. Added brushes are built as models instead (done 2026-11-12,
  next step 4 below), which draws and collides the same for a player.
- New static models in the world's own tables, new scriptable instances, lights. A new prop is a `script_model`.
- Playing the result without replacing the stock file: the owner's client takes a stock map of the same name over a
  package ("A stock map of the same name always wins", `DW_MapAvailability.cpp:468`). Giving the edited map a new id
  means renaming every asset that carries the map name (world, collision, entities, sound bank, vision, streaming
  info, key value pairs) and its script; not done. So an edited stock zone is installed over the stock file by the
  owner, with a backup.

## Editor side (2026-11-12)

- `src/core/stock_zone_edit.*`: the model catalogue (`data/asset_catalog/iw8_models.tsv`), the carry list (models the
  document places that neither the edited zone nor an always-loaded zone holds; from the first other `mp_` zone), the
  hidden prop list (worldspawn `iw8r_hide_smodels`), and the edits JSON. Core tests `stock_zone_edit_*`.
- `src/qtapp/main_window_stock_edit.cpp`: `prepareStockEdit` writes `edit.ents` (the entity text; with the spawn
  entities when Edit Compiled Spawns was used) and `edits.json`; `rebuildStockZoneFast` runs the converter, then the
  ACTS loader test on each zone written, and reports in the status line and the Console. Menu: File > Rebuild Stock
  Zone. The dump script path stays as "Rebuild Stock Zone (dump script, slow)".
- End to end (`tools/stock_edit_editor_check.py` on the files the GUI test leaves): an unchanged mp_shipment document
  with its 611 spawns as entities rebuilds to the stock entity string and the stock spawn list, ACTS 0 failed.

## Next steps

1. Editor: File > Rebuild Stock Zone through `editzone` (entities, spawns, models the document adds), with the ACTS
   check, in place of the dump script.
2. Done 2026-11-12: File > Hide Stock Props In Selection / Show All Stock Props (`StockModelInstance::index`,
   worldspawn `iw8r_hide_smodels`, the reference display skips hidden rows). GUI check added 2026-11-12
   (`qt_gui_test_loot.cpp`, with injected props). Brushes and patches of the document are no longer written into the
   entity text of a stock map (`splitStockDocument`): a helper brush left in the document used to end up there.
3. Done 2026-11-12: the edit installs as patch files. `editzone` writes `<output>\patch\<zone>.fp` (fork 1ad5320d,
   `write_patch`); the game applies `<zone>.fp` beside `<zone>.ff`, so the stock zones are never replaced. Proof: a
   copy of the stock mp_shipment.ff with the 0.1 MB patch beside it loads in ACTS (its own patch reader) with 0
   failed and reads back as the edited entities and spawns (`tools/stock_edit_test.py`, part 3). Editor: File >
   Install Edited Stock Map Into Game / Restore Stock Map In Game (core `stock_install.*`, tests on temporary
   folders; a stock patch of the same name is saved as `<name>.stock` in the app data folder `stock_backup\<zone>`).
   Only the owner runs them; the session never writes to the game folder itself. This replaces the "installed over
   the stock file" note above.
4. Done 2026-11-12: added brushes as a carried custom model. File > Rebuild Stock Zone turns the visible brushes and
   patches of the worldspawn and of each func_group into one Y-up OBJ model each (core `stockOverlayModels`; tool-only
   brushes such as a caulk helper and other brush entities are left out with a note), writes a helper map
   (`mp_iw8rovl`: a room, the spawn set a build needs, one targeted `script_model` per model so it stays a live model
   of its own name with its physics) and builds it with the editor's own headless build (stock textures, collision).
   Then `editzone` carries each model from the helper package (carry entry with `folder`, fork 9f1472de) and a
   `script_model` places it at the brushes' centre. Proof: `tools/stock_overlay_test.py` on mp_br_tut (the model and
   its physics asset are in the zone, in the level model table, placed in the entity text, ACTS tests the zone like
   the stock one, the techsets zone loads with 0 failed) and a GUI check of the plan.
5. The Warzone maps with the same command (research 87).
6. Done 2026-11-12: a hidden prop loses its collision too. `editzone` `remove_static_model_collision`
   [{model, origin}] (fork 63d4f338) removes the instance of that model at that place (half a unit) from the
   collision map's static model collision lists (model names compared without their folder: the world says
   `tm/<name>`, the lists `<name>`); the editor sends the hidden props of the stock reference. Proof:
   `tools/smodel_collision_remove_test.py` on mp_shipment (3 of 4085 instances removed, exactly those, the props at
   scale 0, ACTS like stock). The big maps keep this collision in their collision tiles (research 90), not edited.
