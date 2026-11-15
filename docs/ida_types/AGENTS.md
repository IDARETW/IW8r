# IDA local-type repair: agent roster

Generated from the 2026-11-15 review (see PLAN.md). 100 Haiku agents in 10 waves of 10, most important first. Every agent follows AGENT_PROCEDURE.md; its full entry (sizes, hints, notes) is in agents.json. A range `[lo-hi)` means the agent owns only that byte range of a struct that other agents share.


## Wave 1

| Agent | Area | Structs (range) | Starting evidence |
|---|---|---|---|
| A001 | core game state | `gentity_s`, `entityState_t`, `entityShared_t` | gentity_s: 1456 here, 1456 in game-test. G_Missile_ExplodeInternal, CG_PlayerCorpseMP_SetNextSnap |
| A002 | core game state | `BgEntityComponents`, `hint_t`, `GMovingPlatformInfo`, `GDamageNotifyInfo`, `LerpEntityState`, `DObjPartBits`, `item_ent_t`, `trigger_ent_t`, `mover_ent_t` | BgEntityComponents: 160 here, 160 in game-test. BgMissile::BounceMissile, BG_PlayerStateToEntityState, DObjCalcAnim |
| A003 | core game state | `missile_ent_t`, `blend_ent_t`, `Weapon`, `trajectory_t_secure`, `trajectory_t`, `LerpEntityStateTypeUnion`, `DObjPartBitsT_bitarray_simd_256_bitarray_traits_simd128_bitarray_memory_traits_simd128u`, `mover_positions_t`, `missile_fields_grenade` | missile_ent_t: 136 here, 136 in game-test. CgEventSystem::HandleEntityEventCommon, BG_PlayerToEntity_SetTrajectory, CG_SnapshotMP_FixupCorpseTrajectory |
| A004 | core game state | `playerState_s` [0x0-0x340) | playerState_s: 21372 here, 21412 in game-test. MSG_WriteDeltaPlayerstate |
| A005 | core game state | `playerState_s` [0x340-0x537C) | playerState_s: 21372 here, 21412 in game-test. MSG_WriteDeltaPlayerstate |
| A006 | core game state | `PlayerASM_State`, `PlayerVehicleState`, `playerEvents_t`, `playerState_s___unnamed_type_cursorHintParams`, `SprintState`, `MantleState`, `SlideState`, `ContextMountState` | PlayerASM_State: 20 here, 20 in game-test. BG_AddPredictableEventToPlayerstate, Mantle_Activate, BG_ContextMount_CanMount |
| A007 | core game state | `PlayerActiveWeaponState`, `PlayerEquippedWeaponState`, `SkydivePlayerState`, `compressedAnimData_s`, `MountSurfaceAbbreviatedProperties` | PlayerActiveWeaponState: 80 here, 80 in game-test. CycleWeapPrimary |
| A008 | core game state | `gclient_s` | gclient_s: 64592 here, 64592 in game-test. G_ActiveMP_ClientEndFrame |
| A009 | core game state | `clientSession_t`, `MLGSpectatorClientInfo`, `ClientExtrapData` | clientSession_t: 2456 here, 2456 in game-test. MSG_ReadDeltaMLGSpectatorInfo |
| A010 | core game state | `viewClampState`, `UsableHandle`, `FrameNotifyState`, `PlayerWeaponAnimArrays`, `OmnvarPmoveData`, `BgGroundState`, `GExecutionState`, `EntityMarkState`, `PlayerViewState`, `WeaponOffsetPatternCache`, `AutoUseDelayInfo`, `EntityLoDs` | viewClampState: 112 here, 112 in game-test. G_DefaultViewAngleClampGoal, G_PlayerUse_UpdateActivate, BG_ReloadMultipleAnimOverride |

## Wave 2

| Agent | Area | Structs (range) | Starting evidence |
|---|---|---|---|
| A011 | core game state | `bitarray_2048`, `usercmd_s`, `clientState_t`, `ScoreInfo`, `viewClamp`, `UsableHandleInfo`, `FootstepAnim` | bitarray_2048: 256 here, no game-test type of that name. SV_ClientMP_UserMove, CL_ParseMP_ParsePacketClients, MSG_ReadDeltaScoreboard, Load_FootstepAnimArray |
| A012 | core game state | `ViewMovementState`, `WeaponMovementState`, `BSplineRelaxedCBezier`, `WeaponOffsetPatternScaleCache`, `AutoUseDelayArray`, `UserCmdClientBits`, `ClientCustomizationInfo`, `MountWorldmodelAbbreviatedProperties`, `DoorClientState`, `FootstepTime`, `MovementTime`, `WeaponIdleMotionCache`, `BezierCurve`, `AutoUseDelayElement` | ViewMovementState: 1672 here, 1672 in game-test. BG_CalculateWeaponMovement, G_SaveMP_WriteAgent, BG_Customization_BuildClientCustomization |
| A013 | core game state | `scrContext_t`, `scrAnimGlob_t`, `scrAnimPub_t`, `scrReadWriteGlob_t`, `scrVarGlob_t`, `scrVmGlob_t`, `scrVmDebugPub_t` | scrContext_t: 981960 here, 992528 in game-test. VM_Execute, Scr_LoadAnimTreeAtIndex |
| A014 | core game state | `scrVarPub_t`, `scrVmPub_t`, `scr_classStruct_t`, `VariableValue`, `function_frame_t`, `function_stack_t`, `dvar_t`, `BbConstUsageFlags` | scrVarPub_t: 512 here, 512 in game-test. VM_Execute, Scr_InitVariables |
| A015 | core game state | `centity_t`, `cpose_t`, `ClientEntitySoundState`, `SecureOrigin`, `SecurePrevOrigin`, `GfxSkinCacheEntry`, `CEntActorInfo` | centity_t: 760 here, 760 in game-test. CG_SnapshotMP_SetNextSnap, CG_Utils_ProcessNotetracks, CG_ClientModel_InitClient |
| A016 | map and assets | `WeaponCompleteDef`, `AttachmentList`, `ZoomSettings`, `SceneZoomSettings`, `WeaponZoomSettings`, `WeaponDef` [0x0-0x328) | WeaponCompleteDef: 624 here, 624 in game-test. Load_WeaponCompleteDef, Load_AttachmentList, Load_WeaponDef |
| A017 | map and assets | `WeaponDef` [0x328-0x83C) | WeaponDef: 5296 here, 5336 in game-test. Load_WeaponDef |
| A018 | map and assets | `WeaponDef` [0x83C-0xED8) | WeaponDef: 5296 here, 5336 in game-test. Load_WeaponDef |
| A019 | map and assets | `WeaponDef` [0xED8-0x11C8) | WeaponDef: 5296 here, 5336 in game-test. Load_WeaponDef |
| A020 | map and assets | `WeaponDef` [0x11C8-0x14B0), `WeaponDamageInfo`, `ADSOverlay`, `SwaySettings`, `WeaponOffsetCurveDescription`, `GrenadeRotationParams`, `CarryAnimOverride`, `WeaponOffsetPatternScaleInfo` | WeaponDef: 5296 here, 5336 in game-test. Load_WeaponDef, Load_ADSOverlay, varSwaySettings, Load_WeaponOffsetPatternScaleInfo |

## Wave 3

| Agent | Area | Structs (range) | Starting evidence |
|---|---|---|---|
| A021 | map and assets | `AdvancedIdleSettings`, `BallisticInfo`, `WeaponDamageCommon`, `HipSwaySettings`, `AdvancedHipSwaySettings`, `CommonSwaySettings`, `WeaponAttachment` [0x0-0x260) | AdvancedIdleSettings: 152 here, 152 in game-test. PM_UpdateHoldBreath, Load_BallisticInfo, varAdvancedHipSwaySettings, Load_WeaponAttachment |
| A022 | map and assets | `WeaponAttachment` [0x260-0x3C8), `AttProjectile`, `GfxWorld`, `GfxWorldDpvsPlanes`, `GfxMayhemSelfVis` | WeaponAttachment: 968 here, 976 in game-test. Load_WeaponAttachment, Load_AttProjectile, Load_GfxWorld, Load_GfxWorldDpvsPlanes, Load_GfxMayhemSelfVis |
| A023 | map and assets | `GfxWorldSurfaces`, `GfxWorldStaticModels`, `GfxWorldDraw`, `GfxDynamicLightset`, `GfxFrustumLights`, `GfxPrecomputedSkyIllumination`, `sunflare_t`, `GfxLightAABB`, `GfxWorldDpvsStatic`, `GfxWorldDpvsDynamic` | GfxWorldSurfaces: 136 here, 168 in game-test. Load_GfxWorldSurfaces, Load_GfxWorldStaticModels, Load_GfxWorldDraw, Load_GfxDynamicLightset, Load_GfxFrustumLights, Load_GfxPrecomputedSkyIllumination, Load_su |
| A024 | map and assets | `StreamTree`, `StreamTreeGrid`, `GfxWrappedBuffer`, `GfxWrappedRWBuffer`, `GfxWorldReflectionProbeData`, `GfxWorldLightmapReindexData`, `GfxWorldVolumetrics`, `GfxCompressedSunShadowParams`, `GfxDynamicLightsetLMData`, `GfxDynamicLightsetProbeData`, `StreamMtlGroup`, `ExtentBounds`, `GfxWrappedBaseBuffer`, `GfxShaderBufferRWView`, `GfxWorldDrawVerts`, `GfxIndexBuffer`, `GfxWorldTransientZone`, `GfxWorldDrawCells`, `GfxGpuLightGridTransientData` | StreamTree: 96 here, 96 in game-test. Load_StreamTree, Load_StreamTreeGrid, Load_GfxWrappedBuffer, Load_GfxWrappedRWBuffer, Load_GfxWorldReflectionProbeData, Load_GfxWorldLightmapReindexData, Load_G |
| A025 | map and assets | `GfxLightGridVolumeAtlas`, `GfxImageDesc`, `GfxLightGridVolumeAtlas__LGVBlock`, `bitarray_1536`, `GfxLightGridVolumeAtlas__AtlasState`, `MPSCQueue_XZoneHandleUnique_1024`, `GfxImage`, `GfxLightGridVolumeAtlas__TextureAllocator`, `GfxLightGridVolumeAtlas__ParamsAllocator`, `StreamerMemLoan`, `GfxImageStreamData`, `bitarray_512`, `GfxGpuLightGrid`, `GfxCell`, `GfxPortal`, `GfxPortalWritable`, `GfxLight`, `GfxLightDef`, `GfxStaticModel` | GfxLightGridVolumeAtlas: 134448 here, 134352 in game-test. GfxLightGridVolumeAtlas::Pack, Load_GfxImage, Mem_Paged_DecommitMemoryPartial, Load_GfxGpuLightGrid, Load_GfxCell, varGfxPortal, Load_GfxLightArray, Load_GfxLig |
| A026 | map and assets | `GfxSunShadow`, `GfxMatrix`, `GfxSunShadowProjection`, `GfxSunShadowPartition`, `GfxSunShadowPartitionCache`, `GfxSunShadowOverlaySetup`, `GfxCloudShadow`, `tmat44_t_vec4_t`, `GfxViewParms`, `t_36BFE971E0ECD0A211EA488D8E5210B7`, `GfxCachedSunShadowEntryData`, `GfxCachedSunShadowOverlap`, `GfxCamera`, `clipMap_t`, `MapTriggers` (base `ClientMapTriggers`), `PhysicsCapacities`, `ComWorld`, `ComPrimaryLight` | GfxSunShadow: 20384 here, 20384 in game-test. R_SetupSunShadowMaps, R_MDAO_DrawVolumeOccluders, R_SetupSunShadowMapProjection, Load_clipMap_t, Load_MapTriggers, Load_ComWorld, varComPrimaryLight |
| A027 | map and assets | `MapEnts`, `ClientTriggers`, `ClientTriggerBlend`, `SpawnPointRecordList`, `SplineRecordList`, `SpawnGroup`, `ScriptableMapEnts`, `ClientSideEffects`, `CreateFxAssetData`, `ServerSideEffects`, `SpawnerList`, `ClientMapTriggers`, `Scriptable_EventSun_Data`, `ScriptableReservedDynents`, `SpawnVar`, `StTerrain`, `StClutterSamplePoints` | MapEnts: 1032 here, 1064 in game-test. Load_MapEnts, Load_ClientTriggers, Load_ClientTriggerBlend, Load_SpawnPointRecordList, Load_SplineRecordList, Load_SpawnGroup, Load_ScriptableMapEnts, Load_Clie |
| A028 | map and assets | `StDiskTerrainSurface`, `StTerrainGrid`, `GfxPlacement`, `StClutterSampleBitmask`, `StMesh`, `StVertexData0`, `StMeshLayerMaskData`, `GfxVertexBuffer`, `StTerrainNode`, `XModel`, `XModelLodInfo`, `XModelSurfs`, `XPakEntryInfo`, `XSurface` | StDiskTerrainSurface: 496 here, 376 in game-test. Load_StDiskTerrainSurface, Load_StTerrainGrid, Load_StClutterSampleBitmask, Load_StMesh, Load_StVertexData0, Load_StMeshLayerMaskData, varGfxVertexBuffer, varSt |
| A029 | map and assets | `XSurfaceSubdivLevel`, `GfxBufferResource`, `GfxShaderBufferView`, `XSurfaceSubdivInfo`, `GfxSubdivCache`, `XModelDetailCollision`, `Material`, `MaterialTechnique`, `MaterialTechniqueSet`, `TechniqueMask`, `MaterialTextureDef`, `MaterialConstantDef`, `MaterialConstantBufferDef`, `GfxConstantBuffer`, `GfxConstantBufferDesc`, `ScriptableDef`, `ScriptablePartDef` | XSurfaceSubdivLevel: 568 here, 296 in game-test. Load_XSurfaceSubdivLevelArray, R_LGV_CombineSampledLighting, Load_XSurfaceSubdivInfo, varGfxSubdivCache, Load_XModelDetailCollision, Load_Material, Load_Materia |
| A030 | map and assets | `ScriptableStateDef`, `ScriptableStateBaseDef`, `ScriptableStateDefUnion`, `ScriptableStateUsableDef`, `ScriptableEventDef`, `ScriptableEventBaseDef`, `ScriptableEventDefUnion`, `ScriptableEventAnonymousDef`, `ScriptableEventCompassIconDef`, `ScriptableInstance`, `ScriptableStateHealthDef`, `XAnimParts`, `PhysicsAsset`, `PhysicsFXShape` | ScriptableStateDef: 160 here, 160 in game-test. varScriptableStateDef, Load_ScriptableStateBaseDef, varScriptableStateDefUnion, varScriptableStateUsableDef, Load_ScriptableEventDefArray, varScriptableEventBas |

## Wave 4

| Agent | Area | Structs (range) | Starting evidence |
|---|---|---|---|
| A031 | map and assets | `DynEntityDef`, `DynEntityClient`, `DynEntityList`, `FxWorld`, `FxGlassGlobals`, `FxGlassSystem`, `FxGlassDef`, `SndBank`, `SoundTable`, `SndBankStreamInfo` | DynEntityDef: 80 here, 80 in game-test. Load_DynEntityDefArray, DynEntCL_AddEntity, Load_DynEntityList, Load_FxWorld, Load_FxGlassGlobals, Load_FxGlassSystem, Load_FxGlassDef, Load_SndBank |
| A032 | map and assets | `SndAlias`, `SndAliasList`, `VehicleDef` [0x0-0x6A8) | SndAlias: 232 here, 232 in game-test. varSndAlias, Load_SndAliasList, Load_VehicleDef |
| A033 | map and assets | `VehicleDef` [0x6A8-0xC80) | VehicleDef: 3848 here, 3848 in game-test. Load_VehicleDef |
| A034 | map and assets | `VehicleDef` [0xC80-0xF08), `VehiclePhysicsDef`, `VehiclePhysicsSoundGroup`, `VehiclePhysicsVfxGroup` | VehicleDef: 3848 here, 3848 in game-test. Load_VehicleDef, Load_VehiclePhysicsDef, varVehiclePhysicsSoundGroup, varVehiclePhysicsVfxGroup |
| A035 | map and assets | `TransientInfo`, `TransientSets`, `TransientCosts`, `StreamingInfo`, `AlwaysloadedFlagSet`, `AlwaysloadedFlags`, `XAssetHeader`, `XAsset`, `XAssetList`, `ScriptStringList`, `DB_AssetEntry`, `DB_AssetEntryTable`, `TriggerModel`, `TriggerHull` | TransientInfo: 184 here, 216 in game-test. Load_TransientInfo, Load_TransientSets, Load_TransientCosts, Load_StreamingInfo, Load_AlwaysloadedFlagSet, Load_AlwaysloadedFlags, Load_XAssetHeader, Load_XAsse |
| A036 | core game state | `cg_t` [0x0-0x49D70) | cg_t: 620656 here, 807600 in game-test. cg_t::cg_t |
| A037 | core game state | `cg_t` [0x49D70-0x59F5C) | cg_t: 620656 here, 807600 in game-test. cg_t::cg_t |
| A038 | core game state | `cg_t` [0x59F5C-0x66AB8) | cg_t: 620656 here, 807600 in game-test. cg_t::cg_t |
| A039 | core game state | `cg_t` [0x66AB8-0x97870) | cg_t: 620656 here, 807600 in game-test. cg_t::cg_t |
| A040 | core game state | `ViewMotionSpring`, `XCamData`, `shellshock_t`, `tmat43_t_vec3_t`, `WeaponHand`, `vector4`, `XCamValues`, `XCamFrame` | ViewMotionSpring: 256 here, 256 in game-test. cg_t::cg_t, XCamData::IsActive, PM_BeginWeaponChange |

## Wave 5

| Agent | Area | Structs (range) | Starting evidence |
|---|---|---|---|
| A041 | core game state | `characterInfo_t` | characterInfo_t: 13728 here, 14792 in game-test. LUIElement_PlayerNameplates_Render |
| A042 | core game state | `lerpFrame_t`, `MountWorldmodelDerivedProperties`, `VehicleWorldmodelAAnim`, `playerASMData_t`, `characterAnimData_s`, `BGMovingPlatformPlayerAnim`, `playerASMCustomNodeCache`, `pmove_t` | lerpFrame_t: 88 here, 88 in game-test. BG_PlayerASM_ApplyAnim, BG_PlayerAnimation, BGVehicles::PhysicsUpdateVehicleAnimationParams |
| A043 | core game state | `pml_t`, `MantleResults`, `ContextMountPMLocalState`, `MantleDebugDrawData`, `trace_t`, `BgWeaponMap`, `GWeaponMap`, `GScrWeaponData` | pml_t: 704 here, 704 in game-test. PM_SlideMove, Mantle_CanMantle, BgMissile::BounceMissile |
| A044 | core game state | `DObj`, `DObjModelBits`, `DSkel`, `DSkelPartBits`, `XAnimTree`, `XAnim_s`, `XAnimInfo`, `XAnimState` | DObj: 336 here, 336 in game-test. ScriptableCl_UpdateEvent, DObjCloneWithClientFlag, DObjApplyPreviousSkelPose |
| A045 | core game state | `sentient_s`, `tacpoint_ref_t`, `actor_s` (base `ai_scripted_t`) | sentient_s: 184 here, 184 in game-test. Bot_ForceClaimNode, tacpoint_ref_t::IsDefined, AIActorInterface::Free |
| A046 | core game state | `ai_scripted_t` [0x0-0x968) | ai_scripted_t: 3664 here, 3664 in game-test. AIScriptedInterface::DoMove |
| A047 | core game state | `ai_scripted_t` [0x968-0xE50), `ai_script_goal_t`, `ai_agent_t` (base `ai_scripted_t`), `AIScriptedInterface`, `AIActorInterface`, `AIAgentInterface`, `AICommonInterface` | ai_scripted_t: 3664 here, 3664 in game-test. AIScriptedInterface::DoMove, AIScriptedInterface::SetDefaults, AIAgentInterface::GetScriptedAgentInfo |
| A048 | core game state | `msg_t`, `clientInfo_t`, `SvClient`, `SvClientMP`, `CmdPredict`, `DLogTDigest_100_8` | msg_t: 56 here, 56 in game-test. NetConnection::SendReliable, LUIElement_PlayerNameplates_Render, SV_SnapWorkersMP_InitSnapshotBuildWorkers |
| A049 | core game state | `SavedPlayerState`, `CmdPredictExtrap`, `SvClientSP`, `ServerCommandsSP`, `hudelem_t` | SavedPlayerState: 224 here, 224 in game-test. BG_PlayerExtrap_SavePlayerState, SvClientSP::IsLocalClient, MSG_ReadDeltaHudElems |
| A050 | core game state | `SvGameGlobals`, `scriptTimeStats_t`, `ServerProfileTimes`, `serverSnapshot_t`, `clientSnapshot_t`, `ScoreboardInfo`, `GSnapshotWeaponMap`, `SvClientPredictedOrigin`, `SvSnapshotEntries`, `SvSnapshotStreamSyncData`, `SvSnapshotStreamSyncList` | SvGameGlobals: 312 here, 312 in game-test. SvGameModeAppMP::ServerStart_Spawn, SV_BuildServerSnapshot_AddEntitiesClientsCanSee, SV_SnapshotMP_FindClientDeltaSnapshot |

## Wave 6

| Agent | Area | Structs (range) | Starting evidence |
|---|---|---|---|
| A051 | core game state | `CgSnapshotMP`, `agentState_s`, `WeaponMapEntry`, `ComSnapshotStreamSync`, `bitarray_384`, `BgScriptedCameraState`, `ComSnapshotStreamSyncList`, `ComSnapshotStreamSyncItem` | CgSnapshotMP: 763992 here, 763992 in game-test. CG_SnapshotMP_SetNextSnap, MSG_ReadDeltaAgent, CL_ParseMP_ParsePacketWeapons |
| A052 | core game state | `CgSnapshotSP`, `OmnvarGameData`, `ClSnapshot`, `game_hudelem_t`, `pathnode_t`, `pathnode_constant_t`, `pathnode_transient_t` | CgSnapshotSP: 74584 here, 74584 in game-test. CgSnapshotSP::GetServerPing, G_SaveLevelState, CL_ParseMP_ParseSnapshot |
| A053 | core game state | `bot_data_t`, `bot_match_data_t`, `bot_debug_data_t`, `bot_goal_t`, `bot_mem_t` | bot_data_t: 261536 here, 131056 in game-test. Bot_UpdateRiotShieldDesired, AIBotInterface::GetScriptGoal |
| A054 | core game state | `bot_info_t` [0x0-0x6DBC) | bot_info_t: 29432 here, 29432 in game-test. AIBotInterface::SetBot |
| A055 | core game state | `bot_info_t` [0x6DBC-0x72F8) | bot_info_t: 29432 here, 29432 in game-test. AIBotInterface::SetBot |
| A056 | core game state | `bot_enemy_track_t`, `AimAssistSharedGlobals`, `bot_sentient_info_t`, `ai_search_t`, `bot_score_parms_s`, `bot_debug_score_t`, `bot_error_loc_t` | bot_enemy_track_t: 136 here, 136 in game-test. Scr_GetPredictedEntityPosition, Bot_Score_ScoreNodesInitParms |
| A057 | core game state | `bot_debug_hud_string_t`, `AimTweakables`, `AimScreenTarget`, `bot_sight_threshold_t`, `bot_mem_event_t` | bot_debug_hud_string_t: 84 here, 84 in game-test. Bot_PredictEnemyCampSpots |
| A058 | renderer | `GfxBackEndData` | GfxBackEndData: 11144708 here, 7711488 in game-test. RB_DynamicLightsets_Update |
| A059 | renderer | `GfxReflectionProbeRelightingBackEndData`, `GfxWorldTransientDrawContext`, `GfxViewInfo` | GfxReflectionProbeRelightingBackEndData: 60 here, 60 in game-test. R_SetupSphereStaticGeo, R_GenerateSortedDrawSurfs |
| A060 | renderer | `GfxSceneDef`, `GfxThermalParams`, `GfxScreenSpaceShadows`, `GfxEyeVirtualHighlight`, `FastCriticalSection`, `GfxDepthScanEffectParams`, `GfxDust`, `GfxFog` | GfxSceneDef: 32 here, 32 in game-test. R_RenderFrustumLightsTileBuffer, StreamFrontendGlob::StreamFrontendGlob |

## Wave 7

| Agent | Area | Structs (range) | Starting evidence |
|---|---|---|---|
| A061 | renderer | `GfxBRCircle`, `GfxDepthOfFieldPhysical`, `GfxLensProfile`, `GfxDroneCameraEffects`, `GfxFilm` | GfxBRCircle: 168 here, 168 in game-test.  |
| A062 | renderer | `GfxStageInfo`, `GfxHeightfield`, `GfxCompositeFx`, `GfxAnalogEffects`, `GfxSSAO`, `GfxPostAA`, `GfxVignette`, `GfxVolumeLightScatter`, `GfxVolumetrics` | GfxStageInfo: 48 here, 48 in game-test. varGfxHeightfield |
| A063 | renderer | `GfxFrustumGrid`, `GfxColorGrading`, `GfxSceneRtInput`, `R_RT_ColorHandle`, `GfxMotionBlur`, `GfxRadialMotionBlur`, `GfxTonemap` (base `TonemapControls`) | GfxFrustumGrid: 36 here, 36 in game-test. R_Blur_Apply, CG_Main_InitRadialMotionBlur |
| A064 | renderer | `PerceptualControls`, `GfxScreenSpaceSSS`, `GfxHudOutlineState`, `GfxShieldEffectState`, `GfxVariableRateShading`, `GfxViewportFeatures`, `GfxTaskGraphViewInfo`, `R_RT_DepthHandle`, `TempThreadPriority` | PerceptualControls: 8 here, 8 in game-test. R_RenderScene, R_GenerateSortedDrawSurfs, R_DrawSunshadow_DrawCacheForAllCascades |
| A065 | renderer | `GfxFogSplineBlendEntry`, `Stage`, `GfxColorGradingClutSet`, `GfxColorGradingAnalytical`, `R_RT_BufferHandle`, `R_RT_Group`, `SSSSProfile`, `GfxShieldEffectState__GfxShieldHit`, `R_RT_Handle`, `R_RT_Tracking_HandleInfo` | GfxFogSplineBlendEntry: 24 here, 24 in game-test. Load_StageArray, R_SpotShadow_Blur, R_DrawSunshadow_DrawTranslucent |
| A066 | renderer | `refdef_t`, `RefdefView`, `RimLightingControls` | refdef_t: 69536 here, 69536 in game-test. R_RenderScene, CG_Camera_Vehicles_Update |
| A067 | renderer | `GfxDepthOfFieldFilter`, `RadiantLight`, `RadiantLightAnim`, `TonemapControls`, `RadiantLightEffect` | GfxDepthOfFieldFilter: 32 here, 32 in game-test.  |
| A068 | renderer | `GfxCmdBufState` (base `ComputeCmdBufState`), `GfxDeviceState`, `GfxCmdBufSourceState`, `GfxCmdBufInput` | GfxCmdBufState: 82880 here, 8816 in game-test. R_RenderFrustumLightsClusterBuffer, RBT_Tonemap_Filter_Common |
| A069 | renderer | `GfxCodeSceneConsts` [0x0-0x900) | GfxCodeSceneConsts: 3584 here, 3584 in game-test.  |
| A070 | renderer | `GfxCodeSceneConsts` [0x900-0xE00), `ComputeCmdBufState`, `GfxSceneEntity`, `SecurePlacement` | GfxCodeSceneConsts: 3584 here, 3584 in game-test. R_InitComputeCmdBufState, R_SkinSceneDObjModels |

## Wave 8

| Agent | Area | Structs (range) | Starting evidence |
|---|---|---|---|
| A071 | renderer | `GfxMeshData`, `GfxVertexBufferState`, `GfxDescriptorState`, `GfxDevice`, `GfxTaskInfo` | GfxMeshData: 12672 here, 1016 in game-test. R_InitCachedShadowBuffers, R_CheckReserveDescriptorHeaps, R_RebindDescriptorState |
| A072 | renderer | `visionSetVars_t` [0x0-0x2C4) | visionSetVars_t: 1196 here, 1196 in game-test. CG_VisionSetLoadVisionSettingsFromBuffer |
| A073 | renderer | `visionSetVars_t` [0x2C4-0x4AC), `visionSetBlend_t` (base `visionSetVars_t`), `visionSetBlendLerpInfo_t` | visionSetVars_t: 1196 here, 1196 in game-test. CG_VisionSetLoadVisionSettingsFromBuffer, CG_VisionSetBuildBlendTree |
| A074 | renderer | `ParticleManager`, `ntl__fixed_hash_map_unsigned_int_orientation_t_4096_4099_ntl__hash_unsigned_int__ntl__equal_to_unsigned_int`, `ParticleManager__Particle_SoundData`, `IndexedDataPool_FxMarkSpawnData_512_90`, `FxAccessLock`, `ParticleSystemHandleManager`, `FxAccessLock___unnamed_type_m` | ParticleManager: 2501248 here, 314624 in game-test. ParticleManager::AddSystem, FX_WaitEnterWriteSystemLock |
| A075 | renderer | `ParticleState`, `ParticleModuleGraphLengthData`, `ParticleAtlasData`, `ComputeShader`, `ComputeShaderProgram`, `GfxComputeShaderLoadDef` | ParticleState: 432 here, 416 in game-test. ParticleState::AddModule, FX_GenParticleSimAnimationVerts, Load_ComputeShader, varComputeShaderProgram, Load_GfxComputeShaderLoadDef |
| A076 | game systems | `CgGlobalsMP` (base `cg_t`), `CgGlobalsSP` (base `cg_t`), `ClStatic`, `ClLocalClientActive` | CgGlobalsMP: 747296 here, 955184 in game-test. LUIElement_PlayerNameplates_Render, CgGlobalsSP::GetCharacterInfo, ClStatic::SetGameMapName |
| A077 | game systems | `ClActiveClientMP` | ClActiveClientMP: 153024 here, 153024 in game-test. ClActiveClientMP::AllocSnapshotMemory |
| A078 | game systems | `ClActiveClientSP`, `BgStatic`, `CgWeaponSystem`, `CgEntityWork` | ClActiveClientSP: 56040 here, 56040 in game-test. CL_InputSP_ExecBinding, BgStatic::BuildCharacterModelList, CgEventSystem::HandleEntityEventCommon |
| A079 | game systems | `CgCompassSystemMP`, `CgClientSideEffectsSystem`, `ClientSideRoot` | CgCompassSystemMP: 64280 here, 64280 in game-test. CgCompassSystemMP::DrawPlayerMP, CG_MainMP_FreeClientMemory |
| A080 | game systems | `BgVehiclePhysics`, `BgVehiclePhysicsLinkedPlayers_2`, `BgVehiclePhysicsDeferredActionManager`, `BgVehiclePhysicsControls`, `BgVehiclePhysicsHistory` | BgVehiclePhysics: 1960 here, 728 in game-test. GVehicles::PhysicsOnWorldAction, BgVehiclePhysics::StartPlayerControlling, BgVehiclePhysics::ResetInputControls |

## Wave 9

| Agent | Area | Structs (range) | Starting evidence |
|---|---|---|---|
| A081 | game systems | `BgVehiclePhysicsGround` (base `BgVehiclePhysics`), `BgVehiclePhysicsGround__Wheel`, `BgVehiclePhysicsGround__WheelCommon`, `BgVehiclePhysicsGround__ControlsEx`, `BgVehiclePhysicsGround__ChassisNoise`, `BgVehiclePhysicsGround___unnamed_type_m_friction` | BgVehiclePhysicsGround: 3240 here, 3240 in game-test. BgVehiclePhysicsGround::Setup |
| A082 | game systems | `BgVehiclePhysicsGround__Steering`, `BgVehiclePhysicsGround__Engine`, `BgVehiclePhysicsGround__SoundData`, `BgVehiclePhysicsGround__VfxData`, `BgVehiclePhysicsGround__FrictionCar`, `BgVehiclePhysicsWaterBuoyancy` (base `BgVehiclePhysics`), `BgVehiclePhysicsWaterBuoyancy__BuoyancySphere`, `BgVehiclePhysicsRevSound`, `BgVehiclePhysicsWaterBuoyancy__VfxData` | BgVehiclePhysicsGround__Steering: 72 here, no game-test type of that name. BgVehiclePhysicsWaterBuoyancy::StopPlayerControlling, BgVehiclePhysicsRevSound::Reset |
| A083 | game systems | `Vehicle`, `VehiclePathPos`, `vehicle_physic_t`, `VehicleUsableParts` | Vehicle: 2304 here, 2248 in game-test. G_UtilsMP_DObjUpdate_Internal, G_VehiclePath_UpdatePathPos, G_VehicleScriptCmd_GetAttachPos |
| A084 | game systems | `HeliPathPos`, `VehicleTurret`, `VehicleJitter`, `VehicleOrientTo`, `VehicleTags` | HeliPathPos: 752 here, 752 in game-test. G_VehicleHeli_PathSetAirResistance, VehicleTurret::VehicleTurret |
| A085 | game systems | `BGVehicleInfo`, `VehiclePathNode`, `HelicopterHover`, `BGVehicleInfo___unnamed_type_procedural`, `TerrainModeProceduralAnimInfo`, `GVehicles`, `BgVehiclePhysicsManager`, `GVehicleEventSystem`, `GVehicleSuspendManager`, `GVehicleCollisionSystem`, `VehicleScriptableDmg`, `BgVehiclePhysicsObjectBuffer`, `GVehicleSuspendManager__SuspendInfo` | BGVehicleInfo: 296 here, 296 in game-test. BG_VehicleAnim_Reset, G_VehiclePathCmd_GetNode, GVehicles::PhysicsCrash |
| A086 | game systems | `AINavigator`, `AITask_ReevalPath`, `AINavigator2D` (base `AINavigator`), `nav_path_t`, `bfx__PathSpec`, `PathBlockPlanes`, `nav_path_t__pathpoint_t`, `bfx__PenaltyTable`, `AINavigator3D` (base `AINavigator`), `nav_cornerdata_3D_t`, `BgMissile` | AINavigator: 200 here, 200 in game-test. AINavigator::AINavigator, AINavigator::ClearPath, AINavigator2D::TraceFromCurPos |
| A087 | game systems | `GMissile`, `GBullet`, `GTurret`, `GTurretData`, `GTurretMP` | GMissile: 24 here, 24 in game-test. GMissile::GlassRestorePiece, GBullet::GBullet, GTurret::AimAtEntity |
| A088 | game systems | `hknpWorld`, `hknpBodyManager`, `hknpMotionManager`, `hkBlockStream`, `hknpCollisionFlags` | hknpWorld: 2992 here, 2992 in game-test. NpSimulationSnapshot::importSimulationState, importBodiesAndBroadphase, exportMotions |
| A089 | game systems | `hknpShape`, `Ragdoll`, `RagdollMoverContactList`, `RagdollState`, `RagdollMoverContactInfo`, `BoneOrientation`, `Bone`, `RagdollPoseController`, `PhysicsConstraintLooseningResult`, `RagdollPoseControllerBoneState`, `PhysicsLoosenableConstraintSetup`, `lua_State` | hknpShape: 56 here, 56 in game-test. HavokPhysics_CollisionQueryDispatcher::initIWGetClosestPointsTable, Ragdoll_SaveRagdoll |
| A090 | game systems | `LUIElement`, `LUIAnimationState`, `LUIElementImageData`, `LUIElementPosition`, `LUIElementAxisPosition`, `MemoryFile`, `SaveGame`, `SaveHeader`, `qtime_s` | LUIElement: 320 here, 320 in game-test. LUIElement_Grid_MoveLineFromEndToStart, LUIElement_Grid_OverrideAnimationState, LUI_Measure |

## Wave 10

| Agent | Area | Structs (range) | Starting evidence |
|---|---|---|---|
| A091 | game systems | `HunkUser`, `TaskManager`, `TaskItem`, `GenericTask`, `NetConnection`, `NetEndpoint`, `bdDTLSAssociationTelemetry`, `bdAddr`, `bdSockAddr`, `bdRelayRoute`, `ASM_Instance`, `ASM_Context`, `BehaviorTree`, `PlayerASM_Context`, `PlayerASM_Instance` (base `ASM_Instance`) | HunkUser: 72 here, 72 in game-test. Mem_HunkUser_Create, TaskManager::Update, varBehaviorTree |
| A092 | online | `PartyData`, `MigrateData`, `ntl__fixed_hash_map_unsigned___int64_int_202_389_ntl__hash_unsigned___int64__ntl__equal_to_unsigned___int64` | PartyData: 373704 here, 380336 in game-test. UI_TogglePlayerMute |
| A093 | online | `PartyMember`, `PartyType`, `bitarray_256`, `PartyStateData`, `SceNpSessionId`, `MPSDSessionData`, `BestHostData`, `MigrationHostInfo`, `ClientAuthoritativeMemberInfo`, `NomineeInfo`, `netadr_t` | PartyMember: 504 here, 504 in game-test. TeamBalance_MakeTeamsFromDemonware, Party_StopPartyInternal, PartyClient_PreParsePartyState |
| A094 | online | `OnlineMatchmakerOmniscient`, `ntl__fixed_list_OnlineMatchmakerOmniscient__Player_200_0`, `bdStringResult`, `Qos`, `ntl__fixed_list_tQosEntry_25_0`, `MatchMakerOmniLobbyData`, `ntl__fixed_list_XUID_200_0`, `ntl__fixed_list_PlayerValue_enum_MemberStatus__200_0`, `bdDataCenterPreferences`, `MatchMakerOmniLobbyData__DvarOverride` | OnlineMatchmakerOmniscient: 1077400 here, 1077544 in game-test. PartyAtomicHost_HandleJoinRequest, DWAsyncMatchMaking::initiateDCQoS, Online_DcQos_TryFinalizeResults |
| A095 | online | `DWWrappers`, `DWStorage`, `DWVoteRank`, `DWKeyArchive`, `DWUCD`, `DWStats`, `DWMarketingComms`, `DWUmbrella`, `DWUno`, `DWRedeemableCodes`, `DWAuthService`, `DWCrossPlatformIdentity`, `DWABTesting`, `ntl__fixed_list_BaseDWWrapper___7_0`, `ntl__fixed_vector_DWStorageContext_5_0`, `DWStorageContext`, `ntl__fixed_string_16` | DWWrappers: 17656 here, 17656 in game-test. DWWrappers::isReady, DWWrappers::init, DWUCD::init |
| A096 | online | `DWServicesAccess`, `ntl__fixed_vector_DWWrappers_8_0`, `DWPlatformInfo`, `DWGameMetrics`, `DWStandaloneUmbrella`, `DWDataChannelDispatch`, `DWDataChannelManagerStandard`, `DWLogin`, `bdUmbrellaUserAccount`, `bdEnvironment`, `bdUmbrellaAccessInfo`, `bdUmbrellaProviderAccount` | DWServicesAccess: 163368 here, 163368 in game-test. OnlineStorage::ExecuteTask, DWGameMetrics::pump, DWStandaloneUmbrella::pump |
| A097 | online | `OnlineClan`, `bdClansGetGroupInfosResponse`, `bdClansGetGroupMembersResponse`, `bdClansGetMembershipProposalsByGroupResponse`, `bdClansProposeMembershipsResponse`, `bdClansRemoveProposalsResponse`, `bdClansAddOrUpdateGroupMemberResponse`, `bdClansRemoveGroupMemberResponse`, `bdClansGetGroupFileResponse`, `bdClansUploadGroupFileResponse`, `bdChannelCreateChannelResponse`, `OnlineClan__RemoveData`, `bdStructFixedSizeString_127`, `bdStructUserDetails`, `bdStructFixedSizeString_255`, `bdClansGroupIdentifier`, `bdStructOptionalObject_bdClansGroupMembershipProposal`, `bdClansGroupFile`, `bdChannelInfo`, `bdClansGroupMembershipProposal` | OnlineClan: 3984 here, 3984 in game-test. OnlineClan::Init, FetchClanInfosComplete, OnlineClan::FetchClanMembersCallback |
| A098 | online | `Online_Friends`, `FriendListGlobal`, `Online_Loot`, `Online_Retry`, `BG_SpawnGroup_Loot_Table`, `BG_SpawnGroup_Loot_ItemDef`, `BG_SpawnGroup_Loot_SetItem`, `ObjectiveSettings` | Online_Friends: 1181944 here, 1181944 in game-test. LUI_CoD_LuaCall_GetFriendsData_impl, LUI_CoD_LuaCall_GetMixedFriendsData_impl, Online_Loot::UpdateInventoryFromDWDurable |
| A099 | online | `OnlineChallengesManager`, `bdGetAchievementStatesResponse`, `bdStructFixedSizeArray_bdAchievementState_100`, `bdStructFixedSizeString_1024`, `bdAchievementState`, `OnlineStorage`, `OnlineStorageFileResultBuffer`, `DLogContext`, `UserList`, `bdRemoveUsersFromUserListResponse` | OnlineChallengesManager: 175816 here, 257728 in game-test. OnlineChallengesManager::ProcessParams, DWAchievementEngine::getAchievementStates, OnlineScheduledChallenges::AddProgression |
| A100 | online | `InviteJoinHSM`, `InvitationData`, `OnlineJoinInfo`, `UserIdResolver_1`, `OnlineCommuncationInfo`, `XSESSION_INFO`, `dwLogOnHSM_base` | InviteJoinHSM: 800 here, 800 in game-test. InviteJoinHSM::HandleSessionSearchCompleteLobbyNotFound, Online_InvitationManager::SendJoinInfoToUser, XSESSION_INFO::GetAddress |

## Shared structs (split by byte range)

- `playerState_s`: A004, A005
- `WeaponDef`: A016, A017, A018, A019, A020
- `WeaponAttachment`: A021, A022
- `VehicleDef`: A032, A033, A034
- `cg_t`: A036, A037, A038, A039
- `ai_scripted_t`: A046, A047
- `bot_info_t`: A054, A055
- `GfxCodeSceneConsts`: A069, A070
- `visionSetVars_t`: A072, A073

## Backlog (not in the 100)

- hkAabb24_16_24_Codec, hknpSolverInfo, hknpWorldSignals (Havok SDK internals)
- hknpTimerVolumeTags, hkBitField, hknpThreadSafeObjectPool_hknpBody_hknpBody__FreeListOperations, hkArray_hknpBodyId_hkContainerHeapAllocator, hkArray_hkAabb24_16_24_hkContainerHeapAllocator, hkArray_hknpBodyManager__MotionInfo_hkContainerHeapAllocator, hkArray_hknpMassDistribution___hkContainerHeapAllocator, hkArray_hknpDragProperties___hkContainerHeapAllocator ... (Havok SDK internals)
- hknpWorldSignals__BodiesAddedSignal, hknpWorldSignals__BodiesRemovedSignal, hknpWorldSignals__BodiesDestroyedSignal, hknpWorldSignals__BodyWarpedSignal, hknpWorldSignals__MotionBufferFullSignal, hknpWorldSignals__MotionBufferWillChangeSignal, hknpWorldSignals__MotionBufferChangedSignal, hknpWorldSignals__MotionAllocatedSignal ... (Havok SDK internals)

Everything else (about 3,600 smaller or unused structs) is not assigned yet; rerun the review after wave 10.
