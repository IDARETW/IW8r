# 86. Scripted (scriptable) objects in custom maps: working plan and notes

Date: 2026-11-11. Owner request: "you yourself work on the scripted doors and other objects, and getting those
implemented and working in the radiant tool and for custom map development". This file is the working record of the
main session; it is updated as steps land. Background: research 62 (block layout), 83 (what a stock door is), 41.

Nothing in this file has been seen in game.

## Where the work happens

- Converter: worktree `C:\zsw-iw8r\scriptables`, branch `iw8radiant/scriptables`, created on top of
  `iw8radiant/decal-volumes` (commit 8ff480fa), so adopting both later is one fast-forward of
  `iw8radiant/build-extraction`. Dependencies are directory junctions to `E:\mw124\x64-zt\deps`. Solution:
  `tools\premake5.exe --games=iw3,iw7,iw8 vs2022`; build: MSBuild `build\x64-zt.sln` Release x64. The live converter
  in `E:\mw124\x64-zt` is not touched. Commits are local, never pushed, no Co-Authored-By line. `git status` needs
  `--ignore-submodules=all` there.
- Editor: `src\core\compile_iw8.cpp`, `src\core\map_script.cpp`, `data\iw8_entity_defs_overrides.json`. On 2026-11-11
  the prefab-bundle agent is editing the first two: editor changes for scriptables wait until it has reported.

## Steps

| Step | What | State |
|---|---|---|
| F1 | Let the map script start the stock door script: add `scripts/engine/scriptable_door` `system_init` to `kReplayFunctions` in `src\convert\iw3_iw8\iw3_scripts.cpp` (about line 137) | done, commit ce642bde |
| F2a | Name-only reference stub for a scriptable definition: `Stub()` in `src\games\iw8\assets\replay_stock.cpp` gets `ASSET_TYPE_SCRIPTABLE` (0x78 bytes, align 7) | done, commit ce642bde |
| F2b | Instance records (72 bytes, struct `ScriptableInstance`, `structs.hpp` 10618) written before the "one empty spatial leaf" in `emitMapEntsBody` (`src\games\iw8\zone\map_zone.h` about line 979); pointer `mapInstances` at MapEnts +0x230; `classname` is the script string `scriptable_<definition>`; strings interned in `internMapWorldStrings` through `ReplaySpawns::intern` before the zone string list is written | done, commit 04995144 |
| F2c | The map-wide counts (see below) | done, commit 04995144 (rules below, checked on 21 maps) |
| F2d | The spatial population tree over the instances (stock is not an empty leaf when instances exist) | not needed for a first build: one leaf that holds every instance is written, as for the two dynamic-entity populations |
| F2e | Reader: `scriptable_instances` list in the collision JSON (beside `iw3_fastfile.cpp` about line 3562), passed through `convert.cpp` about line 1744 | done, commit 04995144 (`ReadScriptableInstances`) |
| F3 | A definition in the map's own zones (doors live only in stock map zones). Options: a native writer fed from the ACTS export of the owner's game files, or the fork's existing dump-and-rebuild reader / writer (`src\games\iw8\neutral\scriptable.cpp`, `zone\writer.cpp` 367-370) hooked into the map build | done offline, commit 02182e8a (carried from the stock zone in memory; see "Step F3: done" at the end) |
| F4 | The door's collision model and physics asset | done with F3: the physics asset and detail collision of the door model are part of what is carried |
| E1 | Editor: point entity `scriptable` (keys `scriptable` = definition name, `origin`, `angles`, optional `targetname`), a catalogue of definition names, drawn with the definition's model | done: class `scriptable`, catalogue `data/asset_catalog/iw8_scriptables.tsv`; drawn as a box (no model yet) |
| E2 | Editor: compile writes `scriptable_instances`; the generated map script calls `scripts\engine\scriptable_door::system_init()` when the map has a door | done: `scriptable_instances` with `carry_zone`, and the `system_init` call when a door is placed |

## The map-wide counts (why F2c is not trivial)

`ScriptableMapEnts` (216 bytes at MapEnts +0x1F8, `structs.hpp` 10657): `totalInstanceCount`, `mapInstanceCount`,
`reservedInstanceCount`, `lootInstanceCount`, `serverInstanceCount`, `serverFixedPartCount`, `serverUsablePartCount`,
`physicsLimits` (six counts), `mapInstances`, `sunClientDatas[2]`, `reservedDynents[2]`, `spatialPopulationTree`,
`ffMemCost`.

Stock mp_backlot2 (read from the editor's cached ACTS export of its map_ents): total 0, map 138, reserved 0, loot 7,
server 139, serverFixedPart 750, serverUsablePart 26, ffMemCost 1240. Stock mp_shipment: map 75, server 66,
serverFixedPart 609, ffMemCost 1240 (research 62).

These numbers size the game's run-time memory for scriptables, so they must be at least what the instances need. They
follow from each instance's definition (how many parts are server-instanced, usable, fixed; its physics bodies).
Research 83 found: `serverUsablePartCount` equals the number of usable door instances in three maps, and every
`physicsLimits` count equals the number of door instances in three maps. So the build needs, per definition used, the
header numbers of that definition (`usablePartCount`, `serverInstancedPartCount`, `serverControlledPartCount`,
`maxNumDynEntPartsBase`, part count). Source: the ACTS export of the definition from the owner's game files
(`-a scriptable`), which the editor can make at build time like it does for models.

To do before writing F2c: decode two stock maps completely (definition headers of every instance against the map-wide
counts) and find the exact sums. Check script: to be written under `tools\` when this step starts.

## Findings of 2026-11-11 (decoded from stock mp_backlot2; tool: `tools/scriptable_counts.py`)

Done in the worktree: commit `ce642bde` "iw8: scriptable asset type, name-only scriptable reference, door script
start" (F1 and F2a: `ASSET_TYPE_SCRIPTABLE = 63` in `iw8_structs.h`, the 0x78-byte stub in `replay_stock.cpp`, the
`scriptable_door` `system_init` entry in `iw3_scripts.cpp`). The worktree builds.

The map-wide counts, checked on mp_backlot2 with all 138 instances matched to their definitions (the map's own 20
definitions exported with ACTS `-a scriptable`, plus `hardpoint_chevron` from `common_core_mp`):

| Map-wide field | Stock value | Rule that reproduces it |
|---|---|---|
| `mapInstanceCount` | 138 | number of instance records |
| `lootInstanceCount` | 7 | a map setting (mp_shipment has 0); 0 is a valid value |
| `serverInstanceCount` | 139 | instances whose definition has `serverInstancedPartCount` above 0 (132) + loot (7) |
| `serverFixedPartCount` | 750 | sum of `serverInstancedPartCount` over the instances (743) + loot (7) |
| `serverUsablePartCount` | 26 | sum of `usablePartCount` over the instances (19, the doors) + loot (7) |
| `physicsLimits` (six counts) | 19 each | the number of door instances (19); research 83 saw the same on two more maps |
| `totalInstanceCount`, `reservedInstanceCount` | 0 | 0 |
| `ffMemCost` | 1240 | the same constant on mp_shipment and mp_backlot2 |
| `reservedDynents[0]`, `[1]` | 64 entries each, all zero, `freeHead` 0 | two zeroed arrays of 64 x 8 bytes; filled at run time |

Definition header numbers used (`asset.fields` of the ACTS export): `serverInstancedPartCount`, `usablePartCount`,
`partCount`, `maxNumDynEntPartsBase`. Doors: 1 / 1 / 1 / 2. `hardpoint_chevron`: 4 / 0 / 4 / 5.

Not solved yet, and needed before a build can carry instances:

- **The dynamic-entity side.** A scriptable instance takes a dynamic-entity slot. mp_backlot2 MapEnts has
  `dynEntCountTotal` 202 = 138 instances + 64 reserved, `dynEntMaxPosePartCount` 39 and
  `dynEntsWithExtraPosePartsCount` 194. The converter already writes dynamic-entity counts for dynamic props
  (`DynamicEntityCounts` in `map_zone.h`); the scriptable instances have to be added to those tables. The exact rules
  for the pose-part counts are not decoded.
- **The spatial tree.** Stock has 15,067 partitions (8 bytes each) with 24-byte extents: a subdivision of the whole
  map, not a list of instances. The converter writes one leaf that covers everything; whether that is enough once
  instances exist is not known (it is what the two dynamic-entity populations use today).
- **Which definitions count toward `physicsLimits`.** Settled 2026-11-12 (research 89 section 5): the main rigid body
  and motion limits count instances whose definition has flag bits 13 and 14, the detailed limits those with bits 13 and
  15; exact on all 21 exported maps, Warzone maps included. Doors have all three bits.
- Second map checked the same day: mp_shipment (75 instances, all definitions matched). Fixed parts 609 = the sum (609, no loot); server instances 66 = the 66 instances whose definition has server parts; usable parts 0 and all physics limits 0 (no doors). The rules of the table hold on both maps.

## First game test (when F1 and F2 are built)

One instance of `hardpoint_chevron` (its definition is in an always-loaded zone, so a name-only reference is enough).
Pass: the map loads and the chevron is drawn. Fail modes to expect: a load error naming the scriptable, or a crash at
load if a count is too small.

## Unknowns that only the game settles

- Whether the loader resolves a `,<definition>` name-only reference from a custom map zone.
- Whether a definition carried into a custom zone under its stock name works (F3).
- The meaning of `mapEntLookup` (0 on most doors) and of instance `flags` 1.

## Result of 2026-11-11 (late): instances are built; what is known now

Nothing here has been seen in game.

### The dynamic-entity side (was "not solved")

Decoded from the `dynentitylist` of mp_shipment and mp_backlot2, then checked against all 21 stock multiplayer maps
by `tools/build_scriptable_catalog.py`:

- **Every dynamic entity of a stock map belongs to a scriptable.** There is no other kind in these maps. Two types:
  `DYNENT_TYPE_SCRIPTABLEINST` (3) and `DYNENT_TYPE_SCRIPTABLEPHYSICS` (4). Each entry carries the instance's index
  (`scriptableMapIndex`), a sub index, and `scriptableMaxPartCount` = the definition's `maxNumDynEntPartsBase`.
- **Physics entries**: one per dynamic entity the definition requires (`maxNumDynEntsRequired`; cone 1, cars 1 to 5),
  sub index 0, 1, 2. Priority always-active (255), spawned active, on the list's no-spatial chain.
  `MapEnts.dynEntNoSpatialCount[0]` is their number.
- **Instance entry**: at most one per instance. Radial priority 0 in spatial population 0, or, for doors, priority 3
  (sparse occluder) with collision flags 1 in population 1. Which definitions have one is NOT derivable from the header
  numbers or flags that were tried (a rule on the flag bits failed on 155 of 509 observed definitions), so the
  catalogue records what stock maps do per definition (`instance_dynent`, column `source` = observed).
- Client id 0 / 0, no model pointer, pose = the instance's origin and the quaternion of its angles.
- `dynEntMaxPosePartCount` = the largest `scriptableMaxPartCount`; `dynEntsWithExtraPosePartsCount` = the entries
  above 1 (shipment 47 / 163, backlot2 39 / 194: exact).
- Stock maps also carry 64 reserved physics entries (instance index 500000, 4 parts) and two 64-entry reserved-dynent
  tables. `ffMemCost` 1240 = the 216-byte block + those two tables (1024 bytes). The converter writes neither (as
  before this change, when maps already ran without them) and sets `ffMemCost` 216.

### The map-wide counts on 21 maps

`serverInstanceCount`, `serverFixedPartCount` and `serverUsablePartCount` equal the sums of the table above plus the
loot count on every one of the 21 maps (battle-royale maps included). The physics limits equal the number of door
(sparse-occluder) instances on the core maps, but not everywhere: mp_emporium has 18 against 11 doors, the tutorial
and battle-royale maps differ too. So other definitions add to the limits and the property that decides it is still
unknown. The catalogue therefore marks a definition `verified` only when it was observed on a map whose limits are
fully explained by its doors.

### What can be placed

- 837 stock definitions are catalogued; 73 are in a zone every multiplayer match has loaded (`common_core_mp`,
  `common_base_mp`, `common_mp`, `global_core_mp`): game-mode objects, equipment and killstreak objects.
- Stock maps reference `hardpoint_chevron` from `common_core_mp` with a comma name, the same name-only reference the
  converter now writes. That is the one definition that is always loaded AND verified: the editor builds it.
- `scriptable_auto_ascender` (`common_mp`) is observed only on battle-royale maps, whose limits are unexplained: not
  built.
- Doors, cones, cars, tyres, radios and the other world props live in stock map zones only. Placing one needs step
  F3. The build leaves such an entity out and says why.

### Step F3, for whoever continues

A definition must be carried into the map with its models, collision and effects. Two routes:

1. The fork's own dump-and-rebuild path: `zonetool dumpzone <stock map>` writes `scriptable/<name>.iw8`, and the
   generic writer (`zone/writer.cpp`, `neutral/scriptable.cpp`) can write a definition back. It writes whole zones
   through its own stream class, not through the native `ZoneWriter` the map build uses, so the definition would have
   to go either through an adapter (the generated `stream_scriptabledef` over the native buffer) or into one of the
   map's side zones.
2. A native copier like `iw8::stock::Copier` (ACTS export with native bytes, pointers relocated): generic over the
   ACTS node tree, with the stream order taken from `asset_streams.cpp`.

Either way the models of the definition must come along (the editor's stock-model route can do that part), and the
result can only be proven in the game: the load order of name-only references, the door's collision (F4) and the
`system_init` call (F1, already in the converter) all need a run. This is the one part of the scripted-object work
that cannot be finished offline.

### Proof and tools

- `tools/scriptable_offline_test.py <extraction> <map id> <work folder>`: (1) the old and the new converter give
  byte-identical zones for a map without scriptables (checked on a small map and on showcase 2); (2) with six
  instances (chevron, door, cone, car profiles) ACTS loads the map zone with 0 failed and reads back every record,
  count, script string and chain. 38 checks pass.
- End to end: `out/scr-test/mp_scrtest.map` with three chevrons and one cone builds headless: 3 instances written,
  the cone left out with its reason, all five loader lines 0 failed.
- `tools/build_scriptable_catalog.py` (exports in `out/scriptable_study`), `tools/scriptable_dynents.py`,
  `tools/scriptable_counts.py`, `tools/acts_export.py`.

## Step F3: done offline (2026-11-12)

Nothing here has been seen in game.

The section "Step F3, for whoever continues" above is superseded: the fork already loads every map zone it builds
and writes it again with its generic writer (`writeZone` in `convert.cpp`, `rewrite_fastfile`), and that writer takes
additional definitions that are in memory. So a stock definition is carried like this:

1. The instance in the collision JSON names the stock zone (`carry_zone`, for example `mp_backlot2`). The native map
   body writes that instance with a null definition pointer and no name reference.
2. `CarryScriptableDefinitions` (`convert.cpp`) loads the stock zone with the fork's loader (about a second), finds
   the definition, and asks the new `asset_closure` for everything it uses that the zone holds in full: a scratch
   zone that lists only the definition is written with all of the stock zone's assets at hand, and what the writer
   emitted is the closure. For the wooden door: 1 scriptable, 2 models, 5 model surfaces, 1 physics asset, 1 detail
   collision. The definition names its two materials by reference; those are in the zone's techsets companion
   (`techsets_mp_backlot2`), whose closure gives 2 materials and 1 technique set and names 7 images, which are back
   in the map zone. The two closures are followed until neither names anything the other holds.
3. What is still only named must be in a zone that is always loaded. The build checks it: 12 shaders in
   `code_post_gfx`, technique sets in `techsets_common*`; a name found nowhere is a warning. Engine images
   (`$white`, `$black`) are skipped.
4. When the map zone is loaded to be written again, a prepare hook adds the carried assets that the zone does not
   already hold and points each carried instance's `initialDef` at the stock definition in memory; the writer then
   writes the definition at its first use, as stock zones have it. The techsets zone gets its additions the same
   way. The main zone gets a name reference entry per carried material (the converter's rule: every techsets-zone
   material has a peer in the main zone).
5. Streamed data: the carried images and model surfaces keep their stream keys. Their payload is in the game's
   global packs (`pak_launch*.xpak`, `pak_magma*.xpak`, `pak_tu*.xpak`), not in a per-map pack (those are 32 KB
   stubs), so it is available in any map.

Editor: off by default behind Edit > Preferences > Real stock scripted objects (`build/realScriptables`). A
definition is built when it is `verified` and either always loaded or held by an `mp_` zone (317 definitions). A
placed door (usable sparse occluder) makes the map script call `scripts\engine\scriptable_door::system_init()`
right after the load script. Prefab bundles get the features `scriptable_instances`, `scriptable_def_carry` and
`stock_script_calls` with the switch on, so the shipped stock door builds its main contents.

Proof:

- `tools/scriptable_carry_test.py`: for `scriptable_door_wooden_panel_03_painted_mp` (mp_backlot2),
  `com_trafficcone_01` and `veh8_civ_lnd_decho_rebel_darkblue` (mp_shipment; 150 assets with animations, effects
  and a light) the definition ACTS reads from the built map zone equals the stock one field for field (load
  addresses and zone-relative script-string indices masked); map and techsets zones load with 0 failed; the
  instances name their definition without a comma, as stock maps do for their own definitions.
- `tools/scriptable_offline_test.py --new <exe>`: a map without carried definitions gives byte-identical zones.
- `out/scr-door/mp_scrdoor.map`: the stock door prefab, a cone and a chevron through the editor's headless build:
  switch off gives the fallback door and leaves the cone out; switch on gives "main contents", two stock zones
  read in 4.7 s, all loader lines 0 failed, and the converted script keeps the `system_init` call.

What only the game can settle (one load of a map with the door prefab):

- whether the loader accepts the instances at all (single-leaf spatial tree, no reserved dynent tables);
- whether a door definition inside a custom map zone behaves (prompt, swing, collision);
- for cars and other large definitions: effects and sounds they name from common zones.
