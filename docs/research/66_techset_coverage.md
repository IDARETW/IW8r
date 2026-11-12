# 66. Technique set and material flag coverage (stock IW8 materials)

**Why.** A placed olive tree (`tm/foliage_tree_syrian_olive_02_trunk`) drew its leaves as solid cards. Its canopy
material is alpha tested, but the cut-out image sits in texture slot 27 under a name the cache did not recognise
(`olive_canopy_opacity_leaf_reduction`). The goal is to cover every stock technique set the editor meets, and to have
one place that describes how a set draws, ready for custom technique sets later.

**Evidence.** ACTS dumps of every material body in `techsets_mp_shipment` and `techsets_mp_aniyah_tac` (6,135
materials), surveyed with a script: technique set names, `surfaceFlags`, `materialType`, `sortKey`, `cameraRegion`, and
the texture table (slot index plus image name suffix), grouped by blend mode. ACTS has no technique set pool for MW2019
(`techset` fails, other spellings are unknown pools), so the state is read from the set *name* and the material flags.

## Technique set name grammar

`<prefix>/<family>_<blend>_<table>_<state hex>_<feature hex>_...`

| Family | Count | What it is | Editor |
|---|---|---|---|
| `lit_3_lit` | 6,003 | world and model surfaces | drawn |
| `unlit_6_effect` | 51 | effect sprites and sheets | drawn (blend / additive) |
| `,null` | 51 | info carriers (`MTL_TYPE_INFO_CARRIER`) | hidden |
| `,cust_2_2d` | 13 | 2D / UI | drawn |
| `card_8_lit_card` | 13 | lighting proxy cards (`li_mtl_*_proxy`), GI bake helpers | hidden |
| `sky_3_sky` | 2 | sky | drawn |
| `dist_10_distortion` | 2 | heat haze / water ripples | hidden (no colour of its own) |

Blend tokens: `rpl` replace, `alt` alpha test, `bad` alpha blend, **`bar` alpha blend with reveal: glass** (vehicle
glass, visors, `tw/glass_grunge_02_clear`, 33 materials; read as opaque before this note). Table tokens: `ta0`, `ta1`,
`tca`, `tom`. Counts: rpl_ta1 5,597; bad_tca 221; alt_ta1 144; bad_ta0 39; bad_ta1 29; bar_ta1 27; rpl_ta0 15;
bar_tca 6; alt_tom 3; rpl_tca 2; rpl_tom 1. Unlit `bad_tca` is additive (effects), lit `bad_tca` is a blended decal.

## Texture slots

| Slots | Role | Evidence (suffixes) |
|---|---|---|
| 0-6 | colour, one per layer | `_c`, `_col`, `_albedo` |
| 9-15 | normal + gloss (+ occlusion), one per layer | `_n`, `_nml`, `identitynormalmap` |
| 18-20 | emissive | `_e` |
| **27** | **opacity of alpha-tested and blended sets** (layer 1 for layered rpl) | alt: `_c` 41, `_sdf` 26, `_opacity` 19, `_a` 12, `_r` 9, `_m` 6, `_o` 6 |
| 28-33 | reveal / opacity of layers 2+ | `$white`, `_c`, `_opacity` |
| 36 | detail colour | |
| 45 | thermal | `_thermalmap` |
| 54 / 55 | transition / LUT | `_t`, `_lut` |
| 59-65 | tint per layer | mostly `$black` |
| 68-69 | translucency / absorption (foliage) | `_abs_256`, `_trans`, `_translucency` |
| 77-78 | thickness | |
| 95-99 | height / displacement | `_h`, `_d` |
| 123-125 | cavity / curvature | `_cm` |
| 126 | roughness / dirt reveal | `_r` |
| 128 | sheen | `_sheen` |
| 144 | tint (constant name, no hash) | `_tint_short` |
| 159-164 | impostor albedo, normal, translucence, specular, depth, geometry | |

**All 147 alpha-tested materials have slot 27.** It is a one-channel image (`_sdf`, `_opacity`, `_a`, ...) or the
colour map whose *alpha* is the opacity (`ee_periph_city_ukraine_tree_01_c`, effect sheets). The colour in slot 0
packs specular (or a non-metal mask) in alpha, never opacity.

## Surface flags

`surfaceFlags` bit **0x80 = nodraw**: set on all 16 `*shadowcaster*` / `*shadow_caster*` materials, `tm2o/nodraw`,
`tw/caulk_shadow`, and two blank container logos (21 in all). These surfaces only cast the sun shadow. Foliage models
pair a visible canopy (alt, no 0x80) with a shadowcaster mesh (alt, 0x80) using the same technique set.
Bit 0x400000 differs between the alder canopy and its shadowcaster too, but it is also set on crates, helmets and
monitors, so it is *not* "no shadow"; it is left unread.

## What the editor does now

- `src/core/techset_info.*`: `parseTechset` (family, blend and table tokens, glass), `materialDrawFor` (drawn / shadow
  only / hidden from family + surfaceFlags, by name for entries without flags), `kIw8SurfNoDraw`.
- `material_blend`: `bar` is a blend. `material_slots`: the slot table above (Material Info).
- Material cache (`maskRules` 3): the mask is slot 27 of every non-opaque set (a name match `_sdf` / `_alpha` remains
  the fallback); `surfaceFlags`, `materialType`, `sortKey` are kept; older entries resolve again once.
- Camera: the mask's alpha is read when it varies (colour-image opacity), red otherwise; alpha-tested sets cut at 0.5,
  blended sets keep the soft value. Shadow-only triangles go only into the sun shadow pass; hidden ones are skipped.
  The shadow pass clips textured batches by alpha, so leaves cast dappled shadows instead of solid cards.
- Stock model loading still drops `*shadowcaster*` meshes (`model_cache.cpp`), because builds rebuild stock models
  from the same meshes and a converted shadowcaster would show in game.

## For custom technique sets later

A custom material should state the same things this survey reads from stock: family (lit / unlit / ...), blend
(`rpl`/`alt`/`bad`/`bar`, plus additive), which slot holds opacity and from which channel, and the nodraw / shadow
flags. `TechsetInfo` is the struct to grow (cull mode, depth write, polygon offset, sort) once the state hex fields
are decoded; the `808800` / `808810` / `848804` fields differ between visible and shadow surfaces and are the next
thing to map.
