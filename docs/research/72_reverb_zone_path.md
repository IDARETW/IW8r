# 72. Reverb zone path: why the "hall" box changes nothing in game

Scope: read-only research. Nothing was built, run, edited or committed. No file under `E:\mw124` was changed.
Map: `mp_iw8r_fix1` (built output under `out/agent/mp_iw8r_fix1`). Date: 2026-11-11.

Marks: **VERIFIED** means read from a file or code line. **UNVERIFIED** means an inference that has not been run in game.
"Not found" means the search came back empty.

## Summary

- The files the editor writes match the converter's readers field by field, and every id matches a recomputed `HashName`.
- The map-wide zone is present and plays (bed and one-shots), so the sound table loads and the map zone is selected.
- The hall trigger box is at z 104 to 264. The floor is at z 0 and the walls stop at z 128. A standing player's point
  (eye height about 60 to 70, UNVERIFIED exact value) never reaches z 104. The box floats over the room.

## 1. Fields the converter reads from clienttriggers.json

Reader: `E:\mw124\x64-zt\src\convert\iw3_iw8\iw3_fastfile.cpp`, `ReadClientTriggers` (starts at line 9849). Comment at
9845-9848 says the trigger audio role drops when the name is missing from the bank.

| Field | Read at (iw3_fastfile.cpp) | Requirement | Built value (hall / map zone) | Match |
|---|---|---|---|---|
| `type` | 9895 | `& ~0x40`; bit 0x2 = audio zone | 2 / 2 | yes. Stock audio zones are 2 (room zones 66 = 2 + 0x40, the 0x40 is cleared here) |
| `origin` | 9896 | vec3, world space | [0,0,184] / [0,0,508] | yes, it is the box centre |
| `scriptDelay` | 9897 | float | 0.25 / 0.25 | yes, stock is 0.25 |
| `priority` | 9898 | float, highest wins | 40 / 0 | yes. Stock rooms 30 or 50, map zone 0 |
| `audio` | 9899, 9901-9902 | lower-case zone name; equal to source map name is renamed to target | "hall" / "mp_iw8r_fix1" | yes |
| `npc` | 9900 | string | "" | yes |
| `blend` | 9903 | int16, -1 none | -1 | yes |
| `audioState` | 9904 | int16, -1 none | -1 | yes |
| `rvbPan` | 9905-9914 | optional object; absent = zeros | absent (zeros) | yes. Stock `audioRvbPanInfo` values are all zero in the stock dump |
| `hulls[].midPoint` | 9916 | trigger space, relative to origin | [0,0,0] | yes (stock: 0) |
| `hulls[].halfSize` | 9917 | half extents | [200,200,80] / [528,528,780] | yes, same convention as stock (stock hall-type hull [318,251,53]) |
| `hulls[].slabs` | 9918-9920 | optional list of dir / midPoint / halfSize | [] | yes (stock: 0 slabs for audio zones) |
| hull count | 9924 | at least one, else throw | 1 | yes |

Frame of the hull: the header comment at `E:\mw124\x64-zt\src\games\iw8\zone\replay_client_triggers.h:9-16` says Replay
tests "point minus origin" against the hull, so hull values are relative to the origin. The editor writes exactly that.
No unit or frame mismatch was found. Slabs are not missing in any way that stock does not also do.

Extra fields the converter does not read: none. Fields the editor does not write (`rvbPan`, `slabs`, `npc`) default to the
stock values. The exporter comment at `E:\mw124\x64-zt\src\convert\iw7_iw8\export_entities.cpp:1520-1528` says
Replay plays the highest-priority audio zone, and the stock map zone is a priority-0 trigger named after the map.
The editor follows that.

Stock comparison (VERIFIED): `E:\IW8-Radiant\data\stock_dumps\mp_shipment\mw19replay\mp_shipment\assets\map_ents\maps\mp\mp_shipment.d3dbsp.0.asset.json`,
`clientTrigger`. Stock audio triggers are type 2 or 66, origin = box centre, `midPoint` 0, no slabs, priority 0 for the
map zone (huge box, origin [1361,740,6445], halfSize [31934,31680,20845]) and 30 or 50 for rooms. Audio names are byte
offsets into `triggerString`, which the converter maps back to names.

Built file (VERIFIED): `out\agent\mp_iw8r_fix1\iw8build\mp_iw8r_fix1\extraction\extracted\maps\clienttriggers.json`.
Two triggers: map zone (origin [0,0,508], halfSize [528,528,780], priority 0, audio "mp_iw8r_fix1") and hall (origin
[0,0,184], halfSize [200,200,80], priority 40, audio "hall").

Editor writer: `E:\IW8-Radiant\src\core\audio_zones.cpp:337-354` (`clientTriggersJson`). Type 2 at line 348, centre at 346,
halfSize at 347, hull midPoint [0,0,0] and `slabs:[]` at 350-351.

## 2. How the game is told the map-wide zone (no trigger)

- The converter has no special "default" name. Its comment at `iw3_sound.cpp:1407-1408` says the source map's own zone
  "is the map's default zone (stock mp_shipment.all holds zone 'mp_shipment', id = the table id)". Code at 1409-1417
  renames a zone whose `zoneName` equals the source map to the converted map name and sets `id = HashName(target)`.
- The table id is `HashName(target)` (`iw3_sound.cpp:1492`). Research 63 says the runtime selects the table by
  `HashName(map)` (`replay_sound.h:130`, cited in `docs/research/63_audio_vision_volumes.md:113`).
- In the stock dump the map zone is simply a type 2 client trigger with `audio` = map name, priority 0, and a huge box.
  Replay therefore finds the map zone through the trigger named after the map, and the zone with that name is the bank's
  zone whose id equals the table id.

Built file (VERIFIED):
- Table id: `7e5e0ebc` (build log line 117). Decimal 2120093372.
- Zone 0: id 2120093372 (= 0x7E5E0EBC, equal to the table id), `zoneName` "mp_iw8r_fix1", stateId 0, reverbIndex 0,
  `ambientStream` "test_bed", `ambientDefIndex` 0, occlusion/filter "default". This zone exists.

So the name is "the map's own name" and the zone exists. The bed and one-shots playing confirms the map zone is selected.
UNVERIFIED: whether Replay uses the zone id or the name for the no-trigger zone. Both match here.

## 3. How a zone names its reverb

Converter (VERIFIED):
- Zone record (`iw3_sound.cpp:1251-1295`, stride 200): `reverbIndex` is an int16 at offset 8. The index is range-checked
  at `iw3_sound.cpp:1520-1523` (must be -1 or a valid index into `reverbs`). `reverbName` is a string at offset 96.
- Reverb record (`iw3_sound.cpp:1240-1248`, stride 84): `id` (0), `roomTypeId` (4), `roomType` char64 (8..71), `dryLevel` (72),
  `wetLevel` (76), `fadeTime` (80). All six are read. The editor writes all six, so the record has no missing field.
- Zone-to-reverb link is by index (`reverbIndex`) and by name (`reverbName`, whose hash is the reverb `id`). Research 63
  (`docs/research/63_audio_vision_volumes.md:300`) says the reverb id is `HashName(zone reverbName)`.

Editor (VERIFIED):
- `audio_zones.cpp:81`: `reverbNameOf` = zone name + "_reverb". Reverb id written at 316 = `soundHashName(reverbNameOf(z))`.
  Zone `reverbName` at 322 uses the same string. `roomTypeId` at 317 = hash of the room type.
- `readReverb` (59-79) lower-cases the room type and copies dry/wet/fade from the stock table (97-155). Only then are
  optional overrides applied, clamped to 0..1 and 0..30.
- `iw8_lg_room_warehouse` is in the list (line 117): dry 1, wet 0.75, fade 0.66.

Built file (VERIFIED):
- reverb 3374233589 = "iw8_ext_open_space_med_1", dry 1, wet 0.6, fade 0.66 (map zone, reverbIndex 0)
- reverb 153059763 = "iw8_lg_room_warehouse", dry 1, wet 0.75, fade 0.66 (hall, reverbIndex 1)
- Recomputed with the converter's HashName (replay_sound.cpp:633-641, same algorithm as `soundHashName`): `hall_reverb`
  = 153059763, `mp_iw8r_fix1_reverb` = 3374233589, `iw8_lg_room_warehouse` = 3117376457 (roomTypeId, matches file),
  `iw8_ext_open_space_med_1` = 3712260968 (matches). All ids match.

Parameters are complete. Note that the two presets differ only in wet (0.6 against 0.75). Dry and fade are the same (1 and
0.66), so the reverb change is small. This is the basis of hypothesis B below.

Stock reverb values to compare: Not found in the repo. The stock soundtable export is not present; only map_ents dumps
exist under `data\stock_dumps`.

## 4. When the converter drops or disables a client trigger's audio

Converter (VERIFIED), `iw3_fastfile.cpp`:
- 9875-9876: a zone enters the "stateless" set only if `stateId == 0`. Names come from the bank's zone string at index 1
  (`zoneName`, the 2nd string field of the zone record).
- 9925-9930: if bit 0x2 is set and the audio name is empty or not in that set, the trigger's type loses 0x42 and its audio is
  cleared. A warning is printed after the loop ("client trigger audio zone %s is not a stateless zone ...").
- 9931: `audio` counts only triggers that still have 0x2.
- 9924: a trigger with no hull throws (build fails, not a silent drop).
- 9895: 0x40 is always cleared (the propagation role is removed; no editor input sets it).
- The comment at 9845-9848 names two runtime symptoms: "Client Trigger Audio script_zone %s not matched" when the zone name
  is not in the bank, and a Com_Error for a zone with a state (stateId != 0). Those run in Replay, not in the build.

Editor-side drops (VERIFIED): `audio_zones.cpp:218-235` skips a non-trigger classname, an invalid name, a duplicate name,
and a trigger with no brush. Each adds a warning.

Build log `out\agent\mp_iw8r_fix1\build\compile-log.txt` (VERIFIED):
- Line 28: `audio: 2 sound zone(s) (map reverb iw8_ext_open_space_med_1) written ...`
- Line 117: `sound table 7e5e0ebc: 2 zone(s), 2 reverb(s), 5 filter(s), 0 mix(es), 167 occlusion filter(s)`
- Line 128: `2 client trigger(s), 2 audio zone(s), 0 blend node(s), 0 PA speaker(s)`
- No "not a stateless zone" warning, no "audio:" drop warning, no "client trigger without hulls" error. **No drop for this map.**
- Line 157: `entities: dropped 15 key(s) with no Replay key id: iw8r_audio_interior (1), iw8r_audio_priority (1), iw8r_audio_zone (1), ... iw8r_reverb (1)`.
  These are entity-key drops from the server entity record. They are expected: the audio data travels in the two JSON files,
  not in the entity. This is not the cause.

## 5. Hypotheses for "no audible change inside the box", ranked

**A. (strongest) The box is above anything the owner can stand in.**
- For (VERIFIED from the map source `out\agent\mp_iw8r_fix1\mp_iw8r_fix1.map`):
  - Floor brush z -16 to 0 (entity 0, brush 0).
  - Worldspawn walls z 0 to 128 (brushes 3 to 6). Ladders reach 160 (not walkable space at the room centre).
  - `hall_zone` brush (entity lines 238-251): x -200 to 200, y -200 to 200, **z 104 to 264**.
  - `info_player_start` origin 0 -192 16.
  - Built hull (VERIFIED): origin z 184, halfSize z 80, so z 104 to 264. Consistent with the brush.
  - The camera test (`CG_TriggerUpdateForCameraBounds`, named at `export_entities.cpp:1525`) would be at eye height. Even a
    hand-raised eye is far under 104.
  - The owner's report "walking into the box" is consistent with standing under it.
- Against: the exact test point (feet, eye, or camera) is not verified. A jump or the jump pad (`pad` trigger at
  (0,0,0..16) with `iw8r_jump "0 0 700"`, map lines 187-189) might reach 104. Not tested.
- Cheapest editor-output test: change the `hall_zone` brush in the .map (or rebuild from the editor) to span floor to
  about 128, for example z 0 to 128 at the room centre. Rebuild and check that `clienttriggers.json` shows origin z
  about 64 and halfSize z about 64. Then walk in.

**B. The zone is selected, but the change is too small to hear.**
- For: the hall reverb differs from the map reverb only in wet (0.75 against 0.6). Dry and fade are identical (1 and 0.66).
  The `interior` occlusion/filter presets affect only sounds that route through occlusion, so the owner may hear little.
- Against: the same numbers are used in stock warehouse-type zones (research 63 `Stock room types`), so a difference is
  expected but may be subtle. UNVERIFIED how audible a 0.15 wet change is.
- Cheapest editor-output test: set `iw8r_reverb` on the hall to an extreme stock room, for example `deathsdoor_verb`
  (dry 0.5, wet 1, fade 2; `audio_zones.cpp:97`) or `iw8_lg_swimming_pool_room` (wet 0.7, fade 1; line 119). Rebuild.
  Use this together with test A, because a change here only shows if the box is entered.

**C. The zone switch does not replace the map bed, so a marker is needed to know whether the zone ever became active.**
- For: the hall zone has `ambientStream` "" and `ambientDefIndex` -1, while the map zone plays `test_bed`. If Replay
  swaps the zone's ambience on entry, entering the box would stop the bed. The owner reports the bed plays everywhere
  (UNVERIFIED that the zone switch swaps the bed).
- Cheapest editor-output test: give the hall box its own `iw8r_ambient` (a custom sound such as `test_hum`, which
  `iw8r_ambient` accepts as the map's own sound, `audio_zones.cpp:172-174`). Rebuild. If the hum is heard inside the box,
  the zone is active and the fault is the reverb or occlusion (hypothesis B). If nothing is heard, the zone never activated
  (A or D).

**D. The client trigger is not in the built package (converter emitted it, zonetool did not stamp it, or it did not load).**
- For: the log says 2 triggers, but nothing in the log shows the `.ff` contents.
- Against: `map_zone.h:401-470` stamps the count and the arrays whenever the set is non-empty, and the log matches.
- Cheapest check (not an editor change): run `tools\verify_map_output.py` on the package directory (read-only; it runs
  ACTS on the `.ff`). Not run here.

**E. Name collision or priority tie (low).**
- Zone name "hall" is generic. The bank is per map (`ReadClientTriggers` checks the map's own bank, `iw3_fastfile.cpp:9868-9877`),
  so a collision is unlikely.
- Priority 40 against 0 is not a tie, and the box is inside the map zone, so the hall should win if entered.
- Cheapest test: rename to `iw8r_hall` and set priority 1000 (clamp max, `audio_zones.cpp:239`).

## 6. Not determined

- Which point the trigger test uses (feet, eye, or camera) and whether that point can reach z 104 by jumping. The header
  says "CG point" (`replay_client_triggers.h:13-14`) and the exporter says `CG_TriggerUpdateForCameraBounds`
  (`export_entities.cpp:1525`). Not found: the Replay function body.
- Whether Replay replaces the zone's ambience on entry (hypothesis C).
- Whether Replay accepts the room type string and which field it uses to choose the reverb (index, name hash, or
  `roomTypeId`). The editor is consistent on all three, so this is not the fault.
- Stock reverb values for the room types: not found in the repo.
- Whether the built `.ff` contains the client triggers: not checked (tool not run).
- The runtime messages "Client Trigger Audio script_zone %s not matched" and the stateId Com_Error: no Replay log was
  available to read.
- Whether the rvbPan defaults (all zero) are correct for audible reverb pan. Stock uses zeros too, so likely fine.
- Whether a jump from the `pad` trigger (jump velocity 700) reaches z 104. Not computed.
- Whether the `iw8r_ambient` one-shots and bed are replaced or mixed on zone change.

## Files read

Editor: `E:\IW8-Radiant\src\core\audio_zones.h`, `audio_zones.cpp`, `src\core\compile_iw8.cpp:2565-2583`,
`docs\research\63_audio_vision_volumes.md`, `tools\verify_map_output.py` (header only), `docs\HELPER_RULES.md`.
Built: `out\agent\mp_iw8r_fix1\iw8build\...\extracted\sound\soundtable.json`, `...\extracted\maps\clienttriggers.json`,
`out\agent\mp_iw8r_fix1\build\compile-log.txt`, `out\agent\mp_iw8r_fix1\mp_iw8r_fix1.map`.
Converter (read only): `E:\mw124\x64-zt\src\convert\iw3_iw8\iw3_fastfile.cpp` (9845-9935),
`iw3_sound.cpp` (1186-1300, 1397-1550), `src\games\iw8\zone\map_zone.h` (395-470),
`replay_client_triggers.h`, `src\convert\iw7_iw8\export_entities.cpp` (1515-1530), `src\games\iw8\assets\replay_sound.cpp` (633-641).
Stock: `E:\IW8-Radiant\data\stock_dumps\mp_shipment\...\mp_shipment.d3dbsp.0.asset.json`.
