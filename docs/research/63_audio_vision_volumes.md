# 63. Audio and vision by volume (roadmap 6.22 and 5.18)

Scope: research only. Roadmap row 6.22 (reverb and ambient zones, occlusion, music states for a custom MP map) and row 5.18 (vision or fog by volume). Date: 2026-11-10. Nothing was built, run, edited or committed. x64-zt (`E:\mw124\x64-zt`) was read only, and its branch was not changed.

Path shorthand used below:

- `FORK:` `E:\mw124\x64-zt\src\` (converter, read only)
- `DUMP:` `E:\mw124\tools\dvar_search_web\src\combined\rawfile_source\scripts\` (stock GSC decompile)
- `BI:` `E:\IW8-Radiant\data\generated\iw8_gsc_builtins.txt` (builtin list, `f` = function, `m` = method on an entity). The fork's copy is `FORK:convert\iw3_iw8\iw8_gsc_builtins.inc`, and the two lists agree on every name checked here.
- `ED:` `E:\IW8-Radiant\src\core\`

Marks: **UNVERIFIED** means a guess or an inference from code that has not been run in game. Stock GSC names are `_encstr_...` tokens in the dump, so the literal names of stock visions, music states and zones cannot be read offline. Nothing in this doc depends on them.

---

## Summary

1. **Audio zones are data, not script.** Reverb, ambience, occlusion and stateless audio zones all come from two files that the converter reads from the map extraction: `extracted/sound/soundtable.json` and `extracted/maps/clienttriggers.json`. The editor writes neither today. The only producer of either file in the repo is zonetool's IW7 exporter, which needs an IW7 source bank. An IW8 Radiant map therefore has no audio zones, no reverb and no ambient zones. The fix is for the editor to author both files (section A4).
2. **Ambience you can place today** is `sound_emitter` (loop or repeating one-shot), written to the map script and `extracted/sound/<map>.stock_sounds.txt` (`ED:map_script.cpp:56-158`, `FORK:convert\iw3_iw8\iw3_fastfile.cpp:12383-12440`). It has no zone, no reverb and no falloff volume.
3. **Music** can only use stock music state names. The converter does not read music states at all (no `music` table in `FORK:convert\iw3_iw8\iw3_sound.cpp`). A custom music state is not supported by any path in the repo (UNVERIFIED that no other path exists).
4. **Per-volume vision is possible in GSC**, but the vision it switches to must be a zone asset, and the converter writes only one vision per map (`FORK:convert\iw3_iw8\iw3_fastfile.cpp:10972`). A second named vision needs a fork change (section B5). Until then the recipe in B3 can only switch to a stock vision whose name the offline dump cannot resolve.
5. **Fog by volume has no GSC route.** There is no fog builtin in `BI` (`grep -i fog` on the list returns nothing). `setExpFog` is a compile-time regex in the converter (`FORK:convert\iw3_iw8\iw3_vision.cpp:199-263`), so it is global to the map. The only per-volume fog is a second vision file with different fog fields, which is the same blocker as B5.

---

## A. Audio: reverb, ambient zones, occlusion, music

### A1. What x64-zt reads for map sound

| Input | Reader | Notes |
|---|---|---|
| `extracted/sound/soundtable.json` | `ReadSoundTable` `FORK:convert\iw3_iw8\iw3_sound.cpp:1395-1412` | Fixed file name, not per map. Optional `map` key: a zone whose `zoneName` equals the source map name is renamed to the converted map and gets `id = HashName(target)` (`:1407-1417`). Breath states are filled in from a built-in table when `breath_states` is present (`:1420-1442`). |
| Same file, validation | `iw3_sound.cpp:1500-1530` | Throws if a zone's `reverbIndex` is `>= reverbs.count` or `< -1`, if any range `start + num` passes its array, or if `ambientDefIndex >= ambient_defs.count` or `< -1`. `-1` means none. |
| Bank build | `Convert` `iw3_sound.cpp:1722-1726`; bank name `Lower(map) + ".all"` at `:1845` | A soundtable alone is enough to build a bank: `if (requested.empty() && candidates.empty() && !table) return nullopt;` (`:1725`). |
| Bank into the level | `iw3_fastfile.cpp:12438` (`result.soundBank`) | |
| `extracted/maps/clienttriggers.json` | `ReadClientTriggers` `FORK:convert\iw3_iw8\iw3_fastfile.cpp:9845-9935` | Audio zone names are checked against the bank's **stateless** zones (`stateId == 0`, `:9863-9875`). A name with no match is dropped with a warning and the trigger loses its audio role (`:9921-9929`). Called at `:12440`. |
| Map-script sounds | `extracted/sound/<map>.stock_sounds.txt`, read at `iw3_fastfile.cpp:12388-12400` | Stock aliases only. Registered with the compat layer at `:12424-12432` and `:12666`. |

Field names are the Replay names in `FORK:games\iw8\structs.hpp` and the schema table in `iw3_sound.cpp:1186-1300`. The IW7 exporter that produces the same JSON is `FORK:convert\iw7_iw8\export_sounds.cpp:428-591` (write at `:589`). That exporter is the best reference for a valid file, because it writes every key the reader uses.

#### soundtable.json keys the reader uses

- `map` (string, optional): source map name for the rename above.
- `zones[]`: `id` (u32), `stateId` (u32; 0 = always on for the map, non-zero = named state), `zoneName` (string), `reverbIndex` (i16, -1 none), `ambientDefIndex` (i16, -1 none), `ambientStream` (string), `ambientName`, `mixName`, `reverbName`, `filterName`, `occlusionName`, plus the `start*`/`num*` index pairs for `occlusion_filters`, `filters`, `mixes`, `player_adsr_zones`, `npc_adsr_zones`, `full_occlusions`, `breath_states` (`iw3_sound.cpp:1240-1295`).
- `reverbs[]`: `id`, `roomTypeId` (u32), `roomType` (string, max 63 bytes), `dryLevel`, `wetLevel`, `fadeTime` (floats) (`iw3_sound.cpp:1240-1248`; writer `export_sounds.cpp:525-533`).
- `ambient_defs[]`: `name`, `id`, `ambientEventIndex` (u16), `numEvents` (u16), `timeMin`, `timeMax` (`iw3_sound.cpp:1186-1193`).
- `ambient_events[]`: `id`, `ambientElementIndex` (u16), `weight` (`:1194`).
- `ambient_elements[]`: `id`, `aliasName` (string), `rangeMin`, `rangeMax`, `coneMin`, `coneMax` (`:1196-1204`).
- `occlusion_filters[]`, `filters[]`, `mixes[]` (`:1212-1240`). Read them only when a zone's `num*` is non-zero.

The reader's zone id for a stateless map zone is `HashName(name)`. `HashName` is `hash = 5381; for c in lower(name): hash = 65599*hash + c` (signed char), with 0 mapped to 1 (`FORK:games\iw8\assets\replay_sound.cpp:633-641`). The table is selected by `HashName(map name)` (`FORK:games\iw8\assets\replay_sound.h:130-131`). An editor writer can compute ids with this function. It must lower-case the name first.

#### Reverb room types

- `roomType` is a free string in the file. The IW8 side stores it as a 64-byte field (`FORK:games\iw8\neutral\sound.cpp:1060-1072`). The reader does not check it against a list.
- No list of valid room type names exists in the repo. The stock `is_roomtype_valid` in `DUMP:mp\audio.gsc` is an empty function, and its room name is an `_encstr_` token. **UNVERIFIED** which names Replay accepts. The IW7 exporter copies names from the source bank's send effects (`export_sounds.cpp:525-533`), so the source of names for IW8 is the IW7 bank. Recommendation: take a room type name from a stock soundtable if one can be exported, else ship a single placeholder and test it in game.

### A2. What the editor writes today

- `ED:compile_iw8.cpp:1919`: `"sun":{"ambient":[0.5,0.5,0.5],...}`. This value is dead for editor maps (`docs/research/55_unlit_baseline.md:107-117`).
- `ED:compile_iw8.cpp:2061-2068`: `sound_emitter` entities go to `readSoundPlacements` and then to `extracted/sound/<id>.stock_sounds.txt`. The placements go into the map script (`ED:map_script.cpp:144-158`), which spawns a `script_origin`, calls `playloopsound` for loops and repeats `playsound` every `delay` seconds otherwise.
- `ED:compile_iw8.cpp:1649`: `sound_emitter` is skipped as a brush or point entity. `ED:compile_iw8.cpp:1651`: `iw8r_volume` is skipped and written as a volumetric box (`docs/research/46`).
- No code in `ED:` writes `soundtable.json`, `clienttriggers.json`, a music state, or a reverb. `grep -rn "soundtable\|clienttriggers\|reverb" ED` returns nothing. (The only `ambient` hits are the unused sun key and the light grid, `docs/research/58_light_grid.md`.)
- The sound dock uses 11,709 stock aliases (`data/asset_catalog/iw8_common_sounds.tsv`, `docs/PROGRESS.md:230`). Every `soundalias` must be one of them. The converter's only other alias source is the map's `.csv` and `zone` alias lists (`iw3_sound.cpp:1722-1740`).

### A3. Stock script surface for audio

All names below are confirmed in `BI`. Usage counts come from `DUMP`.

| Need | Builtin (BI line) | Stock use in DUMP | Notes |
|---|---|---|---|
| Put one player in a named audio zone | `m setclienttriggeraudiozone` (2431), `...zonelerp` (2432), `...zonepartial` (2433), `...zonepartialwithfade` (2434) | `cp\cp_hud_util.gsc:151`, `mp\gametypes\br_ending.gsc:2766`, many more | Signature from use: `(zoneName, fadeSeconds)`. Zone names are the `zoneName` of a stateless or named zone. |
| Leave the zone | `m clearclienttriggeraudiozone` (1315) | `cp\cp_hud_util.gsc:171`, `mp\infilexfil\lbravo_infil.gsc` | Argument is the fade time. |
| Priority zone (wins over volumes) | `m setpriorityclienttriggeraudiozone` (2626), `...partial` (2627), `...partialwithfade` (2628); `m clearpriorityclienttriggeraudiozone` (1344) | `cp\gametypes\cp_wave_sv.gsc` family | Same arguments. |
| Music state, per player | `m setplayermusicstate` (2616) | `mp\gametypes\br.gsc:2237`, `mp\gametypes\br.gsc:2541` | Argument: stock music state name. |
| Music state, global | `f setmusicstate` (843), `f stopmusicstate` (927), `f musicplay` (590), `f musicstop` (591) | `setmusicstate` 277 uses in DUMP | Stock state names only. |
| Reverb | `m setreverb` (2636), `m setreverbfromtable` (2637), `m deactivatereverb` (1395) | **0 uses** in DUMP | Signatures **UNVERIFIED**. Stock `mp\audio.gsc:36-60` keeps a reverb table in `level.audio` with no builtin call. Reverb in stock comes from the zone's `reverbIndex` (A1). |
| Ambient | `f ambientplay` (16), `f ambientstop` (17) | **0 uses** in DUMP | Signature **UNVERIFIED**. Use `sound_emitter` (A2) or a zone's `ambient_defs` instead. |
| Sound submix (per player) | `m setsoundsubmix` | `mp\audio.gsc:24` (`self setsoundsubmix(...)`) | Name is a stock submix. |
| Trigger test | `m istouching` (1995), `m getistouchingentities` (1667), `f getent` (300) | `mp\bots\*`, `anim\battlechatter.gsc:3392` | Use with a `trigger_*` brush entity (B2). |

### A4. Recipe: minimal ambient and reverb setup for a custom MP map

Mark each step with its status. Steps 1 and 2 are needed for every zone. Steps 3 to 6 are the new work.

**Step 1 (works today): ambience with no zone.** Place `sound_emitter` points with a stock `soundalias` from the sound dock, `loop 1` for a bed or `delay` for a repeating one-shot. This already compiles to the map script and `stock_sounds.txt`. In-game check pending (`docs/ROADMAP.md:260`).

**Step 2 (new, editor): write `extracted/sound/soundtable.json`.** The minimal valid file is:

```json
{
  "map": "mp_iw8r_yourmap",
  "send_effects": [],
  "reverbs": [
    {"id": 1, "roomTypeId": 0, "roomType": "PLACEHOLDER_ROOM", "dryLevel": 1.0, "wetLevel": 0.3, "fadeTime": 2.0}
  ],
  "zones": [
    {"id": 0, "stateId": 0, "zoneName": "mp_iw8r_yourmap", "reverbIndex": 0, "ambientDefIndex": -1,
     "ambientName": "", "reverbName": "PLACEHOLDER_ROOM", "startOcclusionIndex": 0, "numOcclusion": 0,
     "startFilterIndex": 0, "numFilter": 0, "startMixIndex": 0, "numMix": 0,
     "startAdsrZoneIndex": 0, "numAdsrZone": 0, "startNPCAdsrZoneIndex": 0, "numNPCAdsrZone": 0,
     "startFullOccIndex": 0, "numDisableFullOcc": 0, "startPlayerBreathStateIndex": 0,
     "numPlayerBreathStates": 0}
  ],
  "ambient_defs": [],
  "ambient_events": [],
  "ambient_elements": []
}
```

Rules, all from the reader:

- Every `id` is `HashName(zoneName)` for zones and `HashName(name)` for `ambient_defs` (A1). The reader does not check ids, but `CG_StartClientSideTriggersAudio` selects the table by `HashName(map)` (`replay_sound.h:130`), so the map zone id must be `HashName(map name)`.
- `reverbIndex` must be `-1` or a valid index into `reverbs` (`iw3_sound.cpp:1520-1523`). `ambientDefIndex` must be `-1` or a valid index into `ambient_defs` (`:1528`).
- Each zone's `start*/num*` pair must stay inside its array (`:1525`). Zero counts are always valid.
- The map name must match the bank name `Lower(map) + ".all"` (`iw3_sound.cpp:1845`), which is the converted map.
- **UNVERIFIED:** whether Replay needs a `breath_states` array (the reader adds the stock set only when the array is present and non-empty, `:1420-1442`). Leave it out first, then test.

**Step 3 (new, editor): stateless audio zones as trigger volumes.** Add a `trigger_multiple` brush entity with `targetname` and a new key naming the zone (for example `iw8r_audio_zone` = the zone name from step 2). Today `ED:compile_iw8.cpp:1109-1115` already compiles `trigger_*` brushes as brush models with trigger contents. The editor must also write `extracted/maps/clienttriggers.json`, with:

```json
{"map": "mp_iw8r_yourmap",
 "triggers": [
   {"type": 2, "origin": [0,0,0], "scriptDelay": 0, "priority": 10, "audio": "mp_iw8r_yourmap",
    "npc": "", "blend": -1, "audioState": -1,
    "hulls": [{"midPoint": [0,0,0], "halfSize": [256,256,128],
               "slabs": [{"dir": [1,0,0], "midPoint": 0, "halfSize": 256}]}]}
 ],
 "blend_nodes": [], "pa_speakers": []}
```

- `type` bit `0x2` is an audio zone (`iw3_fastfile.cpp:9921-9929`; exporter keeps `0x2 | 0x8 | 0x10 | 0x40`, `export_entities.cpp:1551`).
- `audio` must match a **stateless** zone in step 2 (`iw3_fastfile.cpp:9865-9880`).
- Each trigger needs at least one hull, or the reader throws (`:9920` "client trigger without hulls").
- **UNVERIFIED:** the exact meaning of `slabs` and `midPoint` in trigger space. The exporter comment says Replay tests `CG point - origins[i]` against `triggerSpaceBounds` and the slabs (`export_entities.cpp:1520-1530`), but how the editor should derive slabs from an axis-aligned brush is not written down. Start with one slab per axis and check the converter's `ReadClientTriggers` info line, then test in game.
- `priority`: the exporter gives every IW7 zone priority 10 and relies on nesting (same comment). Use the same.

**Step 4 (new, editor): occlusion and ambient zones.** Occlusion lives in `occlusion_filters` (`iw3_sound.cpp:1221-1234`) and is referenced by `startOcclusionIndex` and `numOcclusion` on a zone. Ambient zones are `ambient_defs` (timed random one-shots, `timeMin`/`timeMax`) pointing at `ambient_events` (weighted) that point at `ambient_elements` (`aliasName`, range and cone). Each `aliasName` must be a stock alias in the sound dock. Set the zone's `ambientDefIndex` to the def index. **UNVERIFIED** in game.

**Step 5 (GSC, map script): change zones in script.** For a `stateId != 0` zone, select it from script:

```
// user_main(), worldspawn iw8r_script (ED:compile_iw8.cpp:2091-2099)
self setclienttriggeraudiozone("mp_iw8r_hall", 1.0);   // enter
self clearclienttriggeraudiozone(1.0);                    // exit
```

The zone must be a named zone in the soundtable (`stateId != 0`). The stock pattern is in `DUMP:cp\cp_hud_util.gsc:151,171`. The argument is the zone name, not an id.

**Step 6 (GSC, map script): music.** Only stock states are usable. `self setplayermusicstate("<stock state name>")` (BI 2616). A custom state would need a `sound_music_state` reader that does not exist (no `music` table in `iw3_sound.cpp`). The struct exists in `ED`'s core model (`src/core/assets/sound_data.hpp:553-565`, fields `intro`, `exit`, `loops`, `order`, `interrupt_priority`, `persist_priority`, `is_random`, `is_sequential`) but no converter writes it. **UNVERIFIED** whether any stock music state name in `DUMP` is usable on another map. The names are `_encstr_` tokens and cannot be read offline.

### A5. Editor vs fork changes for audio

Editor (IW8-Radiant), in order of value:

1. Write `extracted/sound/soundtable.json` from new entities, using the schema in A1 and the ids in A4 step 2. Add `iw8r_reverb` (a point entity or worldspawn keys for dry, wet and fade, plus a `roomType` string) and `iw8r_audio_zone` (a `trigger_*` brush with the zone name). Put the writer next to `stockSoundList` in `compile_iw8.cpp:2068`.
2. Write `extracted/maps/clienttriggers.json` from `iw8r_audio_zone` brushes (A4 step 3).
3. Optionally an `iw8r_ambient_def` entity for `ambient_defs` (A4 step 4).
4. Add a warning in the build report when a zone exists but no reverb room type is set (the room type string is unknown, see A1).

Fork (x64-zt), only if the editor path above fails in game:

1. Nothing is needed to read the file the editor writes, since `ReadSoundTable` already reads the schema. The one gap is `breath_states`, which only the IW7 exporter writes.
2. Music states are not read at all. A custom music state needs a new `sound_music_state` table reader and a bank writer. This is a large change and is not in the 6.22 scope.
3. Reverb room types: a list of valid names would let the editor validate them. None exists in the repo.

---

## B. Vision and fog by volume

### B1. What stock MP does for one player

Stock MP changes a single player's vision with `visionsetnakedforplayer(name, transitionSeconds)` (BI line 2986). Examples in `DUMP`:

- `mp\gametypes\obj_bombzone.gsc:795-815` (`runobjectivecam`): sets a per-player vision for `0.5` seconds around an objective camera, then restores `_encstr_B40101` for `0.5`. This is a camera, not a volume, but the set and restore pair is the same shape.
- `mp\gametypes\br_zones.gsc:295-305` (enter): `scripts\cp_mp\utility\game_utility::_visionsetnakedforplayer("...", 0.5)`. `br_zones.gsc:362-376` (exit): restores `_encstr_B40101` for `0.5`. The enter and exit functions are registered as callbacks of a `targetsite` helper at `br_zones.gsc:8`. This is the closest stock pattern to "enter a volume, leave, restore".
- The base naked vision for MP is `_encstr_B40101`: `DUMP:mp\gamelogic.gsc:2089,2248,2253`, `DUMP:cp\cp_globallogic.gsc:506-511` (`setdefaultvisionset` also sets night, missile cam and thermal), `DUMP:cp\cp_visionsets.gsc:66-71` (stack base).

Other per-player vision builtins in `BI` (all `m`): `visionsetalternateforplayer` (2982), `visionsetfadetoblackforplayer` (2983), `visionsetkillstreakforplayer` (2984), `visionsetmissilecamforplayer` (2985), `visionsetnightforplayer` (2987), `visionsetpainforplayer` (2988), `visionsetthermalforplayer` (2989). The global forms are `f visionsetnaked` (1023), `visionsetnight` (1024), `visionsetpain` (1025), `visionsetthermal` (1026), `visionsetalternate` (1019), `visionsetfadetoblack` (1020), `visionsetmissilecam` (1022), `visionsetkillcamthirdpersonnight` (1021).

`m setvisionparams` (2741) is in `BI` but has **0 uses** in `DUMP`. Its signature and effect are **UNVERIFIED**. Do not rely on it.

### B2. Recipe: trigger volume that changes one player's vision and restores it

The map needs:

- a `trigger_multiple` brush with `targetname` `vision_vol` (the editor compiles it, `ED:compile_iw8.cpp:1109-1115`), and
- a vision name that exists in the zone (B3 and B5 explain the blocker).

Map script (put it in the file named by worldspawn `iw8r_script`; the editor's generated `main()` calls `level thread user_main();` when a script is present, `ED:map_script.cpp:96-120`, `:143`). Every builtin used is in `BI`: `getent` (300), `isalive` (473), `istouching` (1995), `visionsetnakedforplayer` (2986), and `level.players` as used in `DUMP:mp\gametypes\obj_bombzone.gsc:795`.

```
// IW8 Radiant map script, worldspawn iw8r_script. UNVERIFIED in game.
user_main()
{
    level.iw8r_base_vision = "_encstr_B40101";   // stock base naked vision; see B3 caveat
    level.iw8r_vol_vision  = "mp_iw8r_yourmap_vol"; // needs vision/mp_iw8r_yourmap_vol.vision in the zone (B5)

    trig = getent("vision_vol", "targetname");
    if (!isdefined(trig))
        return;
    level thread iw8r_vision_volume(trig);
}

iw8r_vision_volume(trig)
{
    for (;;)
    {
        wait 0.2;
        foreach (p in level.players)
        {
            if (!isdefined(p) || !isalive(p))
                continue;

            inside = p istouching(trig);
            was = isdefined(p.iw8r_in_vol) && p.iw8r_in_vol;

            if (inside && !was)
            {
                p.iw8r_in_vol = 1;
                p visionsetnakedforplayer(level.iw8r_vol_vision, 0.5);
            }
            else if (!inside && was)
            {
                p.iw8r_in_vol = 0;
                p visionsetnakedforplayer(level.iw8r_base_vision, 0.5);
            }
        }
    }
}
```

Notes:

- Polling is used instead of `waittill` because the stock notify names for brush triggers are not in the dump (`_encstr_`). `istouching` is the stock test (`DUMP:mp\bots\bots_gametype_btm.gsc:209`).
- `wait` and `foreach` are the same statements stock uses (`DUMP:mp\gametypes\obj_bombzone.gsc:795-806`).
- A respawned or disconnected player keeps `p.iw8r_in_vol`. Clear it on spawn if the vision must reset (UNVERIFIED need).
- The transition argument is seconds. `0.5` matches stock.
- **Caveat on the restore name:** the restore target must be the vision the map shows by default. The converter names the map's own vision after the map (`FORK:convert\iw3_iw8\iw3_vision.cpp:268-280`, file `vision/<map>.vision`, or the name in the map's `visionsetnaked`). Whether Replay shows that vision as the base naked vision for a converted map is **UNVERIFIED**. Restoring to `_encstr_B40101` (the stock base) may replace the map's own vision for the player. Safer: set `level.iw8r_base_vision` to the map's own vision name, once confirmed in game.

### B3. Which vision names can a script use

- **Stock names:** only the `_encstr_` tokens in `DUMP`. These are encrypted and the dump does not contain the key, so the literal names cannot be read offline. Using a stock name works only if the name is in the stock zone, which is a separate question (**UNVERIFIED**).
- **The map's own vision:** `vision/<map>.vision`, written by `FORK:convert\iw3_iw8\iw3_fastfile.cpp:10972`. Its name is the map name (or the name in the map's `visionsetnaked`, `iw3_vision.cpp:268-280`).
- **Custom vision volumes:** no path writes a second vision. `iw3_fastfile.cpp:1608` skips every map-local `.vision` file. `iw3_fastfile.cpp:10972` writes only `vision/<options.map>.vision`. The overrides file `extracted/vision/<map>.overrides.json` (`:10953-10963`) changes that one vision only.

So a volume-specific vision name is **not in the zone today**. The GSC in B2 will call a missing vision and do nothing visible (UNVERIFIED what Replay does with an unknown name).

### B4. Fog

- **No fog builtin** exists in `BI` (`grep -i fog` returns nothing). The map has one global fog, set at compile time by `setExpFog(start, halfway, r, g, b)` (regex in `FORK:convert\iw3_iw8\iw3_vision.cpp:199-263`, default and clamps at `:757-768`). The editor writes it in the generated `main()` (`ED:map_script.cpp:100-102`). A script call to `setExpFog` in a volume trigger would not be parsed.
- Per-volume fog therefore has to come from the vision itself. The vision fields are `fogDensity` (halfway, field 35) and `fogStartDist` (field 36) plus colour (`docs/research/31_atmosphere_route.md:315-316`). A second vision file with those fields set would give a volume its own fog, using the same switch as B2.
- Blocker: the same as B3. A second vision file is not written.

### B5. Editor vs fork changes for vision and fog

Editor (IW8-Radiant):

1. Add `iw8r_vision_volume` as a `trigger_multiple` brush (or reuse B2's `trigger_*` brush) with a `vision` key that names a vision file.
2. Write `extracted/vision/<name>.vision` for each volume. Reuse the writer already used for the map vision (`ED:compile_iw8.cpp:2105-2117`, `filmVision` and `visionOverridesJson`). Set the fog fields from the volume's own values.
3. Generate the switch code (B2) into the map script for each volume. Use `iw8r_script` for user code and a generated `iw8r_vision_volumes()` for the editor's own volumes, or generate both into the map `.gsc` (`ED:map_script.cpp`).
4. Warn when a volume names a vision that is not written.

Fork (x64-zt), required for B2 to show a volume's vision:

1. `PrepareFastfile` (`iw3_fastfile.cpp:10972`) writes only `vision/<map>.vision`. Change it to also emit every `extracted/vision/*.vision` file as `vision/<name>.vision` rawfiles. The name of each file must be the vision name the script uses. Test this with one map.
2. `iw3_fastfile.cpp:1608` skips map-local `.vision` files. Keep the skip for the map's own file if needed, but do not skip the extra volume visions.
3. Optional, for native vision triggers (client triggers with role `0x1`): `ReadClientTriggers` (`iw3_fastfile.cpp:9845-9935`) has no `vision` field and ignores role `0x1`. The IW7 exporter already clears that role for converted maps (`export_entities.cpp:1551`, comment at `:1520-1530`, and the `vision` field write at about `:1592-1596`). Reviving it would need a reader change plus `vision/<name>.vision` in the zone (step 1). This is a bigger change than the GSC route in B2; the GSC route is the first thing to try.

---

## Open items (UNVERIFIED list)

1. Whether Replay accepts a `roomType` string not in stock, and which names are valid (A1).
2. Meaning of `slabs` and `midPoint` in trigger space for an editor-drawn box (A4 step 3).
3. Whether a soundtable written by the editor (no `breath_states`, zone ids from `HashName`) loads and plays in game (A4 step 2).
4. Signature and effect of `setreverb`, `setreverbfromtable`, `ambientplay`, `setvisionparams` (zero stock uses, BI 2636, 2637, 16, 2741).
5. Stock music state names and zone names that work on another map (`_encstr_` in DUMP).
6. What Replay shows for `visionsetnakedforplayer` with a name not in the zone (B2).
7. Whether the map's own vision is the base naked vision for a converted map, and so the correct restore name (B2 caveat).
8. Whether the B2 polling loop is needed or whether a stock `trigger` notify exists for brush triggers (not in the dump).

## Files read

- `E:\mw124\x64-zt\src\convert\iw3_iw8\iw3_sound.cpp` (1108-1130, 1186-1300, 1395-1445, 1500-1545, 1722-1726, 1845)
- `E:\mw124\x64-zt\src\convert\iw3_iw8\iw3_fastfile.cpp` (1600-1615, 9839-9935, 10953-10972, 12383-12440)
- `E:\mw124\x64-zt\src\convert\iw3_iw8\iw3_vision.cpp` (199-290, 757-768)
- `E:\mw124\x64-zt\src\convert\iw7_iw8\export_sounds.cpp` (428-591)
- `E:\mw124\x64-zt\src\convert\iw7_iw8\export_entities.cpp` (1520-1606)
- `E:\mw124\x64-zt\src\games\iw8\assets\replay_sound.cpp` (633-641), `replay_sound.h` (125-140), `src\games\iw8\neutral\sound.cpp` (1055-1075)
- `E:\IW8-Radiant\src\core\compile_iw8.cpp` (1109, 1649-1668, 1919, 2046-2117), `src\core\map_script.cpp` (56-158), `src\core\assets\sound_data.hpp` (553-565)
- `E:\IW8-Radiant\data\generated\iw8_gsc_builtins.txt` (lines cited inline)
- `E:\mw124\tools\dvar_search_web\src\combined\rawfile_source\scripts\` (mp\audio.gsc, mp\gametypes\br_zones.gsc, mp\gametypes\obj_bombzone.gsc, mp\gamelogic.gsc, cp\cp_hud_util.gsc, cp\cp_visionsets.gsc, cp\cp_globallogic.gsc)
- `E:\IW8-Radiant\docs\research\31_atmosphere_route.md`, `55_unlit_baseline.md`, `58_light_grid.md`, `ROADMAP.md` and `PROGRESS.md` (rows 5.6, 6.7, 6.22)

## C. Built (2026-11-11): reverb and audio zones

Stock facts from an ACTS export of `mp_shipment.all` (soundbank) and the mp_shipment `map_ents` client triggers:

- Every id is `HashName` of its name (`replay_sound.cpp`): zone `id` = HashName(zoneName) (48 of 48), reverb `id` =
  HashName(the zone's reverbName) (32 of 32), `roomTypeId` = HashName(roomType) (59 of 59). All 48 stock zones are
  stateless.
- Stock audio client triggers: type 2, hull `midPoint` 0 with the box half size and no slabs (the trigger origin is the
  box centre), script delay 0.25, priority 0 for the map-wide trigger and 30 to 50 for rooms.

The editor (core `audio_zones.*`, compile_iw8) writes both files from worldspawn `iw8r_reverb` and `iw8r_audio_zone`
trigger brushes. Real build (out/agent/mp_iw8r_audiotest): `iw3 sound: sound table ...: 2 zone(s), 2 reverb(s)` and
`iw3: 2 client trigger(s), 2 audio zone(s)`. Zone ambience is not possible this way: the bank carries only aliases with a
source definition (`alias amb_mp_ac130 has no IW3 definition; left silent`), so a looping `sound_emitter` stays the way to
place stock ambience. How the reverb sounds in game is NOT VERIFIED.

Occlusion and filter presets (2026-11-11): the stock bank has two of each, "default" (83 occlusion rows, 1 filter) and
"interior" (84, 4); occlusion row id = HashName(preset + channel name), filter row id = HashName(preset + channel name +
band) (checked on every row); channels are written by name (x64-zt kEntChannels). The editor copies them into the table
(core audio_presets.inc, generated from the export) and points each zone at one (iw8r_audio_interior). Real build:
"5 filter(s), 0 mix(es), 167 occlusion filter(s)".

Custom sounds (2026-11-11): the converter builds an alias when it has an IW3 definition for it
(extracted/soundaliases/<name>.iw3.json, ReadZoneAliases; file under extracted/sound) and the alias is a map-script literal
("alias(es) passed to map script helpers as literals") or a sound-table alias (zone ambientStream, ambient element). The
editor writes both for WAV / MP3 files in <map dir>/sounds (core custom_sounds.*). Ambient one-shot ids follow the stock
bank: def HashName(defName) (exterior_elements = 1385228915), event HashName(defName + alias), element HashName(alias).
Real build: 4 aliases, 4 sounds in the map bank.

PA speakers (2026-11-11): MapEnts.audioPASpeakerCount is 0 in all eight stock MP maps checked (mp_shipment,
mp_hideout, mp_m_king, mp_aniyah_tac, mp_emporium, mp_rust, mp_backlot2, mp_village2), so stock MP has no PA speakers to
match; the editor writes none.

Stock room types (with the dry / wet / fade of one stock reverb each; `iw8r_reverb "<room>"` uses these, or
`"<room> dry wet fade"`):

| Room type | dry | wet | fade |
|---|---|---|---|
| deathsdoor_verb | 0.5 | 1 | 2 |
| generic | 0.9 | 0.1 | 1 |
| iw8_bus_interior | 1 | 0.75 | 0.66 |
| iw8_ext_courtyard_village | 1 | 0.6 | 0.66 |
| iw8_ext_lg_valley | 0.8 | 0.3 | 2 |
| iw8_ext_open_space_lg_1 | 1 | 0.6 | 0.66 |
| iw8_ext_open_space_lg_2 | 1 | 0.6 | 0.66 |
| iw8_ext_open_space_lg_3 | 1 | 0.6 | 0.66 |
| iw8_ext_open_space_lg_4 | 1 | 0.6 | 0.66 |
| iw8_ext_open_space_med_1 | 1 | 0.6 | 0.66 |
| iw8_ext_open_space_med_2 | 1 | 0.6 | 0.66 |
| iw8_ext_open_space_med_3 | 1 | 0.6 | 0.66 |
| iw8_ext_open_space_med_4 | 1 | 0.6 | 0.66 |
| iw8_ext_rocky_forest | 1 | 0.6 | 0.66 |
| iw8_ext_street_village | 1 | 0.6 | 0.66 |
| iw8_lg_bank_vault | 1 | 0.75 | 0.66 |
| iw8_lg_partcover | 1 | 0.75 | 0.66 |
| iw8_lg_room | 1 | 0.75 | 0.66 |
| iw8_lg_room_dark | 1 | 0.75 | 0.66 |
| iw8_lg_room_tile | 1 | 0.75 | 0.66 |
| iw8_lg_room_warehouse | 1 | 0.75 | 0.66 |
| iw8_lg_room_wood | 1 | 0.75 | 0.66 |
| iw8_lg_swimming_pool_room | 1 | 0.7 | 1 |
| iw8_lg_tunnel | 1 | 0.75 | 0.66 |
| iw8_md_cave | 1 | 0.6 | 0.66 |
| iw8_md_ext_alley | 1 | 0.6 | 0.66 |
| iw8_md_ext_alley_2 | 1 | 0.6 | 0.66 |
| iw8_md_hallway | 1 | 0.75 | 0.66 |
| iw8_md_hallway_wood | 1 | 0.75 | 0.66 |
| iw8_md_partcover | 1 | 0.75 | 0.66 |
| iw8_md_room | 1 | 0.75 | 0.66 |
| iw8_md_room_dark | 1 | 0.75 | 0.66 |
| iw8_md_room_wood | 1 | 0.75 | 0.66 |
| iw8_no_wet_all_dry | 0.9 | 0.4 | 6 |
| iw8_sm_bathroom | 1 | 0.75 | 0.66 |
| iw8_sm_bathroom_dark | 1 | 0.75 | 0.66 |
| iw8_sm_container | 1 | 0.75 | 0.66 |
| iw8_sm_container_metal | 1 | 0.75 | 0.66 |
| iw8_sm_ext_alley | 1 | 0.75 | 0.66 |
| iw8_sm_hallway | 1 | 0.75 | 0.66 |
| iw8_sm_hallway_dark | 1 | 0.75 | 0.66 |
| iw8_sm_hallway_wood | 1 | 0.75 | 0.66 |
| iw8_sm_partcover | 1 | 0.75 | 0.66 |
| iw8_sm_pipe | 1 | 0.75 | 0.66 |
| iw8_sm_room | 1 | 0.75 | 0.66 |
| iw8_sm_room_dark | 1 | 0.75 | 0.66 |
| iw8_sm_room_wood | 1 | 0.75 | 0.66 |
| iw8_stairwell | 1 | 0.75 | 0.66 |
| iw8_stairwell_concrete | 1 | 0.75 | 0.66 |
| iw8_th_dining_room | 1 | 0.75 | 0.66 |
| iw8_th_ext_alley | 1 | 0.3 | 0.66 |
| iw8_th_kitchen | 1 | 0.75 | 0.66 |
| iw8_th_stairwell | 1 | 0.75 | 0.66 |
| iw8_th_upper_rooms | 1 | 0.75 | 0.66 |
| iw8_th_urban_street | 1 | 0.3 | 0.66 |
| iw8_urban_ext | 1 | 0.6 | 0.66 |
| iw8_urban_street | 1 | 0.6 | 0.66 |
| iw8_xlg_room_tile | 1 | 0.75 | 0.66 |
| underwater | 0.7 | 0.5 | 0.66 |
