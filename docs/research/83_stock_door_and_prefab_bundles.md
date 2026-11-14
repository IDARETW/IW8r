# 83. The real stock door, and prefabs as bundles

Date: 2026-11-11. Analysis and design only. No source file, game file or converter file was changed. The game was not
started or touched. Modern Warfare 2019 (Replay 1.20) data only.

Owner request (their words): "the door is made up, it should be a real door from one of the stock maps, it has the full
UI tooltip and other parts that this is missing. the door should come from the prefabs. the prefabs are meant to be
bundles of data, like a model, script behaviour, other pieces of data, basically the whole set of data attributed to that
object/set of objects, and saving it as a prefab that can be dragged and dropped and seen in the viewport".

Marks used on every statement: **[V]** verified (file and line, or asset path, given), **[I]** inferred (reasoned from
verified facts, not seen directly), **[U]** unknown.

Roots used in citations:

- `ED:` = `E:\IW8-Radiant` (the editor).
- `X:` = `E:\mw124\x64-zt\src` (the converter fork, branch `iw8radiant/build-extraction`, read only).
- `STK:` = `E:\mw124\tools\dvar_search_web\src\combined\rawfile_source\scripts` (decompiled stock scripts of a later
  build, "IW8 8.19 build win64 Apr 18 2020", `E:\mw124\tools\dvar_search_web\out\README.md:13`; most text in it is
  encrypted as `_encstr_<hex>`).
- `SC:` = `%USERPROFILE%\AppData\Local\Temp\claude\2b25dc13d65a86cc\f955f107-10f4-450a-a1df-e62aedc1d809\scratchpad\r83`
  (scratch folder of this session: read-only ACTS exports of Replay 1.20 files and small analysis scripts).
- `TR:` = `ED:out\x64zt-verify\runs` (lists of the assets each Replay zone holds, lines `A <type number> <name>`).

Terms, defined once:

- **Zone / fastfile**: one `.ff` file of the game; it holds assets (models, scripts, sounds). A map has its own zones;
  some zones are loaded for every multiplayer match ("always loaded").
- **Entity**: one object of a map written as text keys (`classname`, `origin`, `targetname`...). The list of them is the
  **entity string**.
- **Scriptable**: the game's native scripted object. Its behaviour is not written in script: it is a data asset, the
  **scriptable definition** (`ScriptableDef`), made of **parts**; each part has **states**; entering a state fires
  **events** (show this model, use this collision, rotate, play this sound).
- **Scriptable instance**: one placement of a definition in a map (definition name, position, angles). Instances are
  not in the entity string. They are a separate binary list inside the map's entity asset (`MapEnts`), called the
  **scriptable block** in this document.
- **Converter / fork**: x64-zt, the tool that turns the editor's output into game zones.
- **Localized string**: a text the game looks up by a name such as `MP/DOOR_USE_OPEN` and shows in the player's language.
- **Prefab**: a saved group of objects that is placed as one object. **Bundle**: the new prefab form designed in Part B.

---

## 0. Short answers

1. **What a stock door is.** A stock door is not an entity and not a brush. It is one scriptable instance in the map's
   scriptable block, for example definition `scriptable_door_wooden_panel_03_painted_mp` in mp_backlot2. The "Open Door"
   / "Close Door" prompt, the swing, the collision and the sounds all come from the definition. One always-loaded stock
   script (`scripts/engine/scriptable_door`) reacts to Use and to a sprinting player and picks the next state. [V]
2. **Can an editor map have it today?** No. Answer **(b), and more than (b)**: the converter must write the scriptable
   block (it writes it empty today), **and** it must put a full copy of the door definition and its two models into the
   map's own zones, because every door definition lives only in stock map zones, which are not loaded with a custom
   map. Neither piece exists on any branch of the fork. [V for the gaps, section A.3]
3. **What is possible today, (a).** A door built from the real stock parts: the stock door model, the stock door sounds
   (always loaded), the stock prompt words, and the stock timing, driven by the editor's generated script. It is an
   imitation of the behaviour, not the native object, and its prompt is not proven in game. [I]
4. **Prefabs.** Today a prefab is a single `.map` file with brushes, patches and entities; it carries no assets, no
   script, no description, and names clash when it is placed twice. The bundle is a folder with that same `.map` plus a
   manifest, a script, the custom assets, and names of the stock assets it needs. Old prefab files keep working
   unchanged. [V for today, design below]

### Corrections to earlier research

- Doc 53 section 0 says no door exists in the two dumped stock maps. That is true for the entity string only.
  mp_backlot2 has **19 door instances** in its scriptable block (section A.1). [V]
- Doc 45 section 3 and doc 62 section 2.1 describe stock doors as `script_brushmodel` or `script_model` entities run by
  `scripts/mp/door.gsc`. That script exists and handles entities named `dynamic_door`, but none of the seven stock maps
  checked places one. The doors players see in these maps are scriptables. [V]

---

# Part A. What a real stock door is

## A.1 Where the doors are

How this was checked: the entity asset (`map_ents`) of seven Replay 1.20 maps was read. Two were already in
`ED:data\stock_dumps`. Five more were exported read-only with ACTS into `SC:mapents` with the command given in the task.
Script `SC:scripts\doorscan.py` lists, per map, the instances whose definition name contains "door" and every entity
with "door" in `classname`, `targetname`, `target`, `script_noteworthy` or `script_parameters`.

| Map | Door instances in the scriptable block | Door definitions | Door entities in the entity string |
|---|---|---|---|
| mp_shipment | 0 | none | none |
| mp_rust | 0 | none | none |
| mp_backlot2 | 19 | 7 | 3, all infil gate props (`infil_door_left`, `infil_door_right`, `infil_door_chain`) |
| mp_hideout | 16 | 7 | the same 3 infil props |
| mp_village2 | 12 | 5 | none |
| mp_aniyah_tac | 52 | 2 | none |
| mp_emporium | 15 | 4 (one is a locker door) | none |

All rows [V] (`SC:scripts\doorscan.py` output; mp_backlot2 from
`ED:data\stock_dumps\mp_backlot2\mw19replay\mp_backlot2\assets\map_ents\maps\mp\mp_backlot2.d3dbsp.0.asset.json`,
field `asset.fields.scriptableMapEnts.mapInstances`).

- No map has an entity with targetname `dynamic_door` or `lean_dynamic_door` (the names the entity door script looks
  for, section A.2.6). [V]
- The editor's catalogue (`ED:data\asset_catalog\iw8_scripted_objects.tsv`) lists no door kind because it is built from
  entity strings only (`ED:tools\build_scripted_objects.py:235-250`). [V]
- Door definitions per zone, from the zone asset lists (`TR:trace_<zone>.txt`, lines `A 63 <name>`; 63 is the
  scriptable asset type): they appear only in mp_backlot2, mp_hideout, mp_village2, mp_aniyah_tac and mp_emporium. None
  of the always-loaded zones that hold scriptables (common_mp 3, common_core_mp 31, common_base_mp 49, global_stream_mp
  3, global_core_mp 1, code_post_gfx 1) holds a door definition. [V]

## A.2 One door, completely: `scriptable_door_wooden_panel_03_painted_mp`

The plain single wooden door. It is in mp_backlot2 (2 placed), mp_hideout (2) and mp_village2 (1). [V]

### A.2.1 What the map holds

One 72-byte instance record and nothing else. Instance 10 of mp_backlot2: [V, `SC:scripts\doors.py` output]

| Field | Value |
|---|---|
| definition (`initialDef`) | `scriptable_door_wooden_panel_03_painted_mp` |
| `initialOrigin` | 83.5 323.5 69.0 |
| `initialAngles` | 0 180 0 |
| `classname` (a script string) | `scriptable_scriptable_door_wooden_panel_03_painted_mp` (the definition name with `scriptable_` in front) |
| `target`, `targetname`, `script_linkname`, `script_linkto`, `script_noteworthy` | all empty |
| `flags`, `ffMemCost`, `lightIndices` | 0 |
| `mapEntLookup` | 1 (0 on 17 of the 19 backlot2 doors; meaning [U]: it is not an index into the entity string, checked with `SC:scripts\lookup.py`) |

- No entity, no trigger, no brush, no `collmap_scriptable` record belongs to the door. Entities within 80 units of the
  19 doors are path nodes and unrelated objects. [V]
- The door frame is a separate static model placed in the world, for example `tm/door_frame_wooden_panel_03_painted_mp`
  (`ED:data\asset_catalog\iw8_models.tsv:10822`, zones mp_backlot2, mp_hideout, mp_village2). That it stands at each
  door is [I] (the name is in the map's string list; the placement was not matched by position).
- The instance origin is the hinge: the definition rotates the model about its own origin (Move events below), and the
  two leaves of a double door sit 52.85 units apart with angles 90 and 270 (instances 4 and 5). [I]
- Map-wide counts that follow the doors: `serverUsablePartCount` equals the number of usable door instances in
  mp_aniyah_tac (52), mp_hideout (16), mp_village2 (12); every `physicsLimits` count equals the number of door instances
  in mp_backlot2 (19), mp_aniyah_tac (52), mp_village2 (12). [V for the numbers; that the game needs them set is I]

### A.2.2 The definition

Exported read-only from `mp_backlot2.ff` with ACTS (`-a scriptable`) to
`SC:backlot2_scriptable\mw19replay\mp_backlot2\assets\scriptable\scriptable_door_wooden_panel_03_painted_mp.14.asset.json`.
A readable summary is in `SC:door_wooden_panel_summary.txt`; the full field tree is in `SC:door_def_shape.txt`. All [V].

- Header: `flags` 4776647, 1 part, `usablePartCount` 1, `serverInstancedPartCount` 1, `serverControlledPartCount` 1,
  event stream size 32 (server and client), `maxNumDynEntPartsBase` 2, `spatialActivationMode`
  `DYNENT_PRIORITY_SPARSE_OCCLUDER`, no animation tree.
- Models (2): `door_wooden_panel_03_painted_left_mp_nophysics` (drawn) and `door_wooden_panel_03_painted_left_mp`
  (used for collision).
- One part named `door` (part flags 50118) with 18 states:

| # | State | Kind | Prompt | Events on entering the state |
|---|---|---|---|---|
| 0 | `closed` (script id `setup`) | Usable | `MP/DOOR_USE_OPEN` | Model: the `_nophysics` model. Collision: the physics model, `aiObstacle` yes, `canTouch` yes. Move `return_to_center`: angles 0 0 0 in 0.4 s. Sound `scrpt_door_wood_single_close` at `tag_door_handle`. |
| 1-3 | `left_30`, `left_50`, `left_70` | Usable | `MP/DOOR_USE_CLOSE` | Move: yaw 30 / 50 / 70 in 0.4 s |
| 4 | `left_90` | Usable | `MP/DOOR_USE_CLOSE` | Move: yaw 90 in 0.8 s (accel 0.1, decel 0.5). Sound `scrpt_door_wood_single_open`. |
| 5-8 | `right_30` ... `right_90` | Usable | `MP/DOOR_USE_CLOSE` | the same with negative yaw |
| 9-10 | `bash_left_90`, `bash_right_90` | Usable | `MP/DOOR_USE_CLOSE` | Move: yaw +90 / -90 in 0.25 s. Sound `scrpt_door_wood_single_bash`. |
| 11-12 | `explode_left_90`, `explode_right_90` | Usable | `MP/DOOR_USE_CLOSE` | as bash |
| 13-14 | `ajar_owned`, `closed_owned` | Usable | close / open | none |
| 15 | `peek_audio` | Simple | none | Sound `scrpt_door_wood_single_start_peek` |
| 16 | `frozen` | Simple | none | none |
| 17 | `health` | Health | none | none |

- Every Usable state carries the same use settings: `useFOV` 120, `useRadius` 125, `useRadiusDisplay` 200, `useOffset`
  45 0 35, no hint icon, `hintString` as in the table.
- The other six door definitions of mp_backlot2 have the same 18 states and differ in models and sound names
  (`scrpt_door_heavy_metal_single_*`, `scrpt_door_wood_double_*`); summaries in `SC:*.summary.txt`.

### A.2.3 What makes the "Open Door" prompt appear

- The prompt is native. A state of kind Usable carries `hintString`, the use radius and the use angle; the game draws
  the prompt for a player inside them. No script draws it. [V for the data; that the engine draws it from this data
  without script is I, from the script below never setting a hint]
- The words: Replay 1.20's `eng_common_base_mp.ff` holds the localized strings `mp/door_use_open` = "Open Door" and
  `mp/door_use_close` = "Close Door" (exported read-only with ACTS `-a localize` to
  `SC:eng_loc\mw19replay\eng_common_base_mp\assets\localize\mp\door_use_open.972.json` and `door_use_close.969.json`). [V]
  That zone is loaded for every multiplayer match. [I]

### A.2.4 The script that drives it

- File: `scripts/engine/scriptable_door`, script id 1804 (gsc-tool id table,
  `E:\mw124\x64-zt\deps\gsc-tool\src\gsc\engine\iw8_token.cpp:1817`). Replay 1.20 has it in `global_mp.ff`
  (`TR:trace_global_mp.txt:31742`, line `A 52 1804`). [V] That `global_mp` is loaded for every match is [I].
- Replay's own copy was exported (`SC:decomp\in\1804.gscbin`). gsc-tool 1.6.2 cannot decompile it, but its text
  constants are readable (`SC:scripts\gscstr.py`): `door`, `closed`, `setup`, `left`, `right`, `use`, `touch`, `radius`,
  `scriptable_event_model`, `grenade_rumble`, `scriptable_addusedcallbackbypart`, `door_wait_auto_close`,
  `scriptable_door_auto_close`. They fit the decompiled 8.19 file `STK:engine\scriptable_door.gsc` constant for
  constant, so that file is used below to describe the logic. [V for the constants; I that the logic is the same]
- Start: `system_init()` (`STK:engine\scriptable_door.gsc:310-319`) registers three callbacks: after-load, "used" for
  the part named `door`, and "touched". [V in STK]
- Who calls `system_init`: Replay's map script of mp_backlot2 and Replay's `scripts/mp/load` (id 1872,
  `TR:trace_common_base_mp.txt:135403`) both contain the reference pair (file 1804, function `system_init` = token
  0xC4EE, `iw8_token.cpp:50427`). The mp_backlot2 script also references (file 1872, function `main`). Found with
  `SC:scripts\farcall.py`. The 8.19 sources say the same (`STK:mp\load.gsc:150`,
  `STK:mp\maps\mp_backlot2\mp_backlot2.gsc:11-12`). [V]
- Use: the engine calls `scripts/common/scriptable::scriptable_used` (script id 11, in `global_mp`), which forwards to
  the callbacks registered for the part (`SC:decomp\decompiled\iw8\11.gsc`, `1803.gsc:46-88`, Replay's own copies,
  decompiled). [V] The door callback (`STK:engine\scriptable_door.gsc:274-288`) starts
  `scriptable_door_hinge_progression` (`:168-242`): it finds which side of the door the player stands on and sets the
  part state to `<other side>_90`, or to `closed` when the door is open, with `setscriptablepartstate`. [V in STK]
- Sprinting into the door: the touched callback (`:290-294`) asks `shouldbashopen` (`:143-166`: speed at least 200,
  facing the door) and sets the bash state, with rumble and a small screen shake (`bashopen`, `:131-141`). [V in STK]
- A door with a `target` also moves its partner door (`:278-287`); the backlot2 doors have no `target`. [V]
- Auto close exists but is off unless a script sets `level.scriptable_door_autoclose_delay` (`:244-272`, `:321-327`). [V]

### A.2.5 Sounds and collision

- Sounds are aliases in the always-loaded bank `common_mp`: `scrpt_door_wood_single_bash`, `_close`, `_open`, `_start_peek`
  (`ED:data\asset_catalog\iw8_common_sounds.tsv:8090-8093`; heavy metal and double wood
  sets at `:8078-8089`). [V]
- Collision comes from the Collision event of state `closed`: the physics model
  `door_wooden_panel_03_painted_left_mp`, which has a physics asset and a detail-collision asset of the same name in
  mp_backlot2 (`TR:trace_mp_backlot2.txt:5394` type 3, `:79242` type 101, `:87981` type 9). [V] The collision follows
  the door as it swings and blocks bots (`aiObstacle`). [I]
- Both models, the physics asset and the detail collision are in map zones only
  (`ED:data\asset_catalog\iw8_models.tsv:8537-8538`: mp_backlot2, mp_hideout, mp_village2). [V]

### A.2.6 The other door system (not used by these maps)

`scripts/mp/door` (id 1839, Replay copy in `common_base_mp`, `TR:trace_common_base_mp.txt:135206`) handles **entities**:
`getentarray("dynamic_door", "targetname")` and `"lean_dynamic_door"`, a model with `tag_door_handle`, prompts
`MP/DOOR_USE_OPEN_DOUBLE`, `MP/DOOR_USE_LOCK`, `MP/DOOR_USE_ALARM`, a clip brush reached through `target`, bash and
damage monitors (text constants of Replay's own copy, `SC:decomp2\in\1839.gscbin`; logic in `STK:mp\door.gsc:4-125`). [V]
It is started for every match in the 8.19 sources (`STK:mp\gamelogic.gsc:3203`); whether Replay 1.20 starts it is [U].
No checked stock map places such an entity, so there is no stock example to copy, and which model, tags and keys it
needs is [U] beyond the constants above. It is a lead, not a recipe.

## A.3 What an editor-built map needs for the stock door

### A.3.1 Needed assets and where they are

| Needed | Where it is in Replay 1.20 | Reaches a custom map? |
|---|---|---|
| Door script `scripts/engine/scriptable_door` (1804), `scripts/common/scriptable` (11), `scripts/engine/scriptable` (1803) | `global_mp` [V] | yes, always loaded [I] |
| A call of `scriptable_door::system_init` | stock: the map script and `scripts/mp/load::main` [V] | **no**: the editor's map script calls `maps\mp\_load::main`, which the converter maps to its own `iw3compat_load_main` (`ED:src\core\map_script.cpp:354`, `X:convert\iw3_iw8\iw3_scripts.cpp:99-100`); that function does not call the stock load script (`X:convert\iw3_iw8\resources\iw3_compat.gsc:35-49`). [V] |
| Prompt words `MP/DOOR_USE_OPEN`, `MP/DOOR_USE_CLOSE` | `eng_common_base_mp` [V] | yes [I] |
| Door sounds `scrpt_door_wood_single_*` | `common_mp` sound bank [V] | yes [V: the editor already plays aliases of this bank from map scripts, `ED:src\core\map_script.h:31-40`] |
| The door definition | stock map zones only [V] | **no**: another map's zone is not loaded with a custom map (`ED:docs\research\51_reference_instead_of_carry.md:8-12`). It must be written into the custom map's own zones. |
| Drawn model `..._left_mp_nophysics` | stock map zones only [V] | as a carried copy: the editor already builds stock models into a map as copies named `iw8r_stock_*` (`ED:src\core\compile_iw8.cpp:494`, `:2204`). [V] Whether the copy keeps `tag_door_handle` is [U]; the sound events allow a missing tag (`allowMissingTag`, `useRootOnMissingTag` are true). [V] |
| Collision model with its physics asset | stock map zones only [V] | **no** as a stock copy: the fork's stock copier does not carry a zone-local physics asset and cannot reproduce models that use detail collision (`ED:docs\research\18_x64zt_stock_assets.md:40-41`). The fork can write a generated physics asset for a model (`X:games\iw8\assets\write_xmodel.cpp:149`, `:216-223`). [V] Using a generated one for the door is [I]. |
| The instance record and the map-wide counts | the map's `MapEnts` | **no**: see below. |

### A.3.2 What the converter can and cannot write today

Can: [V]

- Entities, brush models, triggers, live `script_model`s with a physics body, and the translated map script (current
  builds).
- Script calls of the scriptable builtins: `setscriptablepartstate`, `getscriptablepartstate`, `spawnscriptable`,
  `getentitylessscriptablearrayinradius` are in the builtin table (`ED:data\generated\iw8_gsc_builtins.txt:307, 908,
  1776, 2641`).
- Name-only references to assets of always-loaded zones (`X:games\iw8\assets\replay_stock.cpp:1007`), for models,
  effects, physics shapes, vector fields and light definitions; not for scriptables (`:946-996`).

Cannot: [V]

- **The scriptable block.** The native map writer writes one empty leaf and no instance
  (`X:games\iw8\zone\map_zone.h:979-985`, doc 62 section 1.2). `git grep` for `mapInstanceCount`, `ScriptableInstance`,
  `scriptable_instances`, `ASSET_TYPE_SCRIPTABLE` in `src/games/iw8/zone/map_zone.h`, `src/convert/iw3_iw8` and
  `src/games/iw8/assets` finds nothing on `iw8radiant/build-extraction`, `iw8-conversion`, `iw8radiant/decal-volumes`,
  `swarm4/I01-scriptable-machines`, `swarm4/I02-destructibles` and `swarm4/I10-entities`.
- **A scriptable definition in a converted map.** The same search shows no native writer for the asset. The fork can
  read and write definitions only in its dump-and-rebuild route (`X:games\iw8\neutral\scriptable.cpp`, 3335 lines;
  `X:games\iw8\zone\writer.cpp:367-370`), which the map conversion does not use (doc 62 section 1.3).
- **A call into the stock door script.** The script translator turns a call of another file's function into a no-op
  unless the function is on a list of three (`X:convert\iw3_iw8\iw3_scripts.cpp:137-148`, `:484-487`).
  `scriptable_door::system_init` is not on it.
- **Localized text in the map script.** The translator reads `&"NAME"` as the plain text `"NAME"`
  (`X:convert\iw3_iw8\iw3_scripts.cpp:236-253`), so a script cannot ask for a localized string by name.
- The editor has no scriptable entity and writes no instance list (`ED:data\iw8_entity_defs.json` has only
  `collmap_scriptable`, marked not built; no `scriptable_instances` in `ED:src\core\compile_iw8.cpp`).

What `swarm4/I01-scriptable-machines` does: 44 commits ahead of the shared base (`4ac87b7e`), 22 files, +1402 / -344.
It converts the scriptables of **Infinite Warfare (IW7) maps** into script-driven "state machines" on `script_model`s
(`iw3_compat.gsc` +144, `iw7_iw8\export_entities.cpp` +298, `export_effects.cpp` +224): breaking, effects, sounds at
tags, collision from collision maps. It does not write native IW8 scriptables. It is a working model for the imitation
door of A.3.3 (a), not for the native door. [V, `git log` and `git diff --stat`, read only]

### A.3.3 Shortest path

**(a) Possible today with entities and the map script alone: a door made of the real stock parts.**
Not the native object. What can match the stock door:

| Part | How | Status |
|---|---|---|
| Look | carried copy of `door_wooden_panel_03_painted_left_mp_nophysics` as a live `script_model`, origin at the hinge; frame `tm/door_frame_wooden_panel_03_painted_mp` as a static model | editor mechanism exists [V]; hinge at the model origin [I] |
| Motion | swing 90 degrees away from the user in 0.8 s (accel 0.1, decel 0.5), close in 0.4 s; the editor's hinged door already swings away from the user (`ED:src\core\map_script.h:89-92`) | numbers [V], editor change S |
| Sounds | `playsound` of `scrpt_door_wood_single_open` / `_close` on the door | aliases always loaded [V]; the current door plays none (`ED:src\core\map_script.cpp:523-560`) [V] |
| Prompt | `makeusable` + `sethintstring("Open Door")` on the door model, text swapped to "Close Door" when open; use radius 125 | builtins exist (`ED:data\generated\iw8_gsc_builtins.txt:2075, 2518, 2722`) [V]; that a usable `script_model` shows the native prompt in an editor-built map is **[U]**: the owner's playtest found the game's own trigger entities did not fire (`ED:src\core\map_script.h:123-127`), and the current door has no prompt at all |
| Collision | the door brush or the model's keyframed body, made solid by the map script | exists [V], in game [U] |
| Not matched | sprint-bash, peek, half-open states, bot obstacle, damage states | |

Size: editor M. No fork change. This is the stop-gap to ship as the first door prefab's fallback.

**(b) The real stock door: needs converter work, in this order.** Each step has its own game check.

| Step | Fork change | Size | Proves |
|---|---|---|---|
| F1 | One more entry on the stock-function list: `scripts/engine/scriptable_door` `system_init` (`iw3_scripts.cpp:137-141`), so the generated map script can start the door script | S | script start |
| F2 | Write instance records and the counts of section A.2.1 into the scriptable block (doc 62 sections 1.1-1.6), read from a new list `scriptable_instances` in the collision JSON; one more name-only stub kind for `ScriptableDef` (0x78 bytes) | M-L (150-250 lines, doc 62) | test with a definition that **is** always loaded: `hardpoint_chevron` (`common_core_mp`), which stock maps themselves reference by name only (91 times in mp_backlot2) [V] |
| F3 | A native writer for a full definition (header 0x78, part 104, state 160, event 176 bytes; event kinds Model, Collision, Move, Sound; state kinds Usable, Simple, Health), fed from the ACTS export of the owner's own game files at build time | L | the door appears and shows its prompt |
| F4 | The door's collision model: the carried model plus a generated physics asset, named in the definition's Collision event | M | the door blocks players and swings with its collision |

After F1-F4 the editor side is a point entity `scriptable` (definition name, origin, angles; doc 62 section 1.5), the
instance list in the compile step, and the map-script line that calls `system_init`. Editor size S-M.

Unknowns that only a game run settles: [U]

- Whether the game accepts a name-only scriptable reference from a custom map zone (F2).
- Whether a definition copied into a custom zone under the same name works, and whether its event stream and flat-id
  numbers need to change when it is the only definition in the zone.
- The meaning of `mapEntLookup` (0 on most doors, so 0 is the safe value) and of instance `flags` 1 (8 doors of
  mp_aniyah_tac).
- Whether a second zone built by the fork's dump-and-rebuild route (which already handles definitions) could be loaded
  with the map instead of F3.

**(c)** does not apply: nothing found says it cannot be done; the route is known and unbuilt.

---

# Part B. The prefab bundle

## B.1 Prefabs today

- **Format.** A prefab is an `iwmap 4` text file. A map places it with a point entity `misc_prefab` whose `model` is the
  file path, plus `origin`, `angles`, optional `modelscale` (`ED:src\core\prefab.h:3-6`). [V]
- **Per-placement changes.** Keys `iw8r_ovr:<selector>:<key>` change a key on entities inside; `iw8r_mat:<from>` swaps a
  material (`prefab.h:27-36`). [V]
- **Finding the file.** Next to the map, in its parent folders, then in the prefab folders (Preferences) and the
  bundled library `data\prefabs` (`prefab.h:54-57`, `ED:src\qtapp\prefab_browser.cpp:370-379`). The library has 6,162
  files: the editor's own (`data\prefabs\iw8r`) and groups of stock models harvested from stock maps, by model name only
  (`data\prefabs\stock\README.md`). [V]
- **Viewport.** A `misc_prefab` is drawn with its real contents, not a box (`ED:src\viewport\scene.cpp:593`,
  `scene.h:39, 152-154`). [V]
- **Browser and drag and drop.** The Prefabs dock lists every `.map` under the roots with a drawn thumbnail; a drag
  carries `prefabs<TAB><file>` and the drop creates the `misc_prefab` at the surface point
  (`prefab_browser.cpp:52-66`, `ED:src\qtapp\main_window.cpp:5927-5929`, `:6103-6124`). [V]
- **Build.** The compile step first replaces every `misc_prefab` by its contents, nested up to 8 levels
  (`ED:src\core\compile_iw8.cpp:1370-1382`, `prefab.cpp:409-459`). [V]
- **Sharing.** The map package collects a map with its prefabs into a zip (`ED:src\core\map_package.h:1-17`). [V]

Limits: [V]

1. **No assets travel.** A prefab names materials, models, effects and sounds; custom files stay wherever they were.
2. **No script.** A behaviour key (`iw8r_door`, `iw8r_lift`...) can sit on an entity inside, but a prefab cannot bring
   script functions.
3. **Names clash.** Keys are copied as they are (`prefab.cpp:205-229`); two placements of a door prefab give two
   entities with the same `targetname`, and both triggers open the same door.
4. **Saving loses brush entities.** "Save Selection As Prefab" moves the brushes of every selected brush entity into
   the prefab's worldspawn and drops the entity and its keys (`prefab.cpp:549-557`). A selected `script_brushmodel`
   door is saved as plain wall brushes.
5. **No description, tags, thumbnail file or parameters.** The browser shows the file name and a picture drawn from the
   contents.
6. **Nothing for stock objects.** A prefab cannot say "this needs the stock door definition" or fall back when the
   build cannot provide it.

## B.2 The bundle: decisions

### B.2.1 A folder, with a zip for sharing

A bundle is a **folder** named `<name>.iw8prefab` that holds a normal prefab `.map` and everything around it.

Why a folder and not one file:

- Custom materials and models are already loose files the editor reads directly (`*.iw8mat.json` with images, model
  files next to the map). A folder lets the bundle's asset folders be added to the editor's custom asset roots, so
  nothing is copied into the map folder and a prefab update reaches every map that uses it.
- The geometry stays a plain `.map` that every existing tool (parser, thumbnail, package, tests) reads unchanged.
- Text files in a folder can be compared and versioned.

For sending a prefab to someone, the same folder is written as one file `<name>.iw8prefab.zip` with the zip writer the
editor already has (`ED:src\core\map_package.h:44-55`); the browser imports the zip by extracting it into the user's
prefab folder.

Old prefabs: a bare `.map` stays valid and means "a bundle with an empty manifest". Nothing is converted on load.
"Convert To Bundle" wraps one in a folder on request.

Layout:

```
<name>.iw8prefab/
  prefab.json      manifest (B.3)
  prefab.map       iwmap 4: brushes, patches, entities (the current prefab format)
  thumb.png        optional picture for the browser
  script.gsc       optional script snippet
  fallback.map     optional replacement contents (B.2.8)
  fallback.gsc     optional
  materials/       custom material definitions and their images
  models/          custom model files
  sounds/          custom sound files (not built yet, roadmap 6.22)
  effects/         custom effect files (when the editor can build them)
```

The `misc_prefab` entity names the folder: `"model" "prefabs/doors/stock_door_wood_single.iw8prefab"`. The resolver
(`resolvePrefabPath`) accepts a folder and then reads `prefab.map` inside it.

### B.2.2 Unique names when a prefab is placed several times

Two kinds of names need care: entity names and script function names.

**Entity names.** Each placement gets a short placement id in a key on the `misc_prefab`: `iw8r_pid "p7"`. The editor
assigns the smallest unused number when the prefab is placed and never changes it, so the names stay the same across
saves and Live Link.

When the prefab is expanded (viewport and build), a name is rewritten to `<pid>_<name>`:

- in the keys `targetname`, `target`, `script_linkname`, `script_linkto`, and any key the manifest lists under
  `names.keys` (for example `iw8r_teleport`, whose value is a targetname);
- only when the name is **local**. A name is local when the manifest lists it in `names.local`, or, when the manifest
  has no list, when it is both defined and referenced inside the prefab (a `target` that points at a `targetname` in the
  same prefab). A name listed in `names.global` is never touched.

Why this rule: stock scripts find some objects by fixed names (`flag_primary`, `hqloc`, `minimap_corner`, catalogue
`iw8_scripted_objects.tsv`). Renaming those would break them. A link between two entities of the same prefab keeps
working when both ends get the same prefix; the stock hardpoint zone is such a link (`target hardpoint_03_chevrons` on
the trigger, the same `targetname` on the chevron instances, mp_backlot2 [V]).

Nested prefabs chain the ids (`p7_p2_door`). A bare `.map` prefab (no manifest) is expanded exactly as today, with no
renaming, so existing maps build the same; the build warns when two placements of it define the same linked name.

**Script function names.** The snippet is included **once per prefab kind**, not once per placement. In `script.gsc`
the author starts every function name with `prefab_`. The build replaces that prefix with `iw8rp_<id>_`, where `<id>` is
the manifest id (whole identifiers only, with the tokenizer the script checker already has). The generated map script
then starts the entry function once per placement and hands it the placement:

```
level thread iw8rp_stock_door_wood_single_main( iw8r_prefab_inst( "p7", (83.5, 323.5, 69), (0, 180, 0) ) );
```

Inside, `iw8r_pname( inst, "door" )` returns `"p7_door"`. Two prefabs with the same id in one map stop the build with a
message that names both folders.

No-code behaviour keys need nothing new: they sit on the entities, the names in them are rewritten by the rule above,
and the existing readers see ordinary entities after expansion.

### B.2.3 One object in the viewport, real geometry

- The placement stays one `misc_prefab` entity: one click selects it, the gizmo moves and rotates it, copy and paste
  give a new `iw8r_pid`.
- It is drawn with its expanded contents (already so). New: a `scriptable` point entity inside is drawn with the
  definition's drawn model (the stock door leaf), taken from a new catalogue `iw8_scriptables.tsv` (definition, zones,
  first model), so a stock door looks like the door and not like a marker.
- **Parameters** from the manifest appear in the inspector of the `misc_prefab` (swing angle, material choice). Each
  writes one of the existing override keys (`iw8r_ovr:` / `iw8r_mat:`), so the map file format does not change.
- **Edit Prefab** opens `prefab.map` of the bundle as a second document; saving redraws every placement.
  **Explode** (exists) turns one placement into loose objects. **Make Unique** copies the bundle next to the map under
  a new id and points this placement at the copy.
- Editor-only markers inside a prefab stay editor-only, as for loose objects.

### B.2.4 "Save Selection As Prefab" collects everything

One dialog: name, description, category, tags, and check lists filled in by scanning the selection.

1. **Geometry and entities.** World brushes and patches go to the prefab's worldspawn. Brush entities stay entities
   with their keys (this fixes limit 4). Point entities as today. The pivot is the bottom centre of the selection's
   bounds, snapped to the grid; a button picks another point (a door wants its hinge).
2. **Linked objects.** If a selected entity targets an unselected one, or is targeted by one, the dialog lists it with
   a tick box ("the trigger that opens this door is not selected").
3. **Assets.** The existing usage scan (`ED:src\core\asset_usage.cpp`) run on the selection gives materials, models,
   effects and sounds. Custom ones are copied into `materials/` and `models/` of the bundle (a model's own materials
   and images follow). Stock ones are written into `requires` by name with the zones the catalogue gives.
4. **Behaviour.** Behaviour keys are on the entities already. Names that are linked inside the selection are written
   to `names.local`. A script file can be attached; the dialog checks that its functions start with `prefab_` and that
   it has `prefab_main`.
5. **Picture.** The camera renders the selection alone, three-quarter view, to `thumb.png`.

Then the selection is replaced by one placement of the new bundle (optional tick box), so the map keeps looking the same.

### B.2.5 Drag and drop

Unchanged for the user: drag from the Prefabs dock, drop in the Camera or a Grid view. Added:

- A see-through preview of the real contents follows the cursor during the drag.
- `placement.surface` in the manifest: `floor` drops on the surface under the cursor (today's behaviour), `wall` turns
  the prefab to face out of the wall that was hit, `ceiling` hangs it.
- The drop assigns `iw8r_pid` and adds the bundle's `materials/` and `models/` folders to the map's custom asset roots.
- A bundle whose `requires` cannot be met shows a small badge in the dock and says why on hover ("needs stock door
  definition; built as a scripted door until the converter supports it").

### B.2.6 How the compile step expands it

In order, inside the existing flatten step:

1. Resolve the folder, read the manifest, choose `prefab.map` or the fallback (B.2.8).
2. Expand as today (transform, overrides, material swaps), then rewrite local names with the placement id.
3. Collect each bundle's script once and one start line per placement; hand both to the map-script generator.
4. Add the bundle asset folders to the custom material and model roots of this build.
5. Check `requires` against the catalogues; a missing stock model, sound, effect or definition is a build warning that
   names the prefab.
6. `scriptable` entities are taken out of the entity list and written as the `scriptable_instances` list for the
   converter (step F2). Until the converter accepts that list, they are handled by the fallback.

The map package (zip of a map) includes the bundle folders the map uses.

### B.2.7 Stock-object prefabs shipped without game assets

A shipped bundle holds **names, numbers and editor-made files only**: the `.map` with entity keys, the manifest, and a
script written for the editor. No stock model, texture, sound, definition or picture of one is in it.

- Stock models and textures are fetched from the owner's own game files with ACTS on first use, as the model and
  material browsers already do, and built into the map as copies.
- A stock scriptable definition is exported the same way at build time (`-a scriptable` from the first zone in
  `requires.scriptable_defs[].zones` that the owner's game has) into the editor's cache.
- Thumbnails of stock-object prefabs are drawn on the user's machine from the cached models (as today) and are not
  shipped.
- This is the same rule the stock prefab library already follows (`data\prefabs\stock\README.md`: model names only). [V]

### B.2.8 Fallback contents

A bundle may carry a second set of contents for a build that cannot provide a required feature. The stock door needs
it: `prefab.map` holds the native door (one `scriptable` entity and the frame); `fallback.map` holds the stock-parts
door of A.3.3 (a). The manifest says when to use which. The build log states which one was built, so a door that is an
imitation is never presented as the real one.

## B.3 The file format

`prefab.json`, UTF-8. Unknown fields are kept when the editor rewrites the file. Only `format` and `id` are required.

| Field | Type | Meaning |
|---|---|---|
| `format` | text | `"iw8r-prefab-1"`. Another value: the bundle is listed but cannot be placed. |
| `id` | text | 1-40 characters `a-z 0-9 _`. Script prefix and identity of the prefab kind. |
| `title` | text | Name shown in the dock. Default: folder name. |
| `description` | text | One or two sentences, shown under the title and on hover. |
| `category` | text | Dock group (`doors`, `gameplay`, `props`...). Default: parent folder name. |
| `tags` | list of text | Search words. |
| `author`, `version` | text, number | Shown in the dock details. |
| `map` | text | Contents file. Default `prefab.map`. |
| `thumbnail` | text | Picture file. Absent: drawn from the contents. |
| `pivot` | text | What the origin means, shown as a hint (`"hinge, floor level"`). |
| `bounds` | `{mins:[x,y,z], maxs:[x,y,z]}` | Cached size for the drag preview. Recomputed on save. |
| `placement.surface` | text | `floor` (default), `wall`, `ceiling`, `free`. |
| `placement.snap` | number | Grid step for the drop. Default: the editor grid. |
| `placement.yaw_step` | number | Degrees the wheel turns it during a drag. Default 90. |
| `names.local` | list of text | Names rewritten per placement. |
| `names.global` | list of text | Names never rewritten. |
| `names.keys` | list of text | Extra keys whose values are names (besides `targetname`, `target`, `script_linkname`, `script_linkto`). |
| `parameters[]` | list | Inspector controls. Each: `name`, `label`, `type` (`number`, `choice`, `material`, `text`), `default`, `min`, `max`, `choices`, and `apply`: either `{selector, key, template}` (writes `iw8r_ovr:<selector>:<key>` with `{name}` replaced) or `{material}` (writes `iw8r_mat:<material>`). |
| `script.file` | text | Snippet file. Default `script.gsc` when it exists. |
| `script.entry` | text | Function started once per placement. Default `prefab_main`. |
| `script.prefix` | text | Prefix replaced by `iw8rp_<id>_`. Default `prefab_`. |
| `script.once` | text | Optional function started once per map, before the placements. |
| `assets.materials[]` | `{name, file}` | Custom material definitions inside the bundle. |
| `assets.models[]` | `{name, file}` | Custom model files inside the bundle. |
| `assets.sounds[]` | `{alias, file, loop}` | Custom sounds (cannot be built yet). |
| `assets.effects[]` | `{name, file}` | Custom effects (when buildable). |
| `requires.stock_models[]` | `{name, zones[]}` | Stock models by name. |
| `requires.stock_materials[]` | `{name, zones[]}` | Stock materials by name. |
| `requires.stock_sounds[]` | text | Aliases of the always-loaded sound banks. |
| `requires.stock_effects[]` | text | Stock effect names. |
| `requires.stock_strings[]` | text | Localized string names the prefab shows. |
| `requires.scriptable_defs[]` | `{name, zones[]}` | Stock scriptable definitions. |
| `requires.stock_scripts[]` | text | Stock functions the map script must be allowed to call (`scripts/engine/scriptable_door::system_init`). |
| `requires.build[]` | text | Build features: `scriptable_instances`, `scriptable_def_carry`, `stock_script_calls`, `custom_sounds`. |
| `fallback.when_missing[]` | text | Build features whose absence selects the fallback. |
| `fallback.map`, `fallback.script` | text | Replacement contents and snippet. |
| `fallback.note` | text | Line written to the build log when the fallback is used. |
| `game_modes[]` | text | Modes the prefab is for (`koth`...); empty = all. Shown as a note. |

Example (the first prefab):

```json
{
  "format": "iw8r-prefab-1",
  "id": "stock_door_wood_single",
  "title": "Stock door, painted wood, single",
  "description": "The wooden door of Talsik Backlot. Use opens it away from you; sprint into it to bash it open.",
  "category": "doors",
  "tags": ["door", "interactive", "stock", "scriptable"],
  "pivot": "hinge, floor level",
  "placement": {"surface": "floor", "yaw_step": 90},
  "requires": {
    "scriptable_defs": [{"name": "scriptable_door_wooden_panel_03_painted_mp", "zones": ["mp_backlot2", "mp_hideout", "mp_village2"]}],
    "stock_models": [
      {"name": "door_wooden_panel_03_painted_left_mp_nophysics", "zones": ["mp_backlot2", "mp_hideout", "mp_village2"]},
      {"name": "tm/door_frame_wooden_panel_03_painted_mp", "zones": ["mp_backlot2", "mp_hideout", "mp_village2"]}
    ],
    "stock_sounds": ["scrpt_door_wood_single_open", "scrpt_door_wood_single_close", "scrpt_door_wood_single_bash"],
    "stock_strings": ["MP/DOOR_USE_OPEN", "MP/DOOR_USE_CLOSE"],
    "stock_scripts": ["scripts/engine/scriptable_door::system_init"],
    "build": ["scriptable_instances", "scriptable_def_carry", "stock_script_calls"]
  },
  "fallback": {
    "when_missing": ["scriptable_instances", "scriptable_def_carry"],
    "map": "fallback.map",
    "script": "fallback.gsc",
    "note": "stock door built as a scripted door from stock parts (the converter cannot write scriptables yet)"
  }
}
```

`prefab.map` of this bundle: worldspawn (empty), one `misc_model` for the frame, and

```
{
"classname" "scriptable"
"scriptable" "scriptable_door_wooden_panel_03_painted_mp"
"origin" "0 0 0"
"angles" "0 0 0"
}
```

## B.4 Editor changes by file, with tests

Sizes: S up to about 100 lines, M 100-400, L more. Tests use fixtures under `tests\data` only.

| # | File | Change | Size | Tests |
|---|---|---|---|---|
| 1 | `src\core\prefab_bundle.h/.cpp` (new) | Read, check and write the manifest; a bare `.map` gives an empty manifest; folder helpers | M | new `tests\prefab_bundle_tests.cpp`: round trip keeps unknown fields; bad id, bad format, missing `map` file rejected with a message; bare `.map` accepted |
| 2 | `src\core\prefab.cpp/.h` `resolvePrefabPath` | Accept a `.iw8prefab` folder | S | `prefab_tests.cpp`: folder next to the map, in a root, nested; old `.map` paths resolve as before |
| 3 | `src\core\prefab.cpp` `selectionToPrefabText` | Keep brush entities as entities with their keys | S | brush entity with keys survives save, load, expand; world brushes still land in worldspawn |
| 4 | `src\core\prefab.cpp` `instantiatePrefab`, `flattenText` | Placement id and local-name rewrite; nested chaining; warn on clashes of bare prefabs | M | two placements give different names and working links; `names.global` untouched; inferred local names; nested prefix; a bare `.map` prefab expands byte-for-byte as before (golden file) |
| 5 | `src\core\map_script.h/.cpp` | `MapScriptExtras::prefabScripts`: one snippet per id with prefix replacement, one start line per placement, helpers `iw8r_prefab_inst`, `iw8r_pname` | M | text tests: prefix replaced on whole identifiers only, strings and comments untouched; duplicate id is an error; generated script compiles with gsc-tool (existing script check run) |
| 6 | `src\core\compile_iw8.cpp` | Bundle asset roots; `requires` check; fallback choice; `scriptable` entities to `scriptable_instances` (off until the fork has F2) | M | compile of a fixture map with a bundle: custom material found in the bundle; missing stock name warned with the prefab name; fallback chosen and logged; instance list JSON shape |
| 7 | `src\core\map_package.cpp` | Package bundle folders; write and read `<name>.iw8prefab.zip` | S | `map_package_tests.cpp`: zip round trip of a bundle; a map package contains its bundles |
| 8 | `src\core\asset_usage.cpp` | Usage scan limited to a selection | S | counts for a selection equal the counts of the same objects saved as a map |
| 9 | `data\iw8_entity_defs.json`, `tools\build_asset_catalog.py`, `src\core\stock_scripted_catalog.*` | Point class `scriptable`; catalogue `iw8_scriptables.tsv` (definition, zones, first model) | S | catalogue test: door definitions listed with their zones; Check Map names scriptables without build support |
| 10 | `src\core\map_script.cpp` (or new `stock_door.cpp`) | The stock-parts door of A.3.3 (a): key `iw8r_stock_door "<sound set> [angle]"` on a `script_model`: stock timing, both directions, open / close / bash sounds, prompt text | M | generated text; compiles with gsc-tool; Behaviour Preview swings it |
| 11 | `src\qtapp\prefab_browser.cpp/.h` | List bundles; use `thumb.png`; title, description, tags in the list and filter; badge for unmet `requires`; Import Bundle | M | `qt_gui_test.cpp`: a fixture bundle is listed with its title; filter by tag; thumbnail read from file; badge shown |
| 12 | `src\qtapp\main_window.cpp` | Save Selection As Prefab dialog (B.2.4); drop assigns `iw8r_pid` and follows `placement`; Convert To Bundle, Make Unique, Edit Prefab; parameters in the inspector | L | GUI tests: save then place gives the same picture and the same expanded entity list; copy and paste gives a new id; a parameter writes the override key and undo removes it |
| 13 | `src\viewport\scene.cpp` | Draw a `scriptable` entity with its model; drag preview of prefab contents | M | scene test: draw list of a bundle with a `scriptable` contains the model mesh; preview adds no document change |
| 14 | `docs\USER_GUIDE.md`, `docs\KEYS_REFERENCE.md` | Prefab bundles, the new keys (`iw8r_pid`, `scriptable`, `iw8r_stock_door`) | S | help index test finds the new keys |

Order that gives the owner something to see soonest: 3, 1, 2, 4, 11 (bundles can be saved, listed, placed twice
safely), then 10 and 5 (the first door prefab with its fallback), then 12 and 13, then 6-9.

Fork work is listed in A.3.3 (b) (F1 S, F2 M-L, F3 L, F4 M) and one more for scripts: keep `&"NAME"` as a localized
reference in the translator (S), so a script prompt can use the game's own translated words.

## B.5 The first three prefabs

**1. Stock door, painted wood, single** (`doors/stock_door_wood_single.iw8prefab`).
Contents: the frame model and one `scriptable` door (B.3 example); fallback: the stock-parts door.

- Blocks the fallback: editor items 1-5 and 10. In game unknown: the prompt on a usable `script_model`. If it does not
  show, the fallback draws its own text line near the door, which is not the native prompt.
- Blocks the real door: fork F1-F4 and a game run for each; editor items 6 and 9.
- Same bundle, other definitions, once the first works: heavy metal single (`scriptable_dyn_door_metal_single_b_01_grey`),
  ornate double (`scriptable_door_wood_ornate_01_blue_double_l` / `_r`).

**2. Jump pad with effect and sound** (`gameplay/jump_pad.iw8prefab`).
Contents: a pad model, a `trigger_multiple` with `iw8r_jump`, an `fx_origin` with a stock effect, a `sound_emitter`
with a stock alias, one parameter (launch speed). It is the owner's description in the small: model, behaviour, effect
and sound in one object, and two placements must not share the trigger name.

- Blocks: editor items 1-4 and 11. Nothing in the fork. In-game behaviour of `iw8r_jump` is not verified yet (roadmap
  6.12).

**3. Hardpoint zone with stock chevrons** (`gameplay/hardpoint_zone.iw8prefab`).
Contents: a `trigger_multiple` (`script_gameobjectname hardpoint`, `script_label` as a parameter, `targetname
hardpoint_zone` kept global) whose `target` names a group of `scriptable` entities with definition
`hardpoint_chevron`, as mp_backlot2 does (13 zones, 91 chevrons [V]). The definition is always loaded
(`common_core_mp`), so this prefab needs only the instance list.

- Blocks: fork F2 (it is F2's game test, and so the first proof that a custom map can hold scriptables at all); editor
  items 6 and 9. Whether the Hardpoint mode keeps the zone in a custom map is unknown (doc 53, section 3).
- Without F2 it still places a working zone trigger; the chevrons are left out with a build note.

---

## C. Evidence files

Read-only exports of Replay 1.20 files made in this session with the task's ACTS command (the asset type after `-a`
varied; nothing under `E:\IW8` was written):

| What | Command part | Output |
|---|---|---|
| Entity assets of mp_hideout, mp_village2, mp_emporium, mp_aniyah_tac, mp_rust | `-a map_ents` | `SC:mapents` |
| Scriptable definitions of mp_backlot2 (20 files) | `-a scriptable` | `SC:backlot2_scriptable` |
| English localized strings of the common MP zone | `-a localize` on `zone\english\eng_common_base_mp.ff` | `SC:eng_loc` |
| Script files of global_mp, common_base_mp, mp_backlot2 | `-a scriptfile` | `SC:global_mp_scripts`, `SC:cbm_scripts`, `SC:bl2_scripts` |

Analysis scripts (Python, read only) are in `SC:scripts`: `doors.py`, `doorscan.py`, `lookup.py`, `chev.py`,
`defshape.py`, `doorsum.py`, `gscstr.py`, `farcall.py`. Two Replay scripts were decompiled into `SC:decomp` with the
owner's gsc-tool 1.6.2 (ids 11 and 1803); ids 1804, 1839 and 1872 did not decompile and were read for their text
constants and reference pairs only.

Not done: no game run, no build, no converter build, no decompile of Replay's door script logic (its logic is taken
from the 8.19 sources and checked only against its text constants).
