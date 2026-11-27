#pragma once
#include <cstddef>
#include <cstdint>

// Integrated into the build (was reference/iw8_map_structs_pinned.h). Namespaced under iw8::maps so
// the pinned field maps + static_asserts compile-validate the load-critical sizes/offsets WITHOUT
// colliding with the offset-stamp constants in map_zone.h. The map zone emitter cross-references
// these offsets.
namespace iw8
{
namespace maps
{

#pragma pack(push, 1)

// ----- shared scalar helpers -----
struct vec2_t
{
    float v[2];
};
struct vec3_t
{
    float v[3];
};
struct Bounds
{
    vec3_t midPoint;
    vec3_t halfSize;
}; // 24

// ===========================================================================================
// clipMap_t  — col_map (23) — g_assetSizes 0xF8 (248). CONFIRMED(PDB), dev==retail.
// ===========================================================================================
struct MapTriggers
{                   // 0x50 (reused in clipMap_t.stageTrigger and MapEnts.trigger)
    uint32_t count; // 0x00
    uint32_t _pad04;
    void *models;       // 0x08  TriggerModel*   [count]
    uint32_t hullCount; // 0x10
    uint32_t _pad14;
    void *hulls;        // 0x18  TriggerHull*    [hullCount]
    uint32_t slabCount; // 0x20
    uint32_t _pad24;
    void *slabs;           // 0x28  TriggerSlab*    [slabCount]
    uint32_t windingCount; // 0x30
    uint32_t _pad34;
    void *windings;             // 0x38  TriggerWinding* [windingCount]
    uint32_t windingPointCount; // 0x40
    uint32_t _pad44;
    void *windingPoints; // 0x48  TriggerWindingPoint* [windingPointCount]
};
static_assert(sizeof(MapTriggers) == 0x50, "MapTriggers");

struct TriggerModel
{
    uint32_t contents;
    uint16_t hullCount;
    uint16_t firstHull;
    uint16_t windingCount;
    uint16_t firstWinding;
    uint32_t flags;
    void *physicsAsset;
    uint16_t physicsShapeOverrideIdx;
    uint8_t _pad1A[6];
};
static_assert(offsetof(TriggerModel, physicsAsset) == 0x10, "TriggerModel.physicsAsset");
static_assert(sizeof(TriggerModel) == 0x20, "TriggerModel");

struct TriggerHull
{
    Bounds bounds;
    uint32_t contents;
    uint16_t slabCount;
    uint16_t firstSlab;
};
static_assert(sizeof(TriggerHull) == 0x20, "TriggerHull");

struct TriggerSlab
{
    vec3_t direction;
    float midpoint;
    float halfSize;
};
static_assert(sizeof(TriggerSlab) == 0x14, "TriggerSlab");

struct cmodel_t
{                  // 0x38
    Bounds bounds; // 0x00
    float radius;  // 0x18
    uint32_t _pad1C;
    void *physicsAsset;               // 0x20  PhysicsAsset* (Havok)
    uint16_t physicsShapeOverrideIdx; // 0x28
    uint16_t navObstacleIdx;          // 0x2A
    uint32_t edgeFirstIndex;          // 0x2C
    uint32_t edgeTotalCount;          // 0x30
    uint32_t _pad34;
};
static_assert(sizeof(cmodel_t) == 0x38, "cmodel_t");

struct Stage
{
    const char *name;             // 0x00
    vec3_t origin;                // 0x08
    uint16_t triggerIndex;        // 0x14
    uint8_t sunPrimaryLightIndex; // 0x16
    uint8_t _pad17;
    uint32_t entityUID;           // 0x18
    vec3_t skyRotationAngles;     // 0x1C
};
static_assert(offsetof(Stage, sunPrimaryLightIndex) == 0x16, "Stage.sunPrimaryLightIndex");
static_assert(sizeof(Stage) == 0x28, "Stage");

struct StaticModelCollisionModelList
{                                           // 0x18
    uint32_t numModels;                     // 0x00
    uint32_t _pad04;
    void *models;                            // 0x08  StaticModelCollisionModel*
    uint32_t structureMemoryCost;            // 0x10
    uint32_t _pad14;
};
static_assert(offsetof(StaticModelCollisionModelList, models) == 0x08,
              "StaticModelCollisionModelList.models");
static_assert(offsetof(StaticModelCollisionModelList, structureMemoryCost) == 0x10,
              "StaticModelCollisionModelList.structureMemoryCost");
static_assert(sizeof(StaticModelCollisionModelList) == 0x18,
              "StaticModelCollisionModelList");

struct StaticModelCollisionModel
{                                      // 0x28
    uint32_t name;                   // 0x00  scr_string_t
    uint32_t _pad04;
    void *physicsAsset;              // 0x08  PhysicsAsset*
    void *detailCollision;           // 0x10  XModelDetailCollision*
    uint32_t numInstances;           // 0x18
    uint32_t _pad1C;
    void *instances;                // 0x20  StaticModelCollisionInstance*
};
static_assert(offsetof(StaticModelCollisionModel, physicsAsset) == 0x08,
              "StaticModelCollisionModel.physicsAsset");
static_assert(offsetof(StaticModelCollisionModel, detailCollision) == 0x10,
              "StaticModelCollisionModel.detailCollision");
static_assert(offsetof(StaticModelCollisionModel, instances) == 0x20,
              "StaticModelCollisionModel.instances");
static_assert(sizeof(StaticModelCollisionModel) == 0x28, "StaticModelCollisionModel");

struct StaticModelCollisionInstance
{                      // 0x1C
    vec3_t origin;     // 0x00
    vec3_t angles;     // 0x0C
    float scale;       // 0x18
};
static_assert(offsetof(StaticModelCollisionInstance, angles) == 0x0C,
              "StaticModelCollisionInstance.angles");
static_assert(offsetof(StaticModelCollisionInstance, scale) == 0x18,
              "StaticModelCollisionInstance.scale");
static_assert(sizeof(StaticModelCollisionInstance) == 0x1C,
              "StaticModelCollisionInstance");

struct clipMap_t
{                                               // 0xF8
    const char *name;                           // 0x00  PTR
    int32_t isInUse;                            // 0x08
    uint32_t numStaticModelCollisionModelLists; // 0x0C  CNT
    void *staticModelCollisionModelLists;       // 0x10  PTR[0x0C]
    void *mapEnts;                              // 0x18  PTR  (MapEnts*, cross-link)
    Stage *stages;                              // 0x20  PTR[stageCount]
    uint8_t stageCount;                         // 0x28
    uint8_t _pad29[7];                          //
    MapTriggers stageTrigger;                   // 0x30  (0x50)
    vec3_t broadphaseMin;                       // 0x80
    vec3_t broadphaseMax;                       // 0x8C
    uint8_t physicsCapacities[32];              // 0x98  PhysicsCapacities
    uint32_t havokWorldShapeDataSize;           // 0xB8  CNT
    uint32_t _padBC;
    void *havokWorldShapeData;           // 0xC0  PTR[0xB8] (null = NO collision)
    uint32_t numCollisionHeatmapEntries; // 0xC8  CNT
    uint32_t _padCC;
    void *collisionHeatmap;            // 0xD0  PTR[0xC8]
    uint32_t totalGlassInitPieceCount; // 0xD8
    uint32_t totalGlassPieceLimit;     // 0xDC
    void *topDownMapData;              // 0xE0  PTR
    const char *visionSetName;         // 0xE8  PTR
    uint32_t checksum;                 // 0xF0
    uint32_t _padF4;
};
static_assert(offsetof(clipMap_t, mapEnts) == 0x18, "clipMap_t.mapEnts");
static_assert(offsetof(clipMap_t, broadphaseMin) == 0x80, "clipMap_t.broadphaseMin");
static_assert(offsetof(clipMap_t, havokWorldShapeDataSize) == 0xB8, "clipMap_t.havokSize");
static_assert(offsetof(clipMap_t, havokWorldShapeData) == 0xC0, "clipMap_t.havokData");
static_assert(offsetof(clipMap_t, checksum) == 0xF0, "clipMap_t.checksum");
static_assert(sizeof(clipMap_t) == 0xF8, "clipMap_t == g_assetSizes[23]");

// ===========================================================================================
// MapEnts — map_ents (29) — Replay 1.20 loader size 0x408.
// Later IW8 PDBs extend this type to 0x428 with audio-edge and collmap fields; those fields are not
// part of the 1.20 wire body. The dynentitylist is inline at 0x190..0x1EB.
// ===========================================================================================
struct SpawnPointRecordList
{
    uint32_t count;
    uint32_t _pad;
    void *records;
}; // 0x10

struct MapEnts
{                           // 0x408
    const char *name;       // 0x00  PTR
    char *entityString;     // 0x08  PTR[0x10] ⭐
    int32_t numEntityChars; // 0x10  CNT
    uint32_t _pad14;
    MapTriggers trigger;             // 0x18  (0x50)
    uint8_t clientTrigger[176];      // 0x68  ClientTriggers (inline; ptrs count-gated)
    uint8_t clientTriggerBlend[16];  // 0x118
    SpawnPointRecordList spawnList;  // 0x128 (0x10)
    uint8_t splineList[16];          // 0x138 SplineRecordList {count, PTR}
    uint32_t havokEntsShapeDataSize; // 0x148 CNT
    uint32_t _pad14C;
    char *havokEntsShapeData; // 0x150 PTR[0x148]
    uint32_t numSubModels;    // 0x158 CNT
    uint32_t _pad15C;
    void *cmodels;                   // 0x160 PTR[0x158] (cmodel_t*)
    uint32_t edgeListUsedQueryTypes; // 0x168
    uint32_t numEdgeLists;           // 0x16C CNT
    void *edgeLists;                 // 0x170 PTR[0x16C] (MapEdgeList**)
    void *edgeListSpatialTree;       // 0x178 PTR
    uint32_t numClientModels;        // 0x180 CNT
    uint32_t _pad184;
    void *clientModels;           // 0x188 PTR[0x180]
    uint32_t dynEntCount[2];      // 0x190
    uint32_t dynEntCountTotal;    // 0x198
    uint16_t dynEntityListsCount; // 0x19C
    uint16_t _pad19E;
    void *dynEntListIds;                   // 0x1A0 PTR
    uint16_t dynEntPhysicsSetupHead[2][2]; // 0x1A8
    uint16_t dynEntPhysicsSetupTail[2][2]; // 0x1B0
    uint16_t dynEntNoSpatialCount[2];      // 0x1B8
    uint16_t dynEntMaxClientHistoryCount;  // 0x1BC
    uint8_t dynEntMaxPosePartCount;        // 0x1BE
    uint8_t _pad1BF;
    uint32_t dynEntsWithExtraPosePartsCount; // 0x1C0
    uint32_t _pad1C4;
    void *dynEntSpatialPopulation[2];   // 0x1C8 PTR×2
    void *dynEntSpatialTransientMap[2]; // 0x1D8 PTR×2
    uint32_t dynEntNoSpatialList;       // 0x1E8
    uint32_t clientEntAnchorCount;      // 0x1EC CNT
    void *clientEntAnchors;             // 0x1F0 PTR[0x1EC]
    uint8_t scriptableMapEnts[216];     // 0x1F8 (inline; own PTR/CNT)
    uint8_t spawnGroupLoot[48];         // 0x2D0
    uint8_t clientSideEffects[0x78];    // 0x300 ClientSideEffects (120 B in Replay 1.20)
    uint8_t createFxAssetData[0x10];    // 0x378 CreateFxAssetData
    uint32_t exploderNameTotal;         // 0x388 CNT
    uint32_t _pad38C;
    void *exploderNames;                // 0x390 PTR[0x388] (scr_string_t*)
    uint8_t serverSideEffects[0x10];    // 0x398 ServerSideEffects
    uint32_t createFxEffectTotal;       // 0x3A8
    uint32_t numMayhemScenes;           // 0x3AC CNT
    void *mayhemScenes;                 // 0x3B0 PTR[0x3AC] (MayhemInstance*)
    uint8_t spawners[0x10];             // 0x3B8 SpawnerList {count, PTR}
    uint32_t audioPASpeakerCount;       // 0x3C8 CNT
    uint32_t _pad3CC;
    void *audioPASpeakers;              // 0x3D0 PTR[0x3C8] (AudioPASpeaker*)
    uint32_t numAudioPropNodes;         // 0x3D8 CNT
    uint32_t _pad3DC;
    void *audioPropNodes;               // 0x3E0 PTR[0x3D8] (AudioPropagationNode*)
    uint32_t numAudioPropEdges;         // 0x3E8 CNT
    uint32_t _pad3EC;
    void *audioPropEdges;               // 0x3F0 PTR[0x3E8] (AudioPropagationEdge*)
    uint32_t numCollmaps;               // 0x3F8 CNT
    uint32_t _pad3FC;
    void *collmapLookups;               // 0x400 PTR[0x3F8] (CollMapLookupEntry*)
};
static_assert(offsetof(MapEnts, entityString) == 0x08, "MapEnts.entityString");
static_assert(offsetof(MapEnts, numEntityChars) == 0x10, "MapEnts.numEntityChars");
static_assert(offsetof(MapEnts, trigger) == 0x18, "MapEnts.trigger");
static_assert(offsetof(MapEnts, clientTrigger) == 0x68, "MapEnts.clientTrigger");
static_assert(offsetof(MapEnts, spawnList) == 0x128, "MapEnts.spawnList");
static_assert(offsetof(MapEnts, numSubModels) == 0x158, "MapEnts.numSubModels");
static_assert(offsetof(MapEnts, cmodels) == 0x160, "MapEnts.cmodels");
static_assert(offsetof(MapEnts, clientSideEffects) == 0x300, "MapEnts.clientSideEffects");
static_assert(offsetof(MapEnts, createFxAssetData) == 0x378, "MapEnts.createFxAssetData");
static_assert(offsetof(MapEnts, exploderNames) == 0x390, "MapEnts.exploderNames");
static_assert(offsetof(MapEnts, audioPASpeakerCount) == 0x3C8, "MapEnts.audioPASpeakerCount");
static_assert(offsetof(MapEnts, audioPASpeakers) == 0x3D0, "MapEnts.audioPASpeakers");
static_assert(offsetof(MapEnts, audioPropNodes) == 0x3E0, "MapEnts.audioPropNodes");
static_assert(offsetof(MapEnts, audioPropEdges) == 0x3F0, "MapEnts.audioPropEdges");
static_assert(offsetof(MapEnts, collmapLookups) == 0x400, "MapEnts.collmapLookups");
static_assert(sizeof(MapEnts) == 0x408, "MapEnts == Replay 1.20 loader size");

// ===========================================================================================
// ComWorld  — com_map (24) — g_assetSizes 0xA8 (168). CONFIRMED(PDB), dev==retail.
// ===========================================================================================
struct ComWorld
{                               // 0xA8
    const char *name;           // 0x00 PTR
    int32_t isInUse;            // 0x08
    int32_t useForwardPlus;     // 0x0C
    uint32_t bakeQuality;       // 0x10
    int32_t proxyLODQuality;    // 0x14
    float trVisRadii[5];        // 0x18
    float trVisFacingDistAdd;   // 0x2C
    uint32_t primaryLightCount; // 0x30 CNT
    uint32_t _pad34;
    void *primaryLights;                  // 0x38 PTR[0x30] (ComPrimaryLight* — element 0xA0)
    uint32_t scriptablePrimaryLightCount; // 0x40
    uint32_t firstScriptablePrimaryLight; // 0x44
    uint32_t transientTableSize;          // 0x48 CNT
    uint32_t _pad4C;
    void *transientTable;       // 0x50 PTR[0x48] (u16*)
    uint8_t changeListInfo[16]; // 0x58 ComChangeListInfo
    uint32_t numUmbraGates;     // 0x68 CNT
    uint32_t _pad6C;
    void *umbraGateNames;               // 0x70 PTR[0x68] (const char**)
    uint8_t umbraGateInitialStates[48]; // 0x78 bitarray<384>
};
static_assert(offsetof(ComWorld, primaryLightCount) == 0x30, "ComWorld.primaryLightCount");
static_assert(offsetof(ComWorld, primaryLights) == 0x38, "ComWorld.primaryLights");
static_assert(sizeof(ComWorld) == 0xA8, "ComWorld == g_assetSizes[24]");

// ===========================================================================================
// GfxWorld — gfx_map (31) — Replay 1.20 Load_GfxWorld reads a 0x4590-byte body.
// The prefixes through draw are pinned below. The remaining Replay body stays opaque until each
// field is confirmed from the 1.20 loader.
//
// ⚠ RENDER-SAFETY: an ALL-ZEROS GfxWorld is NOT render-safe — R_SetupDpvsForPoint does
// `for(i=*dpvsPlanes.nodes; ...)` with NO null/count guard, so nodes==NULL => crash. map: either
// OMIT gfx_map, or emit the minimal-VALID form (cellCount=1, 1 GfxCell, nodeCount=1, terminating
// nodes[]). See the .md §5. Do NOT memset-0 and ship.
// ===========================================================================================
struct GfxWorldDpvsPlanes
{                        // 0x28  (render-critical) — CONFIRMED(instance)
    uint32_t cellCount;  // 0x00
    uint16_t planeCount; // 0x04
    uint16_t _pad06;
    void *planes;       // 0x08 PTR (GfxWorldDpvsPlane*)
    uint16_t nodeCount; // 0x10
    uint8_t _pad12[6];
    void *nodes;            // 0x18 PTR (u16*) <- R_SetupDpvsForPoint derefs *nodes
    void *sceneEntCellBits; // 0x20 PTR (u32*)
};
static_assert(sizeof(GfxWorldDpvsPlanes) == 0x28, "GfxWorldDpvsPlanes");

struct GfxCell
{                         // 0x28
    Bounds bounds;        // 0x00
    uint16_t portalCount; // 0x18
    uint8_t _pad1A[6];
    void *portals; // 0x20 PTR[portalCount]
};
static_assert(sizeof(GfxCell) == 0x28, "GfxCell");

struct GfxWorld
{                                                    // 0x4590 (Replay 1.20 verified)
    const char *name;                                // 0x0000 PTR
    const char *baseName;                            // 0x0008 PTR
    uint32_t bspVersion;                             // 0x0010 (=243 in real instances)
    uint32_t lastSunPrimaryLightIndex;               // 0x0014
    uint32_t primaryLightCount;                      // 0x0018
    uint32_t firstMutablePrimaryLight;               // 0x001C
    uint32_t mutablePrimaryLightCount;               // 0x0020
    uint32_t firstStaticScriptablesPrimaryLight;     // 0x0024
    uint32_t staticScriptablesPrimaryLightCount;     // 0x0028
    uint32_t firstStaticScriptablePrimaryLight;      // 0x002C
    uint32_t staticScriptablePrimaryLightCount;      // 0x0030
    uint32_t firstMovingScriptablePrimaryLight;      // 0x0034
    uint32_t movingScriptablePrimaryLightCount;      // 0x0038
    uint32_t sortKeyLitDecal;                        // 0x003C
    uint32_t sortKeyEffectDecal;                     // 0x0040
    uint32_t sortKeyTopDecal;                        // 0x0044
    uint32_t sortKeyEffectAuto;                      // 0x0048
    uint32_t sortKeyDistortion;                      // 0x004C
    uint32_t sortKeyEffectDistortion;                // 0x0050
    uint32_t sortKey2D;                              // 0x0054
    uint32_t sortKeyOpaqueBegin;                     // 0x0058
    uint32_t sortKeyOpaqueEnd;                       // 0x005C
    uint32_t sortKeyDecalBegin;                      // 0x0060
    uint32_t sortKeyDecalEnd;                        // 0x0064
    uint32_t sortKeyTransBegin;                      // 0x0068
    uint32_t sortKeyTransEnd;                        // 0x006C
    uint32_t sortKeyEmissiveBegin;                   // 0x0070
    uint32_t sortKeyEmissiveEnd;                     // 0x0074
    Bounds bounds;                                   // 0x0078 (0x18)
    GfxWorldDpvsPlanes dpvsPlanes;                   // 0x0090 (0x28) — cells/nodes here
    void *cells;                                     // 0x00B8 PTR[dpvsPlanes.cellCount]
    void *cellTransientInfos;                        // 0x00C0 PTR[cellCount]
    uint8_t surfaces[0x88];                          // 0x00C8 GfxWorldSurfaces
    uint8_t smodels[0x4F8];                          // 0x0150 GfxWorldStaticModels
    uint8_t draw[0x3248];                            // 0x0648 GfxWorldDraw (ends 0x3890)
    uint8_t dynamicLightset[0x3A0];                  // 0x3890 GfxDynamicLightset
    uint8_t mayhemSelfVis[0x70];                     // 0x3C30 GfxMayhemSelfVis
    uint8_t frustumLights[0xC0];                     // 0x3CA0 GfxFrustumLights
    void *lightViewFrustums;                         // 0x3D60 GfxLightViewFrustum*
    void *primaryLights;                             // 0x3D68 GfxLight*
    int32_t voxelTreeCount;                          // 0x3D70
    uint32_t _pad3D74;
    void *voxelTree;                                 // 0x3D78 GfxVoxelTree*
    int32_t heightfieldCount;                        // 0x3D80
    uint32_t _pad3D84;
    void *heightfields;                              // 0x3D88 GfxHeightfield*
    uint8_t precomputedSkyIllumination[0x60];        // 0x3D90 GfxPrecomputedSkyIllumination
    int32_t modelCount;                              // 0x3DF0
    uint32_t _pad3DF4;
    void *models;                                    // 0x3DF8 GfxBrushModel*
    uint32_t lmapLookupCount;                        // 0x3E00
    uint32_t _pad3E04;
    void *lmapLookup;                                // 0x3E08 GfxModelLightmapInst*
    uint32_t gfxMapEntLookupCount;                   // 0x3E10
    uint32_t _pad3E14;
    void *gfxMapEntLookup;                           // 0x3E18 GfxMapEntLookup*
    uint32_t checksum;                               // 0x3E20 (verified R_LoadWorld 0x1418D8FB0)
    uint32_t _pad3E24;
    uint8_t sun[0x70];                               // 0x3E28 sunflare_t (verified Load_GfxWorld +15912)
    float outdoorLookupMatrix[16];                   // 0x3E98 tmat44_t
    void *outdoorImage;                              // 0x3ED8 GfxImage*
    void *dustMaterial;                              // 0x3EE0 Material*
    float materialLod0SizeThreshold;                 // 0x3EE8
    uint32_t _pad3EEC;
    void *cellCasterBits;                            // 0x3EF0 uint32_t*
    void *cellHasSunLitSurfsBits;                    // 0x3EF8 uint32_t*
    void *sceneDynModel;                             // 0x3F00 GfxSceneDynModel*
    void *sceneDynBrush;                             // 0x3F08 GfxSceneDynBrush*
    uint32_t primaryLightMotionDetectBitsEntries;    // 0x3F10
    uint32_t _pad3F14;
    void *primaryLightMotionDetectBits;              // 0x3F18 uint32_t*
    uint32_t entityMotionBitsEntries;                // 0x3F20
    uint32_t _pad3F24;
    void *entityMotionBits;                          // 0x3F28 uint32_t*
    uint32_t staticSpotOmniPrimaryLightCountAligned; // 0x3F30
    uint32_t shadowBitsArrayPitch;                   // 0x3F34
    uint32_t numPrimaryLightEntityShadowVisEntries;  // 0x3F38
    uint32_t _pad3F3C;
    void *primaryLightEntityShadowVis;               // 0x3F40 uint32_t*
    uint32_t dynEntMotionBitsEntries[2];             // 0x3F48
    void *dynEntMotionBits[2];                       // 0x3F50 uint32_t*[2]
    uint32_t numPrimaryLightDynEntShadowVisEntries[2]; // 0x3F60
    void *primaryLightDynEntShadowVis[2];            // 0x3F68 uint32_t*[2]
    void *shadowGeomOptimized;                       // 0x3F78 GfxShadowGeometry*
    uint8_t lightAABB[0x18];                         // 0x3F80 GfxLightAABB
    uint8_t dpvs[0x278];                             // 0x3F98 GfxWorldDpvsStatic
    uint8_t dpvsDyn[0x230];                          // 0x4210 GfxWorldDpvsDynamic
    uint32_t numUmbraGates;                          // 0x4440
    uint32_t _pad4444;
    void *umbraGates;                                // 0x4448 UmbraGate*
    uint32_t umbraTomeSize;                          // 0x4450
    uint32_t _pad4454;
    void *umbraTomeData;                             // 0x4458 uint8_t*
    void *umbraTome;                                 // 0x4460 char*
    uint32_t mdaoVolumeCount;                        // 0x4468
    uint32_t _pad446C;
    void *mdaoVolumes;                               // 0x4470 MdaoVolume*
    uint8_t enableMdao;                              // 0x4478 bool
    uint8_t _pad4479[7];
    uint8_t streamTree[0x60];                        // 0x4480 StreamTree
    uint8_t materialStreamTreeGrid[0x58];            // 0x44E0 StreamTreeGrid
    uint8_t xmodelStreamTreeGrid[0x58];              // 0x4538 StreamTreeGrid
};
static_assert(offsetof(GfxWorld, baseName) == 0x08, "GfxWorld.baseName");
static_assert(offsetof(GfxWorld, bspVersion) == 0x10, "GfxWorld.bspVersion");
static_assert(offsetof(GfxWorld, dpvsPlanes) == 0x90, "GfxWorld.dpvsPlanes");
static_assert(offsetof(GfxWorld, cells) == 0xB8, "GfxWorld.cells");
static_assert(offsetof(GfxWorld, surfaces) == 0xC8, "GfxWorld.surfaces");
static_assert(offsetof(GfxWorld, smodels) == 0x150, "GfxWorld.smodels");
static_assert(offsetof(GfxWorld, draw) == 0x648, "GfxWorld.draw");
static_assert(offsetof(GfxWorld, dynamicLightset) == 0x3890, "GfxWorld.dynamicLightset");
static_assert(offsetof(GfxWorld, heightfields) == 0x3D88, "GfxWorld.heightfields");
static_assert(offsetof(GfxWorld, checksum) == 0x3E20, "GfxWorld.checksum");
static_assert(offsetof(GfxWorld, sun) == 0x3E28, "GfxWorld.sun");
static_assert(offsetof(GfxWorld, outdoorLookupMatrix) == 0x3E98, "GfxWorld.outdoorLookupMatrix");
static_assert(offsetof(GfxWorld, outdoorImage) == 0x3ED8, "GfxWorld.outdoorImage");
static_assert(offsetof(GfxWorld, dpvs) == 0x3F98, "GfxWorld.dpvs");
static_assert(offsetof(GfxWorld, umbraGates) == 0x4448, "GfxWorld.umbraGates");
static_assert(offsetof(GfxWorld, mdaoVolumes) == 0x4470, "GfxWorld.mdaoVolumes");
static_assert(offsetof(GfxWorld, streamTree) == 0x4480, "GfxWorld.streamTree");
static_assert(sizeof(GfxWorld) == 0x4590, "GfxWorld == Replay 1.20 loader body");

// Replay 1.20 checksum offset (verified in R_LoadWorld 0x1418D8FB0 and Load_GfxWorld 0x140D92E80).
static constexpr size_t GFXWORLD_RETAIL_CHECKSUM_OFFSET = offsetof(GfxWorld, checksum);

// Deterministic non-zero checksum shared by clipMap_t.checksum (+0xF0) and GfxWorld.checksum (+0x3E20).
inline uint32_t mapChecksumForAsset(const char *assetName)
{
    uint32_t h = 2166136261u;
    if (assetName)
        for (const unsigned char *p = reinterpret_cast<const unsigned char *>(assetName); *p; ++p)
            h = (h ^ *p) * 16777619u;
    return h ? h : 1u;
}

#pragma pack(pop)

} // namespace maps
} // namespace iw8
