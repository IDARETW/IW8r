# 09. IW8 rendering and visual-environment features for the IW8 Radiant map editor

Date: 2026-11-08. Scope: IW8 only (Replay 1.20). Read-only research: nothing was built, converted or run.
Target build: Replay 1.20.4.7623265, `game_dx12_ship_replay.exe` (the target named in `tools/neutral_ledger.json`).

Sources read:
- x64-zt docs: `E:\mw124\x64-zt\docs\IW8_CONVERSION.md` (World and lighting, Materials, Models, Effects, Limits, Engine facts), `E:\mw124\x64-zt\docs\NEUTRAL_ASSETS.md`.
- x64-zt tools: `tools\neutral_families\*.json`, `tools\neutral_ledger.json`.
- x64-zt source: `src\games\iw8\structs.hpp` (generated from the ACTS mw19 schema per NEUTRAL_ASSETS.md), `src\games\iw8\iw8_map_structs.h` (partial, hand-pinned), `src\games\iw8\neutral\*.cpp`, `src\core\assets\*_data.hpp`, `src\convert\iw7_iw8\export_*.cpp`, `src\convert\iw3_iw8\*.cpp|*.inc`, `src\games\iw8\assets\replay_ies.{h,cpp}`.
- mw120rproxy: `STOCK_MAP_RENDERING.md`, `LIGHTING.md`.
- IW8-Radiant: `docs\research\08_x64zt_iw8_conversion.md`.

Conventions:
- Struct locations are `src\games\iw8\structs.hpp` line numbers unless a file is named. `iw8_map_structs.h` is cited where it is used.
- "Writer" means the x64-zt neutral-to-IW8 adapter (`from_neutral`). Ledger gates G1 to G5 come from `tools\neutral_ledger.json` (row id in brackets).
- "Source" means the data x64-zt can already read and convert: an IW3 or IW7 extraction, or the JSON the converter consumes. x64-zt has no IW8-native authoring front end (08 section 5), so "writable from source data" always means "from an extraction", not from editor input.
- Anything not verified is marked UNKNOWN. No struct offsets are given that were not read from a `static_assert` or a struct body.

## 0. Ground rules: what the editor must not carry over from IW3

1. Lights are records, not entities. A map's lights are `ComPrimaryLight` in ComWorld (`com_map`, structs.hpp:11058) and `GfxLight` in GfxWorld (structs.hpp:13026). An IES profile is a name on `GfxLightDef` (structs.hpp:3657), not a field on the light.
2. The sun is one primary-light index (`GfxWorld.lastSunPrimaryLightIndex`, structs.hpp:13334; `Stage.sunPrimaryLightIndex`, structs.hpp:10985), not a separate object.
3. Lightmaps are a typed set (`GfxLightmapType`, structs.hpp:12436: REGULAR, REGULAR_UNCOMPRESSED, AB_LIGHTING), not IW3's two `lm_sun` coefficients.
4. Materials bind by slot number. Textures are `MaterialTextureDef.index`, constants are `MaterialConstantDef.index` plus a vec4 literal (structs.hpp:3427, 3457). The names of the slots belong to the technique set (`src\core\assets\material_data.hpp:70-72`). Layers are `Material.layerCount` (structs.hpp:3509) and `material.layers` in the neutral schema.
5. Fog, grading, exposure and volumetric scatter come from a vision set: 218 field tokens read from Replay's table at 0x14462A110 (`src\convert\iw3_iw8\replay_vision_fields.inc`). They are not map fields.
6. Reflection probes are oriented boxes with priority and feather, blended front to back (GfxReflectionProbeInstance, structs.hpp:12348). Not IW3 cubemap points.
7. The GPU light grid is tetrahedral with spherical-harmonic probes (structs.hpp:12537-12602). Static models use a separate volume table (structs.hpp:12635).
8. Visibility is DPVS cell and plane data plus an opaque Umbra tome. A zeroed GfxWorld is not render-safe (`iw8_map_structs.h:311-314`: `R_SetupDpvsForPoint` dereferences `nodes` without a guard).

## 1. Summary table

| # | Feature | IW8 asset and key struct | x64-zt writer (ledger) | Source path in x64-zt |
|---|---|---|---|---|
| 1 | Materials, techsets, constants, layers | `material`, `techset`: Material 3509, MaterialTechniqueSet 3217 | Yes for existing ones [11, 18]; NEUTRAL_ASSETS.md says "open" (conflict, section 9) | IW7 `export_materials.cpp`; IW3 `iw3_render_assets.cpp` |
| 2 | Decal volumes | `decalvolumematerial`, `decalvolumemask`; GfxStaticDecalVolumePacked 12764 | Yes [decal rows G1-G5] | IW3 FX decal path only (`iw3_render_assets.cpp:8510-8524`) |
| 3 | Reflection probes | GfxReflectionProbe 12316, Instance 12348 | Yes within gfx_map [31] | IW7 `export_probes.cpp`; IW3 captures |
| 4 | GPU light grid and ambient | GfxGpuLightGrid 12602; GfxStaticModelLightGridVolume 12635 | Yes within gfx_map [31] | IW3 `iw3_lightgrid.cpp`; IW7 `export_world.cpp:1511` |
| 5 | Baked lightmaps | GfxLightmapType 12436, GfxLightmapRaw 12512 | Yes [31, 32] | IW7 `export_lightmaps.cpp`; IW3 dump |
| 6 | Primary lights, spot/omni, IES, shadows | ComPrimaryLight 11058, GfxLight 13026, GfxLightDef 3657 | Yes [24, 34]; IES only via `ies_profiles` JSON (section 6) | IW7 `export_entities.cpp:1079-1141`; IW3 `convert.cpp:1103` |
| 7 | Sun, sun shadows, sun flare | sunflare_t 13154; GfxCompressedSunShadowParams 12873 | Yes [24, 31] | IW7 `export_sun` (`export_sky.cpp:871`); `.lighting.json` |
| 8 | Sky | GfxPrecomputedSkyIllumination 13107 (null in exports); sky surfaces | Yes within gfx_map [31] | IW7 `export_sky` (`export_sky.cpp:702`) |
| 9 | Fog and fog spline | vision fog fields; GfxFogSpline 13454, GfxFogSplineData 13447 | Spline yes [fogspline]; vision is a raw file | IW3 `setExpFog`; IW7 `export_fog` (`export_sky.cpp:612`) |
| 10 | Volumetrics | GfxVolumetric 12853, GfxWorldVolumetrics 12866 | Yes within gfx_map [31] | IW7 `export_volumetrics` (`export_world.cpp:1973`) |
| 11 | Grading, vision, exposure | GfxGradingClut 13438; vision tonemap fields | Yes [gradingclut]; vision is a raw file | IW3 `BuildClut` (`iw3_vision.cpp:520`) |
| 12 | DPVS, Umbra, MDAO | GfxWorldDpvsStatic 13230, UmbraGate 13261, MdaoVolume 9907 | Yes for present data; cells and tome not generated (UNKNOWN) | Converter emits one conservative cell |
| 13 | Static model lighting and flags | GfxStaticModel 12068, StaticModelFlags 12057 | Yes within gfx_map [31] | IW7 `export_static_model` (`export_world.cpp:1415`) |
| 14 | Glass | GlassWorld 11158, G_GlassData 11147, FxGlassDef 11654 | Yes [25, 30] | IW7 `export_glass.cpp` |
| 15 | Foliage and wind (reactive motion, flag cloth) | ReactiveMotionModelInfo 9777, ReactiveMotionWindParams 9731 | Yes [xmodel 9] | IW7 reactive motion; techset flag waves |
| 16 | World particle FX | FxWorld 11813; ParticleSystemDef 4701; init modules 3724-4313 | Yes [30, 44, 66] | IW7 createfx to IW3 createfx and vfx JSON |
| 17 | Reactive destruction: scriptables, dynents | ScriptableDef 9470, DynEntityDef 16825 | Yes [63, 29, dynentitylist] | IW7 `export_entities.cpp` |

## 2. Feature catalogue

### 1. PBR material layers, techniques and constants

- Asset and structs: `material` (Material, structs.hpp:3509: contents, surfaceFlags, materialType, cameraRegion, sortKey, drawSurf, textureCount, constantCount, constantBufferCount, layerCount, packedAtlasData, techniqueSet, textureTable, constantTable, decalVolumeMaterial, constantBufferTable, subMaterials). `techset` (MaterialTechniqueSet, structs.hpp:3217; MaterialTechnique, 3185: pipelineStates, vertex/hull/domain/pixel shaders, stateBits, args). Slots: MaterialTextureDef 3427, MaterialConstantDef 3457, MaterialConstantBufferDef 3493.
- Channel contract (IW8_CONVERSION.md, Materials): `TS_COLOR_SPECULAR_MAP` carries F0 (reflectance times 255) in alpha; the surface alpha comes from a `TS_ALPHA_REVEAL_THICKNESS_MAP` texture; normal maps keep x in alpha and y in green; packed normals keep gloss in red and occlusion in blue. Layered materials blend by a vertex colour channel with per-layer UVs from the transient zone. Unlit materials emit their tint with `emissionScale`. The BRDF itself is UNKNOWN (the docs describe inputs, not the equation).
- Editor must let a mapper: pick a material per surface; see its layer count and blend channel; tune colour, normal, specular and gloss inputs; set emission scale; preview with the same channel decoding the converter uses.
- x64-zt writes it: yes for existing materials and techsets. Ledger rows `material` [11] and `techset` [18] are G1 to G5 true. Neutral adapter: `src\games\iw8\neutral\material.cpp`; schema `src\core\assets\material_data.hpp:72-91`. Source: IW7 `export_materials.cpp:31-46` (alpha and reveal), `:125-128` (TS mapping).
- Mapper test: assign a layered material to a wall, change the vertex-colour channel that drives layer 2, build, confirm the blend moves in game.
- UNKNOWN: the shader equations; the constant names (only slot numbers are stored); whether an editor can author a new techset (no shader compile path located in the evidence).

### 2. Decal volumes

- Asset and structs: `decalvolumematerial` (GfxDecalVolumeMaterial, structs.hpp:3463: name, channels[6], flags, colorTint, alphaDissolveParms, emissiveScale, displacement cutoffs, temperature, atlas rows and columns). `decalvolumemask` (GfxDecalVolumeMask, 9935: name, blendMap). Placement in GfxWorld: GfxStaticDecalVolumePacked (12764: obbCenter, obbQuat, obbHalfSize, opacityParams, uvMatrixAndOffset[3], packedMaterialInfo), GfxDecalVolumeCollection (12422), GfxDecalVolumeTransientData (12780). Counts sit in GfxWorldDraw (12887). Model-attached volumes: XModelDecalVolume (9941), XModelDecalVolumesInfo (9951). Vision field `decalVolumeDrawDistance` (vision index 207).
- Editor must let a mapper: place an oriented box (centre, orientation, half size), choose material and mask, set opacity and draw distance; preview the box bounds.
- x64-zt writes it: yes. Ledger rows `decalvolumematerial` and `decalvolumemask` are G1 to G5 true. The GfxWorld neutral schema carries the static decal arrays (`src\core\assets\gfxworld_data.hpp:332-334`).
- Source: only the IW3 FX decal path creates decal volumes (`src\convert\iw3_iw8\iw3_render_assets.cpp:8510-8524`). No IW7 world-decal export was found in `export_*.cpp`, so IW7 world decal volumes are UNKNOWN.
- Mapper test: place a 256-unit box over a wall with a grime mask, build, confirm the decal shows only inside the box.
- UNKNOWN: the bit layout of `obbQuat`, `opacityParams` and `packedMaterialInfo` (no comments in the struct); whether decal volumes can be authored without an FX source.

### 3. Reflection probes

- Asset and structs: GfxReflectionProbe (structs.hpp:12316: livePath, origin, angles, probeInstances, probeInstanceCount, probeRelightingIndex, probeImageIndirection). GfxReflectionProbeInstance (12348: probePosition, probeImageIndex, lodLevel, flags, probeRotation quaternion, volumeObb, priority, feather, expandProjectionNeg, expandProjectionPos). GfxReflectionProbeObb (12339: center, xAxis, yAxis, zAxis, halfSize). GfxWorldReflectionProbeData (12368: image arrays, octahedron variants, relighting data, instances, SH per probe). GfxSH3Vec (12363: 9 floats). GfxReflectionProbeRelightingData (12327).
- Behaviour (IW8_CONVERSION.md, World and lighting, and Engine facts): every box that holds the point is blended front to back and box-projected. The engine orders instances by priority (inferred, not proven). IW7's fallback box of 262144 units sits at the lowest priority.
- Editor must let a mapper: place an oriented box with rotation, priority, feather and projection extents; see which box wins at a point; preview the probe capture.
- x64-zt writes it: yes within `gfx_map` [31] (G1 to G5 true). The probe schema is at `gfxworld_data.hpp:324-331`. Source: IW7 `export_probes.cpp`; IW3 filtered reflection captures with Replay probe instances and SH (STOCK_MAP_RENDERING.md, Conversion coverage).
- Mapper test: place two overlapping probe boxes with priorities 1 and 2 over a glossy floor, build, confirm the priority-2 reflection shows only inside its feather.
- UNKNOWN: whether relighting data and the octahedron images are written for editor-built probes; capture or bake tooling in the repo (none located in the evidence).

### 4. GPU light grid and ambient

- Asset and structs: GfxGpuLightGrid (structs.hpp:12602: probeCount, probes, probePositions, zones, zoneVoxelTrees, tetrahedronCount and tetrahedronCountVisible, tetrahedrons, tetrahedronNeighbors, tetrahedronVisibility, voxelStartTetrahedron, boundingVolume[7]). GfxGpuLightGridZone (12537: numProbes, numTetrahedrons, voxel fields, fallbackProbeData). GfxSHCompressedProbeData (12526: 8 words per probe). GfxSHProbeData (12531: 29 halves plus 3 pad). GfxGpuLightGridTetrahedronVisibility (16 words, 64 bytes). GfxLightGridType (12429: NONE, SINGLE, MULTI) in GfxWorldDraw. GfxWorldDraw.bakedLightScale (12887+).
- Behaviour: the grid lights players and moving models (IW8_CONVERSION, World and lighting). Entities read SH: 27 halves per entry, or 27 signed bytes when the grid is too large (`MAP_ENTITY_GRID_SH8`). Grids larger than the image are indexed in 8x8x8 blocks. Stock grids mark tetrahedra with bit 31 and a visibility record (Engine facts). `bakedLightScale` must be 1.0, since zero suppresses secondary diffuse (LIGHTING.md).
- Static-model light volumes: GfxStaticModelLightGridVolume (structs.hpp:12635: firstProbe, m_volumeSize[3], flags (axis mask, thin push), singleProbe, volumeTransform[3]).
- Editor must let a mapper: set grid bounds and spacing; see probe placement; tune intensity; preview the ambient on a moving model walking from light into shadow.
- x64-zt writes it: yes within `gfx_map` [31]; `GfxGpuLightGrid` is read and written (`src\games\iw8\neutral\gfxworld.cpp:355-357`). Source: IW3 `.gpulightgrid.bin` (LIGHTING.md, "Source light grids"); IW7 `export_gpu_light_grid` (`export_world.cpp:1511`). The IW3 grid comes from the lightmaps of the floors under its points (IW8_CONVERSION.md).
- Mapper test: place a grid volume over a basement, build, walk a player from outdoors into it; the viewmodel and a dynamic prop darken.
- UNKNOWN: how tetrahedra are generated (no generator located); the difference between SINGLE and MULTI in practice; the light-grid intensity calibration (LIGHTING.md, "not established").

### 5. Baked lightmaps (planes and formats)

- Asset and structs: GfxWorldDraw.lightmapType (structs.hpp:12436 enum: REGULAR 0, REGULAR_UNCOMPRESSED 1, AB_LIGHTING 2), lightmapCount, lightmapTransientIndex (12887+). GfxLightmapRaw (12512: textures[3] of GfxImageRaw). GfxLightmapAtlasDataTemp (12517: up to 32 lightmaps per atlas). GfxSurface.lightmapIndex (11990). GfxWorldPackedLightmap (12397: imageWidth, imageHeight). GfxWorldLightmapReindexData (12403). GfxModelLightmapInst (13137) for lookup.
- Format (IW8_CONVERSION.md, World and lighting): the primary image holds sun visibility; the secondary image holds A over B in high range; the third image holds directions. Values use a log encoding (`lightmap_encoding`, `MAP_LIGHTMAP_LOG`). Lightmap UVs address texel centres (STOCK_MAP_RENDERING.md, "Source data").
- Editor must let a mapper: choose lightmap resolution per surface or room; view each of the three planes; see which surfaces are lightmapped (`lightmapIndex`) versus probe-lit (static model flags).
- x64-zt writes it: yes. `gfx_map` [31] and `gfx_map_trzone` [32] are G1 to G5 true; lightmap images go through `image` [19]. Source: IW7 `export_lightmaps.cpp`; IW3 render dump.
- Mapper test: darken one room's sun visibility plane by editing its texels, build, confirm only that room's shadow edges change. (Needs a lightmap editor; no baker located.)
- UNKNOWN: the pixel format of each plane (GfxImageRaw format field not read in this pass); the exact log-encoding constants; whether any lightmapper exists in x64-zt.

### 6. Primary lights: spot, omni, IES profiles and shadows

- Asset and structs: ComPrimaryLight (structs.hpp:11058): spawnFlags, type, canUseShadowMap, needsDynamicShadows, isVolumetric, transientZoneList, entityId, intensity, uvIntensity, irIntensity, heatIntensity, colorLinearSrgb, dir, up, origin, radius, fadeOffset, bulbRadius, bulbLength, cosHalfFovOuter, cosHalfFovInner, shadowNearPlaneBias, shadowSoftness, shadowBias, shadowArea, distanceFalloff, cosHalfFovCollimation, rotationLimit, translationLimit, defName. GfxLight (13026): runtime copy plus flags, tonemappingScaleFactor and `def` (GfxLightDef*). GfxLightDef (3657): name, iesProfile, firstPixel, coordOffset, coordScale. GfxLightViewFrustum (13016), GfxFrustumLights (13001).
- Light records (IW8_CONVERSION.md, Engine facts): spawnFlags is 0 for dynamic shadows, 32 for static and 40 for none. A zero tube becomes (1,1,1)/255. Replay builds its own volume per light from origin, direction, radius, cone, bulb and tube. Shadowed spots score up to 64 view candidates. The light-volume array has `min(primaryLightCount, 256)` slots; Frontier's 159 lights overran 144 slots, so maps with local lights reserve at least 256. Replay's `shadowNearPlaneBias` (ComPrimaryLight+0x74) differs from IW7's `shadowBias`; converted lights take 8.
- IES: `*ieslookup` is a 512x1 R8 strip of 8 levels, one 128-texel segment per profile (`src\games\iw8\assets\replay_ies.h:6-21`). Each profile is 128 `uint8` texels sampled over the spot's forward hemisphere (`replay_ies.h`, struct Profile). Stock segments are white (no profile), IES_16 and `ies_nvg_flashlight`. Strip writer: `replay_ies.cpp:52-125`. The GfxWorldDraw field is `iesLookupTexture` (structs.hpp:12891).
- Editor must let a mapper: place a spot or omni light; aim it; set radius, cone, bulb and tube; set intensity (in light units) and shadow mode and softness; pick an IES profile from the map's list; preview the light volume and the cone.
- x64-zt writes it: `com_map` [24] and `lightdef` [34] are G1 to G5 true. IES profiles are written by `iw8::ies::Register` from an `ies_profiles` JSON array of `{definition, profile, texels[128]}` (`src\convert\iw3_iw8\convert.cpp:1103-1115`, `:1830`; `src\convert\iw7_iw8\export_entities.cpp:1098-1141`). The neutral lighting adapter refuses to write a light definition that carries an IES profile from another profile (`src\games\iw8\neutral\lighting.cpp:87-89`). An editor IES must therefore use the JSON path, not the neutral path.
- Mapper test: place a spot at 2 m height aimed at the floor, assign IES profile "X" from the map's list, intensity 3, static shadows, build, and see the profile pattern on the floor in game.
- UNKNOWN: the colour convention (linear sRGB as stored, per the field name); the intensity-to-light-unit calibration (LIGHTING.md: a multiplier, "not an established physical conversion"); the meaning of `rotationLimit` and `translationLimit`; the numeric values of `type`.

### 7. Sun, sun shadows and sun specular

- Asset and structs: the sun is `GfxWorld.lastSunPrimaryLightIndex` (structs.hpp:13334) and `Stage.sunPrimaryLightIndex` (10985). `GfxWorld.sun` is `sunflare_t` (structs.hpp:13154: hasValidData, spriteMaterial, flareMaterial, spriteSize, flareMin/MaxSize, flareMin/MaxDot, flareMaxAlpha, fade times, blindMin/MaxDot, blindMaxDarken, blind fade).
- Sun-shadow membership: GfxSurfaceFlags `casts_sun_shadow` plus `casts_sun0_shadow` to `casts_sun4_shadow` and `is_umbra_occluder` (structs.hpp:11979; names at `src\core\assets\gfxworld_data.hpp:105-115`). Static models: StaticModelCollectionFlags SUN_SHADOW_1 to 4 and `_x` (structs.hpp:12086).
- Compressed sun shadow: GfxCompressedSunShadowParams (structs.hpp:12873: sunDirection, resolution, centerX, centerY, sampleSize, nearPlane, farPlane, forestSize, flags). Tile data in GfxCompressedSunShadowDataTemp (12791). Vision fields: `sunshadowSoftness` (index 148) and `sunshadowSampleSizeNear` (index 149).
- Shader contract (LIGHTING.md, "Sun-shadow tile classification"): the world shader reads the lighting-tile buffer at t5 and the static-model shader at t4; class 11 forces zero sun visibility when `cb7[83].w` is positive.
- Units: the sun converts at IW3 sun brightness (IW8_CONVERSION.md). The converter default for IW3 is a source sun with `--sun-intensity-scale 6` (LIGHTING.md), described there as a calibration, not a physical conversion.
- Editor must let a mapper: set sun direction, colour and intensity; choose which sun-shadow class each surface and model joins; set flare, blind and glare; preview the shadow.
- x64-zt writes it: yes. `com_map` [24] and `gfx_map` [31] are G1 to G5 true. Sun flare comes from the `sun` file written by `export_sun` (`src\convert\iw7_iw8\export_sky.cpp:871`). The IW3 path reads a schema-1 `.lighting.json` with `intensity`, `color`, `direction` and `up` (LIGHTING.md, Converter profiles), which an editor could also write.
- Mapper test: rotate the sun 30 degrees, build, confirm a pillar's shadow moves; set a pillar to `casts_sun0_shadow` only and confirm it casts in the first class and not the others.
- UNKNOWN: the number and selection rule of cascades (the five sun bits suggest up to five classes, but nothing says they are cascades); any sun-specular term (no field found in these structs or the vision table); the shadow map resolution limits.

### 8. Sky

- Asset and structs: the sky cube is world geometry. Sky surfaces are GfxSurface entries with a sky material (the converter reads `skyObject`, `src\convert\iw3_iw8\iw3_fastfile.cpp:5312`). `GfxPrecomputedSkyIllumination` (structs.hpp:13107: detailMask, heightMap, SVDCoefficient, SVDBasis, boxMin, boxMax, SVDCoefficientMinMax, heightConsts) is null or zero in the retained Replay exports (STOCK_MAP_RENDERING.md). `Stage.skyRotationAngles` (structs.hpp:10985). The worldspawn key `0x102A4` sets the rotation (IW8_CONVERSION.md).
- Behaviour: Replay's sky lies at infinity; the pipeline's sky box is six faces of 32768 units around the camera, at reversed depth 1e-7, so effects beyond it draw over it (Engine facts, "Sky depth"). Models beyond the model range are drawn into the cube (IW8_CONVERSION.md, "Distant models").
- Sky colour: IW8_CONVERSION.md multiplies the cube by `r_hdrSkyIntensity` and `r_hdrSkyColorTint` from IW7's sky shader (`export_sky.cpp:475`). Those two names are not in Replay's vision field table (`replay_vision_fields.inc`); see section 9.
- Vision sky fields: `fogSkyIntensity`, `fogSkyMinAngle`, `fogSkyMaxAngle`, `fogSkyDistance`, `r_skyBlendAmount`, `r_skyBlendFeather` (vision indices 33-34, 46-52).
- Editor must let a mapper: pick a sky material or cube; rotate it; set sky intensity and tint; preview the rotation against the sun flare.
- x64-zt writes it: yes within `gfx_map` [31]. Source: IW7 `export_sky` (`export_sky.cpp:702`) and `export_sky_objects` (`export_world.cpp:464`).
- Mapper test: change the worldspawn sky rotation by 90 degrees, build, confirm the cube's sun aligns with the sun flare.
- UNKNOWN: whether the precomputed sky illumination can be non-null for an editor-built map (its owner resource is unchecked, per STOCK_MAP_RENDERING.md).

### 9. Fog and fog spline

- Vision fog (indices 35-62 in `replay_vision_fields.inc`): fogDensity (halfway distance), fogStartDist, fogColor, fogColorIntensity, fogMaxOpacity, fogSun{Density, ColorIntensity, Color, Dir, BeginFadeAngle, EndFadeAngle}, fogSky{Intensity, MinAngle, MaxAngle, Distance}, fogAtmosphericScattering, fogHalfPlaneDistance, fogMaxDistance, fogExtinctionDesaturation, fogInscatteringEv, fogRayleighAlbedo, fogMieAlbedo, fogMieAnisotropy, fogMieBlendStrength, fogHeightFog{BaseHeight, HalfPlaneDistance, MaximumOffset, AdditionalFog}. Index 217 is `fogSpline`.
- Structs: GfxFogSpline (structs.hpp:13454: name, image, data) and GfxFogSplineData (13447: fogStartDist, fogHalfPlaneDistance, fogMaxDistance). Asset type `fogspline` [ledger, G1 to G5 true]; world.json names it `ASSET_TYPE_FOG_SPLINE` (`tools\neutral_families\world.json`).
- Behaviour: the converter bakes IW3 `setExpFog` into the surface shader (`src\convert\iw3_vision.cpp:220-260`; `FogShaderDefines` at `:462-514`). The shader factor is `ln 2 / halfway` (`iw3_vision.cpp:512`). Replay's native fog stays off for converted maps (IW8_CONVERSION.md, World and lighting).
- Editor must let a mapper: set density, start, colour, sun-side fog, sky fog and height fog; optionally a spline by distance; preview fog at two distances.
- x64-zt writes it: the spline is yes (`fogspline` G1 to G5). The vision set is written as the raw file `vision\<map>.vision` (`src\convert\iw3_iw8\iw3_fastfile.cpp:10752-10760`). Source: `export_fog` (`src\convert\iw7_iw8\export_sky.cpp:612`).
- Mapper test: change fogDensity from 70000 to 35000 (the table's default is 70000), build, confirm surfaces reach half opacity at about half the distance.
- UNKNOWN: whether an editor-built map with native fog enabled would render Replay's own fog (the converter disables it; no test found); the spline image format.

### 10. Volumetrics

- Asset and structs: GfxVolumetric (structs.hpp:12853: livePath, flags, obb, color, density, falloff, heightFade, anisotropy, masks[4]). GfxVolumetricMask (12843: type, axis, image, scale, offset, scroll). GfxWorldVolumetrics (12866: count, volumetrics, mask atlas image). Enums: VolumetricMaskType (12826: UNUSED, HEIGHTMAP, OPACITYMAP) and VolumetricAxis (12834: X, Y, Z, negative flag 4). Lights carry `isVolumetric` (ComPrimaryLight, 11058; GfxLight, 13026). ParticleModuleInitVolumetric (4186).
- Vision scatter fields: r_volumeLightScatter, LinearAtten, QuadraticAtten, AngularAtten, DepthAttenNear and Far, BackgroundDistance and Color (`replay_vision_fields.inc`).
- Behaviour: converted maps keep Replay's volumetrics because the translated map script does not set `r_volumetrics` (IW8_CONVERSION.md, World and lighting). IW7 air, veil and light scatter come from `iw7_volumetrics` (`export_sky.cpp:578`, consumed at `iw3_fastfile.cpp:10747`).
- Editor must let a mapper: place an oriented volume; set density, falloff, height fade, anisotropy, colour and up to four masks; set per-light scatter; preview the haze.
- x64-zt writes it: yes within `gfx_map` [31]. The neutral adapter has the volumetric readers and writers (`src\games\iw8\neutral\gfxworld.cpp:172-174` and onward).
- Mapper test: place a 512x256x512 volume over a corridor with density 0.2, build, confirm haze is visible only inside it.
- UNKNOWN: the bit meanings of `flags`; what `livePath` controls.

### 11. Colour grading, vision, exposure and tonemap

- Asset and structs: GfxGradingClut (structs.hpp:13438: name, inputColorimetry, outputColorimetry, clutSize, hdrClutImage). Asset type `gradingclut` [ledger, G1 to G5 true]. A 32 cubed RGBA8 sRGB table (IW8_CONVERSION.md). Vision grading slots: indices 213 and 214 (kind gradingClut). Tonemap fields: tonemapMode, tonemapAuto, tonemapExposure, tonemapExposureAdjust, tonemapMaxExposure, tonemapAdaptSpeed, tonemapDarkEv, tonemapMidEv, tonemapLightEv and the per-band exposure adjusts (vision indices 63-81). Light-level tonemap: `GfxLight.tonemappingScaleFactor` (structs.hpp:13048).
- Exposure: the map metadata `exposureAdjust` is in EV from -8 to 8 and scales the grading CLUT's linear input by 2 to the EV (IW8_CONVERSION.md, Commands table; `src\convert\iw3_iw8\iw3_vision.cpp:520-525` checks the range).
- Editor must let a mapper: choose a grading LUT and preview it; set exposureAdjust; set auto-exposure bands; preview the grade on a sample room.
- x64-zt writes it: `gradingclut` yes. The vision set is a raw file (see section 9). Source: `BuildClut` (`iw3_vision.cpp:520`).
- Mapper test: set exposureAdjust to +1, build, confirm interior input brightness doubles before the grade.
- UNKNOWN: the numeric meanings of `tonemapMode` and of the two colorimetry enums (no names in the code); the defaults the table gives are its missing-field defaults, not design values.

### 12. Visibility: DPVS cells, Umbra and MDAO

- DPVS: GfxWorldDpvsPlanes (`iw8_map_structs.h:316`; structs.hpp:11921), GfxWorldDpvsPlane (11913), GfxCell (structs.hpp:11957: bounds, portalCount, portals; `iw8_map_structs.h:329`). GfxWorldDpvsStatic (structs.hpp:13230: 33-entry arrays of static-model and surface visibility, primary-light, reflection-probe, volumetric and decal visibility, sortedSurfIndex, surfaceCastsSunShadow). GfxWorldDpvsDynamic (13253).
- Umbra: UmbraGate (structs.hpp:13261: objID, bounds, closeDistance). Umbra_Tome (13268). GfxWorld.numUmbraGates, umbraGates, umbraTomeSize, umbraTomeData (13334+). The writer requires the tome pointer to be zero and carries the tome as bytes (`src\games\iw8\neutral\gfxworld.cpp:2267-2271`, `:2362-2365`). Occluder flags: GFX_SURF_IS_UMBRA_OCCLUDER 64 (structs.hpp:11979), static model collection flag 128.
- MDAO: MdaoVolume (structs.hpp:9907); GfxWorld.mdaoVolumes and enableMdao (13334+). Vision fields r_mdaoCullDistance, r_mdaoFadeoutDistance, r_mdaoBoneSizeThreshold (indices 16-18).
- Constraint: an all-zero GfxWorld crashes in `R_SetupDpvsForPoint` (`iw8_map_structs.h:311-314`). The converter's Umbra graph is one conservative cell holding every object (STOCK_MAP_RENDERING.md, Lighting section).
- Editor must let a mapper: mark occluders; place Umbra gates and MDAO volumes; see cells and portals; see culling in a debug view.
- x64-zt writes it: yes for present data (gates, tome bytes, occluder flags) within `gfx_map` [31]. Generating cells, portals or an Umbra tome from editor geometry is not located in the evidence.
- Mapper test: mark a wall as an umbra occluder, build, confirm a room behind it is culled when the wall is between the camera and the room (needs a culling debug view).
- UNKNOWN: how the Umbra tome is produced; whether DPVS cells can be generated from brushes; the portal rules.

### 13. Static model lighting and flags

- Asset and structs: GfxStaticModel (structs.hpp:12068: model, flags, firstMtlSkinIndex, firstStaticModelSurfaceIndex). StaticModelFlags (12057): LIGHTPROBE_LIGHTING 1, LIGHTMAP_LIGHTING 2, ANIMATED_OPACITY 4, REACTIVEMOTION 8, SPLINED 16, DISABLE_VOLUME_DECALS 32, CLUTTER_TINT 64. GfxStaticModelCollection (12097) and GfxStaticModelInstance (12108).
- Behaviour: static models take IW7's grid or vertex lighting; entities take SH (IW8_CONVERSION.md, World and lighting). Flags on script models stay still, as IW3's (IW8_CONVERSION.md, Materials).
- Editor must let a mapper: choose per prop whether it is lightmap-lit or probe-lit; set clutter tint, decal opt-out and animated opacity; preview a prop moved into shadow.
- x64-zt writes it: yes within `gfx_map` [31]. Source: IW7 `export_static_model` (`export_world.cpp:1415`) and `collect_static_models` (`export_world.cpp:1870`).
- Mapper test: set a crate to probe lighting, move it into a shadowed room, build, confirm it darkens.
- UNKNOWN: the converter's default flag choice for each prop class.

### 14. Glass

- Asset and structs: GlassWorld (structs.hpp:11158: name, g_glassData). G_GlassData (11147), G_GlassPiece (11130), G_GlassName (world.json). FxGlassDef (11654), FxGlassGlobals (11681), FxGlassSystem (11776). FxWorld holds `glassGlob` and `glassSys` (11813).
- Contents: XModel bits 0x10 and 0x11 are glass (IW8_CONVERSION.md, Models). Glass pieces become pane hulls in collision (IW8_CONVERSION.md, Collision). Translucent glass in the world is set by `translucent_glass` (IW8_CONVERSION.md, World and lighting); the region is 3, 26, after the sky.
- Editor must let a mapper: place glass pane brushes; set material and fracture or hole layout; preview a shot.
- x64-zt writes it: yes. `glass_map` [25] and `fx_map` [30] are G1 to G5 true. Source: `export_glass.cpp` (no line references read).
- Mapper test: make a 128 by 128 unit pane brush with a glass material, build, shoot it, confirm a fracture and a pane hull in collision.
- UNKNOWN: how FxGlass piece tables are authored; the rules for hole layouts.

### 15. Foliage, wind and flag cloth

- Asset and structs: ReactiveMotionModelInfo (structs.hpp:9777: windParams, modelPartTweaks, modelParts, modelPartsBuffer, numParts, flags). ReactiveMotionWindParams (9731: windAreaScale, windAmplitudeScale, windFrequencyScale, unused0). ReactiveMotionModelPartTweaks (9739), ReactiveMotionModelPart (9760). XModel flag 8, XSurface flag 4, and `STATIC_MODEL_FLAG_REACTIVEMOTION` 8 (structs.hpp:12062).
- Behaviour: Replay's reactive sim turns parts about a pivot at the model root; the pipeline keys the turn by vertex green (part) and blue (weight) (IW8_CONVERSION.md, Limits). Flag cloth and foliage use IW7's flag waves: techsets named `*_flagext_wv0[wv1]_*` and `*_flag_*`, with `flagWave0Parms`, `flagWave1Parms` and `flagParms` (IW8_CONVERSION.md, Materials).
- Editor must let a mapper: mark a model reactive; set its wind tweaks; set flag-wave parameters on a cloth or sign material; preview a player walking through foliage.
- x64-zt writes it: yes. `xmodel` [9] is G1 to G5 true; `src\games\iw8\neutral\xmodel.cpp` exists.
- Mapper test: place a reactive bush, detonate or walk near it, confirm the stems bend.
- Known discrepancy: ACTS reads the reactive record with a 72-byte layout and reports 0 parts; x64-zt's reader uses Replay's layout (IW8_CONVERSION.md, Limits).
- UNKNOWN: a global wind field (none found; the only wind struct is per model); the meaning of `unused0`.

### 16. World particle FX (FxWorld, createfx, particle lights)

- Asset and structs: FxWorld (structs.hpp:11813: name, glassGlob, glassSys, transientZoneCount, transientZones[1536], activeTransientZones). FxWorldTransientZone (11806). ParticleSystemDef (4701) with ParticleModuleDef (4535). Init modules: ParticleModuleInitDecal (3724), ParticleModuleInitFlare (3746), ParticleModuleInitLightOmni (3809), ParticleModuleInitLightSpot (3832: fovOuter, fovInner, bulbRadius, bulbLength, distanceFalloff, fovCollimation, brightness, intensity UV/IR/heat, shadow softness, bias, area, near plane, tone-mapping factor), ParticleModuleInitVolumetric (4186), ParticleModulePhysicsLight (4313). FxParticleSimAnimation (3605).
- Behaviour: effects come from the map script's createfx, decompiled and rewritten as IW3 createfx scripts with native Replay effects per module (IW8_CONVERSION.md, Effects). Spot lights in effects use Replay's collimation and near plane 8 (IW8_CONVERSION.md, Effects). Physics debris becomes Replay physics shapes (IW8_CONVERSION.md, Effects).
- Editor must let a mapper: place an effect (vfx) at a point; choose the spot-light or decal module; set its IES profile if any; preview in game.
- x64-zt writes it: yes. `fx_map` [30], `vfx` [44] and `particlesimanimation` [66] are G1 to G5 true (the last with only one stock asset in the evidence).
- Mapper test: place a spot-light effect with a profile, build, see the glow and the light on a wall.
- UNKNOWN: the authoring rules for FxWorld transient zones; effect bounds and culling.

### 17. Reactive destruction: scriptables and dynamic entities

- Asset and structs: ScriptableDef (structs.hpp:9470: name, flags, numParts, parts, state and event sizes, animation tree, viewRadius). ScriptableStateDef (9316), ScriptableEventSpawnDynentDef (4727), ScriptableEventLightDef (8569), ScriptableEventSunDef (8584). ScriptableInstance (10618: initialOrigin, initialAngles, classname, targetname, lightIndices[5], flags). ScriptableMapEnts (10660). DynEntityDef (16825: baseModel, initialPose, type, priority, clientId, brushModel). DynEntityList (16864).
- Behaviour: simple breaks, animated kinds and damageable kinds convert to state machines; debris flies as a physics prop; light events drive no light in IW7 MP and stay out (IW8_CONVERSION.md, Limits). The converter's scriptable and destructible functions: `export_entities.cpp:459` (destructible_of), `:543` (animation_states), `:649` (scriptable_machine).
- Editor must let a mapper: place a scriptable instance and choose its definition; step its states in preview; place dynamic entities and set their physics class.
- x64-zt writes it: yes. `scriptable` [63], `dynentitylist`, and `mapents` [29] are G1 to G5 true.
- Mapper test: place a health-state scriptable, build, shoot it until it changes state.
- UNKNOWN: scriptable light and sun events beyond the IW7 conversion (they are not used in IW7 MP).

## 3. Cross-cutting facts that shape the editor

- No native authoring path. The IW8 map build reads an extraction folder written by the IW3 or IW7 front end, with a `complete` marker and single `*.replay-world.json`, `*.replay-collision.json` and `*.replay-comworld.json` files (08 section 5; `iw3_fastfile.cpp:10427-10472`). An editor must either write that layout or add an input path. `PrepareFastfile` also needs a readable `.ff` path (08 section 5, UNKNOWN whether this can be bypassed).
- Writers exist for almost every render asset family in the ledger, so the gap is source data and baking, not the binary format.
- No lightmapper, light-grid generator, probe capturer, DPVS or Umbra baker was located in the evidence docs or the source paths read. Their absence is "not located", not proven.
- The IW8 neutral schemas for lights, fog and grading are hand-written (`lighting.cpp`, `lighting_data.hpp`) while the world, FX and scriptable schemas are generated from `tools\neutral_families\*.json`. No `lighting` or `material` family JSON exists in that folder.

## 4. Discrepancies in the evidence

1. Material status. `NEUTRAL_ASSETS.md` says "materials proper: open". `tools\neutral_ledger.json` rows `material` [11] and `techset` [18] are G1 to G5 true with evidence `zt-neutral-material-20261027`. 08 section 2 follows the doc. Unresolved.
2. `iw8_map_structs.h` GfxWorld is pinned only to 0x648 (draw) and has an opaque tail to 0x4590 (`iw8_map_structs.h:338-365`). `structs.hpp` gives the full typed struct at line 13334. Use `structs.hpp`; do not use the pinned header for layout beyond `draw`.
3. `r_hdrSkyIntensity` and `r_hdrSkyColorTint` are used by IW7's sky shader (`export_sky.cpp:475`) and cited in IW8_CONVERSION.md, but are not in Replay's vision field table. Whether Replay reads them is UNKNOWN.
4. Hand-stamped offsets. `src\games\iw8\assets\write_maps.cpp` writes GfxWorld fields at fixed offsets (for example `gw[0x7C0]` for the grid mode, `0x37F0` for `bakedLightScale`, and `0x648 + 0xF0` for the IES alias). These are not struct-derived. The editor must go through the neutral or struct writers and must not copy these offsets.
5. ACTS and x64-zt disagree on the reactive-motion record (section 15).

## 5. UNKNOWN register

1. Shader equations for the IW8 material model, and the constant names.
2. Whether the editor can author new techsets or shaders.
3. Lightmap plane pixel formats and log-encoding constants.
4. Sun-shadow cascade count and selection rule; any sun-specular term.
5. Reflection probe relighting data and capture method.
6. Light-grid tetrahedron generation and the SINGLE versus MULTI rule.
7. DPVS cell, portal and Umbra tome generation for editor geometry.
8. Native fog path for editor-built maps (converted maps disable it).
9. Global wind field.
10. Meaning of ComPrimaryLight `rotationLimit`, `translationLimit`, `type`, `transientZoneList`, and the GfxLight `flags` bits.
11. Decal volume packing bits.
12. IW7 world decal volumes (none found in the converter).
13. Whether `Stage` has roles beyond `sunPrimaryLightIndex` and `skyRotationAngles`.
14. Whether `r_hdrSkyIntensity` and `r_hdrSkyColorTint` are consumed by Replay.
15. Whether any in-game load of an x64-zt-built map has been verified (08 section 7: NOT-VERIFIED).

## 6. Mapper-test checklist (one per feature)

| # | Test | Pass condition |
|---|---|---|
| 1 | Layered material, edit layer-2 vertex channel | Blend moves in game |
| 2 | 256-unit decal box over a wall | Decal visible only inside box |
| 3 | Two probe boxes, priorities 1 and 2 | Priority-2 reflection only inside its feather |
| 4 | Grid volume over basement | Moving player darkens entering the basement |
| 5 | Lightmap plane edit in one room | Only that room's shadow edges change |
| 6 | Spot with IES "X", static shadow | Cone and profile pattern on floor |
| 7 | Sun rotated 30 degrees | Pillar shadow moves; sun-shadow class respected |
| 8 | Sky rotated 90 degrees | Cube sun aligns with flare |
| 9 | fogDensity 70000 to 35000 | Half opacity at about half the distance |
| 10 | 512x256x512 volume, density 0.2 | Haze only inside |
| 11 | exposureAdjust +1 | Interior input doubles before grade |
| 12 | Umbra occluder wall | Room behind culled in debug view |
| 13 | Probe-lit crate in shadow | Crate darkens |
| 14 | Glass pane, shot | Fracture and pane hull |
| 15 | Reactive bush near explosion | Stems bend |
| 16 | Spot-light effect with profile | Glow and wall light |
| 17 | Health-state scriptable, shot | State changes |

Each test is a manual in-game check. Nothing was run in this research.
