# 62. Scriptables, doors, water and mantle hints (roadmap 6.14, 6.24)

Date: 2026-11-10. Read-only research. Nothing was built, converted, run or edited. Nothing in `E:\mw124\x64-zt` was touched. Items marked UNVERIFIED need a game run or a Replay table that is not in the repos.

Roots used in citations:

- `X:` = `E:\mw124\x64-zt\src` (converter fork, read only).
- `ED:` = `E:\IW8-Radiant` (editor).
- `STK:` = `E:\mw124\tools\dvar_search_web\src\combined\rawfile_source\scripts` (decompiled stock GSC; most identifiers are `_encstr_<hex>`).
- Stock MapEnts: `ED:data\stock_dumps\mp_shipment\...\mp_shipment.d3dbsp.0.asset.json`.

Prior research this builds on: doc 41 (scriptables, dynents), doc 45 (nav, water, doors, mantle), doc 53 (stock scripted objects), doc 40 (encrypted names).

## 0. Summary

1. **Stock scriptable instances (6.14).** x64-zt writes an empty scriptable block. The fix is one emitter change in `X:games\iw8\zone\map_zone.h` `emitMapEntsBody` (75 records of 72 bytes plus counts) and one reader in the converter. The hard part is not the records: the native map path refuses any asset reference it did not emit (`X:games\iw8\zone\iw8_zone.cpp:17-24`), so each referenced `ScriptableDef` needs a name-only stub. Fork size M-L (about 150-250 lines across three files). Editor entity `scriptable` supplies def name, origin and angles; everything else is zero in stock.
2. **Doors (6.24).** The stock door system (`STK:mp\door.gsc`, 2892 lines) is data-driven and encrypted; do not port it. The cheap route is GSC on a `script_brushmodel` with `rotateyaw`, driven by a use trigger. Every builtin it needs is in `ED:data\generated\iw8_gsc_builtins.txt`. The editor already has the pattern (`ED:src\core\script_check.cpp:197-215`, `rotating_door`). Needed editor edit: pass the user to the trigger wait and use `trigger_use` instead of a touch trigger. Fork change: none. In-game behaviour UNVERIFIED.
3. **Water.** Editor water brushes are dropped from collision (`ED:src\core\compile_iw8.cpp:308-311`, `contents 0`, `collides false`). The converter also strips IW3 contents `0x20` (`X:convert\iw3_iw8\iw3_fastfile.cpp:6958-6976`, mask `0x33683`). Replay names `physicscontents_water` (stock) and its mask `kReplayContents` (`X:games\iw8\common\replay_havok.h:22`) contains bit `0x20`. Candidate: `0x20` is water. Change: editor writes water brushes with contents `0x20` and collision; fork keeps `0x20` for water-only brushes. Fork size S (about 5-10 lines), editor S-M. Whether Replay swims on that bit is UNVERIFIED.
4. **Mantle hints.** Confirmed: no authored mantle data is needed or written. Replay computes mantle from collision at runtime (doc 45 §3.3). A contents name `physicscontents_mantle` exists in stock (`STK:engine\sp\utility_code.gsc:1437`), but its bit value and mantle use are UNVERIFIED. No stock mp script calls `forcemantle`. Nothing to do now beyond testing ledges in play preview.

---

## 1. Stock scriptable instances (roadmap 6.14)

### 1.1 What the stock block holds (verified)

Stream layout, from the x64-zt stream code (the authority for order):

- `X:games\iw8\zone\asset_streams.cpp:3223-3240` `stream_scriptablemapents`: order is `mapInstances` (inline array, stride 72, align 8), then `reservedDynents[0]`, `reservedDynents[1]`, then `spatialPopulationTree` (one inline).
- `X:games\iw8\zone\asset_streams.cpp:3205-3215` `stream_scriptableinstance`: `stream.asset(ASSET_TYPE_SCRIPTABLE, initialDef)`, then six `script_string` fields (`classname`, `target`, `targetname`, `script_linkname`, `script_linkto`, `script_noteworthy`).
- `X:games\iw8\zone\asset_streams.cpp:3533` calls it from `stream_mapents`, after `clientEntAnchors` and before `spawnGroupLoot`.

Struct: `X:games\iw8\structs.hpp:10618-10634` (`ScriptableInstance`, 72 bytes). `X:games\iw8\structs.hpp:10657-10672` (`ScriptableMapEnts`, 216 bytes). Offsets are asserted at `X:games\iw8\structs.hpp:22656-22670` and `:22688-22698`.

| ScriptableMapEnts field | offset in struct | offset in MapEnts (+0x1F8) |
|---|---|---|
| `totalInstanceCount` | 0x00 | 0x1F8 |
| `mapInstanceCount` | 0x04 | 0x1FC |
| `serverInstanceCount` | 0x10 | 0x208 |
| `serverFixedPartCount` | 0x14 | 0x20C |
| `mapInstances` (pointer) | 0x38 | 0x230 |
| `spatialPopulationTree` (pointer) | 0xC8 | 0x2C0 |

The `0x2C0` offset is already stamped by the emitter (`X:games\iw8\zone\map_zone.h:909`, comment "ScriptableCl spatial initialization"), which confirms the base `0x1F8`.

Stock values (decoded from `ED:data\stock_dumps\mp_shipment\...\mp_shipment.d3dbsp.0.asset.json`, `asset.fields.scriptableMapEnts`), checked by a script this session:

- Header: `totalInstanceCount` 0, `mapInstanceCount` 75, `reservedInstanceCount` 0, `lootInstanceCount` 0, `serverInstanceCount` 66, `serverFixedPartCount` 609, `ffMemCost` 1240.
- Per record, all 75: `ffMemCost` 0, `mapEntLookup` 0, `flags` 0, `target` 0, `script_linkname` 0, `script_linkto` 0, `script_noteworthy` 0, `lightIndices` all 0, `initialAngles` set, `initialOrigin` set.
- `targetname` nonzero on 41 of 75 (doc 41 said "some"; this is the count).
- `classname` is a script-string index (e.g. 8005 = `scriptable_com_trafficcone_01`). The def name is the classname minus the `scriptable_` prefix.
- `initialDef` is written as `{"type":"ScriptableDef","asset_reference":true,"name":"com_trafficcone_01"}`. Nine distinct defs. One name carries the leading `,` cross-zone marker (`,hardpoint_chevron`, 37 uses).
- Not known: which 9 of 75 are not server instances (`serverInstanceCount` 66). UNVERIFIED.

### 1.2 What x64-zt writes today (verified)

- `X:games\iw8\zone\map_zone.h:815` `emitMapEntsBody` builds `me[kSizeMapEnts]` (zeroed, `:824`), stamps the name, entity string and trigger/edge/dynent fields, and never stamps `0x1F8..0x23F`.
- `X:games\iw8\zone\map_zone.h:979-985` writes a 24-byte "one empty spatial leaf" tree, the `spatialPopulationTree`. There is no write of `ScriptableInstance` or `mapInstanceCount` anywhere in `X:games\iw8` outside the neutral reader/writer (`X:games\iw8\neutral\mapents.cpp:797-834, 1868-1889`).
- Stream position for the insert: after the two dynent populations (`map_zone.h:~915-975`) and before the PA speaker list (`:~993`). The stock order in section 1.1 is the same, so instance records go immediately before the comment at `map_zone.h:979`.

### 1.3 The reference problem (the real blocker)

Each instance names a `ScriptableDef`. Two writers exist:

- **Native map path** (used by the converter: `X:convert\iw3_iw8\convert.cpp:22-23`, `buildMapZone` at `map_zone.h:~1020-1050`, `:1397`). It uses `ZoneWriter` (`X:games\iw8\zone\iw8_zone.h:28-31`). `ZoneWriter::assetAlias` throws `asset dependency was not emitted before its reference` for any asset not added to this zone (`X:games\iw8\zone\iw8_zone.cpp:17-24`). `ZoneWriter::add` (`iw8_zone.h:28`) is the only way to emit an asset.
- **Generic writer** (`X:games\iw8\zone\writer.cpp`, used by `X:games\iw8\build.cpp:355` and `dump.cpp:186`). It already writes a name-only stub for a referenced asset the zone does not hold (`writer.cpp:355-400`, `stub(...)`), and it keeps a leading `,` on the name for cross-zone references (`writer.cpp:~394-398`). It runs `validate_scriptable` only for non-stub assets (`writer.cpp:367-370`).

So the native path cannot reference a stock def. Recipe for the fork:

1. Add a reference-stub writer to `map_zone.h`: for each distinct def name, write a 0x78-byte `ScriptableDef` header with `name` (offset 0) pointing at `",<def>"` and all else zero (the same shape as `writer.cpp` `stub`). Keep the pointer in a local map; do not add it to `ZoneWriter` assets.
2. Write the instance records (72 bytes each) with `initialDef` pointing at the stub, script strings interned (see 1.4).
3. Stamp the counts in section 1.1's table.

UNVERIFIED: whether the loader resolves `,<def>` from a required zone for every stock def, and which zone must be in the map's required list. The stub mechanism is evidence the generic writer relies on that; a game run is needed. The 17-zone list of def names is in doc 41 section 2.

### 1.3a Addendum (2026-11-11): the native path does have name-only references

`X:games\iw8ssetseplay_stock.cpp:1007` `RegisterReference(writer, type, name)` adds a name-only stub to the native
`ZoneWriter` (header with `PTR_FOLLOWS`, then `",<name>"` in the virtual block), so a later `assetAlias` finds it. Stub
layouts exist for xmodel, vfx, physics FX pipeline / shape, vector field and light def (`Stub`, `:946-965`); materials and
physics assets have their own reference writers. The converter already uses it on every build: effect references
(`convert.cpp:1797-1802`, `RegisterNameReference`) and the surface-effect closure ("164 name-only references" in each
build log), and those maps load in game. So the blocker of 1.3 narrows to one new `Stub` case,
`ASSET_TYPE_SCRIPTABLE` with the 0x78-byte ScriptableDef header (align 7), plus the records, counts, strings and the
JSON reader of 1.6. Whether the loader resolves a `,<def>` scriptable reference from the required zones stays
UNVERIFIED until a game run.

### 1.4 Script strings

- `X:games\iw8\zone\map_zone.h:1360-1372` interns spawn strings before the zone is built and throws "map script strings must lead the zone string list" if the order is wrong. `ZoneWriter::internScriptString` throws after the strings are written (`X:games\iw8\zone\iw8_zone.cpp`, the `scriptStringsWritten_` check).
- Instance strings must go through the same setup phase, before `buildMapZone` runs. The fork change interns `scriptable_<def>` plus any targetname, linkname, linkto, noteworthy.

### 1.5 Editor entity: what it supplies

| Field | Source | Notes |
|---|---|---|
| def name | new key `scriptable` (must match a stock def; catalogue from doc 41 section 2) | written as classname `scriptable_<def>` |
| origin | entity origin (3 floats) | `initialOrigin` |
| angles | entity angles (3 floats) | `initialAngles` (stock values are Euler, unverified order) |
| targetname | key `targetname` (optional) | stock: 41 of 75 |
| target, script_linkname, script_linkto, script_noteworthy | keys, optional | zero in stock |
| model | none | comes from the def (`ScriptableDef.models`) |
| collision | none per instance | comes from the def parts; `collmap_scriptable` is a separate lookup (doc 41 §1.4) |
| flags, lightIndices, ffMemCost, mapEntLookup | zero | matches all 75 stock records |

The editor entity class is a point class (new entry in `ED:data\iw8_entity_defs.json` with `buildAccepted: false` until the fork lands). The compile step writes a list `scriptable_instances` into the collision JSON, next to the empty dynamic tables at `ED:src\core\compile_iw8.cpp:1623-1624`. The converter reads it the way `ReadDynamicEntities` reads `dynamic_models` (`X:convert\iw3_iw8\iw3_fastfile.cpp:3562-3670`), and passes it to `buildMapZone` (`convert.cpp` call site near `:1744-1806`, same place as `RegisterDynamicEntityList`).

### 1.6 Fork change size

| Piece | Where | Lines (estimate) | Risk |
|---|---|---|---|
| Records and counts | `X:games\iw8\zone\map_zone.h` before `:979` | 40-60 | low (layout verified) |
| Reference stubs for defs | `map_zone.h` (new helper) | 40-70 | high: loader resolution UNVERIFIED |
| JSON reader and pass-through | `X:convert\iw3_iw8\iw3_fastfile.cpp` beside `:3562`, `convert.cpp` near `:1744` | 50-80 | low |
| Script string interning | `map_zone.h:1360-1372` setup | 10-20 | low |

Total: about 150-250 lines, M-L. Doc 41 said M because it assumed the neutral writer; the native path makes it L for the stub part. Editor side: entity class, one compile block and the defs JSON entry, about 80-120 lines, S-M.

Stop line: a game run with one instance (a stock `com_trafficcone_01`) is the test. Until it is run the change is UNVERIFIED.

---

## 2. Doors (roadmap 6.24)

### 2.1 How the stock door works (read only)

- `STK:mp\door.gsc` (2892 lines) is a data-driven door system. `door_dynamic_setup` (`:4-16`) finds door entities with `getentarray(<encrypted classname>, <encrypted key>)` (`:18-22`, `:35`), reads `script_parameters` (`door_dynamic_parse_parameters`, `:199`), and builds hint objects, a `clipent` brush linked to the door, and per-door state (`changestate`, `:1485`; `closedoor`, `:1645`). Physics content overrides come from `physics_createcontents` (`:21-23` of the adapter).
- The opening motion is `self rotateyaw(angle, time, accel, decel)` (`STK:mp\door.gsc:1167`, `:1357`). The use prompt is set with `sethintstring` (`:195`, `:485`).
- The use wait is `self waittill("<encrypted>", var_1)` (`:150`). The encrypted name is not decoded in the dump. The sp equivalent is `waittill("trigger", player)` (`STK:sp\trigger.gsc:75`).
- No door entity is placed in either stock map (doc 53 §0). Door placement must come from a stock map's door-tagged `script_brushmodel` or script_model. Their names are encrypted.

Verdict: do not port `door.gsc`. It needs the encrypted names and the stock `script_parameters` set; a custom map gets none of that.

### 2.2 Cheapest route: GSC on a script_brushmodel (recipe)

Ingredients, each checked against `ED:data\generated\iw8_gsc_builtins.txt` (format: `f` = function, `m` = method on an entity):

| Used | Kind | Row in builtins file |
|---|---|---|
| `getent` | f | yes |
| `isplayer` | f | yes |
| `rotateyaw` | m | yes |
| `makeusable`, `makeunusable` | m | yes |
| `sethintstring` | m | yes |
| `waittill`, `wait`, `endon`, `isdefined`, `notify` | language keywords | not in the file (correct) |

The stock snippets confirm the call shapes: `rotateyaw(angle, time)` and `rotateyaw(angle, time, accel, decel)` (`STK:mp\door.gsc:1167, 1357`); `trig waittill("trigger", who)` then `isplayer` (`STK:sp\trigger.gsc:75-78`).

Recipe (user_main or a thread started from it):

```
iw8r_use_door(doorName, triggerName, yaw, moveTime, holdTime)
{
    level endon("game_ended");
    door = getent(doorName, "targetname");
    trig = getent(triggerName, "targetname");
    if (!isdefined(door) || !isdefined(trig))
        return;
    trig sethintstring("Open");
    for (;;)
    {
        trig waittill("trigger", user);
        if (!isplayer(user))
            continue;
        trig makeunusable();
        door rotateyaw(yaw, moveTime, moveTime * 0.25, moveTime * 0.75);
        wait moveTime + holdTime;
        door rotateyaw(0 - yaw, moveTime, moveTime * 0.25, moveTime * 0.75);
        wait moveTime;
        trig makeusable();
    }
}
```

Editor objects: `script_brushmodel` (door, solid: `ED:src\core\compile_iw8.cpp:1133` gives script_brushmodel contents 1), and a `trigger_use` brush (button use) named by `targetname`. Pivot: rotation is about the entity origin. The current snippet comment says "origin brush at the hinge" (`ED:src\core\script_check.cpp:199-200`). In stock, `script_brushmodel` origin is set by an origin key or the brush; the editor's pivot rule is UNVERIFIED.

### 2.3 What the editor already has, and the edit needed

- `ED:src\core\script_check.cpp:176-196` `door` (sliding door, `moveto`), and `:198-216` `rotating_door` (`rotateyaw`). Both use `trig waittill("trigger")` without the player argument (`:189`, `:211`). The other snippets in the same file already use `waittill("trigger", player)` (`:248`, `:267`, `:350`), so the door pair is the odd one out. Both are inserted by `ED:src\qtapp\script_panel.cpp:193-196` and `:337-342`.
- Edit 1: change `trig waittill("trigger")` to `trig waittill("trigger", user)` and add the `isplayer` check in both snippets. Size S (about 6 lines in `script_check.cpp`).
- Edit 2: change the rotating_door trigger hint from touch to use: the editor's comment says "touched" (`script_check.cpp:167-169`), so the user-facing text must say "used". Size S.
- Editor trigger class: `trigger_use` (`ED:data\iw8_entity_defs.json:5527`, buildAccepted true) is the use trigger. `trigger_use_touch` (`:5597`) is touch. Use `trigger_use` for "opens on use".
- Fork change: none. The converter already writes triggers as hulls with trigger contents (`ED:src\core\compile_iw8.cpp:1108-1133`, `kTriggerContents` 0x8000001).

### 2.4 Doors: unverified

- `rotateyaw` on a converted `script_brushmodel` (pivot, collision update).
- `trigger_use` raises notify `"trigger"` with a player argument in Replay (stock SP does; MP is encrypted).
- `sethintstring` accepts a plain string (stock uses localized `&"..."` references).
- Stock door `spawnflags 1` on script_brushmodel (doc 45 §3.1) is still unknown.

---

## 3. Water (roadmap 6.24)

### 3.1 Editor side (verified)

- Face tool `water` is a rule with `contents 0`, `collides false`, `rendered true` (`ED:src\core\iw8_contents.cpp:65`). The header comment says "water contents bit UNKNOWN" (`ED:src\core\iw8_contents.h:51`).
- `out.water` is set (`ED:src\core\iw8_contents.cpp:170`) and never read anywhere in `ED:src` (grep `\.water\b` returns only that line). The water flag is dead.
- `gatherBrush` writes a collision brush only when `bc.collides || forceCollision` (`ED:src\core\compile_iw8.cpp:308-311`). Water brushes therefore produce no collision brush, so no contents reach the converter at all.
- The water brush is drawn with the built-in water material (`ED:src\qtapp\compile_panel.cpp:545-561`). Surface type 20 (water) maps to Replay surface 21 (`ED:src\core\iw8_contents.cpp:269`; `X:convert\iw3_iw8\iw3_fastfile.cpp:7006`).

### 3.2 Converter side (verified)

- `X:convert\iw3_iw8\iw3_fastfile.cpp:6958-6976` `ConvertContents`: `replayContents = 0x33683` (comment at `:6960-6962`); result is `source & 0x33683`, then glass and player-clip fixes. Bit `0x20` is not in `0x33683`, so an IW3 brush with contents `0x20` loses it here.
- `X:convert\iw3_iw8\iw3_fastfile.cpp:7128-7132` `ReadCollision`: if the source JSON has `contents_encoding: "replay"`, `source & kReplayContents` is used instead. Otherwise `ConvertContents`.
- The editor writes IW3-style contents (`ED:src\core\compile_iw8.cpp:260` comment), so `ConvertContents` applies.
- `X:games\iw8\common\replay_havok.h:22` `kReplayContents = 0x27F7FFFB`. Bit `0x20` is set; bit `0x4` is clear. The header comment (`:19-21`) names solid 1, foliage 2, vehicletrigger 8, glass 0x10, useclip 0x20000000 and says this is "the contents bits that Replay names". `0x20` is therefore a named Replay bit, but its name is not in the header.
- Stock trace masks name `physicscontents_water`: `STK:sp\molotov.gsc:781, 1231, 1549` and `STK:engine\sp\utility_code.gsc:1437`. The IW7 bullet shim lists it too (`X:convert\iw7_iw8\export_effects.cpp:2154`).
- The name-to-bit table is not in either repo or in ACTS (grep of `atian-cod-tools\docs`, `config`, `research` found no contents table).

### 3.3 Which bit is water

- Candidate `0x20`: it is the IW3 water contents bit by the CoD engine family convention (IW3 knowledge, not in these repos; UNVERIFIED here), it is inside `kReplayContents`, and the converter's mask drops it. Those three facts agree but do not prove it.
- Replay's own table would settle it (the `q_PhysicsScript_CreateContents` table cited at `replay_havok.h:19-21`; not in the repos).

### 3.4 Change needed

Fork (S, about 5-10 lines in `iw3_fastfile.cpp` `ConvertContents`):

- Keep `0x20` only for brushes whose whole contents are water: `if (source == 0x20u) return 0x20u;` before the mask, or add a new encoding flag. Do not keep `0x20` on mixed brushes until the IW3 corpus is checked (`0x20` on non-water IW3 brushes is UNVERIFIED).
- Check the nav mask: `replay_navigation` uses player mask `0x2014011` (doc 45 §1.1), which excludes `0x20`, so water does not block bots.

Editor (S-M):

- `ED:src\core\iw8_contents.cpp:65`: change the water rule to `contents 0x20`, `collides true` (no solid bit), `rendered true`. Keep the material drawn.
- `ED:src\core\compile_iw8.cpp:308-311`: a water brush now produces a collision brush with `contents 0x20` (no solid bit). The player mask excludes it, so it does not block movement. Hull generation for a non-solid brush is the one path to verify (`isValidBrush` at `:300-303` applies to all brushes).
- `ED:src\core\iw8_contents.h:51`: update the comment.

Swim behaviour: UNVERIFIED. Replay's player swim may need the `allowswim` method (builtin: `m allowswim`, in `ED:data\generated\iw8_gsc_builtins.txt`; no stock mp call found) or a trace against `physicscontents_water`. `setwatersheeting(1, time)` on a `trigger` user (`STK:sp\trigger.gsc:78`) is a stock wading effect that can be used from GSC without the contents bit.

### 3.5 Water: unverified

- Bit value of `physicscontents_water` (candidate `0x20`).
- Whether a non-solid `0x20` hull is accepted by the bake (`replay_havok.cpp:474` `supportedContents` check uses `kReplayContents`, so `0x20` passes that check).
- Swim trigger and `allowswim` default.

---

## 4. Mantle hints (roadmap 6.24, rest of 6.8)

Confirmed: mantles are not authored in the stock pipeline and the fork writes none.

- Fork: no mantle write in `X:convert\iw3_iw8` or `X:games\iw8\zone`. The only mantle text is a builtin table (`X:convert\iw3_iw8\iw8_gsc_builtins.inc:1209, 1372, 1376, 1647, 2039, 2646`) and a comment about pane mantling (`iw3_fastfile.cpp:12788`).
- Editor: the `hint` tool has no collision and is not drawn (doc 45 §3.3, `ED:src\core\iw8_contents.cpp:72`). A hint brush does nothing.
- Stock: `allowmantle` is used by vehicle damage (`STK:mp\vehicles\vehicle_damage_mp.gsc:157, 175`) and loadouts (`STK:cp\cp_loadout.gsc:474`, `STK:cp\survival\survival_loadout.gsc:1474`). No stock mp script calls `forcemantle`, `canmantle` or `cancelmantle`, so their semantics are UNVERIFIED.
- Contents: `physicscontents_mantle` exists in stock (`STK:engine\sp\utility_code.gsc:1437`), in the same trace list as `physicscontents_water` (dyndof). Its bit value and whether Replay uses it for mantle queries are UNVERIFIED. It is the one lead for an authored mantle hint (a brush with that bit), and would need the same contents change as water (section 3.4).
- Editor constants (min ledge 16, max 72, high branch 58) are in `ED:src\core\player_mantle.cpp:10-21` and cited to a source not in this repo (doc 45 §3.3).

Recipe now: none. Test ledges in play preview (`ED:docs\ROADMAP.md:167`) and in game. If a hint is needed later, test `forcemantle` first (GSC, no fork change) before adding a contents bit.

---

## 5. Unverified items (collected)

- Loader resolution of `,<def>` stubs for stock scriptable defs, and the required-zone list (section 1.3).
- Which 9 of 75 stock instances are not server instances (`serverInstanceCount` 66).
- Euler order of `initialAngles` versus the editor's angles.
- Whether `0x20` is Replay's water bit, and whether the bake accepts a non-solid `0x20` hull.
- Whether a `0x20` brush is safe for every IW3 source (only water is assumed).
- `rotateyaw` pivot on `script_brushmodel` and the `origin` key rule.
- `trigger_use` notify with a player argument in MP (stock MP names are encrypted).
- `sethintstring` with a plain string.
- Meaning of `forcemantle`, `canmantle`, `cancelmantle`, and whether `physicscontents_mantle` is a mantle query bit.
- Meaning of `script_brushmodel` `spawnflags 1` (doc 45 §4).

## 6. Files read

Fork (`X:`, read only):
- `X:games\iw8\zone\map_zone.h` (lines 39-59, 815-835, 877-898, 915-1050, 1360-1372, 1380-1413).
- `X:games\iw8\zone\asset_streams.cpp` (3205-3240, 3486-3540).
- `X:games\iw8\zone\iw8_zone.h` (20-60, 150-162), `X:games\iw8\zone\iw8_zone.cpp` (15-30).
- `X:games\iw8\zone\writer.cpp` (24, 224-232, 280-300, 355-400).
- `X:games\iw8\structs.hpp` (10608-10672, 22656-22698).
- `X:games\iw8\iw8_map_structs.h` (247).
- `X:games\iw8\neutral\mapents.cpp` (106-115).
- `X:games\iw8\common\replay_havok.h` (13-22), `replay_havok.cpp` (474).
- `X:convert\iw3_iw8\iw3_fastfile.cpp` (3562-3670, 6958-7006, 7128-7132, 12788).
- `X:convert\iw3_iw8\convert.cpp` (22-23; call sites from doc 41).
- `X:convert\iw3_iw8\iw8_gsc_builtins.inc` (1209, 1372, 1376, 1647, 2039, 2646).
- `X:convert\iw7_iw8\export_effects.cpp` (2154-2162).

Editor (`ED:`):
- `ED:src\core\compile_iw8.cpp` (260, 297-325, 1108-1140, 1623-1624), `ED:src\core\iw8_contents.cpp` (55-70, 131, 170, 269, 285), `ED:src\core\iw8_contents.h` (25-80), `ED:src\core\script_check.cpp` (12-14, 150-215), `ED:src\qtapp\script_panel.cpp` (154-196, 337-342), `ED:src\qtapp\compile_panel.cpp` (545-561).
- `ED:data\generated\iw8_gsc_builtins.txt` (rows checked by name), `ED:data\iw8_entity_defs.json` (5527, 5597).
- `ED:docs\research\41_scriptables_dynents.md`, `45_nav_water_doors.md`, `53_stock_scripted_objects.md`.
- `ED:data\stock_dumps\mp_shipment\...\mp_shipment.d3dbsp.0.asset.json` (read via a JSON parse; no file was written).

Stock GSC (`STK:`, read only):
- `mp\door.gsc` (4-40, 138-150, 195, 485, 1167, 1357), `sp\trigger.gsc` (70-82), `sp\molotov.gsc` (781, 1231, 1549), `engine\sp\utility_code.gsc` (1437), `common\utility.gsc` (1195-1205), `mp\vehicles\vehicle_damage_mp.gsc` (157, 175), `cp\cp_loadout.gsc` (474).

ACTS (`E:\mw124\atian-cod-tools`): searched for a contents table; none found in `docs`, `config` or `research`.

Scratchpad: none written. No repo file was changed other than this document.
