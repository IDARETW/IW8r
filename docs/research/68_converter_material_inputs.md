# 68. What the converter reads from a material definition

Roadmap 12.3. Read-only research. Nothing was built or edited.

Sources (all read-only):
- Converter: `E:\mw124\x64-zt\src\convert\iw3_iw8\iw3_render_assets.cpp` (cited as `RA:`)
- Converter header: `E:\mw124\x64-zt\src\convert\iw3_iw8\iw3_render_assets.h` (cited as `RAH:`)
- Editor writers: `E:\IW8-Radiant\src\core\compile_iw8.cpp` (`customMaterialJson`, lines 1008-1024) and
  `E:\IW8-Radiant\src\core\fx_author.cpp` (`authoredFxMaterialJson`, lines 176-193)

## 0. Which reader does what

The converter has two material JSON readers. Both take a material name and open `<exportRoot>/materials/<name>.json`
(`FindMaterial`, RA:1954-1979; names beginning `mc/` also try `materials/<name without mc/>.json`).

| Reader | Lines | Used for | Output struct |
|---|---|---|---|
| `ReadMaterial` | RA:2539-2990 | World, model and surface materials. Called at RA:5234, RA:5300, RA:7684 | `SourceMaterial` (RA:83-207) |
| `ReadFxMaterial` | RA:1996-2349 | FX / particle materials. Called at RA:8371 | `SourceFxMaterial` (RA:223-~270) |

Most of the keys named in the brief (`surfaceTypeBits`, `stateBits`, `stateBitsEntry`, `sortKey`, `textures`,
`constants`) are read by `ReadMaterial`. The keys `cameraRegion`, `stateFlags`, `samplerState`, `_game`, `_type`,
`_version`, `textureAtlas` and `frame_blend` are read only by `ReadFxMaterial`. Sections 1-7 cover `ReadMaterial`
unless marked FX.

`ReadMaterial` takes its name from the request, not from the file. Its `name` argument is stored at RA:2565.
Unlisted helpers used: `ReadJson` (RA:535-546, parses the whole file with nlohmann), `Literal<N>` (RA:1937-1952),
`FxLiteral` (RA:1981-1994), `DecodeFloats` (RA:2432, water only).

Parsing rules that apply to every field: `source.value(key, default)` returns the default when the key is absent.
`source.at(key)` throws when the key is absent. A JSON type mismatch makes nlohmann throw (assumed; not checked per field).

## 1. Top-level fields read by ReadMaterial

Default values are those used when the key is missing. "Built material" means the `SourceMaterial` fields and the
`flags` bits they set (`SourceMaterial` RA:83-207; `MaterialPlan` RAH:34-82; flag constants RAH:19-23).

| Field | Type | Default if missing | Effect | Lines |
|---|---|---|---|---|
| `surfaceTypeBits` | unsigned | 0 | `surfaceTypeBits`, copied to `MaterialPlan::sourceSurfaceTypeBits` (RAH:49) | RA:2566, RA:86 |
| `gameFlags` | array of strings | absent: `castsShadow` stays true | If present and not an array, throws. `castsShadow` = array contains `"CASTS_SHADOW"` | RA:2567-2576 |
| `techniqueSet` | string | **required** (`.at`, throws) | Drives the name rules in section 4 (`flagWave`, `shadowOnly`, `skipped`, `authoredAlpha`, `blended`, falloff, water, unlit) | RA:2577 |
| `iw7FlagWave` | array of exactly 12 numbers | absent: none | Sets `iw7FlagWave` and `flagWave = true`. Wrong size throws | RA:2579-2587 |
| `iw7FlagClassic` | bool | false | Only read inside the `iw7FlagWave` branch. Sets `iw7FlagClassic` | RA:2588 |
| `iw7ReactiveMotion` | bool | false | `reactiveMotion` (foliage bends by reactive-motion sim) | RA:2591 |
| `iw7Flow` | object: `map`, `surface` (strings), `scale[0]`, `period[0]`, `period[2]`, `hdr` (bool) | absent: no flow | `flow`. Throws on empty map or surface, `..` in a name, non-finite scale, or period <= 0 | RA:2592-2606 |
| `iw7ModelUvd` | object: `map`, `scroll[4]`, `strength[2]`, optional `cells[2]` | absent: no UV distortion | `uvd`. Throws on bad values, or when `iw7Flow` is also present | RA:2607-2624 |
| `iw7ShadowOnly` | bool | false | `entityNoDraw`; also sets `staticModelShadowOnly` | RA:2628-2629 |
| `stateBits` | array of objects | `[]` | See section 2 | RA:2637, 2760, 2790, 2835 |
| `stateBitsEntry` | array of ints | absent | See section 3 | RA:2786, 2832, 2960 |
| `textures` | array of objects | `[]` | See section 5 | RA:2660-2681 |
| `unlitMask` | bool | absent: `response` comes from `specularMap` only | If present: `unlitMask` = value, and `response` = the `maskMap` image if true, else empty. Overwrites a `specularMap` response | RA:2684-2688 |
| `skyObject` | bool | false | `skyObject` (drawn at infinity, no distance fog). Note: a techset matching `(^|_)sky($|_)` returns before this, RA:2649-2653 | RA:2689 |
| `replayLayers` | array of `{material (string, required), multiply (bool, false), reveal (float, optional), alpha (bool, false)}` | `[]` | Stock layered material: `replayLayers` | RA:2690-2699 |
| `replaySpecularAlpha` | bool | false | `replaySpecularAlpha` | RA:2700 |
| `replayGloss` | bool | false | `replayGloss` | RA:2701 |
| `replayStockOnly` | bool | false | `replayStockOnly`. Required to be true when `iw7Distortion` is present | RA:2702, 2709-2710 |
| `iw7Distortion` | object: `map` (string), `uvAnimation[4]`, `scale[2]` | absent: none | `distortionMap`, `distortionUvAnimation`, `distortionScale`. Throws unless `map` is non-empty and `replayStockOnly` is true | RA:2703-2711 |
| `replayRevealSharpness` | float | absent: none | `replayRevealSharpness` (optional) | RA:2712-2713 |
| `replayAlpha` | bool | false | `replayAlpha` | RA:2714 |
| `replayEmission` | array of 3 floats | absent: none | `replayEmission`. Each channel must be finite, 0 to 65504, else throws | RA:2716-2727 |
| `constants` | array of `{name, literal}` | `[]` | See section 6 | RA:2728-2759 (and 2876, 2913, 2936) |
| `iw7DecalBias` | bool | false | `depthBias = true` (decal bias) | RA:2785 |
| `sortKey` | unsigned | 4 | `sortKey`. If > 8, flag bits 12-17 get `min(63, sortKey - 8)` (layer rank). Glass sorts are mapped to translucent sort keys | RA:2802, 2820-2821, 7977-7981, 3000-3018 |
| `iw7BackFaceNormals` | bool | false | Flag bit 21 (`kBackFaceNormalsFlag`) | RA:2979-2980; RAH:19 |
| `iw7RevealSharpness` | float | absent: no reveal layer | Presence alone sets flag bit 22 (`kRevealLayerFlag`). `revealSharpness` = max(value, 1e-6) | RA:2981-2986; RAH:23 |
| `iw7ClampAxes` | unsigned | 0 | `clampAxes` = value & 3 (bit 0 U, bit 1 V) | RA:2987 |
| `iw7VolumetricExtinction` | bool | false | `volumetricExtinction` | RA:2988 |

Not read by `ReadMaterial`: `_game`, `_type`, `_version` (FX only, RA:2068), `samplerState` (FX only, RA:2217),
`cameraRegion` (FX only), `stateFlags` (FX only), `textureAtlas` (FX only).

Fields that `ReadMaterial` derives from others (not read from JSON directly):
- `flagWave` is true when `techniqueSet` starts with `mc_` and contains `_flag_` (RA:2578), or when `iw7FlagWave` is present (RA:2589).
- `shadowOnly` / `kind = skipped` when `techniqueSet == "shadowcaster"` or the name contains `caulk_shadow` (RA:2627-2648).
- `staticModelShadowOnly` also true for `mc_shadowcaster` (RA:2629-2630).
- `flags` bit 32 (normal map) when a normal image other than `$identitynormalmap` exists (RA:2949-2950). Bit 64 when a response image exists (RA:2951-2952). Bit 128 (unlit) per section 4 (RA:2967-2968). Bit 20 (emission gain) when unlit and emission > 1 (RA:2971-2975). Bits 18-19 = polygon offset level (RA:2978).
- `cullMode` default 1 (back). See section 2.

Constant-driven fields (`tint`, `detailScale`, `environment`, `emission`, `uvAnimation`, `maskUvAnimation`, `transmission`) are in section 6.

## 2. `stateBits`

`stateBits` is an array of state objects. The converter reads it in four places. The first colour state is the one with
`colorWriteRgb` true and `polymodeLine` not true.

### 2a. Keys read by ReadMaterial (with defaults)

| Key | Read at | Default | Accepted values / comparison | Effect |
|---|---|---|---|---|
| `cullFace` | RA:2639-2644 (shadowcaster path, first state that has the key only), RA:2765-2768 (first colour state) | `"back"` in the colour path (RA:2765). The shadow path skips states without the key (RA:2640) | `"none"`, `"back"`, `"front"`. Any other string throws (RA:2642, 2766) | `cullMode`: none = 0, back = 1, front = 2 (RA:2768). Replay: none=0, back=1, front=2 (RAH:43-44) |
| `colorWriteRgb` | RA:2762, 2775, 2844 | false | bool | Selects the colour state (see above). A state without it is never a colour state |
| `polymodeLine` | RA:2763, 2775, 2844 | false | bool | A line-polymode state is never the colour state, never an alpha source, never a blend source |
| `alphaTest` | RA:2771-2777 | `""` | `"gt0"`, `"lt128"`, `"ge128"`. Any other value is ignored | Any of these sets `authoredAlpha` = true (cut-out). `alphaMode` = the value, only from a colour state (RA:2775-2776). The last such state wins (no break in the loop) |
| `polygonOffset` | RA:2794-2795 (stateBitsEntry index 7 only), RA:2846-2847 (first valid of indices 7, 8, 9, 4, 5) | `"offset0"` | `"offset0"`, `"offset1"`, `"offset2"`, `"offsetShadowmap"`. Other strings are not validated | Index 7: any value other than `offset0` and `offsetShadowmap` sets `depthBias` (RA:2795). Polygon offset level: `offset1` = 1, `offset2` = 2, anything else = 0 (RA:2847). Level goes to flag bits 18-19 (RA:2978) |
| `srcBlendRgb` | RA:2848 | `"one"` | Compared against: `srcalpha`, `one`, `invdestcolor`, `zero`, `destcolor` (RA:2850-2856) | Blend classification (below) |
| `dstBlendRgb` | RA:2849 | `"zero"` | Compared against: `invsrcalpha`, `one`, `srccolor`, `zero`, `destcolor` (RA:2850-2856) | Blend classification (below) |
| `depthWrite` | RA:2850 | true | bool | Only matters with `srcalpha`/`invsrcalpha` (glass state, below) |

Keys read by ReadMaterial's shadowcaster path only: `cullFace` (RA:2639-2644).

No `colorWriteAlpha` key is read by any reader (grep of the file: no hit). No `depthTest`, `blendOpRgb` or `cullFace` key is
read by `ReadFxMaterial` except as noted in 2c. `cullFace` is not read by `ReadFxMaterial`.

### 2b. Blend classification (ReadMaterial)

Only the states named by `stateBitsEntry` indices 7, 8, 9, 4, 5 are examined, in that order. The loop breaks after the
first state that is a colour state (RA:2836-2859). Without `stateBitsEntry`, no blend is set from states at all
(see section 3). `src` defaults to `one`, `dst` defaults to `zero`.

| Source / destination (first matching rule wins as listed) | Result | Lines |
|---|---|---|
| `srcalpha` / `invsrcalpha` and `depthWrite` false | `stateBlended = true` -> `kind = glass` (alpha blended, no depth write) | RA:2850-2851, 2897-2899 |
| `dst one` and `src one` or `src invdestcolor` | `blend = 1` (additive; IW3 "screen" adds in Replay's HDR target) | RA:2852-2853 |
| `dst one` and `src srcalpha` | `blend = 3` (srcalpha additive; drawn one/one with alpha applied in shader) | RA:2854-2855; RAH:137 |
| `src zero` / `dst srccolor`, or `src destcolor` / `dst zero` | `blend = 2` (multiply) | RA:2856-2857 |
| anything else | `blend` stays 0 (alpha or opaque) | RA:2858 |

Downstream effects of `blend` != 0 (RA:2861-2866): `castsShadow = false`; flags bit 9 (256) for blend 1 or 3, bit 10 (512)
for blend 2. `kind` = `glass` (RA:2897). Water overrides `blend` to 0 (RA:2921).

Glass blend bits sent to Replay (RAH:131-140): blend 1 -> one/one (`0x8010`), blend 2 -> zero/srccolor (`0x8821`),
blend 3 -> one/one with the shader applying alpha (`0x8010`), blend 0 -> srcalpha/invsrcalpha (`0x28050`).

### 2c. Keys read by ReadFxMaterial (FX materials, RA:2243-2294)

Every colour state (`colorWriteRgb` true, `polymodeLine` not true) must have all of these keys, because `.at` is used:
`depthTest`, `blendOpRgb`, `depthWrite` (`get<bool>`), `alphaTest`, `srcBlendRgb`, `dstBlendRgb`. A second colour state throws
(RA:2248-2249). No colour state throws (RA:2295-2296).

| Derived flag | Rule | Lines |
|---|---|---|
| `noDepth` | `depthTest` is `"always"` or `"disabled"` | RA:2251, 2255 |
| `shared` | `blendOpRgb == "add"` and (`depthTest == "less_equal"` or noDepth) and `depthWrite` false | RA:2252-2254 |
| `cutout` | `alphaTest == "ge128"` and `srcBlendRgb == "one"` and `dstBlendRgb == "zero"` | RA:2258-2260 |
| `alpha` | (`alphaTest == "gt0"`, `src srcalpha`, `dst invsrcalpha`) or `cutout` | RA:2261-2264 |
| `additive` | `alphaTest == "disabled"`, `src` one or invdestcolor, `dst one` | RA:2266-2269 |
| `alphaAdditive` | `src srcalpha`, `dst one` | RA:2270-2271 |

Outcome: `shared && alphaAdditive` -> family additive or additiveFeather, `premultiplied = true` (RA:2275-2283).
Otherwise the state is rejected (family becomes `unsupported` with a warning, not a throw) when
`(!shared && !cutout) || (wantsAlpha ? !alpha : !additive)`, where `wantsAlpha` is true for alpha, alphaFeather and cloud
families (RA:2272-2274, 2284-2293).

## 3. `stateBitsEntry`

What it is: an array of integers. Entry `i` is an index into `stateBits` (or a negative value for none). The
comments name the Replay technique slots: 7 = lit (decals offset their lit pass), 8 = lit sun, 9 = lit sun shadow,
4 = unlit, 5 = emissive (RA:2781-2782, 2822-2823, 2953-2957).

Which indices are read and where:

| Index | Read by | Use | Lines |
|---|---|---|---|
| 7 | `depthBias` | Only index 7 sets the decal / polygon-offset bias (`offset` other than `offset0` or `offsetShadowmap`) | RA:2786-2797 (needs size > 7) |
| 7, 8, 9, 4, 5 (first valid colour state, in that order) | `polygonOffset`, `stateBlended`, `blend` | Blend classification (2b) and polygon offset level | RA:2832-2859 |
| 4, 5 and 7 through 20 | `noLitPass` | `noLitPass` = entry 4 or 5 is >= 0 AND entries 7-20 are all < 0. Sets flag 128 (unlit). Needs size > 20 | RA:2960-2968 |

Behaviour when the field is absent, or not an array:
- The `depthBias` check is skipped (RA:2786-2787). Decal bias then comes only from `iw7DecalBias` (RA:2785).
- The blend and stateBlended loop is skipped (RA:2832-2834). `blend` stays 0. `polygonOffset` stays 0. The kind comes only from the name rules (section 4) and `alphaTest` (section 2a).
- `noLitPass` is skipped (RA:2960-2962). No flag 128 from this rule.
- An entry with an index beyond the `stateBits` array, or a negative index, is skipped (RA:2841-2842).
- An array shorter than 8 entries still works for the blend loop (indices past the end are skipped, RA:2838-2839).

`ReadFxMaterial` does not read `stateBitsEntry`.

## 4. Technique set name rules

These are exact comparisons in the code. Quoted as written.

### 4a. ReadMaterial (world, model, surface materials)

| Rule | Quoted code | Effect | Line |
|---|---|---|---|
| Sky | `std::regex_search(techset, std::regex("(^|_)sky($|_)", std::regex::icase))` | `kind = skipped`; returns before textures and states | RA:2649-2653 |
| Shadow caster | `techset == "shadowcaster" \|\| caulkShadow` where `caulkShadow = name.find("caulk_shadow") != npos` (the material name, not the techset) | `kind = skipped`, `shadowOnly = true`, returns after the cull state | RA:2627, 2633-2648 |
| Static model shadow | `techset == "mc_shadowcaster"` (also `shadowcaster`, `caulk_shadow`, `iw7ShadowOnly`) | `staticModelShadowOnly` | RA:2629-2630 |
| Alpha tested (name) | `std::regex_search(techset, std::regex("(^|_)[at][0-9]", std::regex::icase))` OR `techset.find("alphatest") != npos` | `authoredAlpha = true` (cut-out kind unless blended) | RA:2654-2656 |
| Blended (name) | `std::regex_search(techset, std::regex("(^|_)b[0-9]", std::regex::icase))` | `blended = true` -> `kind = glass` | RA:2657, 2897-2899 |
| Flag wave | `techset.starts_with("mc_") && techset.find("_flag_") != npos` | `flagWave` | RA:2578 |
| Falloff | `techset.find("falloff") != npos` | Falloff parms, tint multiply, flag 1024, environment override | RA:2873-2896 |
| Feather | `techset.find("zfeather") != npos` (only inside the falloff block) | `featherParms.x` -> `effect[0]` | RA:2885-2886 |
| Eye offset | `techset.find("eyeoffset") != npos` (only inside the falloff block) | `eyeOffsetParms.x` -> `effect[1]` | RA:2887-2888 |
| Water | `techset.find("water") != npos` | Forces opaque, blend 0, flags 256/512/1024 cleared, colour `$white`, response `$water_specular` | RA:2910-2942 |
| Unlit | `techset.find("unlit") != npos` (or `noLitPass`) | Flag 128 (unlit: colour map shown as emitted light) | RA:2967-2968 |
| Glass name exception | `std::regex_search(name, std::regex("glass", std::regex::icase))` (material name, not techset) | A glass-kind layer with `blend` 0 gets flag 2048 (lit like the surface under it) unless the name matches | RA:2904-2906 |

Name rules that are NOT in ReadMaterial: no rule for `add`, `multiply`, `effect`, `zfeather` as a blend, `distortion`,
`emissive`, `lamp` or `screen`. The comments at RA:2824-2830 and RA:2953-2958 mention `wc_unlit_add`, `wc_unlit_multiply`,
`mc_effect`, `lamp` and `screen`, but the code has no matching check. Additive and multiply come only from the state
(section 2b), and an unlit flag comes only from the substring `unlit` or from `noLitPass`.

### 4b. ReadFxMaterial (FX / particle materials)

Exact matches on the technique set with `_eyeoffset` removed (RA:2012-2014). `_nofog` aliases are handled at RA:2015.

| Technique set (exact) | Family | Line |
|---|---|---|
| `effect`, `effect_nofog` | alpha | RA:2016-2017 |
| `effect_zfeather`, `effect_zfeather_nofog` | alphaFeather | RA:2018-2020 |
| `effect_zfeather_add`, `effect_zfeather_add_nofog` | additiveFeather | RA:2021-2023 |
| `effect_add_nofog`, `effect_add`, `particle_cloud_add`, `particle_cloud_outdoor_add`, `effect_falloff_add`, `effect_falloff_add_nofog` | additive | RA:2027-2032 |
| `effect_falloff`, `effect_falloff_nofog` | alpha | RA:2035-2037 |
| `effect_zfeather_falloff`, `effect_zfeather_falloff_nofog`, `effect_zfeather_spot`, `effect_zfeather_spot_nofog` | alphaFeather | RA:2038-2042 |
| `effect_zfeather_falloff_add`, `effect_zfeather_falloff_add_nofog` | additiveFeather | RA:2045-2047 |
| starts with `distortion` (`technique.starts_with("distortion")`) | distortion (returns early; reads only `colorMap` image and `distortionScale`) | RA:2048-2058 |
| `particle_cloud`, `particle_cloud_outdoor` | cloud | RA:2059-2061 |
| `std::regex_match(techniqueSet, std::regex("[mw]c_l_sm_b0c0(n0)?(s0)?"))` (full match, no suffix stripping) | decal | RA:2062-2064 |
| anything else | unsupported (returns) | RA:2065-2066 |

Other FX string tests: `_spot` in the technique set makes the spot-shadow constant skipped (RA:2333-2334). `falloff` and
`eyeoffset` substrings require their constants (RA:2310-2311, 2338-2340).

## 5. `textures`

ReadMaterial loop (RA:2660-2681). Each entry: `semantic` (string, default `""`) and `name` (string, default `""`).
Image is `image` (string, **required** with `.at` for each recognised entry).

| Condition (evaluated in this order; first match wins) | Stored as | Used for | Lines |
|---|---|---|---|
| `semantic == "detailMap"` OR `name == "detailMap"` | `detail` | IW3 detail map (lit detail variant). The name wins, because IW3 exports detail with the colour semantic | RA:2665-2666 |
| `semantic == "colorMap"` | `color` | Base colour. Required: a material with no colour image throws `IW3 material has no color map` (RA:2779-2780). Last one wins | RA:2667-2668 |
| `semantic == "normalMap"` | `normal` | Normal map. Sets flag 32 unless `$identitynormalmap` (RA:2949-2950) | RA:2669-2670 |
| `semantic == "specularMap"` | `response` | Response (gloss / specular). Sets flag 64 (RA:2951-2952). Overwritten by `unlitMask` if that key is present | RA:2671-2672 |
| `semantic == "replayColorMap"` | `replayColor` | Stock Replay world colour (IW7 colour with its specular alpha) | RA:2673-2674 |
| `semantic == "replayNormalGlossMap"` | `replayNormalGloss` | Stock Replay packed normal with gloss in red | RA:2675-2676 |
| `semantic == "replayRevealMap"` | `replayReveal` | Stock layer alpha / reveal / thickness | RA:2677-2678 |
| `semantic == "maskMap"` | `mask` | Unlit mask. Copied to `response` when `unlitMask` is true | RA:2679-2680, 2684-2687 |

Unrecognised semantics are ignored. `samplerState` is NOT read by ReadMaterial (its only read is at RA:2217, in
`ReadFxMaterial`). Texture `name` other than `detailMap` is ignored by ReadMaterial.

Special textures: `waterMap` semantic with a `water` object (`m`, `n`, `h0`, `wTerm`) builds the wave spectrum, only inside
a water material (RA:2925-2941). Requires `m == n`, `m` a power of two, `m <= 64`.

FX materials (ReadFxMaterial): exactly one texture for sprite families (RA:2203-2209), which must have `name` and
`semantic` both `colorMap`. The sampler is read from `texture.samplerState` (RA:2217-2219):
- `filter`: `linear`, `aniso2x`, or `aniso4x` (only for `particle_cloud_outdoor`) (RA:2227-2228)
- `mipMap`: `disabled`, `nearest`, or `linear` (RA:2233)
- `clampU`, `clampV`, `clampW` (bool). Cloud families require all true. Other families require all true or all false (RA:2220-2231)
- Failing sampler does not throw: the family becomes `unsupported` with a warning (RA:2231-2241).
Decal FX (RA:2147-2168): 1 to 3 textures, semantics `colorMap`, `normalMap`, `specularMap`. A `$`-name other than the colour
map is skipped (RA:2157-2158). Any other semantic throws.

## 6. `constants`

Each entry: `{ "name": string, "literal": ... }`. Read with `Literal<4>` (RA:1937-1952): the literal must be an array
of exactly the required size, otherwise the fallback is used silently. Non-finite values, values above 64 in absolute
value, and negative values (unless `signedValues`) throw `invalid IW3 material constant`.

| Name | Read at | Effect | Default |
|---|---|---|---|
| `colorTint` | RA:2731-2732 | `tint` (RGBA multiplier on the colour map). Range 0 to 64 | `{1,1,1,1}` (RA:187) |
| `detailScale` | RA:2733-2745 | `detailScale[0..1]`. Needs an array of at least 2. Each axis non-zero, `abs <= 4096`, finite; otherwise throws `invalid IW3 detailScale` | `{1,1}` |
| `envMapParms` | RA:2746-2747 | `environment` (RGBA): reflection parameters | `{0.8, 4.0, 2.5, 0.625}` (RA:188) |
| `emissionScale` | RA:2748-2749 | `emission` = first component of the literal (Literal fallback `{1,1,1,1}`) | 1 |
| `uvAnimParms` | RA:2750-2758 | `uvAnimation[0..2]`. No range check beyond finite reads; short literal throws (`.at`) | `{0,0,0}` |
| `maskUvAnimParms` | RA:2750-2758 | `maskUvAnimation[0..2]` | `{0,0,0}` |
| `transmission` | RA:2750-2758 | `transmission[0..2]` (light through to the back side) | `{0,0,0}` |
| `falloffParms` | RA:2879-2880 (falloff techset only) | Signed allowed. Sets `parms`; `parms[2]`, `parms[3]` become `environment[0..1]` and `environment[2] = endLevel / beginLevel` | `{0, 0, 2.128, -0.064}` |
| `falloffBeginColor` | RA:2881-2882 (falloff only) | `begin`; multiplies `tint` RGB (RA:2892-2893) | `{1,1,1,1}` |
| `falloffEndColor` | RA:2883-2884 (falloff only) | `end` | `{.5,.5,.5,.5}` |
| `featherParms` | RA:2885-2886 (only when techset has `zfeather`) | `effect[0]` = `literal[0]` (near fade) | 0 |
| `eyeOffsetParms` | RA:2887-2888 (only when techset has `eyeoffset`) | `effect[1]` = `literal[0]` (eye offset toward camera) | 0 |
| `waterColor` | RA:2913-2915 (water only) | Water colour; `tint` RGB | `{0.1,0.1,0.1,1}` |
| `envMapParms` (water) | RA:2936-2938 | Water environment | `{0.1, 0.95, 5.0, 2.5}` |

Note: `environment` is also overwritten by the falloff block (RA:2894) and by `emission` for unlit with gain (RA:2974).

FX constants (ReadFxMaterial, RA:2306-2337): `featherParms` (feather families, must be > 0 at index 0), `colorTint` (required
except cloud, which must have no constants at all, RA:2300), `falloffParms`, `falloffBeginColor`, `falloffEndColor`
(falloff techset), `eyeOffsetParms` (eyeoffset techset). Any other name throws unless the techset has `_spot`. Required:
`colorTint`, and `featherParms` for feather families (RA:2346-2347). Decal FX requires `colorTint` and accepts only
`colorTint` and `envMapParms` (RA:2172-2185). Distortion FX reads `distortionScale` (RA:2054-2056).

## 7. Table: to get X, write Y

Columns: the JSON to write, what the converter does with it, and whether the editor's current writer
(`customMaterialJson`, compile_iw8.cpp:1011-1024) produces it.

| Want | Write | Converter result | Editor writes it today? |
|---|---|---|---|
| Two-sided | First `stateBits` entry: `{"cullFace":"none","colorWriteRgb":true}` (RA:2762-2768) | `cullMode = 0` | Yes, when `twoSided` (compile_iw8.cpp:1017) |
| Alpha-tested cut-out | `techniqueSet` contains `_a1` (RA:2655), and/or a colour state with `"alphaTest":"gt0"` (flags 8), `"lt128"` (flags 16) or `"ge128"` (flags 0) (RA:2771-2777, 2944) | `kind = cutout` | Yes, `_a1` when `alphaTest` and not blended (compile_iw8.cpp:1015) |
| Alpha blended | `techniqueSet` contains `_b0` (RA:2657). Or a `stateBitsEntry` index in {7,8,9,4,5} pointing to a state with `srcBlendRgb "srcalpha"`, `dstBlendRgb "invsrcalpha"`, `depthWrite false`, `colorWriteRgb true` (RA:2848-2851) | `kind = glass`, `blend = 0` (default alpha blend, 0x28050). Flag 2048 unless the material name contains `glass` (RA:2904-2906) | Yes, `_b0` (compile_iw8.cpp:1015). Stateless path: no `stateBitsEntry` written |
| Additive | `stateBitsEntry` index in {7,8,9,4,5} -> state with `colorWriteRgb true`, `polymodeLine` absent/false, `srcBlendRgb "one"` (or `invdestcolor`), `dstBlendRgb "one"` (RA:2852-2853) | `blend = 1`, `kind = glass`, flags bit 8 (256), `castsShadow = false` (RA:2861-2866) | No. The writer emits no `stateBitsEntry` and no `_b` value for it. The name tag alone (`_b<n>`) gives glass with blend 0, not additive |
| Multiply | Same as additive but `srcBlendRgb "zero"` and `dstBlendRgb "srccolor"`, or `srcBlendRgb "destcolor"` and `dstBlendRgb "zero"` (RA:2856-2857) | `blend = 2`, flags bit 9 (512) | No |
| Decal layering (sort order) | `"sortKey": N` with N > 8 (RA:2802, 2820-2821). Layer rank = N - 8, bits 12-17 | Vertex shaders pull the layer toward the camera by rank (RA:2814-2819). Sort keys for glass go to translucent slots (RA:3000-3018) | No (writer omits `sortKey`, so default 4) |
| Decal layering (polygon offset) | `iw7DecalBias: true` (RA:2785), or a `stateBitsEntry` index 7 -> state with `"polygonOffset":"offset1"` or `"offset2"` (RA:2794-2795, 2847) | `depthBias = true` (and polygon level 1 or 2 into bits 18-19) | No. Note: `sortKey` alone does NOT set depth bias (RA:2803-2805) |
| Unlit / emissive | `techniqueSet` contains `unlit` (RA:2967-2968). Or no lit pass: `stateBitsEntry` entry 4 or 5 >= 0 and entries 7-20 all < 0 (RA:2960-2966). Gain: `constants` `emissionScale` with first value > 1 (RA:2748-2749, 2971-2975) | Flag 128; flag bit 20 and `environment[3] = emission` when gain > 1 | No. Writer never writes `unlit` or `emissionScale` |
| Tinted | `constants`: `{"name":"colorTint","literal":[r,g,b,a]}`, each 0 to 64 (RA:2731-2732) | `tint` (multiplies the colour map). Negative or > 64 throws | No |
| UV detail map | `textures`: `{"name":"detailMap","semantic":"detailMap","image":"<image>"}` (RA:2665-2666), and `constants`: `{"name":"detailScale","literal":[su,sv]}` (RA:2733-2745) | Lit detail variant per distinct scale (RA:7982-7987) | No |

Additional requirement for any of the above: the material needs a `colorMap` texture, because ReadMaterial throws
without it (RA:2779-2780). Sky-named and shadowcaster materials return before this check.

## 8. Open questions and unverified points

1. **Export root.** `FindMaterial` opens `<root>/materials/<name>.json` where `root` is the `exportRoot` passed down from
   `PrepareRenderAssets` (RA:2539-2544, 5238). The editor writes `<root>/extracted/materials/<name>.json`
   (compile_iw8.cpp:2197). I did not trace the exact `exportRoot` value at its call site (iw3_fastfile.cpp:10683 and
   :11997 are the callers I saw). The header comment at iw3_fastfile.h:50 says the extraction cache has an `extracted/`
   layout. It is likely, but not verified, that `exportRoot` is the folder above `extracted/`. Not found: confirmation of how
   IW8 editor output reaches this converter.
2. **Other material JSON readers.** `iw3_fastfile.cpp` reads material JSON in several places (lines 1877-1889, 4253-4258,
   9296-9437). I did not read those. Not checked whether they consume fields not listed here (for example `gameFlags`,
   `stateBits`) for other purposes.
3. **Editor custom materials cannot express additive, multiply or emissive.** `customMaterialJson` (compile_iw8.cpp:1011-1024)
   writes `techniqueSet`, `surfaceTypeBits`, an optional `stateBits` entry (cull none only), and the colour,
   normal and specular textures. It writes no `stateBitsEntry`, `constants`, `sortKey`, `unlit` name tag, or `colorTint`.
   Per the code in section 2b and 3, that means no blend other than the name tag `_b0`, no polygon offset, and no unlit flag.
   It also cannot combine `_a1` and `_b0` (the ternary at line 1015 picks one). This is a reading of the code, not a test run.
4. **`lamp` and `screen` names.** Comments at RA:2953-2954 mention them, but the code has no check for those names. Not found in code.
5. **`colorWriteAlpha`.** Not read by any reader. Not found.
6. **Downstream use of `flags` bits.** I cited the consumers of bits 8/9/10/12-22 by grep only (for example RA:5372-5377,
   5996, 7153, 7217, 7519, 7566-7567, 7638). I did not read the shader side (Replay), so the exact effect of each bit on
   rendering is not verified here.
7. **Blend tag without `stateBitsEntry`.** Section 2b and 3 say that `_b<n>` marks a surface as glass, but the number in
   the tag does not set `blend`. I did not find any other use of the number. Not found.
8. **Type errors.** A JSON type mismatch on a field read with `.value()` or `.at()` makes nlohmann throw. This is assumed, not
   tested per field.
9. **FX sampler and cull.** `ReadFxMaterial` does not read `cullFace` or `polygonOffset`. Whether FX draws use two-sided is not
   covered by the FX reader. Not found.

## 9. Editor cross-check (FX)

`authoredFxMaterialJson` (fx_author.cpp:176-193) writes the keys `ReadFxMaterial` requires: `_game "iw3"`, `_type
"material"`, `_version 1`, `textureAtlas`, `stateFlags 16`, `cameraRegion "emissive"`, one colour state with
`depthTest`, `blendOpRgb`, `depthWrite`, `alphaTest`, `srcBlendRgb`, `dstBlendRgb`, a `colorTint` and (for feather families) a
`featherParms` constant, and a single `colorMap` texture with `samplerState`. Its technique names `effect_zfeather` and
`effect_zfeather_add` match the exact rules in 4b. Its state (`gt0` / `srcalpha` / `invsrcalpha` for alpha, `disabled` /
`srcalpha` / `one` for additive) matches the rules in 2c. Not checked: whether the `featherParms` values it writes pass the
`> 0` check at RA:2317 (they do: 0.0333333).

## Summary of the findings most likely to matter

- Blend, additive, multiply and "lit or not" come from `stateBitsEntry` plus `stateBits`, not from names (except `_b<n>` for glass and `_a<n>` for cut-out).
- The editor's custom-material writer produces only: cut-out (`_a1`), alpha blended (`_b0`), two-sided, and textures. Additive, multiply, emissive, tint, UV detail and decal bias are not written.
- `sortKey` alone does not bias depth; decal bias needs `iw7DecalBias` or a `polygonOffset` on lit state index 7.
