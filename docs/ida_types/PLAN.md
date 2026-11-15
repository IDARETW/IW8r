# IDA local types: review and repair plan (2026-11-15)

Database: the owner's IDA 9.5 session on `game_dx12_ship_replay.exe` (Replay 1.20). Reference: the game-test build in
`E:\1-game-test` (PDB + MAP of an older Xbox build of the same game). Replay is newer, so the game-test layout is a guide
for names and order. Replay's own code decides every offset and size. Tools built from this database (x64-zt headers,
the crossref plugin's SQLite, the IW8-Radiant source) are not references: they repeat its mistakes.

Files: [AGENT_PROCEDURE.md](AGENT_PROCEDURE.md) (what every agent does), [AGENTS.md](AGENTS.md) (the 100 agents),
[agents.json](agents.json) (machine-readable entries with hints), `tools/ida_types/gt_layout.py` (game-test lookups),
`tools/ida_types/ida_fill_pad.py` (the only edit helper agents use). Agent output goes to `out/ida_types/`.

## 1. Review

Numbers come from a read of every local type and a game-test PDB lookup for each struct name (`pdb-layout.exe`).

| | |
|---|---|
| Local types | 5,650 ordinals; 4,378 game structs and unions (ordinal 640 and up), 32 enums |
| Structs with `_pad` filler | 2,422; 56.7 MB of the 149 MB of struct bytes are padding (38%) |
| Same size as game-test | 3,021 (not proof of anything: see `gentity_s` below) |
| Different size from game-test | 204 |
| No game-test type with the same name | 1,142 (templates were renamed, e.g. `BgObjectHandle_GTurret` for `BgObjectHandle<GTurret>`, or Replay-only types) |
| Members at a different offset than game-test | 1,602, in 115 structs (`WeaponDef` alone has 568) |
| Game-test fields that fall inside Replay pads | 6,491 |
| Typed uses | about 891 game types appear in any typed function or data declaration (`scrContext_t` 1,872, `gentity_s` 289, `playerState_s` 170) |

Findings that shape the plan:

1. **Sizes were copied from game-test and never checked.** `gentity_s` is 1456 bytes (the game-test size), but Replay's
   `GetEntity` returns `g_entities + 1440 * n`: the Replay size is 1440. `scripted` is at +0x498 in Replay
   (`G_FreeEntity`, `G_StopAnimScripted`) and +0x4A0 in game-test, so 8 of the 16 bytes go before it.
2. **`cg_t` is probably 20 KB too big.** In game-test `CgGlobalsMP` inherits `cg_t` and its first own field
   `m_omnvarIndexIsNightMap` sits at `sizeof(cg_t) + 0x14`. In Replay that field is at +0x926B4, which implies a
   `cg_t` of about 0x926A0 (599,712 bytes). The local type is 620,656.
3. **Every struct is `pack(1)` and not fixed-layout.** Offsets are running sums of member sizes, so a wrong-width member
   or a child struct of the wrong size moves everything after it, with no warning. This is the likely cause of runs of
   members sitting at a constant distance from their game-test offsets (`WeaponDef`, `GfxWorld`, `playerState_s`).
4. **Workarounds hide the mismatches.** `gclient_s.ps` is `unsigned __int8[21412]` (the game-test `playerState_s` size)
   because the local `playerState_s` is 21,372.
5. **Game-test base classes are flattened.** 13 cases among the assigned structs (`CgGlobalsMP`/`SP` from `cg_t`,
   `actor_s` and `ai_agent_t` from `ai_scripted_t`, `visionSetBlend_t` from `visionSetVars_t`, the vehicle and
   navigator families). In Replay these are one big pad with a few fields. They are planned as "base first, own fields
   only".
6. **Strong Replay evidence exists and is cheap.** Every asset has a `Load_<Type>` function (stream size and every
   pointer member's offset; `Load_ScriptableDef` alone confirms the size 0x78, the part stride 104 and five members).
   The network delta code (`MSG_ReadDeltaPlayerstate`, `MSG_WriteDeltaEntityState`) walks net-field tables with the
   offset of every networked field of `playerState_s`, `entityState_t` and `clientState_t`. About 95,000 Replay
   functions are named, mostly with their game-test names, so a field used in a game-test function can usually be
   read in the same-named Replay one.

## 2. Priority

Each struct got a score from how much it is used (functions that mention it in the game-test decompile, typed uses in
Replay, how many structs embed it) times how wrong it looks (pad bytes, members off their game-test offset, size
mismatch, game-test fields in pads). Structs were then grouped with the child structs they embed by value, ordered by
area, and split by byte range where one agent could not cover them:

1. core game state: entities, player state, clients, script VM, client entities, `cg_t`, AI, server and snapshots;
2. map and assets: weapons, `GfxWorld` and its parts, `MapEnts`, terrain, models, materials, scriptables, sound,
   vehicles, transient zones;
3. renderer runtime: back-end data, view info, command buffers, scene constants, vision sets, particles;
4. game systems: `CgGlobalsMP`/`SP`, client statics, vehicles, navigation, missiles, Havok world, UI, memory;
5. online and meta (lowest): party, matchmaking, DW services, clans, friends.

Result: 100 agents covering 673 structs (688 entries, because 9 big structs are split between agents by byte range). Backlog: three Havok SDK
packages, and about 3,600 small or unused structs to rank again after wave 10.

## 3. Waves

Ten waves of ten Haiku agents, in roster order. All agents share IDA's main thread, so ten at a time is the ceiling;
IDA will be slow for the owner while a wave runs.

| Wave | Agents | Area | Lead structs |
|---|---|---|---|
| 1 | A001-A010 | core game state | `gentity_s`, `BgEntityComponents`, `missile_ent_t`, `playerState_s`, `PlayerASM_State`, `PlayerActiveWeaponState`, `gclient_s`, `clientSession_t`, `viewClampState` |
| 2 | A011-A020 | core game state, map and assets | `usercmd_s`/`clientState_t`, `ViewMovementState`, `scrContext_t`, `scrVarPub_t`/`scrVmPub_t`/`dvar_t`, `centity_t`, `WeaponCompleteDef`, `WeaponDef` |
| 3 | A021-A030 | map and assets | `WeaponAttachment`, `GfxWorld`, `GfxWorldSurfaces`, `StreamTree`, `GfxLightGridVolumeAtlas`, `GfxSunShadow`, `MapEnts`, `StDiskTerrainSurface`/`XModel`, `Material`, `ScriptableStateDef` |
| 4 | A031-A040 | map and assets, core | `DynEntityDef`, `SndAlias`, `VehicleDef`, `TransientInfo`/`XAssetHeader`, `cg_t`, `ViewMotionSpring` |
| 5 | A041-A050 | core game state | `characterInfo_t`, `lerpFrame_t`/`pmove_t`, `pml_t`/`trace_t`, `DObj`/`XAnimTree`, `sentient_s`/`actor_s`, `ai_scripted_t`, `msg_t`/`SvClientMP`, `SvClientSP`, `SvGameGlobals` |
| 6 | A051-A060 | core, renderer | `CgSnapshotMP`, `CgSnapshotSP`/`pathnode_t`, `bot_data_t`, `bot_info_t`, `GfxBackEndData`, `GfxViewInfo`, `GfxSceneDef` |
| 7 | A061-A070 | renderer | `GfxBRCircle`, `GfxStageInfo`, `GfxFrustumGrid`, `refdef_t`, `GfxCmdBufState`, `GfxCodeSceneConsts` |
| 8 | A071-A080 | renderer, game systems | `GfxMeshData`, `visionSetVars_t`, `ParticleManager`, `ParticleState`, `CgGlobalsMP`/`SP`, `ClActiveClientMP`, `ClActiveClientSP`, `CgCompassSystemMP`, `BgVehiclePhysics` |
| 9 | A081-A090 | game systems | `BgVehiclePhysicsGround`, `Vehicle`, `BGVehicleInfo`/`GVehicles`, `AINavigator`, `GMissile`/`GTurret`, `hknpWorld`, `hknpShape`/`Ragdoll`, `LUIElement`/`MemoryFile` |
| 10 | A091-A100 | game systems, online | `HunkUser`/`TaskManager`, `PartyData`, `OnlineMatchmakerOmniscient`, `DWWrappers`, `DWServicesAccess`, `OnlineClan`, `Online_Friends`, `OnlineChallengesManager`, `InviteJoinHSM` |

### Wave 0 (coordinator, before any agent)

1. Back up every local type as C: `backup_all(r"E:\IW8-Radiant\out\ida_types\backup\local_types_<date>.h")`.
2. Ask the owner to save the IDA database (and keep a copy of the .i64 if they want a full restore point).
3. Test `fill_pad(..., apply=True)` once on a scratch struct, then delete the scratch struct. The dry-run path and its
   refusals were tested on `gentity_s` on 2026-11-15; the write path was not.
4. Check that the owner's crossref plugin keeps a type edit made through the bridge (it syncs types to its SQLite).
5. Create `out/ida_types/results`, `out/ida_types/before`, `out/ida_types/backup`.

### Between waves (coordinator, the main session)

1. Read every report and re-check two evidence rows per agent in IDA. Haiku reports can be wrong while sounding sure.
2. Apply the CONFIRMED proposals in dependency order: child structs first, then parents (adjusting the pad after a
   resized child so the parent's later offsets stay put), base classes before derived structs.
3. Run the diff again and regenerate `agents.json` for the remaining waves: a size change moves the byte ranges of
   every later agent that shares that struct or embeds it.
4. Back up the local types again.

### Launching an agent

Agent tool, `model: "haiku"`, ten per message, prompt:

> You are agent AXXX of the IDA local-type repair. Read `E:\IW8-Radiant\docs\ida_types\AGENT_PROCEDURE.md` fully,
> then your entry (id AXXX) in `E:\IW8-Radiant\docs\ida_types\agents.json`, and follow the procedure. Edit only your
> structs and ranges. Report in `E:\IW8-Radiant\out\ida_types\results\AXXX.md`.

## 4. Risks

- **Haiku accuracy.** Agents may apply a wrong field. Limits: they can only fill pads (no size change, no moves) and
  every edit is one IDA undo step; the coordinator re-checks evidence after each wave and the C backup restores any
  type.
- **IDA load.** Ten agents decompiling at once makes IDA slow. Long scripts can stop the owner's
  `replay_crossref_sync` timer; agents may only run the short helper calls.
- **Shared structs.** Agents that share a struct by range edit it at the same time. Each `fill_pad` call reads and
  writes the struct in one step on IDA's main thread and only inside the caller's range, so edits do not overwrite
  each other.
- **Cost.** Each agent reads many decompiles; expect a few hundred thousand tokens per agent, tens of millions for all
  100.
- **Coverage.** 100 agents cover 673 of 4,378 structs, chosen by importance. The rest needs another round.
