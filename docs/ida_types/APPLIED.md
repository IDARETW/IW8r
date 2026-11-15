# IDA local-type repair: coordinator log

Changes the coordinator (main session) applied or checked, with the Replay evidence. Agents' own field edits are
listed in their reports under `out/ida_types/results/`. Backups: `out/ida_types/backup/`.

## Wave 1

### Applied by the coordinator

| Struct | Change | Evidence |
|---|---|---|
| `gentity_s` | size 1456 -> 1440: tail pad `_pad4A0` 272 -> 256 bytes, no member moved | `GetEntity` 0x14125ACA0 returned `g_entities + 1440 * n`; `G_FreeEntity` 0x1410342A0 compares `entity - g_entityArrayBase >= 1440`. Members up to `scripted` +0x498 already matched Replay (`G_FreeEntity`, `G_StopAnimScripted`). After: `GetEntity` reads `&g_entities[entityRef]`. |

| `SkydivePlayerState` | `state` unsigned int -> unsigned __int8 + `_pad1[3]` (no move) | `BG_Skydive_UpdateFlightState` 0x140CE3CF0: `movzx edx, byte ptr [rbx+2F28h]`, `cmp byte ptr [rax+2F28h], 2` (struct at ps+0x2F28), five byte accesses (A007) |
| `MountSurfaceAbbreviatedProperties` | `flags` unsigned int -> unsigned __int8 + `_pad2D[3]` (no move) | `BG_ContextMount_ResolveMountEdge` 0x140CA6090: `movzx eax, byte ptr [r8+2Ch]` / `mov [rcx+2Ch], al` (A007; game-test agrees) |

| `Weapon` | relayout at size 60: `stickerIndices` +0x08 -> +0x02, `weaponClientLoadout` +0x10 -> +0x0A, new `weaponOthers` +0x0C, new `weaponAttachments[13]` +0x0E (the game-test layout) | A003 raw reads in `MSG_WriteWeapon` 0x1412BAA90 and `BG_GetWeaponNameComplete` 0x1411574A0 (`+sticker%u` from weapon+2, loadout = word 5, others = word 6); re-checked `movzx edx, word ptr [r13+0Ch]` in `MSG_WriteWeapon`. Size unchanged, so the 39 structs that embed `Weapon` keep their layout |

| `playerState_s` | size 21372 -> 21368: dropped the last member `viewLinkEntsFollowRemoteEyes` (bool +0x5378, placed from game-test, no Replay access) and its 3-byte pad; nothing else moved | `MSG_ReadDeltaPlayerstate` 0x1412D3D90: `memset_0(dest, 0, 0x5378u)` and `memcpy_0(v7, v8, 0x5378u)` (A005); `gclient_s.sess` starts at +0x5378 (A008, A009). Game-test puts the bool at 0x53A0; the Replay shift there is -44 overall, so it probably sits at +0x5374 (inside `_pad5298`), UNCONFIRMED. Nothing embeds `playerState_s` by value (`gclient_s.ps` is a byte array) |

| `EntityLoDs` | size 256 -> 260 (tail pad 252 -> 256) | `MSG_ReadDeltaEntityLoD` 0x1412D1D00 (lodIndex +0, 64 dwords from +4); ring stride `+ 260 * idx` in `SV_ReadDeltaCachedEntityLoD` 0x141384C00 |
| `CgSnapshotMP` | dropped the 4-byte pad after `entitiesLoD` (size stays 763992) | the pad existed only because the old `EntityLoDs` was 256 |
| `SnapshotInfo`, `ClSnapshot` | grew with `EntityLoDs` (+8 and +4): NOT checked in Replay | left for their agents (A052 owns `ClSnapshot`; `SnapshotInfo` is unassigned) |
| `clientSession_t` | size 2456 -> 2432, members shifted as planned below; 7 unconfirmed names dropped | A009, see the plan below |
| `gclient_s` | size 64592 -> 64496; `ps` is now `playerState_s` (21368); `playerGround` is a named 80-byte array until `BgGroundState` / `trace_t` are settled; dropped `mpviewer` and `footstepWeight` (no room, no access) | A008 delta runs: all 15 confirmed checkpoints land on their members (sess 21368, mlg 23800, flags 24016, buttons 24024, omnvarPmoveData 41176, playerBox 43580, playerGroundPersistent 43684, executionState 43696, autoUseDelayInfo 63532, lockedTargetOffset 63932, animOverrideVehicleType 63968, entityLoDs 63980, nearEntities 64240). Note: the clients array global is named `level_transientVisibility` in this database |

### Agent edits re-checked

| Agent | Edit | Check |
|---|---|---|
| A010 | `EntityLoDs.lodIndex` int +0x0 | OK: `MSG_ReadDeltaEntityLoD` 0x1412D1D00 writes `*(_DWORD *)dest = lodIndex`, then 64 DWORDs from +4 |
| A006 | `SprintState` +0x00..+0x1C (7 fields) | OK for +0x10, +0x18, +0x1C: `PM_RestartSprintTimers` 0x141124DB0 writes `ps+812/820/824` with `sprintState` at `ps+0x31C`; +0x20 is the sprint meter (`ps+828`) |
| A001 | `gentity_s.birthTime` int +0x41C (and three `snd_wait_*` members) | OK for birthTime: `G_UtilsMP_InitEntitySlot` 0x14127A270 `mov [rbx+41Ch], eax` |
| A004 | 55 `playerState_s` members in [0, 0x340) from the static net-field list | consistent: one -0xC chain from `movingPlatforms` (game-test 32 bytes, Replay 20) checked by A004 at many points; `sprintState` at +0x31C agrees with `PM_RestartSprintTimers` |
| A003 | `Weapon` size 60 (no edit) | OK: `BG_PlayerToEntity_SetStowedWeaponMP` 0x141111130 `memcmp(rec+2, rec+2, 0x3C)` in 62-byte records |

### Confirmed proposals waiting to be applied

| Struct | Proposal | Evidence |
|---|---|---|
| `EntityLoDs` | size 256 -> 260 (mask is 64 DWORDs at +4) | `MSG_ReadDeltaEntityLoD` copies 2 x 128 + 4 bytes and resets dest+4..dest+260 |

| `gclient_s` | size 64592 -> 64496 | `G_ClientMP_Disconnect` 0x1411F1B10: `level_transientVisibility[0] + 64496 * clientNum`, loop step `v11 += 64496`, result used as `gclient_s *` and `playerState_s *` (A008; the global is misnamed, it is the clients array). `sess` starts at +21368 (`*(v4 + 21368) = 0` = sessionState); game-test has 21416 |

`gclient_s` plan (waits for A005 = playerState_s size and A009 = clientSession_t): `ps` becomes 21368 bytes so `sess`
lands at +21368 (-48); `clientSession_t` is 2432 in Replay if `mlgSpectatorClientInfo` (216, confirmed by A009)
follows it directly, which puts `flags` at 24016 as Replay reads it (-24); A008 places a further -20 in the
playerBox/pad area and -4 in the tail.

`EntityLoDs` = 260 has a second proof: `SV_ReadDeltaCachedEntityLoD` 0x141384C00 indexes its ring as
`s_snapTransientVisibilityRing + 260 * idx`.

`clientSession_t` plan (A009, raw instructions in its report): members up to +0x490 unchanged; from
`weaponHudIconOverrides` (+0x4B0) to `eFlags` (+0x968) at -0x14 (`G_Client_SetEntUnusable` `or [r8+583Ch], edx`,
`PlayerCmd_GetMLGSpectatorTeam` `[rax+5A3Ch]`, `PlayerCmd_GetMLGSelectedCamera` `[rax+5A38h]`, eFlags `[rsi+5CCCh]`);
from `isPrivatePartyHost` (+0x97C) on at -0x18 (stores at sess+0x964/0x968/0x970/0x978); size 0x980. Dropped as
unconfirmed: `sightedByPlayers`, `lastDeathTime` (the 20 bytes are most likely there), `hudData`, `savedSpawnCount`,
`savedServerTime`, `savedFootstepCount`, `persistentDataArchived` (one 4-byte member of these is missing).

`EntityLoDs` is embedded in five structs, so the resize waits for the wave 1 agents to finish:
`SnapshotInfo.newEntitiesLoD` (+340, followed by a 4-byte pad: fits 260) and `CgSnapshotMP.entitiesLoD` (+763732,
followed by a 4-byte pad: fits 260) absorb it in their pads; `ClSnapshot.entitiesLoD` is the last member;
`gclient_s.entityLoDs` (+64076, next member `nearEntities` at +64332) and `SnapshotInfo.oldEntitiesLoD` (+84, next
member at +340) have no room, so their placement must be checked in Replay first.
