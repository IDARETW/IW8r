# 47. Quick test match, live atmosphere and wider hot update (roadmap 7.12, 7.9, 7.7)

Date: 2026-11-10. Scope: read-only research for three open roadmap items, plus an implementation plan. Nothing was started, attached to, injected or written outside this file. No IDA database was opened. No repo was edited, built or committed.

Legend: **VERIFIED** = read in source, data or a doc with file:line. **INFERRED** = derived from names or structure, not run. **UNVERIFIED** = needs an owner test in Replay 1.20 on a scratch match. **NOT FOUND** = searched for and absent.

Shorthands:
- `[LD]` = `src/core/live_direct.cpp/.h` (direct memory link). `[LL]` = `src/core/live_link.cpp/.h` (control file client). `[LP]` = `src/qtapp/live_link_panel.cpp`.
- `[MS]` = `src/core/map_script.cpp/.h`.
- `[PX]` = `E:\mw124\mw120rproxy\mw120rproxy\` (proxy source).
- `[XZ]` = `E:\mw124\x64-zt\` (fork).
- `[DV]` = `E:\mw124\Dvars.txt` (6747-line dictionary, `TOKEN (readable) - description`).
- `[BI]` = `data/generated/iw8_gsc_builtins.txt`.

---

## 0. Summary

| Item | Answer | Confidence |
|---|---|---|
| 7.12 start a private match with bots | No proven single command. The usable levers are: select the package (proxy path, VERIFIED), set the bot and gametype dvars (names VERIFIED, effect UNVERIFIED), then load or start (`map <id>` UNVERIFIED for custom packages; Start Match is manual). No "xpartygo" or start command exists in any source searched (NOT FOUND). | Partial |
| 7.9 live sun, fog, vision | Feasible through the existing dvar channel plus a new GSC thread. Sun has IW8 builtins (`setsuncolorandintensity`, `setsundirection`, `setsunflareposition`). Fog and vision are dvar-named fields (same tokens in `[DV]` and in Replay's vision table). `setExpFog` and `setsunlight` are NOT IW8 builtins. Whether a dvar change is applied live is UNVERIFIED. Save-back from the game needs a proxy read action (no engine poking). | Design verified, behaviour UNVERIFIED |
| 7.7 hot update beyond placements | Lights, effects, sounds: possible with GSC builtins that exist in `[BI]`, but each needs an entity handle the editor can address by targetname. Spawns: not hot (read at match start, INFERRED). Key changes: compile-time only. Proposed message format in section 6. | Mixed |

The one fact that gates all three items: **whether Replay accepts readable dvar names in console text and in GSC `getdvar`/`setdvar`**. The current hot update (7.4) already depends on it (`set iw8r_hot "..."`, `getdvar("iw8r_hot")`). Roadmap 7.4 still says "in-game confirmation pending". Section 5.1 is the first test to run.

---

## 1. How the live link works today (VERIFIED)

### 1.1 Attach and memory channel

- `DirectLink::attach` opens the process with VM read/write, lists its main module, reads the PE header, and if the identity at +0x40 is `RPLYCMP2` reads the `.rplmap` section to map native RVAs (`[LD]`, `attach`). A stock exe uses native RVAs as they are.
- Native RVAs used: `kRvaGEntities = 0xBC20F00` (`g_entities`), `kRvaCbufMain = 0xD216D70` (`s_cbufMainBuffer`). Both match `docs/research/05_runtime_evidence_1_20.md` §4 and §6.
- `DirectLink::sendCommand` writes the text plus `\n` straight into the native command buffer, only when the buffer size is 0 (`[LD]`, `sendCommand`). It refuses while the game is busy. It does not run the proxy's `commandtext::Translate` and does not apply any allowlist or deny list. The proxy's translation happens in its `ConsoleText` hook on `Cbuf_AddText`, not on this write path (`docs/research/05` §4 last paragraph).
- `DirectLink::readPlayer` reads the player origin and angles (client+0x30, +0x1CC). `setPlayerOrigin` writes the origin only.

### 1.2 Who calls what in the panel

| Editor action | Path | Text sent | Evidence |
|---|---|---|---|
| Load Map in Game (Ctrl+F8, 7.2) | `LiveLinkPanel::loadMapInGame` then `direct_.sendCommand` | `map <id>` from `live/loadMapCommand`, default `map %1` (`loadMapCommand`) | `[LP]` ~483-523; `main_window.cpp:2062-2064` |
| Hot update (7.4) | `LiveLinkPanel::sendHotUpdate` | `set iw8r_hot "<seq> <targetname> x y z pitch yaw roll"` | `[LP]` ~494-503; `[MS]` `hotUpdateValue` line 50 |
| Free console box | `LiveLinkPanel::onDirectCommand` | any text, no check | `[LP]` ~525-534 |
| Control-file path (7.1 proxy channel) | `[LL]` `checkConsoleCommand` | allowlist of one verb `set <dvar> <value>`, deny list (map, devmap, map_restart, fast_restart, map_refresh, exec, bind, cmd, ...) | `[LL]` `denyTokens`, `checkConsoleCommand` |

Gap: the direct path has **no allowlist**. The deny list in `[LL]` applies only to the control file. The free console box and the hot and load commands go straight to the engine buffer. Section 7 proposes one gate for both paths.

### 1.3 Hot update protocol (7.4, VERIFIED in source, UNVERIFIED in game)

- Message: `dvar iw8r_hot = "seq targetname x y z pitch yaw roll"`. `hotUpdateValue` builds it with a sequence number so that a repeated move is still a new value (`[MS]` line 50).
- GSC side, emitted by `mapScriptGsc` when `extras.hotUpdate` is true (default) (`[MS]` lines ~112-126):
  - `level thread iw8r_hot_update();` in `main()`.
  - `iw8r_hot_update` polls `getdvar("iw8r_hot")` every 0.1 s, skips empty or unchanged values, splits with `strtok`, and sets `ent.origin` and `ent.angles` on every entity from `getentarray(targetname, "targetname")`.
- Editor side: the checkbox "Hot update" (`[LP]` ~159-163, tooltip says it needs a build made by this editor). `main_window.cpp:588-602` calls `sendHotUpdate` for moved script entities with a targetname.
- Test: `qt_gui_test.cpp:1827-1838` checks the command string only, not the game effect.
- Known limits of this protocol: one dvar slot, last value wins; two sends within 0.1 s can drop the first. `getentarray` plus `ent.origin` is correct for script_model and script_origin only; collision of solid script models after a move is UNVERIFIED.

### 1.4 Map load path (VERIFIED source, UNVERIFIED in game)

- The proxy switches stock packages by setting `ui_mapname` and `ui_gametype` through the buffer and letting the stock lobby load them (`docs/research/05` §11 item 5; `docs/research/19` §2.5). It does not call a devmap function.
- Custom zones are routed only for the **selected** package: `OpenSelected` calls `custommaps::ResolveDiskRead` for the active id (`docs/research/05` §11 item 3). So `map <id>` without a prior select probably loads the stock or no zone. Sequence matters: select first, then load.
- The engine has `map`, `devmap`, `map_restart`, `fast_restart`, `map_refresh` as commands. Evidence: the deny lists in `[LL]` and `[PX]` name them, and the DW log names `map_restart` (`docs/research/25` §2.4). The registration site is not identified (`docs/research/05` §13 gap 2).
- `+devmap`, `+set` startup syntax: UNKNOWN (`docs/research/19` §2.7).

---

## 2. Question 1: console commands and dvars for a private match with bots

### 2.1 Candidate table

Tokens come from `[DV]` (`TOKEN (readable)`) and from the proxy's `dvar_names.h` where it has the row. Readable names are the engine's own dictionary names.

| Purpose | Command or dvar (token) | Evidence | Status |
|---|---|---|---|
| Choose the custom package | `set NSQLTTMRMP <id>` (`ui_mapname`), via control file `select` in the lobby | `[PX]developer_ui.cpp:142-186` as cited in `docs/research/19` §2.5; `[LL]` `Action::Select` | VERIFIED (proxy path). Readback confirmed by `ui_mapname` |
| Gametype for the package | `set MOLPOSLOMO war` (`ui_gametype`) | `docs/research/19` §2.5 step 3, `docs/research/05` §5 (`dvar_names.h:2481`). Proxy comment says `war` is used, `tdm` is not | VERIFIED that the proxy writes `war`. Meaning of `war` as a mode (team deathmatch?) INFERRED, not in repo |
| Other gametype names | `g_gametype` (`NKTMKRMSKR`, `[DV]` line 2287, "for use by script"), `default_gametype_mp` (`ORMSMKOKL`, line 1778) | `[DV]`; `docs/research/32` (`dom`, `sd`, `tdm` classnames and script strings) | Names exist. Full list of gametype names for IW8 MP: NOT FOUND. Only `war` is evidenced for ui_gametype |
| Bots fill the local game before launch | `mp_bots_sec_before_lobby_autofill` (`NLLMRMSTLT`, `[DV]` line 3095): "Time in seconds before adding MP bots to the local game in order to launch" | `[DV]` | VERIFIED text; implies the lobby auto-adds bots after a delay. Lever: set it low. UNVERIFIED effect |
| Bot count | `bot_MaxNumEnemyBots` (`RNRLKSNNR`, `[DV]` 535), `bot_MaxNumAllyBots` (`SNMLPONRP`, 534) | `[DV]` | Names VERIFIED. Effect UNVERIFIED |
| Bot difficulty | `bot_DifficultyDefault` (`MOLTMTTQON`, 513) | `[DV]` | UNVERIFIED |
| Bot mode | `bot_SystemStatus` (`OMSOSSSORO`, 563): "Default mode that bots will connect and participate in the match" | `[DV]` | UNVERIFIED |
| Bot spawn control | `bot_spawnControlledByDvar` (`MSLNRKRRKK`, 564) | `[DV]` | Name only; purpose INFERRED from name. UNVERIFIED |
| Private match gate (frontend) | `lui_mp_private_match_enabled` (`LQKTNLONLP`, 2829) | `[DV]` | Menu gate. Setting it from the editor: UNVERIFIED and probably not needed for local play |
| Private match password | `privateMatch_serverPassword` (`LRSTPRQOKQ`), `privateMatch_joinPassword` (`MQSMMPSKQT`) | `[DV]`, `dvar_names.h` | Do not set. Listed so nobody uses them by accident |
| Private match invite flag | `online_mp_private_matches_are_invite_only` (`MNLKKQRTKK`, 3363) | `[DV]` | Online only. Not needed for local bots |
| Player count display (read) | `party_lobbyPlayerCount` (`LNMMMOOLQQ`, 3645), `ui_maxclients` (`LOSORKOTOO`, 6403), `xblive_privatematch` (`LSTLQTSSRM`, 6732, read-only flag) | `[DV]`, `dvar_names.h` | Read-only candidates to verify the state |
| Bot GSC builtins | `addbot` (`[BI]` line 7), `addmpbottoteam` (11), `kick` (546), `isbot` (477), `isbotmatchmakingenabled` (478), `spawnbotortestclient` (method, 2805), `canspawnbotortestclient` (1292) | `[BI]` | Present in the IW8 table. Signatures UNVERIFIED. Callable only from script on the host |
| Gametype GSC | `getgametypeindex` (314), `setasgametypeobjective` (2390), `getgametypevip` (1650) | `[BI]` | Present. Not needed for the quick path |
| Start the match | No command found. "xpartygo", "partygo", "startmatch", "StartPrivateMatch" searched in `E:\DW`, `E:\mw124` (x64-zt, mw120rproxy), `E:\IW8-Radiant` (data, docs, src). | Grep, 2026-11-10 | **NOT FOUND**. Start stays manual (`docs/research/19` §2.5 "Player still has to press Start Match") |
| Load the custom map from the console | `map <id>` (`[LP]` default), `devmap <id>` | `docs/research/05` §13; `[LL]` deny list | UNVERIFIED for custom packages. Route depends on the selected package (section 1.4) |

Canonical key token `addbots` = `3388` appears in `data/generated/iw8_canonical_keys.tsv` line 2081. Its meaning is not established; do not use it without a test.

### 2.2 Candidate sequence for a quick test match (for the owner to run)

1. Lobby (Local Play), proxy running: control `select <id>` (existing editor action). Confirm `status` shows `map == id` and `selected == id`.
2. Set bot dvars with the **token** form (the form the proxy itself writes for ui_mapname and ui_gametype, so the token form is the one with evidence):
   - `set RNRLKSNNR 4` (bot_MaxNumEnemyBots), `set SNMLPONRP 0` (bot_MaxNumAllyBots), `set NLLMRMSTLT 3` (mp_bots_sec_before_lobby_autofill).
   - Readable equivalents are also candidates. The direct path does not translate (section 1.1), so the token form is the one to try first.
3. Start: either the manual Start Match button (proven), or `map <id>` via the direct console (UNVERIFIED for customs).
4. Log markers to check (from `docs/research/19` §1): bot join lines in the Replay log and the match start line. The log format for bots is UNKNOWN.

### 2.3 What is missing for true one-click quick match

- A start action. The lobby Start Match is a Lua or UI action. Finding its command path needs reverse engineering of the lobby (not done here). Until then the feature is "select, set bots, owner presses Start" (size S for the editor, M if the start command is found).
- A gametype list. Only `war` is evidenced for `ui_gametype`. The editor should offer `war` only, plus a free-text box that is off by default.

---

## 3. Question 2: sun, fog and vision at runtime

### 3.1 GSC builtins: present or absent in IW8 (VERIFIED against `[BI]` and gsc-tool)

| Call | In `[BI]` | In gsc-tool IW8 table | Use for 7.9 |
|---|---|---|---|
| `setsuncolorandintensity` | yes (861, f) | yes (`iw8_func.cpp`, 0x1E4) | Sun colour and intensity |
| `setsundirection` | yes (862, f) | yes (0x1E2) | Sun direction |
| `setsunflareposition` | yes (863, f) | not checked | Flare position |
| `resetsundirection`, `resetsunlight` | yes (786-787, f) | resetsundirection yes (0x1B4); resetsunlight not checked | Reset to the map's sun |
| `getmapsunlight`, `getmapsundirection`, `getmapsunangles`, `getmapsuncolorandintensity`, `getmapsuncolorraw` | yes (333-337, f) | not checked | Read the map's sun (for the sun panel initial value) |
| `visionsetnaked` | yes (1023, f) | yes (`iw8_func.cpp` 0x258) | Switch vision set by name (needs a vision file in the package) |
| `visionsetnight`, `visionsetpain`, `visionsetthermal`, `visionsetalternate`, `visionsetfadetoblack`, `visionsetmissilecam`, `visionsetkillcamthirdpersonnight` | yes (1019-1026, f) | not checked | Other vision sets |
| `setvisionparams` | yes (2741, **m**) | yes (`iw8_meth.cpp` 0x838D) | Method on a player or entity. Argument shape UNVERIFIED |
| `visionsetnakedforplayer` | not in `[BI]` | yes (`iw8_meth.cpp` 0x842D) | Per-player vision. Name only |
| `setExpFog` | **absent** | **absent** | Not a runtime call in IW8 |
| `setsunlight` | **absent** | absent | Not a runtime call in IW8 |
| `setfog`, `getfog` | absent | absent | No fog builtin in IW8 |

Note on the dropped list: `[XZ]` `src/convert/iw3_iw8/iw3_scripts.cpp`, `DroppedBuiltins()` (about lines 56-64) lists `setexpfog`, `setsunlight`, `resetsunlight`, `visionsetnaked`, `visionsetnight`, `setsaveddvar` as IW3 builtins that "must not run with IW3 data (fog/vision/ambience owned by the converter)". Those names are in the IW8 table but the converter drops them when it passes IW3 scripts through.

**Consequence for the current generator (VERIFIED read, impact INFERRED):** `[MS]` line 100 emits `setExpFog(start, halfway, r, g, b);` into the map script. `[XZ]` reads the same call at compile time as the fog source (`iw3_vision.cpp` `ReadMapExpFog`, regex on `setexpfog`, lines ~214-263; `docs/research/31` §3.1). At runtime the call is dropped or absent. So fog cannot be driven by calling `setExpFog` at runtime. Runtime fog must go through dvars (section 3.3). Owner check: confirm that the compiled `mp_*.gsc` has no unresolved `setExpFog` error.

### 3.2 Sun dvars (VERIFIED names in `[DV]` and `dvar_names.h`; behaviour UNVERIFIED)

| Dvar (token) | `[DV]` line | Note |
|---|---|---|
| `r_lightTweakEnable` (`NPRKOOLRPL`) | `docs/research/19` §5.3 | Gate for the light tweak set |
| `r_lightTweakSunColor` (`NQQQKTMONT`) | 4781 | Tweak sun colour |
| `r_lightTweakSunHeading` (`NNNMKNQNTK`) | 4782 | Tweak sun heading |
| `r_lightTweakSunLight` (`LMPKPQPRMK`) | 4783 | Tweak sun light amount |
| `r_lightTweakSunPitch` (`NTTSPKPRPQ`) | 4784 | Tweak sun pitch |
| `r_sun_from_dvars` (`NNTTKSKSKK`) | 5246 | "Set sun flare values from dvars rather than the level". Strongest hint that flare can be dvar-driven |
| `r_sun_fx_position` (`MQPTPTQSMN`) | 5247 | Sun effect position in degrees |
| `r_sunIntensityHeatOverride` (`NSSMQLPRNT`) | 5244 | Heat intensity override |
| `r_sunflare_max_alpha`, `r_sunflare_max_size`, `r_sunflare_*angle`, `r_sunblind_*` | 5250-5258 | Flare and blind tuning |
| `r_drawSun` (`LTROMKSMSS`) | 4479 | Show or hide the sun effect |
| `sm_sunEnable` (`MQRQQONQSL`) | 5777 | "Enable sun shadow mapping from script" |
| `r_sunShadowParams` (`LRLKLRNRTS`) | 5245 | Shadow cascade params |

Sun colour and direction in the built map are written from worldspawn (`docs/research/39` §1-2; `src/core/sun_keys.cpp`). The runtime route is the builtins in 3.1.

### 3.3 Fog and vision as dvars (VERIFIED: same tokens in three places)

- The vision parser matches **dvar-named tokens** from a 218-entry table at Replay's `0x14462A110` (`[XZ]` `src/convert/iw3_iw8/replay_vision_fields.inc`, header comments; `docs/research/09` §fog, indices 35-62, 217; `docs/research/31` §5). Example rows: `MSTOLQOTKN` = `fogDensity` (halfway distance, default 70000), `RPNOKKL` = `fogStartDist`, `NMLMMNQLSQ` = `fogColor`.
- The same tokens appear in `[DV]` as `fogDensity` (line 3370 in the readable form, token `MSTOLQOTKN`), `fogColor` (2097), `fogStartDist` (2118), `fogSunDir` (2123) and the rest of the fog block (2096-2124). So the vision fields are dvars.
- Consequence: 7.9 "vision values pushed as dvars" is the correct model. The question is whether a dvar change is applied **live**. The vision parser may copy the table into state only when a vision set loads, which would make a live `set` ineffective until the next `visionsetnaked`. UNVERIFIED.
- Fog ↔ sky: `fogSkyDistance`, `fogSkyIntensity` etc. are in the same block. `r_fog` (`LTOKRMRTMM`, `[DV]` 4586) disables fog.

### 3.4 Driving it from the editor through the hot channel (no engine poking)

The GSC side reads a dvar, as `iw8r_hot_update` does. The editor writes it with `set`. No memory writes are needed.

Stock dvars must be set from GSC with their **tokens**. Evidence: `[XZ]`/`[PX]` `iw3_compat.gsc` lines ~205-215, `iw3compat_setdvar` maps `compassMaxRange` to `setdvar("MKPRSSNNRO", value)`. The Replay team wrote the token, not the readable name, for a stock dvar. The GSC `#"name"` hashed literal (used in `mw124/atian-cod-tools/.../echo_test.gsc` lines 140-148 as `setdvar( #"ac_score", 0 )`) is another form; whether it equals the token is UNVERIFIED.

So the editor needs a readable-to-token table. `[DV]` already holds it (`TOKEN (readable) - text`, 6747 lines). Proposed: generate `data/generated/iw8_dvar_tokens.tsv` from `[DV]` (readable, token, description) so the editor and map_script do not depend on `E:\mw124`. This is the same pattern as `iw8_canonical_keys.tsv`.

### 3.5 Save back (7.9 "then saved back")

- The editor cannot read dvars today. `[PX]` `status` returns only `ui_mapname` and `selected`. `DvarString(token)` exists in the proxy (`developer_ui.cpp:72-77`) but is not exposed for arbitrary dvars.
- Direct memory reads of a dvar need `Dvar_FindVarByName` (`0x13E63A0`, NAMED-ONLY in `docs/research/05` §5). Using it would be engine poking. Not proposed.
- Route that needs no engine poking: a proxy action `dvar <token>` (read, like `status`), owner approval (section 7, Phase B). Until then, save-back is "the editor keeps the values the user chose and writes them to the map's `.iw8map` file". That is not a readback from the game.

---

## 4. Question 3: extending hot update to lights, effects, sounds, spawns, key changes

Each row: what GSC can change at runtime (IW8 builtin presence from `[BI]`), what the editor must address, and the verdict.

| Kind | Runtime GSC (in `[BI]`) | Handle needed | Verdict |
|---|---|---|---|
| Script entity placement | `getentarray`, `ent.origin`, `ent.angles` (in use since 7.4) | targetname | Works in design (7.4). UNVERIFIED in game |
| Effects (fx_origin) | `loadfx` (560), `playfx` (724), `spawnfx` (896), `spawnfxforclient` (897), `triggerfx` (992), `playloopedfx` (730), `stopfxontag` (925), `killfxontag` (547) | Keep the spawned fx entity per targetname in `level.iw8r_fxent[targetname]` (the current code keeps none, `[MS]` `iw8r_spawn_fx`) | Feasible: on a move message, end the old fx (delete the entity, notify the repeat thread), respawn at the new origin. Effect change (new `vfx/` name): needs `loadfx` in `main()`, so a new name is a rebuild. UNVERIFIED how to delete an fx entity (not in `[BI]` by an obvious name; `ent delete()` is likely). |
| Sounds (sound_emitter) | `playloopsound` (m, 2216), `stoploopsound` (m, 2844), `playsound` (m, 2221), `spawnloopingsound` (903), `spawnsound` (911), `playsoundatpos` (734) | The `script_origin` the current code spawns in `iw8r_loop_sound` and `iw8r_repeat_sound` (`[MS]`). Keep it per targetname | Feasible for move: `ent.origin = ...` on the script_origin. Alias change: `stoploopsound` then `playloopsound(newalias)`. Alias must exist in the always-loaded banks (`data/asset_catalog/iw8_common_sounds.tsv`). Loop following a moved origin: UNVERIFIED |
| Lights | `setlightcolor` (m, 2540), `setlightintensity` (2542), `setlightradius` (2543), `setlightfovrange` (2541), `setlightuvintensity` (2544) | An entity handle to a **dynamic** light. Compiled primary lights are not script entities (`docs/research/24` §1-3: lights are `ComPrimaryLight` in the GfxWorld, `entityId`) | **Blocked** for compiled lights until the compiler emits a script-addressable light entity (UNKNOWN whether Replay exposes them). Baked lighting cannot change at runtime (`docs/research/19` §5). Only a light the map spawns as an entity is a candidate. |
| Spawns | `placespawnpoint` (m, 2171), `enablespawnpoints` (197), `disablespawnpoints` (166), `registerscriptedspawnpoints` (774) | Spawn records are a `spawnList` (40-byte records, `docs/research/13` C4, `docs/ARCHITECTURE.md` §4) | **Not hot.** Spawns are read at match start (INFERRED from `spawnList` and the request). Enable or disable of script spawn points is possible but is not a placement move. Verdict: restart the match or rebuild |
| Key changes (gametype keys, `all_modes`, `dom`, worldspawn sun keys) | none at runtime | compile | **Rebuild only.** `docs/research/42` §2.3 and §5 |
| Bots and match rules | `addbot`, `kick`, `spawnbotortestclient` (host only) | none | Possible for a quick match (section 2). Not part of 7.7 |

Summary for 7.7: effects and sounds can be hot after a small GSC change. Lights are blocked by addressing. Spawns and keys are rebuild or restart.

---

## 5. Proof steps before any code (owner runs, scratch match, one map)

These are ordered. Each one gates the next.

- **5.1 Readable name acceptance (gates 7.4, 7.9, 7.7).** Send `set iw8r_hot "1 hot_box 16 32 48 0 0 0"` through the free console box to a map that has a `hot_box` script_model. Check that the entity moves. Then send the same with the token form for a stock dvar (`set MKPRSSNNRO 2500`, the compass range) and watch the minimap. If readable fails and token works for a stock dvar, the custom channel names must be chosen so they hash the same way (see 5.2).
- **5.2 Custom dvar hash.** Check whether `getdvar("iw8r_hot")` and `setdvar("iw8r_hot", ...)` from GSC see each other. Check `#"iw8r_hot"` too. Record which form works.
- **5.3 Live sun.** `setsuncolorandintensity` with one colour, then `resetsunlight`. Argument shape unknown: test a vector and a float. Record the working call in `docs/research`.
- **5.4 Live fog and vision by dvar.** `set` one fog token (for example `fogDensity` at a value half the current) and look at the fog change. If nothing changes, call `visionsetnaked` with the package's vision name (forces a reload) and test again. This tells whether dvars are live or only applied at vision load.
- **5.5 Quick match.** Section 2.2 steps 1-2 only. Record the bot count in the log after the owner presses Start.
- **5.6 `map <id>` after select.** One test, direct console: `map <id>` with a selected package. Record whether the zone loads.
- **5.7 Effect and sound moves.** After 5.1 passes, move one `fx_origin` and one `sound_emitter` (once the GSC change of section 6 exists).

---

## 6. Implementation plan

### 6.1 Message format (proposed)

One dvar per channel, so no channel can overwrite another and each can coalesce. Each value is `seq verb args...`, space separated, numbers printed with the same `num()` as `[MS]`, no quotes or semicolons inside. Channel dvars keep the `iw8r_` prefix so they are easy to find in the Dvar list.

| Dvar | Value example | Consumer (GSC) |
|---|---|---|
| `iw8r_hot` (exists) | `12 hot_box 16 32 48 0 0 0` | placements (7.4) |
| `iw8r_sun` | `13 color 0.90 0.80 0.70 2.5` or `13 dir -40 120 0` or `13 reset` | `setsuncolorandintensity`, `setsundirection`, `resetsunlight` |
| `iw8r_fog` | `14 start 0 halfway 1800 color 0.55 0.60 0.70` | tokens through `setdvar` (section 3.4) |
| `iw8r_vis` | `15 set <token> <value>` or `15 vision <name>` | `setdvar` by token, or `visionsetnaked` |
| `iw8r_fx` | `16 move <targetname> x y z` or `16 delay <targetname> <sec>` | fx registry (section 4) |
| `iw8r_snd` | `17 move <targetname> x y z` or `17 alias <targetname> <alias> <loop>` | sound registry (section 4) |

Rules: seq is an integer, never reused within a session. Maximum length 200 characters (keep under the engine's dvar string limit, UNVERIFIED value; `[LL]` uses 64 for its one-token form, which does not apply here). No `;`, no quotes, ASCII only, one message per channel per send.

### 6.2 GSC side (`src/core/map_script.cpp`, `mapScriptGsc`)

- Add `MapScriptExtras::liveChannels` (bool, default false until 5.1-5.4 pass). `hotUpdate` stays as it is.
- When `liveChannels` is on, emit in `main()`: `level thread iw8r_channel_sun(); level thread iw8r_channel_fog(); level thread iw8r_channel_vis();` and the fx and sound channels when the map has those placements.
- Each channel is the same polling shape as `iw8r_hot_update`. Sketch for sun:

```
iw8r_channel_sun()
{
    last = "";
    for (;;)
    {
        wait 0.1;
        v = getdvar("iw8r_sun");
        if (!isdefined(v) || v == "" || v == last)
            continue;
        last = v;
        t = strtok(v, " ");
        if (t.size < 3)
            continue;
        if (t[1] == "color")
            setsuncolorandintensity((float(t[2]), float(t[3]), float(t[4])), float(t[5]));
        else if (t[1] == "dir")
            setsundirection((float(t[2]), float(t[3]), float(t[4])));
        else if (t[1] == "reset")
            resetsunlight();
    }
}
```

  The call shapes are UNVERIFIED (section 5.3). Keep them in one place so a fix is a single edit.
- Fog and vision channels call `setdvar("<token>", value)` with tokens from the generated table (section 3.4). Generate the token constants into the script so the GSC has no readable names for stock dvars.
- Add the new function names to `src/core/script_check.cpp` line ~12 (the set of names the user's script must not redefine: `main`, `iw8r_place_fx`, `iw8r_place_sounds`, `iw8r_make_solid`, `iw8r_hot_update`).
- Keep `mapScriptGsc` output deterministic: the existing tests in `qt_gui_test.cpp` compare strings for placements, so add new tests, do not change old strings.

### 6.3 Editor side (`src/qtapp/live_link_panel.cpp/.h`, `src/core/live_direct.*`)

- Add `LiveLinkPanel::sendChannel(channel, payload)`: builds `set <channel> "<payload>"`, refuses a payload with `;`, quotes, control characters or more than 200 characters, then calls `direct_.sendCommand`. Coalesce: keep only the latest payload per channel, flush at most every 150 ms with a QTimer. This avoids the one-slot drop described in section 1.3.
- Add an allowlist gate for the direct path (section 7, item 3): only `set iw8r_*` channels, `map <id>` from the preference, and the user's free console text when the advanced box is on.
- Add a "Live atmosphere" group (dock in the Live Link panel): sun colour, intensity, direction; fog start, halfway, colour; vision name. Bind it to `Atmosphere`, `ExpFog` (`src/core/atmosphere.h`) and the sun keys (`src/core/sun_keys.h`). Each control sends its channel on change (coalesced). Off by default, like the hot update checkbox.
- No change to `live_direct.cpp` memory code. `[LD]` stays as it is.

### 6.4 Quick match (7.12) editor side

- A "Quick test match" group: package combo (from the deploy list, `checkMapId` already in `[LP]`), bot count, bot difficulty, mode combo with `war` only (free text off by default), "Select in game" (control `select`), "Apply bot settings" (token-form `set`), then "Start Match: press in game" with a status line. No automatic start until section 2.3 is solved.
- Logs: reuse `ReplayLogPanel` (named in `docs/research/25` §6.1) and show `[Maps]` and bot lines.

### 6.5 Phases

- **Phase A (no proxy change, owner test first):** section 5.1-5.6. Then 6.2 and 6.3 behind `liveChannels` (off by default). Then the token table generation (3.4).
- **Phase B (proxy change, owner approval):** a read action `dvar <token>` in the control file, so the editor can save back values (3.5). Command output in the result (`docs/research/25` §3.4).
- **Phase C:** fx and sound channels (section 4), after 5.7.
- **Phase D:** lights only if the compiler can emit script-addressable light entities. Separate design.
- Quick match start automation: separate, needs the lobby start command (not in the repo).

### 6.6 Files

- Change: `src/core/map_script.cpp`, `src/core/map_script.h`, `src/core/script_check.cpp`, `src/qtapp/live_link_panel.cpp`, `src/qtapp/live_link_panel.h`, `src/qtapp/main_window.cpp` (only if a new menu item is added), `src/qtapp/qt_gui_test.cpp` (new tests only).
- New: `data/generated/iw8_dvar_tokens.tsv` (generated from `E:\mw124\Dvars.txt`; the generator script goes in `tools/`, same as `build_gsc_builtins.py`).
- Do not change: `src/core/live_direct.cpp` memory code, `src/core/live_link.cpp` control-file rules.

### 6.7 Tests (no game)

- Pure functions: payload builder (format, length, forbidden characters), seq counter, coalescing (time injected), token table lookup, GSC string for the sun and fog channels (exact string checks, like the hot test at `qt_gui_test.cpp:1827-1838`).
- GUI: the atmosphere group sends the right channel on change (fake `sendChannel`).
- Compile: gsc-tool compile of a generated script with `liveChannels` on, to catch unknown function names (the names in section 3.1 are in the table, so compile should pass).
- Game: section 5 only, owner-run.

---

## 7. Risks and corrections

1. **Direct console has no gate.** `onDirectCommand` and `sendCommand` accept any text. The deny list is only in the control path (`[LL]`). Fix in 6.3 (item 3 of the plan).
2. **Readable names on the direct path are unproven.** Everything in 7.4, 7.7, 7.9 and the `map` load assumes them. Test 5.1 first.
3. **`setExpFog` in the generated script is a compile-time marker, not a runtime call** (section 3.1). Do not build runtime fog on it. Check that `[XZ]` compile accepts the generated call (owner check).
4. **Stock dvars need tokens in GSC.** `iw3_compat.gsc` shows it (section 3.4). A readable `setdvar("fogDensity", ...)` may silently do nothing.
5. **`ui_gametype` evidence is one value.** Only `war` is written by the proxy. Do not offer other modes without a test.
6. **Doc 25 §2.4 says the DW log has no nonce echo.** Keep that in mind for any new channel: the editor cannot confirm a set from the log. Use the game effect as the check.
7. **Quick match "bots" is a guess until 5.5.** `mp_bots_sec_before_lobby_autofill` is the best evidence (its description says the local game adds bots before launch), but the effect is not tested.

---

## 8. UNVERIFIED register

- U1. Readable dvar names accepted by the console and by GSC `getdvar`/`setdvar` on the proxy build (5.1, 5.2).
- U2. Effect of `set` on a bot dvar, by token or readable name (2.2).
- U3. Whether `map <id>` loads a selected custom package (1.4, 5.6).
- U4. Start Match command or button path (2.3). NOT FOUND in the repo.
- U5. Meaning of `war` as a gametype, and the full list of IW8 MP gametype names.
- U6. Argument shapes of `setsuncolorandintensity`, `setsundirection`, `setsunflareposition`, `setvisionparams`, `visionsetnaked` (IW8).
- U7. Whether fog and vision dvars apply live or only at vision load (5.4).
- U8. Whether `#"name"` and `"name"` hash to the same dvar in GSC.
- U9. Maximum dvar string length in IW8.
- U10. How an fx entity is deleted (for moves, 4).
- U11. Whether a moved script_origin keeps its loop sound.
- U12. Whether compiled lights are script-addressable (4, lights).
- U13. Whether spawn points can be moved or re-read without a match restart (4, spawns).
- U14. The hash that produces the readable-to-token pairs in `[DV]`. Tokens look like K..T digits of a checksum (INFERRED), not verified, so do not compute tokens; look them up.

## Files consulted

- Editor: `src/core/live_direct.h`, `src/core/live_direct.cpp`, `src/core/live_link.h`, `src/core/live_link.cpp`, `src/core/map_script.h`, `src/core/map_script.cpp`, `src/core/atmosphere.h`, `src/core/atmosphere.cpp` (grep), `src/core/sun_keys.h`, `src/core/script_check.cpp` (grep), `src/qtapp/live_link_panel.cpp`, `src/qtapp/live_link_panel.h` (grep), `src/qtapp/main_window.cpp` (grep 588-602, 2062-2110), `src/qtapp/qt_gui_test.cpp` (grep 1823-1838), `data/generated/iw8_gsc_builtins.txt`, `data/generated/iw8_canonical_keys.tsv` (grep), `docs/ROADMAP.md` (7.1-7.13).
- Research docs: `docs/research/25_live_link.md`, `19_replay_test_loop.md` (§2, §5), `05_runtime_evidence_1_20.md` (§4-6, §11-13), `31_atmosphere_route.md` (§3.1, §5), `09_iw8_render_features.md` (fog, vision), `24_lights.md` (grep), `39_sun_shadows.md` (grep), `13_iw8_level_maker_reference.md` (C4), `32_gamemode_objects.md` (grep), `42_stock_keys_killstreaks.md` (§2.3, §5).
- Fork and external: `E:\mw124\Dvars.txt` (lines cited above), `E:\mw124\mw120rproxy\mw120rproxy\dvar_names.h` (grep), `E:\mw124\mw120rproxy\iw8-zonetool\src\zonetool\iw3\resources\iw3_compat.gsc` (lines ~205-215), `E:\mw124\x64-zt\src\convert\iw3_iw8\iw3_scripts.cpp` (DroppedBuiltins), `E:\mw124\x64-zt\src\convert\iw3_iw8\iw3_vision.cpp` (grep), `E:\mw124\x64-zt\src\convert\iw3_iw8\replay_vision_fields.inc` (header and rows), `E:\mw124\x64-zt\deps\gsc-tool\src\gsc\engine\iw8_func.cpp` and `iw8_meth.cpp` (grep), `E:\mw124\atian-cod-tools\test\ff-linker\core_acts\scripts\core_common\acts\echo_test.gsc` (`#"..."` dvar usage).
- Not opened: any IDA database, the game binary, or `E:\IW8\Builds\1.20-replay`. No game was started or attached.
