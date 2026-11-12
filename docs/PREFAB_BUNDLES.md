# Prefab bundles

A prefab bundle is one folder that holds everything a prefab needs: its geometry and entities, its behaviour (script),
its own materials, models, sounds and effects, and the names of the stock assets it uses. A map places it as one
object. This page describes the format and the core code, and lists what the editor window does with bundles. The
design it follows is `docs/research/83_stock_door_and_prefab_bundles.md`, Part B. Status: the core half and the window
half are built and tested; what is still open is listed at the end.

## Terms

- **Prefab**: a saved group of brushes and entities that a map places as one object, with a `misc_prefab` entity whose
  `model` key names it.
- **Placement**: one `misc_prefab` entity. Ten doors in a map are ten placements of one prefab.
- **Plain prefab** (also "bare"): the old form, a single `.map` file. It keeps working exactly as before.
- **Bundle**: the new form, a folder named `<name>.iw8prefab`.
- **Manifest**: the file `prefab.json` in the bundle. It says what the bundle is and what it needs.
- **Contents**: the `.map` file inside the bundle (`prefab.map`) that holds the brushes and entities.
- **Stock asset**: a model, material, sound or effect that ships with the game. A bundle names stock assets; it never
  holds their files.
- **Build feature**: something the build (editor plus converter) can or cannot do yet, for example writing scriptable
  objects. A bundle can say which features it needs.
- **Fallback**: a second set of contents inside the bundle, built when a needed feature is missing.
- **Placement id**: a short text such as `p7` in the key `iw8r_pid` of a placement. It makes the names inside that
  placement unique.
- **Local name / global name**: an entity name (a `targetname`, for example) that belongs to one placement (local) or
  that must stay the same everywhere (global).

## The folder

```
<name>.iw8prefab/
  prefab.json      the manifest (required)
  prefab.map       the contents, an ordinary iwmap 4 prefab (required)
  thumb.png        optional picture for the browser
  script.gsc       optional script
  fallback.map     optional second contents
  fallback.gsc     optional script for the fallback
  materials/       the mapper's own material definitions (*.iw8mat.json) and their images
  models/          the mapper's own model files (.obj, .glb, .gltf)
  sounds/          the mapper's own sound files
  effects/         the mapper's own effect files (*.iw8fx.json)
```

A map places a bundle by naming the folder: `"model" "bundles/doors/stock_door_wood_single.iw8prefab"`. The folder is
looked up like a plain prefab: next to the map, in the map's parent folders, then in the prefab folders (Preferences)
and in `data\prefabs`. Naming the contents file inside a bundle (`.../x.iw8prefab/prefab.map`) means the bundle too.
Any other `.map` path is a plain prefab.

## The manifest (`prefab.json`)

UTF-8 JSON. Only `format` and `id` are required. A field this editor does not know is kept when the editor writes the
file again. A wrong type or value stops the bundle from loading, with a message that names the field, for example
`prefab 'door.iw8prefab': prefab.json: 'placement.surface' must be one of floor, wall, ceiling, free (found "roof")`.

| Field | Type | Meaning |
|---|---|---|
| `format` | text | `"iw8r-prefab-1"`. Another value: the bundle is listed but cannot be placed or built. |
| `id` | text | 1 to 40 characters of `a-z 0-9 _`. Names the prefab kind; it is part of the script function names. Two bundle folders with the same id in one map stop the build. |
| `title` | text | Name to show. Default: the folder name without `.iw8prefab`. |
| `description` | text | One or two sentences. |
| `category` | text | Group to show it under. Default: the name of the folder that holds the bundle. |
| `tags` | list of text | Search words. |
| `author`, `version` | text, number | For display. |
| `map` | text | Contents file. Default `prefab.map`. It must exist. |
| `thumbnail` | text | Picture file. Default: `thumb.png` when it exists. |
| `pivot` | text | What the origin of the prefab means, as a hint (`"hinge, floor level"`). |
| `bounds` | `{mins:[x,y,z], maxs:[x,y,z]}` | Size relative to the origin, for a drag preview. |
| `placement.surface` | text | `floor` (default), `wall`, `ceiling` or `free`. |
| `placement.snap` | number | Grid step for the drop; 0 or absent means the editor grid. |
| `placement.yaw_step` | number | Degrees one wheel step turns it while dragging. Default 90. |
| `names.local` | list of text | The names that are made unique per placement. When this list is present, nothing else is renamed. |
| `names.global` | list of text | Names that are never renamed. |
| `names.keys` | list of text | Extra keys whose values are entity names. |
| `parameters[]` | list | Inspector controls. Each has `name`, `label`, `type` (`number`, `choice`, `material`, `text`), `default`, `min`, `max`, `choices`, and `apply`: either `{selector, key, template}` or `{material}`. |
| `script.file` | text | Script file. Default `script.gsc` when it exists. |
| `script.entry` | text | Function started once per placement. Default `prefab_main`. |
| `script.prefix` | text | The prefix every function name starts with. Default `prefab_`. |
| `script.once` | text | Optional function started once per map, before the placements. |
| `assets.materials[]`, `assets.models[]`, `assets.effects[]` | `{name, file}` | The mapper's own files inside the bundle. A listed file that is missing is a warning. |
| `assets.sounds[]` | `{alias, file, loop}` | The mapper's own sounds. |
| `requires.stock_models[]`, `requires.stock_materials[]`, `requires.scriptable_defs[]` | `{name, zones[]}` | Stock assets by name, with the game zones that hold them. |
| `requires.stock_sounds[]`, `requires.stock_effects[]`, `requires.stock_strings[]`, `requires.stock_scripts[]` | list of text | Stock names. |
| `requires.build[]` | list of text | Build features: `scriptable_instances`, `scriptable_def_carry`, `stock_script_calls`, `custom_sounds`. |
| `fallback.when_missing[]` | list of text | Build features whose absence selects the fallback. |
| `fallback.map`, `fallback.script` | text | The fallback contents (required when there is a `fallback`) and its script. |
| `fallback.note` | text | Line written to the build log when the fallback is built. |
| `game_modes[]` | list of text | Modes the prefab is for; empty means all. |

A parameter writes one key on the placement, so the map format does not change. `{selector, key, template}` writes
`iw8r_ovr:<selector>:<key>` with `{<parameter name>}` in the template replaced by the value. `{material}` writes
`iw8r_mat:<material>` with the value as the new material. The selector names an entity as the prefab's own contents
name it (before the placement id is added).

## Names per placement

Problem: a prefab with a door named `door` and a trigger that targets `door`, placed twice, gives two doors with one
name, and each trigger opens both.

Rule: when a bundle is expanded, every **local** name becomes `<placement id>_<name>`. Placement `p7` turns `door`
into `p7_door` in every key that holds names: `targetname`, `target`, `target2` to `target8`, `script_linkname`,
`script_linkto`, `iw8r_teleport`, `iw8r_visual`, and the keys listed in `names.keys`. A value with several names
(`script_linkto "a b"`) is handled name by name.

Which names are local:

1. With a `names.local` list: exactly the names in it.
2. Without one, a name is local when
   - it is defined and referred to inside the prefab (a `targetname` that a `target` of the same prefab points at), or
   - it is the `targetname` of an entity that refers to such a name (the trigger that opens the door), or
   - it is the `targetname` of an entity with one of the editor's behaviour keys (`iw8r_door`, `iw8r_teleport`,
     `iw8r_jump`, `iw8r_damage`, `iw8r_lift`, `iw8r_spin`, `iw8r_bob`, `iw8r_anim`, `iw8r_vision`,
     `iw8r_no_collision`), because the generated map script finds those entities by name.
3. A name in `names.global` is never local. Use it for a name a stock script looks for (`flag_primary`, `hqloc`,
   `hardpoint_zone`).

A name the prefab only points at (a `target` whose `targetname` is somewhere else in the map) is never renamed, so a
prefab can still reach an object outside itself. To reach into a placement from outside, write the full name
(`p7_door`).

The placement id:

- The editor should give every placement the smallest unused `p<N>` when it is placed, and a new one when a placement
  is copied (`nextPrefabPlacementId`). It never changes afterwards, so names stay the same across saves.
- A bundle placement without an id, or with an id another placement already uses, still builds: it gets `a<N>` for
  that build and the build log says so.
- A prefab inside a prefab chains the ids: `p7_p2_door`. A plain prefab that holds a bundle needs an id too, and gets
  one the same way.
- A plain `.map` prefab is expanded exactly as before, with no renaming. When two placements of one share a linked
  name, the build log now says so and suggests saving it as a bundle.

## Scripts

A bundle can bring a script. It is written once and works for every placement.

Rules for the author:

- Every function name starts with `prefab_`.
- `prefab_main(inst)` is the entry. It is started once per placement. `inst.pid` is the placement id, `inst.origin`
  and `inst.angles` are where the placement is.
- `iw8r_pname(inst, "door")` gives the name the prefab's entity `door` has in this placement (`"p7_door"`).
- `iw8r_trigger(name)` gives the object to wait on for a trigger of the map (the map script tests triggers itself).
- No `#include` or `#using` lines: the script is placed inside the generated map script.

Example (`script.gsc` of a bundle with id `lamp_post`):

```
prefab_main(inst)
{
    wait 0.05;
    lamp = getent(iw8r_pname(inst, "lamp"), "targetname");
    if (!isdefined(lamp))
        return;
    level thread prefab_blink(lamp);
}

prefab_blink(lamp)
{
    for (;;)
    {
        lamp hide();
        wait 1;
        lamp show();
        wait 1;
    }
}
```

What the build writes into the map script, for two placements `p1` and `p2`:

```
    level thread iw8rp_lamp_post_main(iw8r_prefab_inst("p1", (0, 0, 0), (0, 0, 0)));
    level thread iw8rp_lamp_post_main(iw8r_prefab_inst("p2", (300, 0, 0), (0, 90, 0)));
...
iw8rp_lamp_post_main(inst)
{
...
```

The script text is included **once per prefab kind**. Every identifier that starts with `prefab_` is renamed to
`iw8rp_<id>_...`, so two prefab kinds can both have a `prefab_main`. Only whole identifiers are renamed: text inside
strings and comments stays. Fields are identifiers too (`level.prefab_count` becomes `level.iw8rp_lamp_post_count`),
which keeps the level fields of two kinds apart. The build log lists problems of a script (no entry function, a
function that does not start with the prefix, a function defined twice, an `#include`).

A map that places no bundle with a script gets the same map script as before, byte for byte.

## Requires and fallback

`requires` says what the bundle needs. `fallback` says what to build when the build cannot provide it.

- **Build features.** The build has a set of features (`PrefabBuildSupport::features`). Today it holds `custom_sounds`
  only. `scriptable_instances`, `scriptable_def_carry` and `stock_script_calls` need converter work that is not done.
- **The choice** (`choosePrefabContents`): when the bundle has a fallback and at least one feature of
  `fallback.when_missing` is absent, the fallback contents and the fallback script are built. Otherwise the main
  contents and the main script are built.
- **The build log** always says which was built: `prefab bundle 'x' (x.iw8prefab): 2 placement(s), fallback contents;
  ...` and `... fallback contents built: <fallback.note> (missing build feature(s): ...)`. A needed feature that is
  missing and has no fallback is named too; the main contents are then built as they are.
- **Stock names.** When the build is given the stock catalogue (`PrefabStockCatalog`, read from
  `data\asset_catalog`), every required stock model, material, sound and effect that is not in it is a warning that
  names the prefab. Localized strings, scriptable definitions and stock scripts have no catalogue yet and are not
  checked.
- **Scriptable objects.** An entity of class `scriptable` inside a bundle is left out of the build and counted in the
  log while the build lacks `scriptable_instances`.

## What the build does

In the prefab step of `compileIw8Extraction` (before everything else):

1. Every `misc_prefab` is resolved. A bundle's manifest is read; a bundle that cannot be read is left out with its
   reason.
2. Main or fallback contents are chosen, expanded as before (position, angles, scale, `iw8r_ovr:` and `iw8r_mat:`
   keys), then the local names get the placement id.
3. Each bundle script is collected once, with one start per placement, and handed to the map script.
4. The bundles' `models`, `sounds` and `effects` folders are added to the model, sound and effect folders of this
   build. For `materials`, the build loads one material library from `CompileOptions::materialRoots` plus the bundles'
   folders. A caller that passes a ready library without `materialRoots` must have added the bundles' folders itself
   (`prefabBundleAssetRoots` lists them); otherwise a warning says the bundle materials build grey.
5. The log gets one line per bundle kind, plus the lines described above.

A map without a bundle placement takes the old path with untouched options. Proof kept in the tests: the extraction of
a map with plain prefabs is the same, file by file and byte by byte, as before this work.

## Saving a selection as a bundle (core part)

`prefabBundleFromSelection(doc, options)` returns everything the save dialog needs, without writing anything:

- `mapText`: the contents. World brushes go to worldspawn. A selected brush entity stays an entity with its keys (a
  `script_brushmodel` door is saved as a door). Point entities are kept. Everything is moved so the pivot is the origin.
- `pivot`: the bottom centre of the selection's bounds, snapped to the grid, unless the options give a point.
- `manifest`: id (from the name), title, description, category, tags, bounds, `names.local` (by the rule above),
  `names.global` (names the selection points at but does not hold), the asset lists and `requires`.
- `files`: the mapper's own files to copy (found in the material, model, sound and effect folders given in the
  options). A model brings the materials it uses and the files next to it with the same name (`.mtl`, `.bin`). A
  material definition is rewritten so it names itself and its images by file name (`generated`).
- Every material, model, sound and effect that is not the mapper's own is written into `requires` by name, with the
  zones of the catalogue when one is given. With a catalogue, a name that is in neither place is a note for the mapper.
- `linkedOutside`: entities outside the selection that target it or are targeted by it, for the dialog's tick boxes.
- `notes`: things the mapper should know (script problems, an unknown material).

`writePrefabBundleFolder(folder, result, &err)` writes the folder.

## The two bundles shipped with the editor

Both are text files only (`data\prefabs\bundles`): names, numbers and editor-made script. No game asset is in them.

**Jump pad with effect and sound** (`bundles/gameplay/jump_pad.iw8prefab`). A stock manhole cover model
(`tm/me_manhole_cover_02`), a `trigger_multiple` with `iw8r_jump "0 0 700"`, an `fx_origin` with the stock effect
`vfx/core/mp/core/vfx_marker_base_cyan_pulse`, and a `sound_emitter` with the stock loop
`emt_light_fluorescent_hum1_int`. One parameter, `speed`, writes the launch speed. The trigger name is local, so two
pads never share a trigger. It needs no script and no missing build feature. Not verified in game: that `iw8r_jump`
launches a player (roadmap 6.12), and how the chosen effect looks on the ground.

**Stock door, painted wood, single** (`bundles/doors/stock_door_wood_single.iw8prefab`). This is the first prefab of
the design.

- Main contents: the door frame model and one `scriptable` entity with the stock definition
  `scriptable_door_wooden_panel_03_painted_mp`. This is the real stock door. It needs the build features
  `scriptable_instances`, `scriptable_def_carry` and `stock_script_calls`, which do not exist yet.
- Fallback contents, built today: the door from stock parts. The stock door leaf model
  (`door_wooden_panel_03_painted_left_mp_nophysics`) as a live `script_model` at the hinge, the stock frame model, and
  `fallback.gsc`: it opens 90 degrees away from the player in 0.8 s and closes in 0.4 s (the stock numbers), plays
  `scrpt_door_wood_single_open` and `_close`, and shows "Open Door" / "Close Door" through `makeusable`,
  `setuserange(125)` and `sethintstring`. Because the owner's playtest found the game's own triggers silent in built
  maps, the script also tests the players itself (alive, within 125 units of the use point, Use pressed).
- The build log says `fallback contents built: stock door built as a scripted door from stock parts ...` so the
  imitation is never presented as the real door.

Unknowns the door depends on (none can be settled without a game run):

1. Whether a usable `script_model` shows the game's own prompt in an editor-built map. If not, the door still opens
   through the script-side test, without a prompt.
2. Whether the converter's copy of the stock leaf model keeps its hinge at the model origin and runs along +X (taken
   from the definition's use offset `45 0 35`). If the leaf runs the other way, the door swings toward the player.
3. Where the frame sits. It is placed 26 units along the leaf from the hinge, as the stock maps place the frame next
   to the static door of the same family (`data\prefabs\stock\door\tm_door_wooden_panel_03_painted_single_mp_v01.map`).
4. Whether the two stock models reach the build as copies: the editor exports stock models with ACTS when it compiles;
   a model it cannot export is left out with a warning.
5. Whether the door blocks players: the map script calls `solid()` on it, as for other live models.
6. The prompt words are plain English text. The game's own translated words (`MP/DOOR_USE_OPEN`) need a converter
   change.

## Choices made where the design left it open

- **Folder of the shipped bundles**: `data\prefabs\bundles\<category>\`, a new folder next to `iw8r` and `stock`
  (`data\prefabs` itself already existed). `tools\package_release.ps1` copies `data\prefabs` whole, so it needed no
  change.
- **Inferred local names** go further than "defined and referred to inside": the trigger that targets a local name
  and entities with behaviour keys are local too. Without this, two doors made of the editor's own door behaviour
  would still share their trigger name and open together.
- **`resolvePrefabPath` on a bundle** returns the contents file a default build uses (the fallback when it is needed),
  so the existing viewport draws what is built without any change.
- **A missing placement id** is not an error: the build uses `a<N>` and says so.
- **Fallback script**: when the fallback is built, only `fallback.script` is used (the main script may need the
  missing feature).
- **`script.once`** is started with `level thread`, before the placement threads.
- **Unknown fields inside list items** (`parameters[]`, `assets.*[]`, `requires.*[]`) are kept when the item keeps its
  `name` (or `alias`).
- **Material library**: the build cannot add folders to a library it was handed, so `CompileOptions::materialRoots`
  was added (see "What the build does").
- **Scriptable instances** are not written for the converter yet (it cannot read them). They are left out and counted.
- **A material definition without a `name`** in a subfolder is named by its file name by the asset usage scan and by
  its path by the material library. Saving goes through the usage scan, so give such definitions a `name`.

## In the editor

What the window does with bundles (`docs/USER_GUIDE.md`, section "Prefabs", is the mapper's description).

- **Prefabs dock**: lists every bundle of the prefab folders and of `data\prefabs\bundles` first, with the manifest's
  title, and description, tags and parameters in the tooltip. The picture is `thumb.png` when the bundle has one, else it
  is drawn from the contents a build makes (triggers and clips as dashed outlines; the bundle's own models are read from
  its `models` folder). The `.map` files inside a bundle folder are not listed on their own. The filter looks in title,
  tags, category, id and description; every word typed must be found.
- **Badges** on the picture, with the reason in the tooltip: FALLBACK (`choosePrefabContents` chose the fallback),
  LIMITED (a needed build feature is missing and there is no fallback), MISSING (a required stock name is not in
  `data\asset_catalog`), CANNOT PLACE (the bundle does not load; it cannot be dragged or placed).
- **Place and drop**: a bundle becomes one `misc_prefab` whose `model` is the bundle folder and whose `iw8r_pid` is
  `nextPrefabPlacementId`. `placement.snap` snaps the drop on the floor plane. While a prefab is dragged over the Camera
  a provisional placement follows the cursor (the newest undo step; leaving takes it back, dropping keeps it).
- **Copies**: Edit > Clone, Edit > Paste and Arrange > Array / Radial Duplicate / Mirror Copy give every copied
  placement whose id is already held a new id, inside the same undo step.
- **Entity dock**: for a selected bundle placement a panel above the key list shows title, description, badge and
  reason, and one field per `parameters[]` entry (number with `min` / `max`, choice, material, text; label, range and
  default shown). A field writes `prefabParameterKey`; **Default** removes the key; `prefabParameterValue` reads the
  value back.
- **Save Selection As Prefab**: a dialog with id, title, description, category and tags and the list of what
  `prefabBundleFromSelection` found (contents, the mapper's own files, stock assets by name, local names, linked
  objects outside the selection with tick boxes, notes). It writes the folder with `writePrefabBundleFolder` into the
  first prefab folder of the Preferences (else `prefabs` beside the map), in a subfolder named after the category, and
  a 256 pixel `thumb.png` when every model could be drawn. A bundle with the same id in that prefab folder is replaced
  in its own folder, only after a question; `parameters`, `script.*`, `fallback.*`, `placement.*`, `names.keys`,
  `game_modes`, `version` and unknown fields of the old `prefab.json` are kept. File > Save Selection As Plain .map
  Prefab is the old save.
- **Explode** passes `prefabNameScope` to `explodePrefab`. **Enter Prefab** opens the bundle's main contents
  (`prefab.map`), also when a build makes the fallback. **Open Bundle Folder** shows the folder. **Make Unique** copies
  the bundle into `prefabs` beside the map as `<id>_u2` (`_u3`, ...) and points the placement at the copy.
- **Assets while editing**: `prefabBundleAssetRoots` of the open map is worked out again when a placement comes or
  goes; the `materials` folders join the map's material library and the `models` folders join its model lookup.
- **Build** (Compile panel): the bundles' material and model folders join the mapper's folders, `materialRoots` and
  `prefabSupport.stock` (the catalogue of `data\asset_catalog`) are passed on, and the log shows the core's lines. The
  line `prefab bundle '<id>' (...): N placement(s), main contents` is plain; every other bundle line is a warning.

## Still open

1. **Drawing in the Camera** (`src\viewport\scene.cpp`, `buildPrefab`): a prefab's brushes are drawn in one flat colour
   and every point entity inside it, a model included, as a 16 unit box. Wanted: the model meshes of `misc_model`,
   `script_model`, `dynamic_prop` and `scriptable` entities inside a prefab (through the scene's `modelSource`), the
   materials of its brush faces, and a see-through look for the drag preview.
2. (Done 2026-11-12) **Placement hints**: Insert Prefab uses `placement.surface`: a `wall` prefab placed on a wall
   turns its +X out of the wall (yaw from the face normal, rounded to `placement.yaw_step`), `snap` snaps x and y (and
   the height on a wall); the status bar says when the surface does not fit. File > Rotate Selected Prefab turns a
   placement by its `yaw_step` (90 for a plain prefab). Core `prefabPlacementFor`.
3. **No editor for parameters, script and fallback**: they are written into `prefab.json` and the bundle folder by
   hand. (Done 2026-11-12: File > Convert Plain .map Prefab To Bundle wraps a plain `.map` with the pivot at its own
   origin, as Save Selection As Prefab does with the mapper's own assets; File > Export Prefab Bundle As Zip and Import
   Prefab Bundles From Zip share bundles: the import reads stored and deflated zips (Windows "Compressed folder"),
   takes "<name>.iw8prefab/" folders or one bundle's files at the top, never replaces an existing bundle and checks
   each bundle before it lands in the prefab folder. Core `prefabBundleFromPlainMap`, `exportPrefabBundleZip`,
   `importPrefabBundleZip`; `extractZip` inflates with stb.)
4. **Core**: the map package must pack bundle folders (`map_package.cpp`), the build cache should notice a changed
   bundle script or manifest (`build_cache.cpp`), the entity class `scriptable` and the catalogue of scriptable
   definitions, and a `docs\KEYS_REFERENCE.md` entry for `iw8r_pid`.
