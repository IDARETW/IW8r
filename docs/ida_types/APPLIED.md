# IDA local-type repair: coordinator log

Note (2026-11-15, during wave 4): the owner's `replay_crossref_sync` plugin is not loaded in this IDA session, and the
owner said the plugin and its SQLite no longer matter. The IDA database is the only place these edits live.

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

| `GExecutionState` | `phase` unsigned int -> unsigned __int8 + `_pad11[3]` (no move) | `G_Execution_ClearPlayerState` 0x140E868E0: `mov [rax+0AAC0h], cl` (executionState = gclient+0xAAB0, +16); A010 |

A010 size proofs inside the rebuilt `gclient_s` (distance from each child's start to the next member): `viewClampState`
112, `FrameNotifyState` 80, `PlayerWeaponAnimArrays` 16736, `OmnvarPmoveData` 2404, `GExecutionState` 20,
`EntityMarkState` 2876, `PlayerViewState` 3560, `AutoUseDelayInfo` 400, `WeaponOffsetPatternCache` 916 all match
their types. `BgGroundState` is 80 in Replay (type 96): its member layout is still open (backlog).

## Wave 2

| Struct | Change | Evidence |
|---|---|---|
| `WeaponDef` | `stowTag` unsigned __int8[8] -> scr_string_t + `_pad1194[4]` (no move) | `Load_WeaponDef` 0x140D9B9AD `add rax, 1190h` (scr_string_t load), next pointer `add rax, 1198h` (A020) |

| `dvar_t` | size 128 -> 112; +104 is `domainCallback` (void *, 8 bytes); the 24-byte `BbConstUsageFlags` member dropped (not in Replay) | `Dvar_Deregister_Internal` 0x1413E5B10: `imul rcx, rdx, 70h` (x3) and `lea r8d, [rdx+70h]` memset size; `Dvar_Init` 0x1413E6CB0 pool loop `+= 112` over 7012 entries, `memset(..., 0xBFBC0)` = 7012 * 112; `Dvar_RegisterCustomDomainCallback` 0x1413E90B0 writes `*(dvar+104) = domainCallback` (A014). Nothing embeds `dvar_t` by value |
| `VariableValue` | `type` unsigned int -> unsigned __int8 + `_pad9[3]` (no move) | `VM_CallScriptFunction` 0x14132BB2F: `movzx esi, byte ptr [r14+8]`, `mov byte ptr [r14+8], 8` (A014) |

| `cpose_t` | size 288 -> 280: members up to `physicsId` (+0x18) unchanged; `actualOrigin` (12 bytes at +0x1C) dropped, leaving 4 unknown bytes at +0x1C; everything from `origin` on 8 lower (origin +0x20, angles +0x40) | A015: `centity_t` stride 752 (`CG_GetActiveCentity`, `CG_SnapshotMP_SetNextSnap`); `prevState` at +0x118 (16-byte copies in `CG_SnapshotMP_SetNextSnap`, 112-byte block in `CG_PlayerCorpseMP_MoveDetachedPlayerCorpse`); origin +0x20 / angles +0x40 through both a centity pose (`CG_Pose_DoControllers` via `CG_Entity_GetParentAxis`) and `cg_t.viewModelPose` (a 280-byte block in `CG_UpdateViewModelPreviousPositionForHand`). After the change `centity_t` is 752 with prevState 280, nextState 392, flags 640, previousEventSequence 664, all as A015 measured |
| parents of `cpose_t` | moved with it (not checked): `cg_t` 620656 -> 620640, `CgPredictedEntity` 904 -> 896, `MayhemObj` (16 + 1 poses) 5184 -> 5048 | game-test-copied parents, so later members are expected 8 lower per pose; the wave 4 `cg_t` agents have a note |

Findings without edits: `WeaponDef` 5296, `ADSOverlay` 112, `SwaySettings` 256, `WeaponOffsetPatternScaleInfo` 16,
`CarryAnimOverride` 24 match their loaders' stream sizes (A020); `WeaponCompleteDef` 624 and `AttachmentList` 16 match
(A016); `WeaponDef` members from 0x764 on are 24 bytes below game-test because Replay drops six `iExplosionStepped*`
ints, and the type already has that shift (A019); `usercmd_s` 264, `viewClamp` 24, `UsableHandleInfo` 8,
`FootstepAnim` 516 confirmed (A011).

## Wave 3

| Struct | Change | Evidence |
|---|---|---|
| `SpawnVar` | relayout, size 9232 -> 8976: `spawnVarKeys` unsigned int[64] +8, `spawnVarValues` char *[64] +264, `numSpawnVarChars` +776, `spawnVarChars[8192]` +780 (the old 1024-byte `spawnVars` dropped) | `Scr_ParseSpawnVars2` 0x1412E0F50: `mov [r14+rax*4+8], ebx`, `mov [r14+rax*8+108h], rbx`, `mov [r14+308h], esi`, `lea rbx, [r14+30Ch]` (A027; `Scr_SetStructFields` reads the same offsets). Size: the used extent ends at 8972, the struct holds pointers so it is a multiple of 8 -> 8976 (INFERRED; the bss gap to `g_mainStateFloatMaxSentinel` caps it at 9136). Global `level_spawnVarsValid`; nothing embeds it |
| `ScriptableStateDef` | `type` unsigned __int64 -> int + `_pad1C[4]` (no move; game-test: 4-byte `Scriptable_StateType`, not in this database) | `ScriptableCommon_GetPartHasUsableState` 0x1412DF7E8 `cmp dword ptr [rdx+r9+18h], 3`; `ScriptableCommon_GetPartFirstUsableState` and `Load_ScriptableStateDataDispatch` read a dword (A030). Note: IDA's `Load_ScriptableEventFootstepDef` (0x140DBDAD0) is really the usable-state loader (A030) |
| `MapEnts` | `dynEntityListsCount` unsigned int -> unsigned __int16 + `_pad19E[2]` (no move) | `Load_MapEnts` and `Postload_MapEnts` read `*(unsigned __int16 *)(varMapEnts + 412)` (A027) |

Findings without edits: all 10 `GfxWorld` sub-structs match their `Load_*` stream sizes, and
`GfxWorldDpvsDynamic.dynEntVisData` was retyped to `void *[66]` (same size) by A023; 18 of A024's 19 asset structs and
all 13 of A028's (terrain, `XModel` 688, `XSurface` 192, `XModelSurfs` 96, `StMesh` 296) match their loaders;
`MapEnts` 1032 and 15 sub-structs match (A027); `clipMap_t` 248, `ComWorld` 168, `ComPrimaryLight` 160,
`MapTriggers` 80 match (A026). A025 filled `GfxImage.residentMipCount` (u8 +0x34: `Load_ImageStream` 0x140F614A0 stores
`Stream_ComputeImageResidentMipCount(image)` there; game-test has padding at +0x34) and two `GfxWrappedBuffer` members in
`GfxLightGridVolumeAtlas` (same size); `GfxGpuLightGrid` 640 matches its loader.

## Wave 4

| Struct | Change | Evidence |
|---|---|---|
| `VehicleDef` | `doorBones` unsigned __int8[20] -> scr_string_t[4] + `_pad44[4]` (no move) | `Load_VehicleDef` 0x140E1F823: `add rax, 34h` -> `varscr_string_t`, `mov edx, 4`, `call Load_scr_string_tArray` (4 entries); `Postload_VehicleDef` agrees (A032). `SndAlias` 232, `SndAliasList` 32, `VehicleDef` 3848 match their loaders |
| `VehicleDef` | `type` unsigned __int64 -> unsigned __int8 + `_pad9[7]`; `ssAnimTree` u8[8] -> scr_string_t + pad 4; `trophyTags` u8[20] -> scr_string_t[4] + pad 4 (no moves, size 3848) | `BG_VehicleSlideMove` 0x141133E93 `cmp byte ptr [rax+8], 4` (x5 byte accesses; game-test `VehicleType` is 1 byte); `Load_VehicleDef` `add rax, 898h` (scr_string_t) and `add rax, 0CCCh` (`Load_scr_string_tArray(1, 4)`, next member loaded at +3296) (A034). `VehiclePhysicsDef` 1280 and all 146 of its game-test names match (A034) |
| `cg_t` | A039 filled `patternCacheList` (`WeaponOffsetPatternCache[12]`, 10992 bytes) at +0x827E0 | `cg_t::cg_t` builds 12 caches from +534512 (A039) |

| `cg_t` | CORRECTION of the wave 2 ripple: members after `viewModelPoseLeftHand` restored to their earlier offsets (`firstPersonLegsPose` +8, everything after +16; pads absorb the 8 bytes each smaller `cpose_t` frees), size kept at 599712 | `cg_t::cg_t` 0x1416C1C29 `mov [rbx+92688h], r13` = `m_mlgSpectatorPtr` at +599688 (A036 measured `m_weaponHand` at +599608; A039 measured the `patternCacheList` construction at +534512; both land now). Lesson: letting parents move with a shrunk child was wrong here, so the same move in `CgPredictedEntity` and `MayhemObj` (wave 2) is suspect |
| `cg_t` | size 620640 -> 599712 (0x926A0): cut the tail; dropped `m_compassFriendlyFlag` (+0x926A2), `m_compassCarrierValue` (+0x926C4), `m_compassSharedPtr` (+0x97858) and two pads, which are `CgGlobalsMP` members in Replay | A039: `CgGlobalsMP_ctor` writes its omnvar indexes at +599732 = cg_t + 0x14 (the game-test gap between `cg_t` and `m_omnvarIndexIsNightMap`); A037: `cg_t::cg_t` writes up to +599688; 599712 is 16-byte aligned. Nothing embeds `cg_t` by value. `CgGlobalsMP` is 747296 in the type but `CG_AllocLocalClientGlobals` steps 677040 (A076 in wave 8 has the dropped names) |

## cg_t from Replay logic (owner request, during wave 5)

| Struct | Change | Evidence |
|---|---|---|
| `cg_t` | rebuilt at 599712 from Replay's own accesses: 251 named members (82% of the bytes), real base types from the game-test type dump; members whose local type is wrong for Replay are named byte arrays of the Replay size | `docs/ida_types/CG_T.md`: exe scan of every `LocalClientGlobals[n]` / `this` access (2715 offsets), function-by-function alignment with game-test (282 shared functions, 58 anchors, final delta exactly -207888), layout between anchors with corrected local sizes. Independent checks all land (constructor stores, A036/A037/A039/A040 offsets, the four poses from `CG_ViewMP_InitRandomOffsets`). Backup before: `out/ida_types/backup/local_types_20261115_before_cg_t_scan.h` |
| `CgGlobalsMP` | rebuilt at 747296 (size unchanged): base class `cg_t` + 73 MP tail members at game-test -207888 | `CG_MainMP_AllocateClientMemory` 0x14178FDF0 allocates and steps 747296 per `CgGlobalsMP_ctor`; all 182 tail accesses land inside game-test tail fields at -207888. CORRECTION: the 677040 stride quoted earlier for MP (A039) is the SP allocator |
| `CgGlobalsSP` | size 952512 -> 677040: base `cg_t`, `pipViews` 62936, `pipViewParams`, `m_skyOverride`, `m_characterInfo` 13720 @662704, `m_clientCharacterInfo` @676424, `m_charAnimState` 608 @676428 | `CG_AllocLocalClientGlobals` 0x1417CA150 (`677040 * count`, `ms_allocatedType = 1`); `CgGlobalsSP::GetCharacterInfo` `this + 662704 + 13720 * i`; direct SP accesses at +662704 and +676428 |
| methods | `this` typed in `cg_t::cg_t`, `cg_t::GetViewModelTransform`, 2 `CgGlobalsMP::`, 4 `CgGlobalsSP::` methods and `CgGlobalsMP_ctor` | decompiles read `this->m_characterInfo`, `this->predictedPlayerState.hudElems`, `this->localClientNum` |
| `LocalClientGlobals` (0x14F26F940) | typed `cg_t *LocalClientGlobals[1]` (one slot; the next global is 8 bytes later) | decompiles now read `LocalClientGlobals[localClientNum]->inKillCam`, `->compassMapMaterial`, ... |

## All structs from Replay logic (owner request "get ALL of the structs laid out")

Method, numbers and rerun commands: `docs/ida_types/ALL_STRUCTS.md`; unproven differences: `ALL_STRUCTS_REPORT.txt`.
Two rounds applied by the coordinator through `apply_plan()` (plans `out/ida_types/scan/apply_plan.json`,
`apply_plan2.json`; log `apply_log.jsonl`; backup `local_types_20261115_before_all_structs.h`): 836 struct fills
(2,340 fields, 911 of them game-test-only and marked by a member comment), 47 rebuilds, 2 parents kept at their old
offsets (`PartyData` around `PartyMember` 504 -> 488, `BgVehiclePhysicsManager` around
`BgVehiclePhysicsObjectBuffer` 3360 -> 3312). Replay accesses on the right member 19,530 -> 22,517, on a wrong
member 5,449 -> 5,260; structs that were only padding 415 -> 31.

Retype pass (`make_retype_plan.py`): 69 named byte-array members in 34 structs got their struct type back where the
local type now has the same size and the game-test count (`cg_t.viewShake` = `Shake`, `playerState_s.objectives` =
`ObjectiveView[32]`, `SvClientMP.clientConnection` = `NetConnection`, ...). No offset or size changed.

### Report pass, wave 1 (A101-A110, read-only agents; `REPORT_AGENT_PROCEDURE.md`, proposals in `out/ida_types/proposals`)

Applied by the coordinator through the builder (proposal members as anchors, proposal sizes as proofs), plan
`apply_plan4.json`, backup `local_types_20261115_before_report_wave1.h`:

| Struct | Change | Evidence |
|---|---|---|
| `BgVehiclePhysics` | 1960 -> 688, ~60 members (-36 from game-test) | A103: `BgVehiclePhysicsGround::Setup@0x140D1B490` memsets from +688 and runs its wheels there; Load/SaveToMemFile |
| `BgVehiclePhysicsGround` | 3240 -> 3112, wheels at 688 | A104 members; size: `BgVehiclePhysicsTank::Setup@0x140D1C251` writes its first own member at +0xC28 (coordinator) |
| `BgVehiclePhysicsGround__Wheel` | 152 -> 148 (no `m_state`) | A104: `LoadWheelRecord@0x140D220C0`, stride `148*i` in Setup |
| `FlightDynamics` | 22272 (invented) -> 1264 | A107 members (-40 from 688); size = start of `HelicopterDynamics::m_MainRotor` (A102) |
| `HelicopterDynamics` | 2416 -> 2368, tail -48 | A102: Setup / SaveToMemFile / LoadFromMemFile displacements |
| `CgVehicleSystem` / `CgVehicleSystemMP` / `CgVehicleEventSystem` | 1152280 -> 1135880 / 1157400 -> 1141000 / 18144 -> 14040 | A105: `CG_MainMP_Init` memset 0x116908; physics manager memset 0xFF000 at +77328 |
| `CgSnapshotMP` | 763992 -> 405680 (entities 768, weapons 400) | A106: `CG_SnapshotMP_ClearSnapshots@0x1417ADEA0` (two snapshots 405680 apart), `CL_CGameMP_GetSnapshot` |
| `GenericTask` / `TaskItem` | 80 -> 88 (extra pointer at +24) / 88 -> 96 | A109; `TaskManager::Update@0x1413CCB97` step 0x60, `m_nextLocalTaskId` at +0x3000 = 128 * 96 (`TaskManager` kept, its 1024-byte pad was the missing 8 bytes per item) |
| `OnlineMatchmakerOmniscient` | 1077400 -> 1071120 | A110: constructor offsets; lobby data is 92400 (no 6424-byte additional-slots list) |
| `PartyData` | size kept 373704, ~40 offsets fixed (subparties stride 1016, `currentHost` 48) | A108: `g_partyData` to the next global 0x5B3C8, memcpy 0x5B3C8 in `PartyAtomic_BuildJoinInfo` |
| `PartyHostDetails` | 80 -> 48 | anchors to the end, A108 |

Not applied: `bot_data_t` (A101: size unproven, only 4 members), `MatchMakerOmniLobbyData` (wave 3, A124).

### Report pass, wave 2 (A111-A120), plan `apply_plan5.json`, backup `local_types_20261115_before_report_wave2.h`

39 rebuilds, 48 fills, 3 parents kept (`ParticleManager`, `Online_Friends`, `RB_BackendDataCopier`). Sizes the
agents measured (stride / allocation / memset / loader stream / next global), all re-scored by the builder:
`DWWrappers` 17656 -> 1136 (`DWServicesAccess_AddControllerSlot` imul 470h) with its wrapper slots 16 / 32
(`DWWrappers::init` slot gaps), `Vehicle` 2304 -> 2192 (`G_Vehicle_Create` pool stride + memset 0x890), `sd_decoder`
4480 -> 33152 (`SD_DecoderAllocate` slot stride), `NetChannel` 168 -> 24 (`NetStatic::NetStatic` step 24),
`NetLoopback` 103032 -> 98496 (three 32832-byte rings, next global), `BG_SpawnGroup_Loot_Set` 4512 -> 3712
(`FindOrAddSet` stride, `ParseSet` memset 0xE80), `ParticleManager__Particle_SoundData` 64 -> 56,
`FriendListGlobal` 48072 -> 28264, `MPSCQueue_RB_BackendDataCopier__Cmd_16384` 786448 -> 655376 (imul 0A0010h),
`CachedContentIOStreamDCache` 1792 -> 232, `BgAntiLagEntityHistoryExtended` 544 -> 68, `actor_prone_info_t` 32 -> 24,
`NetTelemetry` 39576 -> 27448, `DObjBoneParentMap` 8728 -> 8712, `ActiveClanData` 156872 -> 19648 (anchors to the
end), `StreamLoader` 704 -> 96, `Umbra__DepthBuffer` 200 -> 72, `MatchmakingData` 601 -> 352; members fixed in
`Ragdoll`, `RefString` (`str` at +8), `MemoryFile`, `NetEndpoint`, `Camo`, `PlayerWeaponCommonState`.
Sizes the agents only inferred were not used as proof (`COORD.json` `drop_size`), and rebuilds that would only move
game-test-prior fields are refused (`workerTrace_t`, `Edge`, `EntityTagInfo`, `playlistInfo` stay as they were).

### Report pass, wave 3 (A121-A130), plan `apply_plan6.json`, backup `local_types_20261115_before_report_wave3.h`

37 rebuilds, 50 fills, `GVehicles` kept around `BgVehiclePhysicsManager`. Measured sizes: `ClStatic` 137036 -> 175776
(`CL_Main_ShutdownOnceForAllClients` memset 0x2ACE8 from +440; Replay accesses reach +175769), `ClSnapshot` 94860 ->
119000 (`AllocSnapshotMemory` 119000 * count), `ClActiveClientSP` 56040 -> 55984 (memset 0xDAB0), `GfxCmdBufState`
82880 -> 10368 (`R_InitCmdBufState` memset 0x2880), `GfxMeshData` 12672 -> 1584 (`R_ToggleSmpFrameInternal` stride),
`CgAntiLag` 209728 -> 144376, `CgBallisticsSP` 2288416 -> 286056, `CgCompassSystemMP` 64280 -> 64184,
`CgDynamicMedia` 136 -> 24 (hunk allocations in `CG_Main*_AllocateClientMemory` / `CG_ClientSlots_AllocateMemory`),
`GAntiLagMP` 4403320 -> 4354128, `GBullet` 7680 -> 5064, `GMissileSP` 136 -> 24, `GUtilsMP` / `GUtilsSP` 128 -> 16
(`G_Main*_AllocateGameMemory` hunk sizes), `BgVehiclePhysicsManager` 1056776 -> 1044480 (`GPhysicsVehicleManager::Init`
memset), `RB_BackendDataCopier` 2360040 -> 1966824 (its three queues are 655376), `HavokPhysicsFreeListMemorySystem`
19584 -> 30816 (46 thread datas; accesses reach +30812), 14 more 16-byte Demonware wrappers, `bfx__VRT` 128 -> 80,
`AITaskBucket_Spread` 96 -> 88; members fixed in `OnlineClan`, `bfx__Area`, `CgBallistics`, `ClStatic`.
Not used as proof: `HavokPhysicsShapeViewer` 72 (ambiguous sized free), `bfx__Navigator` 760 (that is the
implementation class), `ClConnection` 322584 (game-test `ClConnection` is 16 bytes; the stride belongs to another
object). Conflicts left for later: `DWServicesAccess` list head, `Online_Friends` favorites (A124), `GfxBackEndData`
-448 bytes between game-test 4064 and 31368 (A123), `GfxCmdBufInput` -72 before `codeBuffers` (A123).

### Report pass, wave 4 (A131-A135), plan `apply_plan7.json`, backup `local_types_20261115_before_report_wave4.h`

11 rebuilds: `FxGlassCrackWork` 174592 -> 21824 (`Glass_DestroyPiece` stack frame), `ParticleState` 432 -> 512
(`InitParticleData` allocates `count << 9`; `ParticleData` is embedded at +416), `SvServerInitSettings` 224 -> 220
(whole-object copies), `SvGameModeApplication` 192 -> 24 (`SvGameModeAppSP_ctor` writes its first own member at +24),
`LUITraceRunner` 37832 -> 37824 (per-client stride 4728 qwords), `SndAliasGroupTracking` 192 -> 176,
`CgClientSideEffectsSystem` 116160 -> 14520, `CgAntiLagSP` 4227768 -> 4219576, `bdUpgradeAnonUnoAccRequest` 45576 ->
12736, members fixed in `DLogEvent_ScreenView` (`screens` 24 x 72 at +24) and `SndMusicState`. Confirmed as already
right: `snd_rv_params` 80, `SndMusicAsset` 120, `SndMusicState` 336, `SndAssetBankEntry` 44,
`ParticleModuleInitRunner` 112. Second retype pass: 28 more members (`DWWrappers` slots, `NetChannel[48]`, ...).

Final state and the 135 structs still unproven: `ALL_STRUCTS.md`, `ALL_STRUCTS_REPORT.txt`. Open conflicts the
agents found and nobody resolved yet: `bot_data_t` size (A101), `DWServicesAccess` list head (A124), `Online_Friends`
favorites vs `FriendListGlobal` 28264 (A124), `GfxBackEndData` -448 (A123), `GfxCmdBufInput` -72 (A123),
`ParticleManager` extent (A112), `HavokPhysicsShapeViewer` (A125), `ClConnection` (A130), `refdef_t` radiantLive
arrays absent in Replay (A113, everything from `scissorViewport` on is -66576), the derived fence classes (A114).

## Wave 5

Pending for the apply pass:
- `characterInfo_t` 13728 -> 13720: stride 0x3598 in `CgGlobalsSP::GetCharacterInfo` (`13720 * i + this + 662704`),
  `CG_Heat_UpdateWorldmodel`, `CG_SnapshotMP_TransitionSnapshot` (also `memset(..., 0x3598)`); the anim tree pointer is
  read at +0x7F0 (type +0x7F8), so the 8 extra bytes sit between +0x5E8 and +0x7F0 (A041, who filled `team` int +0x0C).
- `GWeaponMap` 4112 -> 2424 (`GWeaponMap::AllocateMemory` 0x140FF7170 allocates 2424); `m_header` renamed `m_entries`
  (A043). `GScrWeaponData` 62 looks like {u16 index; Weapon weapon (60)} (stride 62 in `GScr_Weapon_GetRefConst`).
- `trace_t` 96 -> 72? (`BgMissile_TraceMissileImpact` 0x140CD24C0 clears and copies 72 bytes; A043, consistent with
  A008's 80-byte `BgGroundState`).
- `ai_scripted_t`: A046 filled 6 fields; `codeGoal` probably at +0x260 (type +0x258).

### Open questions (need more Replay evidence)

- `clientState_t` 408 -> 400? Strides of 400 in `CL_ParseMP_ParsePacketClients` 0x1415F1B80 (`+ 400 * (index % count)`,
  first dword = clientIndex) and `CG_SnapshotMP_SetNextSnap` (`imul rcx, rax, 190h`); `G_ClientMP_Disconnect` clears
  0x190 bytes at sess+0x284, but the type puts `cs` at sess+0x28C. Candidate explanation: Replay moved the last 8 bytes
  (`serverDobjTurretWeapon` .. `lastStandSelfReviving`) out of `clientState_t` (A004 found `serverDobjHideWeapon` and
  `serverDobjStowHeldWeapon` in the `playerState_s` net list). Not applied: `clientSession_t` +0x270..+0x444
  (`noSpectate`, `cs`, `scores`, `psOffsetTime`) must be measured first; also `CgSnapshotMP.clients` is
  `clientState_t[200]`.
- `ScoreInfo` 26 -> 24? (A011: score readers step in 24-byte units; owner of the array unproven.)
- `BgGroundState` 96 -> 80 (gclient slot is 80): member layout unknown.
- `scrContext_t` size (type 981960, game-test 992528): no whole-struct alloc or clear in Replay. Direct data references
  from `g_scrContext_Server` (0x14D591B00, untyped .bss) reach +1048448 (e.g. +982904/+982912, +966512, +917328 x9,
  +827184 x9), so either the struct is bigger or another context/global follows it; the next named symbol
  (`s_vec4ConstCopy_5B7340`, +153664) is an auto name inside the region. A013 confirmed the start up to +0xC70
  (m_hunkUser 0, variable list sizes 8/12, m_jmpDepth 3168, m_lockJmpDepth 3172, m_precompFlag 3184) and m_vmPub at
  +47528; layout diverges from game-test after +0xC70. Needs a deeper (Opus) pass.
- `scrVarPub_t`: `m_classMap` is at +152 in Replay (scrContext+0x3560; `Scr_InitClassMap`, `Scr_SetClassMap`,
  `Scr_InitVariables`), the type has it at +248; members 152..416 overlap it and need a joint relayout (A014).
- `scrVmPub_t.inparamcount` (+40) is read and written as bytes at +40 and +42 (A014, around 0x1413277DE): unconfirmed.
- `GfxLightGridVolumeAtlas__AtlasState` 52632 -> 52648 (stride 52648 in `Update`, `PrepareForCopy`, `ShouldDefragment`,
  `UpdatePackingBuffer`; A025): inside it the two 32-byte params arrays are two 64-byte `GfxWrappedBuffer`s at +472 and
  +536 and `m_packing` starts at +600; the last four members do not fit. Ripples into `GfxLightGridVolumeAtlas`.
  `StreamerMemLoan.mUpdateID` is 8 bytes (QWORD read in `GiveLoan`).
- `GfxSunShadow` +19924..+20028: Replay has the cloud-shadow block at +19924 and lightDir at +19980 (type +20276 /
  +20332), conflicting with the typed `overlaySetup` (A026); needs a relayout. `GfxSunShadowPartition.farDist` is
  copied as an int. `GfxBackEndData` is probably 11801472 (`R_InitBackEndData` allocates 2 x 11801472; A058 has a note).
- `cg_t` front: Replay constructs `xCam` at +0x74B8 (`cg_t::cg_t`, `CG_View_UpdateXCamIfActive`, `CG_Camera_Active_Get`;
  A040) and A036 saw the same (+29880 vs live +29616): a +264 shift starts between +0x6420 and +0x74B8 and must be
  given back before the confirmed tail (+599608 / +599688). Also open there: `refdef` (2752 at +26840) vs Replay's
  `g_activeRefDef` at +26624, `clientNum` +25632 written as localClientNum, `ViewMotionSpring` at +0x67D8 overlapping
  AdvancedSwayState writes, `SetViewModelTransform` reading viewModelAxis+16 (A036, A040).
- `CgPredictedEntity` (now 896) and `MayhemObj` (now 5048) moved with `cpose_t` in wave 2 without a Replay check; the
  same move proved wrong for `cg_t`. Check their members after `pose` / `prePostPose` and restore if needed.
- `DB_AssetEntryTable` (9314760) is too small: `DB_LinkXAssetEntry` writes a per-type head array at +9463304 (A035,
  who also filled `m_hashTable` unsigned int[358400] at +0x6E0F08). `XAssetHeader` lacks the shader (enum 14..17) and
  several other members (A035). `TransientInfo.transientInfoFlags` is read as one byte at +140 (A035).
- `cpose_t`: what replaced `actualOrigin` in the 4 bytes at +0x1C (A015 found no access). `CEntActorInfo.roll` is a float
  in the type but compared as a byte (A015).

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
