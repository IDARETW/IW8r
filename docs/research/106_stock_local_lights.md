# 106. How Replay 1.20 renders a stock map's local lights

Read-only research, 2026-11-13. Plan item 1.6. Goal: light the editor's Game Lighting viewport with a stock map's own
spot and omni lights (ComWorld primary lights other than the sun) the way the game does. Today the stock loader keeps
only the sun (`MainWindow::loadStockSun`, `src/qtapp/main_window.cpp:7811`, `if (l["type"].toInt() != 1) continue`), and
the Game Lighting light loop (`src/viewport/render.cpp:712-735`, 64 `PixelLight`s) uses its own falloff.

**Labels.** VERIFIED means read in shader bytecode, in Replay's own machine code, or in decoded data (cited). INFERRED
means reasoned from that evidence, or taken from the IW8 game-test decompile without a Replay check. Nothing was run in
the game.

**Sources.**
- W = the world lit pixel shader `01631f08...` (research 95), lines of
  `out/agent/shader_research/shw/mw19replay/techsets_mp_backlot2/assets/pixelshader/01631f08811f0f1923c11aaa9718d79b.0.asm`
  ("W:n"). SM = the static-model lit PS `033abcdb...` (research 95/105), same folder under `sh/`.
- Replay CPU code: `E:\IW8\Builds\1.20-replay\game_dx12_ship_replay.exe` (image base 0x140000000; addresses below are
  RVAs). The IDA MCP bridge was not reachable during this work ("open Assist in IDA first"), so functions were found
  by constant and RIP-relative cross-reference scans and disassembled with capstone over the PE, function bounds from
  `.pdata` (`out/agent/local_lights/pe.py`). The listings are kept in `out/agent/local_lights/asm/`. Function names are
  INFERRED from the game-test match; the arithmetic is VERIFIED.
- GT = the IW8 game-test decompile `E:\mw124\mw164proxy\$dst\game-test-src-cleaned\code_source\src\` (not Replay; used
  for names and for parts not checked in Replay, marked INFERRED).
- Data: the world cache ComWorld exports (`world_cache/<zone>/com_map/.../<zone>.d3dbsp.0.asset.json`) of mp_hideout,
  mp_m_speedball, mp_shipment and mp_aniyah_tac; the `gfx_map` / `gfx_map_trzone` exports and `lightmap.iw8rlm` of
  mp_hideout and mp_shipment; ACTS exports of the `lightdef` assets and the `*ieslookup` image of mp_hideout,
  mp_shipment and mp_aniyah_tac (section 7).

## 0. Summary

- **Every** spot (type 2) and omni (type 3) primary light is a runtime light. Its diffuse and specular are evaluated
  per pixel by the world and static-model shaders (identical loops, W:910-1197 and SM:801-1088). No ComWorld flag
  marks a light as baked only (VERIFIED, code). The stock lightmap does **not** hold their direct light: on the
  unoccluded floors under 32 strong hideout lights and 8 shipment lights the lightmap is 0.34 and 0.30 of the runtime
  direct term (medians), and only 3 of the 40 lights reach 0.8 (VERIFIED, data). Their bounce is presumably baked
  (INFERRED).
- Counts (section 1.2): hideout 124 runtime lights (37 shadowed with entities, 33 static-shadowed, 54 unshadowed),
  speedball 0, shipment 22 (5 / 7 / 10), aniyah_tac 267 (89 / 174 / 4; 2 more have intensity 0 and are dropped).
- Light list (section 2): the DPVS-visible lights that pass 4 filters, the **nearest 256 to the camera**, in a 96-byte
  structured buffer. Culling is z-binned forward+: 8x8-pixel tile bitmasks of up to 8 words plus 8,192 depth bins of
  32 units that hold a (min, max) light-index range (VERIFIED, code and shader; the scheme of Drobot's 2017 Call of
  Duty culling talk, INFERRED).
- Shading (section 3): a sphere or tube light. `E = intensity / (1 - collim)^2 x colorLinearSrgb x NdotL / d^2`, capped
  where the bulb fills the view (`<= intensity / bulbRadius^2`), times an IES profile, a front test, and
  `smoothstep(saturate((1 - d / radius) / distanceFalloff) x spotRamp)`. Specular is GGX / Smith / Schlick with a
  representative point and a sphere-light energy factor. There is no 1/pi, no bakedLightScale and no AO. The term adds
  to the lightmap (`C x cb7[43].z`) and the sun in the same units (VERIFIED). `intensity` is the Radiant value x
  493.38132 (= 39.37^2 / pi; VERIFIED on every stock light), and the colours have luminance 1.
- Shadows (section 4): only lights with `canUseShadowMap`. Each gets one perspective depth map (a quarter of a
  512/768/1024 array slice) looked up with an exponential compare. `shadowSoftness` sets the exponent
  (`88 - 87 s`) and `shadowBias` is a light-bleeding cut (`(e - b) / (1 - b)`). Static geometry is cached; entities
  are drawn only for `needsDynamicShadows` lights (VERIFIED packing; selection and caches from GT, INFERRED).
- Recipe (section 5): everything is in the cached ComWorld export, plus 4 tiny `lightdef` assets and the 512- or
  1024-texel `*ieslookup` strip for IES. No decompressed-zone data is needed. Simplest first step: the stock lights
  into the existing pixel-light loop with the game's window, bulb cap and IES, nearest 256, no shadows.

## 1. Which lights are runtime, which are baked (question 1)

### 1.1 The runtime path takes every spot and omni (VERIFIED)

- **Load**, `R_InitSelectedPrimaryLights` (chunk 0x18D96E5, `asm/init_primary_lights_18d96e5.asm`): every ComWorld
  light (stride 0xA0) is copied to a 0x98-byte `GfxLight`. Changes on the way:
  - `distanceFalloff` is clamped to [0.1, 1] (constants 0x23BA4BC = 0.1, 0x275E798 = 1.0).
  - `bulbRadius = max(bulbRadius, 1)`.
  - A `bulbLength` of exactly (0,0,0) becomes (1/255, 1/255, 1/255) (constant 0x23C43B0).
  - For type 2/3 with `rotationLimit < 1` the bulb length is light-local and is turned by
    `AnglesToAxis(vectoangles(-dir))`: x forward = emission, y left, z up. No stock light of the four maps has
    `rotationLimit < 1`, so stock bulb lengths are world vectors.
  - `tonemappingScaleFactor` is set to 0 (+0x70).
  - `GfxLight.flags` (+0x6C) is 1. It becomes 3 when `spawnFlags & 0x84` and gets |4 when `spawnFlags & 0x10`, and
    0x4000 marks a negative `uvIntensity`. No other spawn flag is read.
- **Per frame**, `R_AddFrustumLight` (0x186C2D0, `asm/add_frustum_light_186c2d0.asm`) rejects a light when:
  - it is a moving scriptable light (handled elsewhere);
  - `radius < 1.01` (0x240D310);
  - `GfxLight.flags & 0x10` (no stock data sets it at load; INFERRED script-off);
  - `max(colour) x intensity == 0`, unless thermal vision is on and `irIntensity > 0`;
  - it is a spot with `cosHalfFovOuter == 1.0`, or its type is not 2 or 3;
  - its transient zone is not loaded (Warzone tiles; every MP light has `transientZoneList` 0).

  Everything else is added (section 2). The packer and the shader then always evaluate both diffuse and specular
  (W:1184-1189). There is no per-light "baked" bit in the 96-byte record and no per-surface skip.
- The scriptable ranges of GfxWorld (`firstMutablePrimaryLight`, `firstStaticScriptablePrimaryLight`,
  `firstMovingScriptablePrimaryLight`) equal `primaryLightCount` on all four maps (127, 3, 24, 272), so no stock light
  is scripted.

### 1.2 Per-map categories (VERIFIED, `categories.py`)

| | mp_hideout | mp_m_speedball | mp_shipment | mp_aniyah_tac |
|---|---|---|---|---|
| ComWorld lights (null + sun) | 127 (1 + 2) | 3 (1 + 2) | 24 (1 + 1) | 272 (1 + 2) |
| spot / omni | 110 / 14 | 0 / 0 | 22 / 0 | 262 / 7 |
| dropped at runtime | 0 | 0 | 0 | 2 (index 264, 270: intensity 0) |
| **runtime (diffuse + specular)** | **124** | **0** | **22** | **267** |
| shadow map, static + entity casters (`canUseShadowMap` 1, `needsDynamicShadows` 1, spawnFlags 0) | 37 spot | | 5 spot | 88 spot + 1 omni |
| shadow map, static casters only (1, 0, spawnFlags 32) | 28 spot + 5 omni | | 7 spot | 171 spot + 3 omni |
| no shadow map (0, 0, spawnFlags 40) | 45 spot + 9 omni | | 10 spot | 2 spot + 2 omni |
| `isVolumetric` | 52 | | 22 | 56 |
| IES lightdef (`ies_*`) | 102 (`ies_6`) | | 22 (`ies_6`, `ies_9`) | 229 (`ies_6`, `ies_9`, `ies_12`, 6 `ies_06`) |
| tube (\|bulbLength\| > 1) | 69 | | 1 | 36 |
| `bulbRadius` > 1 | 80 | | 15 | 18 |

- `spawnFlags` follows the two shadow bits exactly on all three maps: 0 = shadow map with entities, 32 = static
  shadows only, 40 = no shadow map. At runtime only the shadow bytes matter (1.1). INFERRED: 8 = "no shadows" and 32 =
  "no dynamic shadows" are the editor-side spawn flags they came from.
- aniyah_tac has more than 256 runtime lights, so the game keeps the nearest 256 of the visible ones each frame
  (section 2).
- `ies_06` (6 aniyah lights) is not a lightdef in that zone (its zone has `ies_12`, `ies_6`, `ies_9`,
  `light_fx_default`, `light_ies_nvg_flashlight`, `light_point_linear`). What `R_RegisterLightDef("ies_06")` returns
  was not checked (INFERRED: a default def, so no IES).
- `fadeOffset` is (0, 0) on every stock light, `uvIntensity`, `irIntensity` and `heatIntensity` are 0, `rotationLimit`
  is 1. One shipment light (index 3) has `cosHalfFovCollimation` 0.766.

### 1.3 The lightmap does not contain their direct light (VERIFIED, data)

Test (`geom.py`, `lmtest.py`, `occl.py`):
1. All lightmapped world triangles: centroid, normal, lightmap UV (research 94 section 2). Front faces are
   **clockwise**: the normal is `cross(c - a, b - a)`. VERIFIED on hideout: the floor slab under light 82 has its
   up-facing side at z = 176 and its down-facing side at z = 168.
2. For each light, the runtime direct term E (section 3) at every triangle in its radius. This is a point-light
   approximation with the IES, spot ramp, window and bulb cap, and no shadows.
3. The lightmap C there (`lightmap.iw8rlm`, bilinear, luminance).
4. The top 40 triangles per light, kept only when the segment to the light hits no world triangle.

| Map | Lights with visible median E > 1 | Median of C / E | Lights with C / E >= 0.8 |
|---|---|---|---|
| mp_hideout | 32 | 0.34 | 3 |
| mp_shipment | 8 | 0.30 | 0 |

- If the bake had put the direct light in C, then C >= E on these unoccluded triangles (C = direct + bounce). Instead
  C is about a third of E, so the game adds the light on top of a lightmap without it.
- Example: hideout light 82 (ceiling spot, 123,345, radius 250). On the floor 110 units below it the runtime
  E = (8.38, 10.33, 10.70), while the lightmap holds C of about 1.9-2.1.
- Static models: their light grid volumes (research 105) are lit with the same runtime loop (SM), so the same holds
  for them (INFERRED).
- **Bounce**: the light's indirect light is presumably in C and in the light grid volumes (INFERRED). That is the
  only way lamp-lit rooms get bounce, and the converter's own bake works this way (research 95, last section). The
  data test was inconclusive: `bounce.py` compares covered floors near and far from lights, but the "far" ones are
  mostly under overhangs near open sky.

## 2. Light list and culling (question 2)

### 2.1 CPU: which lights, in what order (VERIFIED, `R_AddFrustumLights` 0x186C4F0, `R_AddFrustumLight` 0x186C2D0)

1. Walk the set bits of `GfxWorld.dpvs.primaryLightVisData` (world +0x3FA0 count, +0x41C0 words). This is the DPVS
   visibility of primary lights, a runtime array (4 zero words on hideout). Then walk the frame's dynamic (FX) lights.
2. Apply the filters of 1.1.
3. Insert into a list sorted by **squared distance from the camera to the light origin** (insertion sort with
   `memmove`). The list keeps at most **256** (`cmp ebx, 0x100`). A light farther than the 256th is dropped
   (`R_WARN_FRUSTUM_GRID_MAX_LIGHTS`, GT).
4. Copy each listed light's 96-byte packed record (`packedSceneLights[sceneIndex]`, backend data +0xAB2208, section
   3.2) into the frame light buffer in list order. That buffer is **t49** in the shaders.
5. **Z-bins**: 0x2000 = **8,192** u32 entries, initialised to 0xFFFF0000 (empty: min 0xFFFF, max 0). If z-binning is
   off (`r_frustumLightUseZBinning`, GT default 1) they are 0xFF instead (all lights).
   - Each light's view-depth range comes from its proxy mesh. That is `GfxWorld.frustumLights` for static lights
     (hideout: 127 meshes, 4,234 vertices, 23,961 indices), or a runtime cone or capsule built by 0x186EAE0 (spots)
     and 0x186DE10 (omnis). Both are called from 0x186C4F0; their roles are from the x64-zt notes in
     `replay_locallights.h`.
   - The range is mapped to bins `depth >> 5` (32 units each) clamped to 0x1FFF.
   - Each bin it covers gets `min(index) << 16 | max(index)`.
6. **Tile masks** (GT `RB_RenderFrustumLightsTile`, `cs_frustum_lights_write_bitmask`): the proxies are rasterised
   against the depth buffer into one bit per light per 8x8-pixel tile. This GPU pass was not read; the shader's view
   of it is below.

### 2.2 GPU: the per-pixel loop (VERIFIED, W:910-968)

```
words  = cb7[52].x                                  // u32 words per tile = ceil(lights / 32), at most 8 (INFERRED)
tile   = (px.y >> 3) * ((cb7[54].x + 7) >> 3) + (px.x >> 3)    // cb7[54].x = render width (INFERRED)
bin    = t52[min(uint(viewDepth) >> 5, 8191)]
lo     = bin >> 16; hi = bin & 0xffff               // light-index range of this depth slice
range  = (hi - lo + 1 >= 32) ? ~0 : ((1 << clamp(hi - lo + 1, 0, 32)) - 1)
for w = lo >> 5 .. min(words - 1, hi >> 5):
    m = t50[tile * words + w] & (range << clamp(lo - 32 w, 0, 31))     // conservative for ranges that span words
    for each set bit b of m: light index i = 32 w + b -> t49[i] (96 B), shadow record t41[...] (section 4)
```

- There is no per-pixel light count limit; a pixel loops over every light whose tile bit and depth range match.
- SM:801-1088 is the same loop instruction for instruction (`difflib` over the register-free text: only two
  swizzles differ), so props get the same local light as the world.

## 3. Per-light shading (question 3)

### 3.1 The packed record t49 (VERIFIED, `R_UpdatePackedSceneLightConstants` 0x19A9020, `asm/pack_scene_lights_19a9103.asm`; read by W:969-976, 1078-1081)

`GfxLight` fields used, at Replay offsets: +0 type, +1 canUseShadowMap, +3 isVolumetric, +4/+8/+0xC uv/ir/heat,
+0x10 intensity, +0x14 colorLinearSrgb, +0x20 dir, +0x38 origin, +0x44 radius, +0x50 bulbRadius, +0x54 bulbLength,
+0x60/+0x64 cosHalfFovOuter/Inner, +0x6C flags, +0x70 tonemappingScaleFactor, +0x78 shadowSoftness, +0x7C shadowBias,
+0x84 distanceFalloff, +0x88 cosHalfFovCollimation, +0x90 def.

| Offset | Field | Value (`bR` = max(bulbRadius, 1), `c` = max(cosHalfFovCollimation, 0.0017453), `df` = clamp(distanceFalloff, 0.1, 1)) |
|---|---|---|
| 0x00 | float3 diffuse | `intensity / (1 - c)^2 x colorLinearSrgb / bR^2`. When `tonemappingScaleFactor` t > 0 it is also x `(t k + 1 - t)`, k = clamp(1/staticExposure) x `r_lightIntensityAutoScale`; t is 0 for primary lights. Replay does **not** multiply by `r_diffuseColorScale` (GT does). |
| 0x0C | float collimationOffset | `c bR / sqrt(1 - c^2)`. Not read by W or SM. |
| 0x10 | float3 dir | ComWorld `dir`. It points **back toward the light**: 65 of hideout's 110 spots have dir.z > 0.5, and the shader's front test (below) needs this. |
| 0x1C | u32 packed | bits 0-7 shadow slot (0xFF = none, set per frame by section 4), bits 8-9 type, bit 29 = dynamic (FX) light, bit 30 = isVolumetric |
| 0x20 | float3 bulbDir | the bulb length vector: the **half** axis of the tube (the shader clamps t to [-1, 1]) |
| 0x2C | float bulbRadius | `bR` |
| 0x30 | float3 pos | `origin - cameraOrigin` (the shader works camera-relative) |
| 0x3C | float invRadius | `1 / radius` (0 when radius <= 0) |
| 0x40 | float4 spot | type 2, or type 3 with `canUseShadowMap`: `x = 1 / (cosInner - cosOuter)`, `y = -cosOuter x`, `z = cosOuter bR / sqrt(1 - cosOuter^2)` (the apex offset), `w = 1 / df`. Other omnis: `(0, 0, 0, 1 / df)`. |
| 0x50 | float3 uvIrHeat | uv x `lightUVIntensityScale` (unless flags & 0x4000), ir, heat: thermal and UV vision only |
| 0x5C | u32 iesLookup | two **float16**: low = `def->coordOffset`, high = `def->coordScale` (0 without an IES profile). GT packs unorm16 here; Replay's bit-twiddled float-to-half (0x19A93F8-0x19A947D) matches the shader's `f16tof32`. |

The type, dir, bulb, radius, spot and IES words are written once (when the type bits are still 0). Position and colour
are rewritten every frame.

### 3.2 The shader (VERIFIED, W:969-1190; world space below, the game subtracts the camera from both sides)

```
// --- attenuation (W:977-1020) ---
Q      = pos + clamp(dot(P - pos, bulbDir) / dot(bulbDir, bulbDir), -1, 1) * bulbDir   // closest point on the tube axis
d      = |Q - P|;  L = (Q - P) / d
spot   = type == 2
Lc     = spot ? normalize(Q + dir * spot.z - P) : L          // spot: towards the cone apex behind the bulb
front  = spot ? (dot(Q - P, dir) > 0) : 1
cosA   = dot(dir, Lc)
u      = cosA > 0 ? (1 - cosA^2) / 2 : 1 - (1 - cosA^2) / 2  // 0 on the axis, 0.5 at 90 degrees, 1 behind
ies    = t122.SampleLevel(s8, iesOffset + u * iesScale, 0)    // 1D R8 strip; coordinate 0 is a white texel
spotF  = saturate(cosA * spot.x + spot.y)
w      = saturate(spot.w * (1 - d * invRadius)) * (spot ? spotF : 1)
att    = ies * front * w * w * (3 - 2 w)
shadowWeight = spot ? 1 : spotF                               // shadowed omnis: shadow only inside their cone
att   *= lerp(1, shadow, shadowWeight)                        // shadow from section 4 (1 when slot == 0xFF)

// --- diffuse (W:1082-1117) ---
X      = P + (N + L) * d / (1 + dot(N, L))                    // representative point along the half vector of N and L
Qd     = closest point on the tube axis to X (as Q);  dd = |Qd - P|;  Ld = (Qd - P) / dd
NdotL  = dot(N, Ld)
s2     = (bulbRadius / dd)^2                                  // sin^2 of the bulb's angular radius
y      = NdotL / sqrt(s2)
E      = (1 - y^2 > 0) ? saturate(s2 * (0.5 + 0.5 y))^1.5     // bulb crosses the horizon
                       : saturate(NdotL * s2)                 // = NdotL bR^2 / dd^2, at most 1
E     *= att

// --- specular (W:1119-1176) ---
R      = reflect(-V, N)
t      = clamp((dot(bulbDir,R) dot(R,Q-P) - dot(bulbDir,Q-P)) / (|bulbDir|^2 - dot(bulbDir,R)^2), -1.5, 1.5)
t      = t - t^3 * 4/27                                       // smooth clamp to [-1, 1]
S      = Q + t * bulbDir - P                                  // the axis point nearest the reflection ray
perp   = R * dot(S, R) - S
x      = bulbRadius / |perp|;   x = x / sqrt(x^2 + 0.125)
Ls     = normalize(S + perp * x);  Hs = normalize(Ls + V)
a2     = 2 / (2 + 2^(20 g'))                                  // research 95
D      = a2 / (dot(N,Hs)^2 (a2 - 1) + 1)^2                    // GGX, no 1/pi
Vis    = 1 / ((|NdotL| + sqrt(a2 + (1 - a2) NdotL^2)) (NdotV + sqrt(a2 + (1 - a2) NdotV^2)))   // NdotL of Ld
norm   = a2^0.749 / (a2^0.749 + 0.375892 * bulbRadius / |S|)  // sphere-light energy (bit-trick pow and rcp in the asm)
F      = f0 + (1 - f0) (1 - |dot(V, normalize(Ld + V))|)^5    // Schlick with the DIFFUSE half vector
spec   = diffuse * F * D * Vis * norm * E * cavity

// --- combine (W:1178-1190, 1198) ---
(A, B) = ggx_env_brdf_lut(NdotL * 63/64 + 1/128, g' * 63/64 + 1/128)
color += diffuseColor * diffuse * E * (1 - max3(f0 A + B)) + spec
out    = (ambient + sun + color) * cb7[42].x
```

- No AO (GTAO or SELFVIS), no `kd` retro-reflection polynomial, and no 1.0390625 factor; the sun has the last two.
  The cavity (t2.z) scales only the specular, as for the sun.
- Away from the bulb (`dd >> bR`) the diffuse is the inverse square law with the authored intensity:
  `E x diffuse = intensity / (1 - c)^2 x colour x NdotL / dd^2`. The bulb radius only (a) caps it at
  `intensity / (1 - c)^2 / bR^2` and (b) softens the horizon.
- Window: full until `1 - distanceFalloff` of the radius (0.8 for the usual 0.2), then a smoothstep to 0 at the
  radius. The spot ramp is linear in cos between outer and inner and goes into the same smoothstep.
- Thermal vision (`cb7[13].x`) swaps the colour for `ir x 4.703669 + diffuse.r` and accumulates heat in `.w`. Ignore.

### 3.3 Units (VERIFIED unless marked)

- The local term is added to `ambient + sun` before `x cb7[42].x` (W:1198, 1213). The lightmap enters as
  `C x cb7[43].z` (`r_secondaryDiffuseScale x bakedLightScale`; bakedLightScale is 1.0 on hideout, shipment and
  aniyah_tac) and the sun as `cb7[57] x NdotL` (research 95). Local lights carry no bakedLightScale and no
  diffuseColorScale. So **lightmap C, sun colour x intensity, and `intensity / d^2` are one unit**, the shader's
  "radiance x pi" unit.
- `intensity` = Radiant intensity x **493.38132** (GT `GetGameColorAndIntensity`, `qcommon/radiant_remote_lighting.cpp:1307`).
  Every stock value divides to a round number: hideout 0.1 ... 450, shipment 0.05 ... 120, aniyah 0.05 ... 1,500.
  - 493.38132 = 39.3701^2 / pi (metres to inches squared, over pi). INFERRED: the Radiant value is a luminous
    intensity in the scene's (kilo) candela unit, and the shader unit is luminance x pi in inch units.
- `colorLinearSrgb` = the sRGB colour linearised and divided by its Rec.709 luminance (GT, same function). Every stock
  light has luminance exactly 1 (VERIFIED), so channels above 1 are normal (light 118: 1.32, 0.96, 0.43).
- Scale of the numbers: lamps give E of 1.5-10 on the floors under them (hideout table, section 1.3), against
  lightmap C of 0.3-2 indoors and 2-3.6 on sunlit floors (research 94). A 0.1-0.5 Radiant light (49-247) is a faint
  accent (E 0.005-0.025 at 100 units).

### 3.4 IES profiles (VERIFIED, ACTS exports, `ies.py`)

- A light's `defName` names a `lightdef` asset in the map zone: `{name, iesProfile, firstPixel, coordOffset, coordScale}`.
  Profile-less defs (`light_point_linear`, `light_fx_default`) have offset = scale = 0, so they sample texel 0.
  `light_point_linear` has no attenuation image in IW8; the falloff is the analytic one above.
- The strip is `GfxWorldDraw.iesLookupTexture` = image `*ieslookup` in the map zone: R8_UNORM (DXGI 61), one row, 8
  mips. hideout is 512 wide, shipment and aniyah_tac 1,024. Texels 0-127 are 255 (white). Each profile starts at
  `firstPixel`, with `coordOffset = (firstPixel + 0.5) / width` and `coordScale = 255 / width`.
  - So texel `firstPixel + 255 u` is sampled, and u = 0.5 (90 degrees) is texel offset 127.5.
  - On 1,024 strips the profiles are 128 texels apart, so the back hemisphere (u > 0.5) reads the next profile's
    texels. That is harmless for spots (front test and cone).

| Def (profile) | Zones | Relative intensity at 0 / 10 / 20 / 30 / 40 / 50 / 60 / 70 / 80 degrees |
|---|---|---|
| `ies_6` (IES_6) | all three | 0.98 / 0.99 / 0.96 / 0.73 / 0.42 / 0.27 / 0.03 / 0 / 0 |
| `ies_9` (IES_9) | shipment, aniyah | 0.14 / 0.99 / 0.26 / 0.02 / 0.10 / 0 / 0 / 0.04 / 0 |
| `ies_12` (IES_12) | aniyah | 1.00 / 0.50 / 0.22 / 0.09 / 0.06 / 0.04 / 0.03 / 0.02 / 0 |
| `flashlight` (IES_16) | shipment | 1.00 / 0.98 / 0.94 / 0.87 / 0.76 / 0.62 / 0.45 / 0.23 / 0.04 |
| `light_ies_nvg_flashlight` | all three | 1.00 / 0.54 / 0.21 / 0.04 / 0.01 / 0 ... |

The same profile has identical texels in every zone (only the slot moves), so a built-in copy of the stock curves is
an acceptable fallback (section 5.5).

### 3.5 Fields the forward shading does not use

- `fadeOffset` goes to `GfxLight.fadeOffsetRt` (GT `R_LightFadeOffsetRuntimeEncoding`), but not to the packed record.
  It is 0 on every stock light.
- `cosHalfFovCollimation` only divides the intensity by `(1 - c)^2` (its 0x0C offset is unread by W and SM).
- `isVolumetric` (bit 30), `shadowArea` (only a debug print in GT `cg_draw_debug.cpp:5014`; the Replay packers do not
  read +0x80), `entityId`, `up` (used for the shadow camera roll, section 4), `translationLimit`.

## 4. Shadows (question 4)

### 4.1 Which lights (VERIFIED in the shader and packers; selection INFERRED from GT)

- Only lights with `canUseShadowMap` get a shadow slot (GT `R_ChooseShadowedLights`, `r_primarylights.cpp:573`).
  - A **spot** is shadowed over its whole cone (shadowWeight 1).
  - A shadowed **omni** gets one perspective shadow along `-dir` with its `cosHalfFovOuter` as the half-angle
    (hideout's 5: 85 degrees). It is shadowed only inside that cone, with weight `saturate(cosA x + y)` (W:1024,
    1073-1074). Outside the cone it is unshadowed.
- `needsDynamicShadows` = the light's map also contains entities (players, dynamic models) and is redrawn when
  something moves (motion bits). Without it only static geometry (BSP and static models) is drawn into a map that is
  cached ("stale cache", immortal entries).
- GT limits (not checked in Replay):
  - 64 shadow candidates per view (`rg.spotCandidateLimit`), ranked by a screen-coverage score
    (`sm_spotShadowScore*`).
  - Dynamic redraws for at most `sm_roundRobinPrioritySpotShadows` = 4 lights (max 16).
  - Up to `sm_spotUpdateLimit` = 4 map updates per frame.
  - A canUseShadowMap light without a slot this frame is drawn unshadowed (packed slot 0xFF).

### 4.2 Atlas and record (VERIFIED, `R_UpdatePackedSpotShadowConstants` 0x19A95D0, loop 0x19A9638; read by W:1040-1068)

- Depth lives in **t93**, a Texture2DArray. Slice size = `R_SpotShadow_GetMapSize` (0x1986030) =
  {512, 512, 768, 1024}[shadow quality].
- Each light uses one **quadrant** of a slice: `R_SpotShadow_GetAtlasUVOffsetAndScale` (0x1985F20) has 4 entries
  (u, v, scale) = (0, 0, 0.5), (0.5, 0, 0.5), (0, 0.5, 0.5), (0.5, 0.5, 0.5). That is 256, 384 or 512 texels per light.

Record t41 (96 B, indexed by the packed slot):

| Offset | Value |
|---|---|
| 0x00 | `r_spotLightShadowsPolygonOffset x 0.001 / radius` (not read by W) |
| 0x04 | quadrant u offset |
| 0x08 | `mapSize + quadrant v offset` (integer part = map size, fraction = v offset) |
| 0x0C | `arraySlice + quadrant scale` |
| 0x10 | `k = (1 - shadowSoftness) x 88 + shadowSoftness` (0x23CBDCC = 88.0); stock softness 0.55 gives k = 40.15 |
| 0x14 / 0x18 | `1 / (1 - shadowBias)`, `-shadowBias / (1 - shadowBias)` |
| 0x1C | `1 / zNear` |
| 0x20-0x5F | the light's view-projection rows (camera-relative) |

Lookup (W:1051-1068):

```
uv    = clamp(row01 . P / row3 . P * scale, 0.5 / mapSize, scale - 0.5 / mapSize) + (uOffset, vOffset)
zs    = t93.SampleLevel(s8, float3(uv, slice), 0)
z     = (row2 . P) / zNear
shadow = saturate((exp(k * (z - zs)) - b) / (1 - b))      // b = shadowBias
```

- What t93 stores: the spot shadow downsample (`cs_spotshadow_map.hlsl/cs_downsample.425`) converts the depth buffer
  with `saturate(d f / (d (f - n) + n))` over a gather of 4, then a blur pass (`cs_blur.424`).
  - INFERRED: with reversed-Z depth this is the reversed linear depth `(far - z) / (far - near)`, and `row2 . P / zNear`
    is the same quantity for the receiver. So `z - zs < 0` behind an occluder, and the exponential darkens it.
  - `shadowBias` (stock 0.05-0.99, mostly 0.7-0.85) is therefore **not** a depth offset. It is a light-bleeding cut:
    values of the exponential below b go to 0.
- **Shadow camera** (GT `R_SetViewParmsForLight`, `r_spotshadow.cpp:1817`; INFERRED for Replay):
  - It looks along `-dir`, with roll from `up`.
  - It sits at the origin pushed back along `dir` so the frustum encloses the bulb: by
    `(|bulbLength + bulbLength/|bulbLength| bR| - |dot|) cosOuter / sinOuter + |dot|`, where dot is the length along
    the axis.
  - FOV = `2 acos(clamp(cosOuter, 0.001, 0.999))`, square.
  - near = that offset + |dot| + **shadowNearPlaneBias** (stock 8-60); far = offset + radius.
- Screen-space mask path (W:1027-1038, when `cb7[109].x < 0`): a uint2 texture t94 replaces the lookup. Its bytes 0-3
  hold soft shadows of the first 4 shadowed lights at a pixel, and 32 more bits hold hard ones. INFERRED: the spot
  shadow denoiser output (`cs_spotshadow_denoiser`). When Replay uses it was not established.

## 5. Editor recipe (question 5)

### 5.1 Data

1. **ComWorld lights**: already cached by `loadStockSun` (`main_window.cpp:7698-7718`,
   `world_cache/<zone>/com_map/.../<zone>.d3dbsp.0.asset.json`, `fields.primaryLights.values`). It has every field of
   3.1, as `{"v": [...]}` vectors and `defName.string`. Keep types 2 and 3 instead of only type 1 (line 7811).
2. **IES** (optional, section 5.5 has the fallback):
   - The lightdefs: add `lightdef` to that com_map run (`-a com_map,lightdef`). That is 4-6 assets of about 300 bytes,
     `fields.{name.string, iesProfile.string, firstPixel, coordOffset, coordScale}`.
   - The strip: a separate run `-a image -n "*ieslookup"` writes `assets/image/_encoded/0.0.dds` (DX10 header, R8,
     width x 1, 8 mips; level 0 is enough).
3. Nothing from the decompressed zone. No GfxWorld field is needed either. The `frustumLights` proxies, `lightAABB`
   BVH and `shadowGeomOptimized` caster lists are speed-ups the editor can skip.

### 5.2 CPU packing (once per map; position per frame if camera-relative)

```cpp
struct GameLight {                 // 96 bytes = Replay t49 (section 3.1), world-space pos
    float diffuse[3], collimationOffset;
    float dir[3];      uint32_t packed;      // 0xFF | type << 8  (| slot when shadowed)
    float bulbDir[3];  float bulbRadius;
    float pos[3];      float invRadius;
    float spot[4];
    float uvIrHeat[3]; uint32_t iesLookup;   // f16(coordOffset) | f16(coordScale) << 16
};
bool packStockLight(const ComPrimaryLight& L, const LightDef* def, GameLight& g) {
    if ((L.type != 2 && L.type != 3) || L.radius < 1.01f) return false;
    if (std::max({L.color[0], L.color[1], L.color[2]}) * L.intensity == 0) return false;
    if (L.type == 2 && L.cosHalfFovOuter == 1.0f) return false;
    const float bR = std::max(L.bulbRadius, 1.0f), c = std::max(L.cosHalfFovCollimation, 0.0017453292f);
    const float df = std::clamp(L.distanceFalloff, 0.1f, 1.0f);
    const float k = L.intensity / ((1 - c) * (1 - c)) / (bR * bR);
    for (int i = 0; i < 3; ++i) g.diffuse[i] = k * L.color[i], g.dir[i] = L.dir[i], g.pos[i] = L.origin[i];
    vec3 bl = L.bulbLength;
    if (bl == vec3{0, 0, 0}) bl = {1 / 255.f, 1 / 255.f, 1 / 255.f};
    else if (L.rotationLimit < 1) bl = anglesToAxis(vectoangles(-L.dir)) * bl;   // x fwd, y left, z up
    g.bulbDir = bl; g.bulbRadius = bR; g.invRadius = 1 / L.radius;
    g.collimationOffset = c * bR / std::sqrt(1 - c * c);
    g.packed = 0xFFu | uint32_t(L.type) << 8 | (L.isVolumetric ? 1u << 30 : 0);
    if (L.type == 2 || L.canUseShadowMap) {
        const float co = L.cosHalfFovOuter, x = 1 / (L.cosHalfFovInner - co);
        g.spot = {x, -co * x, co * bR / std::sqrt(1 - co * co), 1 / df};
    } else g.spot = {0, 0, 0, 1 / df};
    g.iesLookup = (def && def->iesProfile) ? f32tof16(def->coordOffset) | f32tof16(def->coordScale) << 16 : 0;
    return true;
}
```

Per frame: frustum-cull, sort by distance from the camera to the origin, and keep the nearest **256** (the game's
rule). Upload them as a `StructuredBuffer<GameLight>`. The `cbuffer Lights` of `render.cpp:216-217` (64 lights)
cannot hold 256 x 96 B in a useful way.

### 5.3 HLSL (the shader's math, world space)

```hlsl
StructuredBuffer<GameLight> gameLights : register(t20);
Texture1D<float> iesStrip : register(t21);           // *ieslookup level 0, R8_UNORM; texel 0 white
float3 segPoint(GameLight l, float3 p) {
    return l.pos + clamp(dot(p - l.pos, l.bulbDir) / dot(l.bulbDir, l.bulbDir), -1.0, 1.0) * l.bulbDir;
}
void gameLocalLight(GameLight l, float3 P, float3 N, float3 V, float3 diffuseColor, float3 f0, float gp,
                    float cavity, float shadow, inout float3 diffSum, inout float3 specSum) {
    float3 Q = segPoint(l, P), v = Q - P; float d = length(v); float3 L = v / d;
    bool isSpot = ((l.packed >> 8) & 3) == 2;
    float3 Lc = isSpot ? normalize(Q + l.dir * l.spot.z - P) : L;
    float front = (!isSpot || dot(v, l.dir) > 0.0) ? 1.0 : 0.0;
    float cosA = dot(l.dir, Lc), s2a = 1.0 - min(cosA * cosA, 1.0);
    float u = cosA > 0.0 ? 0.5 * s2a : 1.0 - 0.5 * s2a;
    float2 ies = f16tof32(uint2(l.iesLookup & 0xffff, l.iesLookup >> 16));
    float prof = iesStrip.SampleLevel(linearClamp, ies.x + u * ies.y, 0);
    float spotF = saturate(cosA * l.spot.x + l.spot.y);
    float w = saturate(l.spot.w * (1.0 - d * l.invRadius)) * (isSpot ? spotF : 1.0);
    float att = prof * front * w * w * (3.0 - 2.0 * w) * lerp(1.0, shadow, isSpot ? 1.0 : spotF);
    if (att <= 0.0) return;
    float3 X = P + (N + L) * (d / (1.0 + dot(N, L)));
    float3 vd = segPoint(l, X) - P; float dd = length(vd); float3 Ld = vd / dd;
    float NdotL = dot(N, Ld), s2 = l.bulbRadius * l.bulbRadius / (dd * dd), y = NdotL * rsqrt(s2);
    float E = att * ((1.0 - y * y > 0.0) ? pow(saturate(s2 * (0.5 + 0.5 * y)), 1.5) : saturate(NdotL * s2));
    float3 R = reflect(-V, N), QP = Q - P; float aR = dot(l.bulbDir, R);
    float t = clamp((aR * dot(R, QP) - dot(l.bulbDir, QP)) / (dot(l.bulbDir, l.bulbDir) - aR * aR), -1.5, 1.5);
    t -= t * t * t * (4.0 / 27.0);
    float3 S = QP + t * l.bulbDir, perp = R * dot(S, R) - S;
    float x = l.bulbRadius * rsqrt(max(dot(perp, perp), 1e-8)); x *= rsqrt(x * x + 0.125);
    float3 Hs = normalize(normalize(S + perp * x) + V);
    float a2 = 2.0 / (2.0 + exp2(20.0 * gp)), NdotH = dot(N, Hs), NdotV = saturate(dot(N, V));
    float D = a2 / sq(NdotH * NdotH * (a2 - 1.0) + 1.0);            // sq(x) = x * x
    float Vis = 1.0 / ((abs(NdotL) + sqrt(a2 + (1.0 - a2) * NdotL * NdotL)) * (NdotV + sqrt(a2 + (1.0 - a2) * NdotV * NdotV)));
    float ap = pow(a2, 0.748925), norm = ap / (ap + 0.375892 * l.bulbRadius * rsqrt(dot(S, S)));
    float3 F = f0 + (1.0 - f0) * pow(1.0 - abs(dot(V, normalize(Ld + V))), 5.0);
    float2 AB = gameEnvBrdf(saturate(NdotL), gp);                 // the LUT render.cpp already samples
    float3 Fl = f0 * AB.x + AB.y;
    diffSum += diffuseColor * l.diffuse * E * (1.0 - max(Fl.r, max(Fl.g, Fl.b)));
    specSum += l.diffuse * F * (D * Vis * norm * E * cavity);
}
```

- Call it in ps_tex's stockRef branch next to the sun. `diffuseColor` (`diffAlbedo`), `f0`, `gp` (g') and `cavity`
  (`cavS`) are the values that branch already computes (`render.cpp:598-628`; the sun at 694-706). Add the result to
  `c` before fog and post, with no AO.
- Props (`propLit`) get the same call (SM is identical).
- The sampler for the strip: linear, clamp, level 0.

### 5.4 Shadows

For each `canUseShadowMap` light among the 256 (or the nearest N within a budget), render a depth map once. Static
world and stock props only, as the game's static cache does; editor-placed props can join on change.
- Camera at `origin + dir x offset`, looking along `-dir` with `up` for roll; FOV `2 acos(cosOuter)`;
  near = offset + `shadowNearPlaneBias`; far = offset + radius. offset is 0 for a 1-unit bulb, or section 4.2's
  formula.
- Store `(far - z) / (far - near)`.
- Look up as `saturate((exp(k (z_r - z_s)) - b) / (1 - b))` with `k = 88 - 87 softness`, `b = shadowBias`. A 3x3
  blur of the stored depth stands in for the game's blur pass.
- Shadowed omnis use the same single frustum (half-angle = cosOuter) and apply it with weight `saturate(cosA x + y)`.
- 256 to 512 texels per light matches the game's quality.

### 5.5 Simplest first step, then fallbacks

1. **First step** (no new GPU resources): in `loadStockSun` keep types 2/3 as well. Fill the existing `PixelLight`s:
   colour = `intensity / (1 - c)^2 x colorLinearSrgb`, pos, radius, dir and cones. Change the Game Lighting loop
   (`render.cpp:712-735`) to:
   - `w = saturate((1 - d/r) / df) x spotRamp`, `att = w^2 (3 - 2w)` (today: `w = (1 - d/r) x spot`, which is too
     dark between 0 and 80 % of the radius);
   - `E = min(NdotL / d^2, 1 / bR^2)`;
   - the spot axis taken as `-dir`;
   - specular from 3.2 with a point light (bulbDir = 0).

   Sort by camera distance and keep the nearest 64 until 5.2's buffer exists. Most hideout lights use `ies_6`, which
   halves the light at about 37 degrees, so without IES the cones look too wide. Add a per-light profile curve
   (128 floats) next.
2. **No IES export**: use the built-in curves of 3.4 keyed by `defName` (the profiles are the same in every zone),
   and 1 for `light_point_linear` / unknown names (`ies_06`).
3. **No tube**: bulbDir = (1/255, 1/255, 1/255) (the game's own zero value) makes it a sphere light. The 69 hideout
   tubes (half-length up to 50: light 26 is a 100-unit tube) then lose their elongated highlight, and the floor
   pattern shrinks slightly.
4. **No shadows**: lights leak through walls within their radius. The game also leaves 54 hideout lights unshadowed,
   and drops shadows beyond its candidate limit.
5. **Own maps** are not this path. The converter's world shader (research 95, last section) divides local lights by
   `MAP_LIGHT_UNIT_SCALE`; keep the current pixel-light path for them.

### 5.6 Checks (mp_hideout; from `locallight.py`, which transcribes 3.1-3.2; not in-game measurements)

Packed records:

| Light | Record |
|---|---|
| 82, spot, `ies_6`, tube | diffuse (26087.38, 32150.43, 33297.63); spot (2.003814, -1.001907, 1.154700, 5.0); bulbDir (0.00013, 25, 0); bulbRadius 2; invRadius 0.004; iesLookup halves (0.2509766, 0.4980469) |
| 118, omni, shadowed | diffuse (980.77, 714.32, 322.23); spot (1.096621, -0.095577, 0.087489, 5.0); bulbRadius 1; iesLookup 0 |
| 9, spot, bulb 5 | diffuse (3663.95, 3120.85, 2184.20); spot (2.350332, -1.348095, 3.501038, 5.0) |

Diffuse E (rgb, before albedo, shadow 1) with N = (0, 0, 1) unless noted:

| Light | Point | E | att | cosA |
|---|---|---|---|---|
| 82 | floor below, (-128.006, 392.382, 176.0) | (8.382, 10.330, 10.699) | 0.9804 | 1.0 |
| 82 | 100 below the origin | (10.230, 12.608, 13.058) | 0.9804 | 1.0 |
| 82 | 0.85 R below | (1.912, 2.356, 2.440) | 0.8272 | 1.0 |
| 82 | 100 units at 30 degrees, (-78.01, 392.38, 199.87) | (5.585, 6.884, 7.129) | 0.6181 | 0.8684 |
| 82 | wall (-68.01, 392.38, 246.48), N = (-1, 0, 0) | (0.0519, 0.0640, 0.0663) | 0.0031 | 0.5648 |
| 118 | 100 below | (0.0981, 0.0714, 0.0322) | 1.0 | 1.0 |
| 9 | 100 below | (8.982, 7.650, 5.354) | 0.9805 | 1.0 |

- Specular of light 82 at its floor point (100 below), V = normalize(0.05, 0, 1), f0 0.04, cavity 1 (near mirror):
  g' 0.3 gives (3.06, 3.77, 3.90), g' 0.6 gives (55.0, 67.8, 70.2), g' 0.9 gives (6.85, 8.44, 8.74). The narrow lobe
  just misses the 2-unit tube.
- Viewport: the floor under light 82 should be about 5 times its lightmap-only brightness (E 9.9 against C of about
  2). The cone edge should be soft from the IES (half at about 37 degrees) well inside the 60-degree outer cone.
- Totals: hideout 124 lights listed, shipment 22, speedball 0, aniyah 267 of which the nearest 256 are drawn.

## 6. Not established

- Replay's shadow-light selection (`R_ChooseShadowedLights`), its cache sizes and limits, and the shadow camera
  (`R_SetViewParmsForLight`). Those come from GT only. The stored depth's sign convention is INFERRED from the
  downsample shader.
- When the t94 screen-space shadow mask path (`cb7[109].x < 0`) is used, and what writes it.
- The identity of `cb7[52].x` (words per tile) and `cb7[54].x` (width), and the tile-mask pass itself.
- Whether the lightmap and light grid hold the local lights' bounce (likely; the data test was inconclusive).
- `ies_06` (aniyah) resolution; the meaning of `GfxLight.flags` 2/4/0x10 at runtime; `isVolumetric` and `fadeOffset`
  (volumetric fog, not read).
- The `spawnFlags` 8/32 names (only their correlation with the shadow bytes is VERIFIED).
- In-game appearance (no run).

## 7. Reproduce

Scripts and outputs in `out/agent/local_lights` (Python 3 with numpy, pefile, capstone). Paths to the world cache and
the exe are inside the scripts.

| Script | What it does |
|---|---|
| `lights.py [zones]` | ComWorld primary lights to `<zone>_lights.json` (kept) and field statistics |
| `categories.py [zones]` | the section 1.2 table |
| `ies.py <zone>` | decodes the exported `*ieslookup` DDS and lightdefs to `ies_<zone>.json` (kept) and prints the profile curves |
| `locallight.py <zone> [check]` | the reference model: `pack` (3.1), `attenuation`, `diffuse_E`, `specular` (3.2); `check` prints 5.6 |
| `geom.py <zone>` | world triangles with lightmap UVs to `<zone>_geom.npz` (about 9 MB; deleted, rerun to rebuild) |
| `lmtest.py`, `lmtest_core.py`, `occl.py <zone> 40` | the section 1.3 test (`occl.py` writes `<zone>_occl.json`, kept) |
| `bounce.py <zone>` | the inconclusive bounce comparison |
| `pe.py` | read-only PE helper: `secs`, `const <hex>`, `xref <rva>`, `calls <rva>`, `func <rva>`, `dis <rva> [n]`, `disf <rva>` |
| `asm/*.asm` | Replay disassembly: init 0x18D96E5, add 0x186C2D0, binning 0x186C4F0, packing 0x19A9020 and 0x19A95D0 / 0x19A9638 |

ACTS (output root kept short):
- `acts --noUpdater fastfile -r mw19replay -g <replay exe> --oodle <oo2core dll> -a lightdef -l -o x <zone>.ff`
- `acts ... -a image -n "*ieslookup" -l -o x <zone>.ff`

The outputs are in `x/mw19replay/<zone>/assets/` (a few KB). No zone was decompressed.
