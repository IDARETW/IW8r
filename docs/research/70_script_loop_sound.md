# 70. Why a map's own looping alias is silent when a map script plays it

Scope: research only. Nothing was built, run, edited or committed. Read only: `E:\IW8-Radiant` (editor sources and
outputs under `out\agent\mp_iw8r_fix1`), the x64-zt converter at `E:\mw124\x64-zt` (read only), and the translated
script under `C:\Users\user\AppData\Local\IW8 Radiant Suite\IW8 Radiant Suite\x64zt_output\.iw3-scripts\mp_iw8r_fix1`.
Date: 2026-11-11. Short names used below:

- `EDIT` = `E:\IW8-Radiant\`
- `X64` = `E:\mw124\x64-zt\src\`
- `OUT` = `EDIT\out\agent\mp_iw8r_fix1\iw8build\mp_iw8r_fix1\extraction\extracted\`
- `GSC` = the translated script in the x64zt_output folder above

## The fault in one paragraph

Four sounds come from the same map. `test_bed` (looping, 2D) is played by the map's ambient zone and is heard.
`test_chirp_a` and `test_chirp_b` (one-shot, 2D) are played by ambient one-shots and are heard. `test_hum` (looping,
3D) is played by the map script with `iw3compat_playloopsound` on a spawned `script_origin` and is not heard. A stock
loop (`bomb_tick`) played by the same script code is heard (owner report; no file in this build shows that test).
The converted `test_hum` and `test_bed` differ in only one converted field that matters here: the entity channel
(bits 10 to 17). The bed gets 2D channel 125 `amb_bed_2d`; `test_hum` gets 3D channel 168
`special_lo_unres_1_3d`. A second difference is the route: the bed is resolved from the sound table by the zone, while
`test_hum` is resolved by name from script. The converter's own template comment says that bit 19 means "precached",
and only the 3D loop template leaves bit 19 clear.

## Build facts checked

- `OUT\soundaliases\test_hum.iw3.json` and `test_bed.iw3.json` match the editor writer (see section 2).
- `OUT\sound\soundtable.json` names the zone bed `test_bed` (the ambient stream) and the chirps. It does not name
  `test_hum` (the file has no `test_hum` string). That is expected: `test_hum` is played by script, not by a zone.
- `OUT\sound\` has no `mp_iw8r_fix1.stock_sounds.txt`, so no alias of this map is a stock alias.
- `OUT\sound\iw8r\` holds `test_bed.wav` (4.0 s), `test_hum.wav` (2.0 s), `test_chirp_a.wav`, `test_chirp_b.wav`.
  Both `test_bed.wav` and `test_hum.wav` are PCM, mono, 44100 Hz, 16 bit (RIFF headers, bytes 20 to 35). No `smpl`
  chunk in either.
- The map source `EDIT\out\agent\mp_iw8r_fix1\mp_iw8r_fix1.map`:
  - lines 8 to 10: worldspawn `iw8r_ambient_oneshots "3 7 test_chirp_a:2 test_chirp_b"`, `iw8r_ambient "test_bed"`,
    `iw8r_reverb`. No `iw8r_audio_interior` on worldspawn, so the map-wide zone uses the "default" occlusion.
  - lines 232 to 235: `fx_origin` at `64 64 0` with `soundalias "test_hum"` and `fx "iw8r/test_campfire"`.
  - lines 254 to 258: `sound_emitter` at `0 0 48`, `soundalias "test_hum"`, `loop "1"`.
  - lines 361 to 365: `sound_emitter` at `230 -230 48`, `soundalias "test_hum"`, `loop "1"`.
  - lines 240 to 249: trigger `hall` (`iw8r_audio_interior 1`), box x and y in plus or minus 200, z from 104 to 264.
    None of the three `test_hum` origins is inside it (z is 0 or 48, and x is 230 for one of them).
- The map `OUT\maps\mp\mp_iw8r_fix1.d3dbsp.ents` lines 53 to 59: the `fx_origin` entity with `soundalias test_hum`.
- `GSC` lines 12 (thread start), 404 to 409 (`place_sounds`, three calls to the loop helper with origins
  `(64,64,0)`, `(0,0,48)`, `(230,-230,48)`), 413 to 417 (loop helper), 546 (`iw3compat_sounds()` call), 611 to 618
  (`iw3compat_sound_kind`), 622 to 627 (`iw3compat_sound_name`), 630 to 633 (`iw3compat_playsound`), 635 to 639
  (`iw3compat_playloopsound`), 1940 to 1948 (`iw3compat_sounds`, with `level.mw120r_sounds["test_hum"] = 2;` at 1947
  and `["test_bed"] = 2;` at 1944).
- `EDIT\out\agent\mp_iw8r_fix1\build\compile-log.txt`:
  - lines 27 to 29: "3 sound placement(s) in the map script", "2 sound zone(s)", "4 of the map's own sound(s) built into
    the map sound bank (sounds folder; in game NOT VERIFIED)".
  - line 33: `preflight warning sound: sound alias 'test_hum' used by entity 10 ... is not in the always-loaded sound
    catalog; it may be silent`.
  - The log has no "iw3 sound" lines. Those come from the converter's own log, which is not in these folders.
- `C:\...\x64zt_output\.iw3-scripts\mp_iw8r_fix1\mp_iw8r_fix1.gsc-tool.log` has 2 lines and no sound line.
- The converter's zonetool log for this map was not found. The SAB (sound bank) was not dumped, so the contents of
  the built bank were not checked.

---

## 1. The path of a script-played converted loop (`test_hum`)

### 1a. Editor side

| Step | Where | What it does |
|---|---|---|
| Read emitters | `EDIT\src\core\map_script.cpp:269-291` `readSoundPlacements` | Takes `fx_origin` entities with `soundalias` (loop unless `sound_loop` is 0, lines 287-290) and `sound_emitter` entities. Loop flag from the `loop` key (line 291). Alias name is lower-cased (line 285). |
| Script helper text | `EDIT\src\core\map_script.h:30-35` | `SoundPlacement { alias, origin, loop, delay }`. The comment says a stock alias is played from a script. |
| Decide custom or stock | `EDIT\src\core\compile_iw8.cpp:2532` | `useCustomSound(p.alias, p.loop, false)`: `ambient2d` is false for script emitters. A custom alias (a wav in the sound roots) is not stock. |
| Custom alias build | `EDIT\src\core\compile_iw8.cpp:2517-2528` | `CustomSoundAlias` gets `looping = true` (from the loop flag) and `ambient2d = false`. The wav is copied to `extracted\sound\iw8r\test_hum.wav`. |
| Sidecar settings | `EDIT\src\core\compile_iw8.cpp:2568-2574`, `EDIT\src\core\sound_settings.cpp:111-117` | If `<stem>.iw8snd.json` exists next to the source wav, it can set `volume`, `dist_min`, `dist_max`, `loop`, and `two_d` (key `two_d`, `sound_settings.cpp:59`). Whether such a sidecar exists for `test_hum` is not checked. |
| Alias JSON | `EDIT\src\core\compile_iw8.cpp:2576` and `EDIT\src\core\custom_sounds.cpp:38-49` | Writes `extracted\soundaliases\test_hum.iw3.json`. Line 40: `channel = ambient2d ? 24 : 1`. Line 41: `flags = loop | (channel << 8)`, so `flags = 1 \| (1 << 8) = 257`. Volume 1 (line 42), distance 120 to 1000 (defaults in `custom_sounds.h`, struct lines 21 to 28). |
| Stock list | `EDIT\src\core\compile_iw8.cpp:2537-2538`, `EDIT\src\core\map_script.cpp:299-304` | Only for aliases that are not custom. Not written in this build (no `stock_sounds.txt` in `OUT\sound\`). |
| Script | `GSC` lines 12, 404 to 417 | `place_sounds` runs as a thread (line 12). It calls the loop helper three times with `"test_hum"`. The helper spawns `script_origin` at the origin (415) and calls `ent iw3compat_playloopsound(alias)` (416). |

### 1b. Converter side (X64, read only)

| Step | Where | What it does |
|---|---|---|
| Alias files read | `X64\src\convert\iw3_iw8\iw3_sound.cpp:695-720` `ReadZoneAliases` | Reads `extracted\soundaliases\*.iw3.json`. `test_hum.iw3.json` is read here. |
| Requested by name | `X64\src\convert\iw3_iw8\iw3_sound.cpp:1084-1100` `ReadScriptStringLiterals`, used at `X64\src\convert\iw3_iw8\iw3_fastfile.cpp:12455` | Every quoted word in `extracted\maps\mp\*.gsc` is a candidate. `OUT\maps\mp\mp_iw8r_fix1.gsc` lines 454 to 456 contain `iw8r_loop_sound("test_hum", ...)`, so `test_hum` is a candidate. |
| Candidate accepted | `iw3_sound.cpp:1744-1752` | A candidate is added to `pending` only if `definition(name)` finds the alias file. `test_hum` has one. |
| Literal `requestedSounds` | `iw3_fastfile.cpp:12430-12459` | The direct regex `ReadScriptSoundNames` (`iw3_sound.cpp:1058-1080`) does not match `test_hum`, because the script passes the name through a helper, not `playloopsound("...")`. The literal path above is what picks it up. |
| Role | `iw3_sound.cpp:1000-1010` `RoleFor` | `if (looping) return Role::loopEmitter;` (lines 1002-1003). |
| Role template | `iw3_sound.cpp:970-979` struct, `iw3_sound.cpp:981-997` `TemplateFor` | `loopEmitter = {0x2A003, 75, "default", 1.0, 1.0, 0, 0, 0, 0, 0.18}` (line 985). Fields: flags, volMod index, curve, reverb, far reverb, spread min, spread max, spread mid, spread angle, gain. Struct comment at line 972: "bit19 = precached (G_CStringMP_SoundAliasIndex), bits 10..17 channel". |
| Base flags | `iw3_sound.cpp:1914`, `1919` | `role = TemplateFor(RoleFor(name, looping))`; `alias.flags = role.flags = 0x2A003`. |
| Channel override | `iw3_sound.cpp:1925-1930` | `kIw3Channel2d[1]` is false (index 1 is "auto", 3D), so the template channel stays. |
| Volume group and gain | `iw3_sound.cpp:1931-1933` | `volModIndex = 75` (volmod name "na", `replay_sound.cpp:582`, array at `replay_sound.cpp:571`). `volMin = volMax = 1.0 * 0.18 = 0.18`. |
| Pitch, distance, probability, start delay | `iw3_sound.cpp:1934-1944` | pitch 1 to 1; distance 120 to 1000 from the JSON; probability 1; start delay 0. |
| Falloff curve | `iw3_sound.cpp:1946-1960` | No curve in the JSON, so the "default" curve (line 1959). |
| Reverb and spread | `iw3_sound.cpp:1961-1966` | Reverb 1.0 and far reverb 1.0. Spread min, max, mid and angle are 0 (template line 985). |
| Sample | `iw3_sound.cpp:1812-1822` | `sample.name = "mw120r\\mp_iw8r_fix1\\test_hum"` (prefix plus stem), `sample.looping = looping` (line 1819). |
| Alias recorded | `iw3_sound.cpp:2030` | `result.aliases["test_hum"] = true`. This is what the converter later writes into `iw3compat_sounds()` as `2` (loop). |
| Bank | `iw3_sound.cpp:1845-1851` | Bank name `mp_iw8r_fix1.all`, zone `vehicle_engine_type4_npc`, stream key `mp_iw8r_fix1_all_loaded`. |
| Bank writer | `X64\src\games\iw8\assets\replay_sound.cpp:894` `RegisterBank`, `replay_sound.cpp:915` `root[60] = 2`, `replay_sound.cpp:976` `header[504] = 1` (always loaded), `replay_sound.cpp:1024` `Store(head, 112, alias.flags)` | The alias is written at offset 112 of its 232-byte record with the flags above. |
| Flag layout | `X64\src\games\iw8\assets\replay_sound.h:38` | "bit0 loop, bits 7..9 load type, bits 10..17 channel". The bit 19 meaning is only in the converter comment (`iw3_sound.cpp:972`). No code in `X64\src\games\iw8` or `X64\src\convert\iw3_iw8` reads bit 19 as a precache flag (grep for "precach" finds only a token table). |
| Entity channel name | `X64\src\games\iw8\assets\replay_sound.cpp:586` `kEntChannels` | Index 0 is `scn_fx_unres_3d`. Index 125 is `amb_bed_2d` (the converter comment at `iw3_sound.cpp:1923-1924` names the same entry). Index 168 is `special_lo_unres_1_3d`. |

Resulting `test_hum` alias flags: `0x2A003` = 172035. Bit 0 (loop) set. Bits 10 to 17 = 168 (channel
`special_lo_unres_1_3d`, 3D). Bit 19 (precache) clear. Load type (bits 7 to 9) 0.

### 1c. Runtime route (script side)

`GSC` 1947 sets `level.mw120r_sounds["test_hum"] = 2`. The loop helper (415-416) calls
`iw3compat_playloopsound("test_hum")`, which calls `iw3compat_sound_kind` (611-617, returns 2) and then
`iw3compat_sound_name` (622-627, returns the name because `level.mw120r_sound_alias` is empty). It then calls
`self playloopsound("test_hum")` (638) on the spawned `script_origin`. Nothing in the editor or converter checks that
the name resolves at runtime. Resolution is done by the engine, which is not in these folders.

---

## 2. The ambient bed (`test_bed`), side by side with `test_hum`

### 2a. Route of the bed

| Step | Where | What it does |
|---|---|---|
| Ambient zone | `OUT`'s map, `mp_iw8r_fix1.map` line 9 | worldspawn `iw8r_ambient "test_bed"`. |
| Custom alias | `EDIT\src\core\compile_iw8.cpp:2551` | `useCustomSound(audio.mapZone.ambient, true, true)`: `looping = true`, `ambient2d = true`. |
| Alias JSON | `custom_sounds.cpp:40-41` | `channel = 24`, `flags = 1 \| (24 << 8) = 6145`. Matches `OUT\soundaliases\test_bed.iw3.json`. |
| Sound table | `EDIT\src\core\audio_zones.cpp:322` and `soundTableJson` (`audio_zones.cpp:259-334`) | The zone row gets `"ambientStream": "test_bed"`, and the map zone gets an occlusion name "default" and filter name "default". Written to `OUT\sound\soundtable.json`. |
| Converter bed check | `iw3_sound.cpp:1852-1866` | If the zone's `ambientStream` has no converted audio, the bed is cleared (`alias.clear()`). Because the bed is heard, `test_bed` passed this check. |
| Converted flags | `iw3_sound.cpp:1925-1930` | `kIw3Channel2d[24]` is true (line 1925 array, index 24). Flags become `0x2A003 & ~(0xFF << 10) \| (125 << 10) = 0x1F403` (128003). |
| Played by | the zone and ambient tables (`soundtable.json`), not by script | The engine resolves the zone's `ambientStream` string. How the engine does this is not in these folders. |

Chirps (`test_chirp_a`, `test_chirp_b`) use the same route as the bed, through `iw8r_ambient_oneshots` (`useCustomSound`,
`compile_iw8.cpp:2554` and `2556`, `ambient2d = true`). They are one-shots, so the converter uses the explosion
template (`iw3_sound.cpp:983`, flags `0x92808`, bit 19 set) and the 2D one-shot channel 2 (`scn_fx_unres_2d`). Their
flags become `0x80808` (526344). Their curve is `standard_concave` and their gain is 1.0.

### 2b. Field-by-field comparison

| Field | `test_hum` (script loop, 3D) | `test_bed` (zone bed, 2D) | Differs? |
|---|---|---|---|
| IW3 source flags | 257 (`custom_sounds.cpp:41`) | 6145 | yes (channel) |
| IW3 channel | 1 "auto" (`custom_sounds.cpp:40`) | 24 "ambient" | yes |
| Who requests it in the editor | script emitter, `ambient2d = false` (`compile_iw8.cpp:2532`) | ambient zone, `ambient2d = true` (`compile_iw8.cpp:2551`) | yes |
| Converter role | loopEmitter (`iw3_sound.cpp:1002-1003`) | loopEmitter (same) | no |
| Base template flags | `0x2A003` (`iw3_sound.cpp:985`) | `0x2A003` | no |
| Entity channel (bits 10 to 17) | 168 `special_lo_unres_1_3d` (3D) | 125 `amb_bed_2d` (2D) | **yes** |
| Final flags | `0x2A003` = 172035 | `0x1F403` = 128003 | yes (only the channel bits) |
| Loop bit 0 | 1 | 1 | no |
| Load type bits 7 to 9 | 0 | 0 | no |
| Precache bit 19 (`0x80000`) | **clear** (template 985) | **clear** | no |
| Dry bit 3, shape bits 4 to 6 | 0 | 0 | no |
| Volume group (volMod index) | 75 "na" (`iw3_sound.cpp:985`) | 75 "na" | no |
| Volume min and max | 0.18 and 0.18 | 0.18 and 0.18 | no |
| Pitch min and max | 1 and 1 | 1 and 1 | no |
| Distance min and max | 120 and 1000 | 120 and 1000 | no |
| Probability, LFE, center, start delay | 1, 0, 0, 0 | 1, 0, 0, 0 | no |
| Falloff curve | "default" (no curve, `iw3_sound.cpp:1959`) | "default" | no |
| Reverb, far reverb | 1.0, 1.0 | 1.0, 1.0 | no |
| Stereo spread min, max, mid, angle | 0, 0, 0, 0 | 0, 0, 0, 0 | no |
| Sample length and format | 2.0 s, mono, 44100 Hz, 16 bit | 4.0 s, mono, 44100 Hz, 16 bit | length only |
| Bank | `mp_iw8r_fix1.all` (`iw3_sound.cpp:1845-1851`) | same bank | no |
| Route at runtime | script name, `playloopsound` on `script_origin` (`GSC` 416, 638) | zone `ambientStream` in `soundtable.json` | **yes** |
| Occlusion row for its channel | yes, "default" preset row for `special_lo_unres_1_3d` (`EDIT\src\core\audio_presets.inc:49`) and "interior" (line 136) | no occlusion row for `amb_bed_2d`; only a filter row in "interior" (line 182) | **yes** |
| Filter row for its channel | not found in the "default" filter table | `amb_bed_2d` is in "interior" filters only (line 182) | yes |

Conclusion of the comparison: the converted alias data is the same except for the channel bits (2D or 3D), and the
route (script name or zone table). Bit 19 is clear on both, so it cannot explain the difference between bed and
`test_hum` by itself. It may still matter if the script route needs the precache index and the zone route does not.

### 2c. Reference row, the chirps

| Field | Chirp (`test_chirp_a`) |
|---|---|
| Role | explosion (no keyword, `iw3_sound.cpp:1009`) |
| Flags | `0x80808` (bit 19 set, channel 2 `scn_fx_unres_2d`) |
| Volume group | 38 `explosion_grenade` |
| Gain | 1.0 |
| Curve | `standard_concave` |
| Route | ambient one-shot elements (`audio_zones.cpp:295-300`) |

---

## 3. How a stock alias differs (`bomb_tick`)

| Step | Where | What it does |
|---|---|---|
| Stock catalog | `EDIT\data\asset_catalog\iw8_common_sounds.tsv` line 125 | `bomb_tick 1 2.5e+06 common_mp`: loop 1, distMax 2.5e6, zone `common_mp` (always loaded). Header on line 2 says the columns are alias, loop, distMax, zones. No flags or channel. |
| Editor decision | `compile_iw8.cpp:2532` | No custom wav, so it goes to `stockSounds`. |
| Editor list | `compile_iw8.cpp:2537-2538` and `map_script.cpp:299-304` | Writes `extracted\sound\<map>.stock_sounds.txt` with `bomb_tick 1`. Not written in this build. |
| Converter read | `X64\src\convert\iw3_iw8\iw3_fastfile.cpp:12410-12427` | Reads that list. Note: the brief named `iw3_scripts.cpp`; `stockSounds` is not in that file. It is only in `iw3_fastfile.cpp`. |
| No bank alias | `iw3_fastfile.cpp:12451`, `12459` | The stock names are removed from `requestedSounds` and from `literals`. So no alias is converted and nothing is added to `mp_iw8r_fix1.all`. |
| Script table | `iw3_fastfile.cpp:12693-12696` | Adds `level.mw120r_sounds["<stock>"] = 1 or 2` in the generated `iw3compat_sounds()`. |
| Script path | `GSC` 416, 638 | Same code as `test_hum`. |

Difference: `bomb_tick` is Replay's own alias, resident in the always-loaded `common_mp` zone. It does not come from
the map bank, and no editor or converter field decides its flags, channel, or volume group. Those values are not in
this repo: "not found".

The owner's report that `bomb_tick` is heard cannot be checked here. The translated script in `GSC` has no
`bomb_tick` line, and the build has no stock list. So that test was run on another build or script: "not found".

---

## 4. Ranked hypotheses for why `test_hum` (script loop) is silent

Each hypothesis lists what the editor could change to test it. The editor only writes IW3 alias JSON (name, loop flag,
IW3 channel 1 or 24, volume, distance), the sound table, the reverb and occlusion zones, and the script. Bit 19 and
the converter template cannot be set from the editor. Changes in the converter are listed separately.

### H1 (most likely): the converted 3D loop template has no precache bit, and script plays by name

- Claim: `playloopsound("test_hum")` resolves the alias by name through a precache index (the converter comment says
  bit 19 is `G_CStringMP_SoundAliasIndex`, `iw3_sound.cpp:972`). The loop template `0x2A003` leaves bit 19 clear
  (`iw3_sound.cpp:985`). The other three templates set it (`0x92808`, `0x92400`, `0x92400` on lines 983, 984, 986).
- For:
  - It is the only converted route that is played by name from script. The bed and chirps go through the zone and
    ambient tables (`soundtable.json`), which refer to aliases by string or hash. The three working sounds are the
    three that do not need the script name lookup, and the one failing sound is the only one that does.
  - The chirp template (bit 19 set) and the bed template (bit 19 clear) differ in exactly this bit, and the bed works.
    So the bit is only decisive if the zone route does not use the same index.
  - Stock aliases played by script (`bomb_tick`, owner report) would most likely carry bit 19. This is unverified.
- Against:
  - The bed also has bit 19 clear and is heard. The explanation requires that the zone route does not use the index.
  - The meaning of bit 19 is only a comment in `iw3_sound.cpp:972`. No code in X64 reads it (section 1b, "Flag layout").
- Cheapest editor test, without changing the converter: make a one-shot copy of the wav and play it by script.
  - Create `test_hum_once.wav` (copy of `test_hum.wav`) and a sidecar `test_hum_once.iw8snd.json` with `{"loop": false}`.
  - Add a `sound_emitter` with `soundalias "test_hum_once"`, `loop "0"`, at the same origin as one of the loops.
  - Its alias uses the explosion template (bit 19 set, `iw3_sound.cpp:983`), and the script plays it by `playsound`
    (`GSC` 630-633).
  - If it is heard and `test_hum` is not, H1 is supported. The test is confounded: gain is 1.0 instead of 0.18, the
    volume group is 38 instead of 75, and the curve is `standard_concave`.
- Converter-side change (not editor output): set bit 19 in `loopEmitter` (`iw3_sound.cpp:985`, 0x2A003 becomes 0xAA003)
  and rebuild. This is the direct test of H1 with no confound.

### H2: the 3D channel (168 `special_lo_unres_1_3d`) for a script-spawned loop is silent or inaudible

- Claim: a spatial loop on a `script_origin` uses channel 168, while the bed uses 2D channel 125.
- For:
  - It is the only converted field that differs between the bed and `test_hum` (section 2b).
  - The 3D channel has occlusion rows in both presets (`audio_presets.inc:49` and `136`). The 2D channel does not.
- Against:
  - No code in the repo silences a 3D channel. The occlusion rows (`audio_presets.inc:49`) have attenuation values
    between 0.71 and 0.94, which are not silence.
  - Distance 120 to 1000 covers the map. The map bounds are -288 to 288 in x and y (compile-log "bounds" line), so the
    farthest point is about 800 units away.
- Cheapest editor test: set `two_d` in the sidecar of `test_hum` (`sound_settings.cpp:59`, key `two_d`).
  - `custom_sounds.cpp:40` then gives channel 24, and the converter gives flags `0x1F403`, the same as the bed except
    for the bit 19 state (still clear) and the route.
  - If `test_hum` becomes audible, H2 (or H3, see below) is the cause. If it stays silent, H2 and H3 are ruled out,
    and H1 or H4 remains.

### H3: occlusion or filter rows for channel 168 in the map-wide default preset

- Claim: the map-wide zone (always written, `audio_zones.cpp:307`, default preset because worldspawn has no
  `iw8r_audio_interior`, `.map` lines 8 to 10) applies an occlusion row to channel 168 to any emitter that is occluded.
- For: the row exists (`audio_presets.inc:49`, `136`); the 2D bed has no occlusion row.
- Against: the attenuation values are not silence. The emitters are not inside the `hall` trigger (section "Build
  facts checked"), so the interior preset is not used for them.
- Cheapest editor test: the same `two_d` test as H2. It removes channel 168 from the path and so tests H2 and H3
  together. A separate test would need a 3D channel that has no occlusion row. None was found in the tables.

### H4: the map bank does not hold `test_hum` (converter dropped it, or the sample did not load)

- For:
  - The bank is built from the alias files and the literal names. A failure in `sampleFor` (iw3_sound.cpp:1790-1830)
    drops an alias with "left silent" (`iw3_sound.cpp:1839`). No zonetool log was found to rule this out.
- Against:
  - The alias JSON exists and the literal path finds it (sections 1b and "Build facts checked").
  - `test_hum.wav` has the same format as `test_bed.wav` (mono, 44100 Hz, 16 bit).
  - The same bank holds the bed, which is heard, so the bank loads.
- Cheapest check, no editor change: run the converter with `IW8_ZONETOOL_SAB_DUMP=<file>` (`iw3_sound.cpp:2107-2112`)
  and look for `test_hum`, or read the converter log for `iw3 sound: test_hum <- ...` (`iw3_sound.cpp:1822`). Not run.

### H5: script call path, timing, or sound-kind lookup

- Claim: `place_sounds` runs before `iw3compat_sounds()` is ready, or the spawned `script_origin` is not in a state to
  play the loop.
- For: nothing in the files. `iw3compat_sound_kind` calls `iw3compat_sounds()` itself if the table is undefined
  (`GSC` 613-614), so the table is ready.
- Against: the same helper, call path and `playloopsound` call work for the stock loop that the owner reports as heard.
- Cheapest test: none in the editor. Add a delay before the first loop, or put the same loop on the `fx_origin` that
  already exists at `64 64 0`.

### H6 (not a cause, but a misleading signal): the preflight warning

- `EDIT\src\core\build_preflight.cpp:486` adds every `soundalias` key to the reference list. Lines 597-598 warn when an
  alias is not in `iw8_common_sounds.tsv` (loaded at line 413). The check does not know about custom sounds, so it
  flags `test_hum` (compile-log line 33) even though its wav and alias file are in the build.
- The warning is correct about one thing: `test_hum` is the only custom sound that is played by a `soundalias`
  key. The bed is not played by a key, so it is not checked.
- No test needed. The warning should not be read as the cause.

---

## 5. What could not be determined

- Whether the runtime resolves `playloopsound("<name>")` through a precache index that needs bit 19. The converter
  comment says so (`iw3_sound.cpp:972`); no code in X64 or EDIT checks it. Engine behaviour is not in these folders.
- Whether the zone `ambientStream` route uses the same index. The engine code is not in these folders.
- The meaning of load type bits 7 to 9 and of the value 75 (volmod "na") at runtime. Names only.
- The flags, channel and volume group of the stock `bomb_tick` alias. The catalog has only loop, distance and zone
  (`iw8_common_sounds.tsv:125`).
- The contents of the built sound bank (SAB). No dump exists. Whether `test_hum` is in it: not verified.
- Whether a sidecar `test_hum.iw8snd.json` exists in the source sound folder. The source folder is in
  `opt.soundRoots` (`compile_iw8.cpp:2517-2521`), which is not in these folders.
- The converter's log for this map (the "iw3 sound" lines). Not found in the repo or the output folders.
- The owner's `bomb_tick` test and its build. Not in these files.
- Distances from the player to each emitter at play time, and the occlusion values as the engine applies them
  (units of the cutoff and attenuation levels, `audio_presets.inc:49`).
- Whether `GSC` `place_sounds` (`GSC` line 12) starts before the map's zones are ready. Not measured.

## 6. Converter change that would test H1 directly (outside the editor)

`X64\src\convert\iw3_iw8\iw3_sound.cpp:985`: give `loopEmitter` bit 19 (`0x2A003` to `0xAA003`), rebuild the map,
and see whether `test_hum` plays from script. The bed and chirps would change only if the zone route also uses bit 19.
This is a converter edit, so it belongs to the x64-zt owner, not to the editor.
