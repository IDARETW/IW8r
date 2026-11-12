# IW8 Radiant entity reference

This page lists the entity classes the IW8 Radiant editor knows. A class is the name you give an entity in a map, such as `light` or `mp_dm_spawn`. For each class, this page says what it does, whether the build keeps it, and which keys it takes.

**This file is generated. Do not edit it by hand.** To change it, edit `data/iw8_entity_defs.json` or `tools/build_entity_reference.py`, then run this from the repo root:

    py -3 -I tools\build_entity_reference.py

The same content is written to `data/generated/entity_reference.html` for the editor's help viewer.

Sources: `data/iw8_entity_defs.json` (classes and keys) and `docs/research/10_iw8_entity_inventory.md` (stock usage counts).

## Summary

- 110 classes in 9 categories.
- 100 kept by the build, 10 dropped.
- Stock usage is listed for 35 classes, from the two zones in the inventory (mp_shipment and mp_backlot2).

## Reading the entries

- **Kind:** a point entity has a box in the editor. A brush entity is drawn from brushes.
- **Build:** kept means the converter accepts the class. Dropped means the converter ignores it.
- **Required:** yes means Check Map reports the entity when that key is empty.
- **Type / values:** the editor checks each value against the type. A value that does not fit is reported.
- **Default:** the default listed in the definitions. None means the definitions give no default.

## Contents

**[Spawns](#spawns)** (46 classes): [`info_player_start`](#info_player_start), [`mp_arena_spawn_allies_start`](#mp_arena_spawn_allies_start), [`mp_arena_spawn_axis_start`](#mp_arena_spawn_axis_start), [`mp_ball_spawn`](#mp_ball_spawn), [`mp_ball_spawn_allies_start`](#mp_ball_spawn_allies_start), [`mp_ball_spawn_axis_start`](#mp_ball_spawn_axis_start), [`mp_ball_spawn_secondary`](#mp_ball_spawn_secondary), [`mp_ctf_spawn`](#mp_ctf_spawn), [`mp_ctf_spawn_allies_start`](#mp_ctf_spawn_allies_start), [`mp_ctf_spawn_axis_start`](#mp_ctf_spawn_axis_start), [`mp_ctf_spawn_secondary`](#mp_ctf_spawn_secondary), [`mp_cyber_spawn_allies`](#mp_cyber_spawn_allies), [`mp_cyber_spawn_axis`](#mp_cyber_spawn_axis), [`mp_dd_spawn_attacker`](#mp_dd_spawn_attacker), [`mp_dd_spawn_attacker_a`](#mp_dd_spawn_attacker_a), [`mp_dd_spawn_attacker_b`](#mp_dd_spawn_attacker_b), [`mp_dd_spawn_attacker_start`](#mp_dd_spawn_attacker_start), [`mp_dd_spawn_defender`](#mp_dd_spawn_defender), [`mp_dd_spawn_defender_a`](#mp_dd_spawn_defender_a), [`mp_dd_spawn_defender_b`](#mp_dd_spawn_defender_b), [`mp_dd_spawn_defender_start`](#mp_dd_spawn_defender_start), [`mp_dm_spawn`](#mp_dm_spawn), [`mp_dm_spawn_secondary`](#mp_dm_spawn_secondary), [`mp_dm_spawn_start`](#mp_dm_spawn_start), [`mp_dom_spawn`](#mp_dom_spawn), [`mp_dom_spawn_allies_start`](#mp_dom_spawn_allies_start), [`mp_dom_spawn_axis_start`](#mp_dom_spawn_axis_start), [`mp_dom_spawn_secondary`](#mp_dom_spawn_secondary), [`mp_front_spawn_allies`](#mp_front_spawn_allies), [`mp_front_spawn_axis`](#mp_front_spawn_axis), [`mp_koth_spawn`](#mp_koth_spawn), [`mp_koth_spawn_allies_start`](#mp_koth_spawn_allies_start), [`mp_koth_spawn_axis_start`](#mp_koth_spawn_axis_start), [`mp_koth_spawn_secondary`](#mp_koth_spawn_secondary), [`mp_rugby_spawn_allies`](#mp_rugby_spawn_allies), [`mp_rugby_spawn_allies_secondary`](#mp_rugby_spawn_allies_secondary), [`mp_rugby_spawn_allies_start`](#mp_rugby_spawn_allies_start), [`mp_rugby_spawn_axis`](#mp_rugby_spawn_axis), [`mp_rugby_spawn_axis_secondary`](#mp_rugby_spawn_axis_secondary), [`mp_rugby_spawn_axis_start`](#mp_rugby_spawn_axis_start), [`mp_sd_spawn_attacker`](#mp_sd_spawn_attacker), [`mp_sd_spawn_defender`](#mp_sd_spawn_defender), [`mp_tdm_spawn`](#mp_tdm_spawn), [`mp_tdm_spawn_allies_start`](#mp_tdm_spawn_allies_start), [`mp_tdm_spawn_axis_start`](#mp_tdm_spawn_axis_start), [`mp_tdm_spawn_secondary`](#mp_tdm_spawn_secondary)

**[Game modes](#game-modes)** (10 classes): [`script_struct_infil_lbravo_alpha`](#script_struct_infil_lbravo_alpha), [`script_struct_infil_van_hackney_alpha`](#script_struct_infil_van_hackney_alpha), [`script_struct_mp_atv`](#script_struct_mp_atv), [`script_struct_mp_cargotruck`](#script_struct_mp_cargotruck), [`script_struct_mp_jeep`](#script_struct_mp_jeep), [`script_struct_mp_littlebird`](#script_struct_mp_littlebird), [`script_struct_mp_rugby_endzone`](#script_struct_mp_rugby_endzone), [`script_struct_mp_rugby_jugg_crate`](#script_struct_mp_rugby_jugg_crate), [`script_struct_mp_tacrover`](#script_struct_mp_tacrover), [`trigger_multiple_mp_rugby_endzone`](#trigger_multiple_mp_rugby_endzone)

**[Triggers](#triggers)** (27 classes): [`trigger_damage`](#trigger_damage), [`trigger_hurt`](#trigger_hurt), [`trigger_multiple`](#trigger_multiple), [`trigger_multiple_bcs_backalley`](#trigger_multiple_bcs_backalley), [`trigger_multiple_bcs_center`](#trigger_multiple_bcs_center), [`trigger_multiple_bcs_chickens`](#trigger_multiple_bcs_chickens), [`trigger_multiple_bcs_compound`](#trigger_multiple_bcs_compound), [`trigger_multiple_bcs_construction`](#trigger_multiple_bcs_construction), [`trigger_multiple_bcs_containers`](#trigger_multiple_bcs_containers), [`trigger_multiple_bcs_courtyard`](#trigger_multiple_bcs_courtyard), [`trigger_multiple_bcs_destroyedbldg`](#trigger_multiple_bcs_destroyedbldg), [`trigger_multiple_bcs_dumpster`](#trigger_multiple_bcs_dumpster), [`trigger_multiple_bcs_electronicstore`](#trigger_multiple_bcs_electronicstore), [`trigger_multiple_bcs_forklift`](#trigger_multiple_bcs_forklift), [`trigger_multiple_bcs_garage`](#trigger_multiple_bcs_garage), [`trigger_multiple_bcs_junkyard`](#trigger_multiple_bcs_junkyard), [`trigger_multiple_bcs_laundromat`](#trigger_multiple_bcs_laundromat), [`trigger_multiple_bcs_mainstreet`](#trigger_multiple_bcs_mainstreet), [`trigger_multiple_bcs_rugshop`](#trigger_multiple_bcs_rugshop), [`trigger_multiple_bcs_shippingarea`](#trigger_multiple_bcs_shippingarea), [`trigger_multiple_bcs_shops`](#trigger_multiple_bcs_shops), [`trigger_multiple_bcs_tireshop`](#trigger_multiple_bcs_tireshop), [`trigger_multiple_bcs_tower`](#trigger_multiple_bcs_tower), [`trigger_multiple_bcs_warehouse`](#trigger_multiple_bcs_warehouse), [`trigger_radius`](#trigger_radius), [`trigger_use`](#trigger_use), [`trigger_use_touch`](#trigger_use_touch)

**[Script](#script)** (3 classes): [`script_origin`](#script_origin), [`script_struct`](#script_struct), [`script_struct_heli`](#script_struct_heli)

**[Lights and atmosphere](#lights-and-atmosphere)** (4 classes): [`light`](#light), [`light_spot`](#light_spot), [`reflection_probe`](#reflection_probe), [`worldspawn`](#worldspawn)

**[Effects and sound](#effects-and-sound)** (2 classes): [`fx_origin`](#fx_origin), [`sound_emitter`](#sound_emitter)

**[Navigation](#navigation)** (4 classes): [`node_3d_pathnode`](#node_3d_pathnode), [`node_negotiation_begin`](#node_negotiation_begin), [`node_negotiation_end`](#node_negotiation_end), [`node_pathnode`](#node_pathnode)

**[Models and prefabs](#models-and-prefabs)** (6 classes): [`collmap_scriptable`](#collmap_scriptable), [`misc_model`](#misc_model), [`misc_prefab`](#misc_prefab), [`script_brushmodel`](#script_brushmodel), [`script_model`](#script_model), [`triggermodeldummydefault`](#triggermodeldummydefault)

**[Miscellaneous](#miscellaneous)** (8 classes): [`dynamic_prop`](#dynamic_prop), [`export_volume`](#export_volume), [`func_group`](#func_group), [`iw8r_decal`](#iw8r_decal), [`iw8r_loot_point`](#iw8r_loot_point), [`iw8r_volume`](#iw8r_volume), [`mp_global_intermission`](#mp_global_intermission), [`scriptable`](#scriptable)

## Spawns

46 classes.

### `info_player_start`

Player start point (inferred from stock values). Accepted by the converter; not required (contract 15 s3.4).

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter. Accepted by the converter; not required (contract 15 s3.4).
- **Seen in stock maps:** 3 (mp_shipment, mp_backlot2).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). Examples: `0 45 0`, `0 90 0`. |
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). Examples: `-288 -288 256`, `28 -308 154`, `-664 -808 8`. |

### `mp_arena_spawn_allies_start`

Spawn point for ARENA (allies start). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_arena_spawn_axis_start`

Spawn point for ARENA (axis start). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_ball_spawn`

Spawn point for BALL (default). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_ball_spawn_allies_start`

Spawn point for BALL (allies start). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_ball_spawn_axis_start`

Spawn point for BALL (axis start). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_ball_spawn_secondary`

Spawn point for BALL (secondary). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_ctf_spawn`

Spawn point for CTF (default). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_ctf_spawn_allies_start`

Spawn point for CTF (allies start). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_ctf_spawn_axis_start`

Spawn point for CTF (axis start). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_ctf_spawn_secondary`

Spawn point for CTF (secondary). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_cyber_spawn_allies`

Spawn point for CYBER (allies). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_cyber_spawn_axis`

Spawn point for CYBER (axis). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_dd_spawn_attacker`

Spawn point for DD (attacker). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_dd_spawn_attacker_a`

Spawn point for DD (attacker a). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_dd_spawn_attacker_b`

Spawn point for DD (attacker b). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_dd_spawn_attacker_start`

Spawn point for DD (attacker start). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_dd_spawn_defender`

Spawn point for DD (defender). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_dd_spawn_defender_a`

Spawn point for DD (defender a). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_dd_spawn_defender_b`

Spawn point for DD (defender b). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_dd_spawn_defender_start`

Spawn point for DD (defender start). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_dm_spawn`

Spawn point for DM (default). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_dm_spawn_secondary`

Spawn point for DM (secondary). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_dm_spawn_start`

Spawn point for DM (start). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_dom_spawn`

Spawn point for DOM (default). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_dom_spawn_allies_start`

Spawn point for DOM (allies start). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_dom_spawn_axis_start`

Spawn point for DOM (axis start). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_dom_spawn_secondary`

Spawn point for DOM (secondary). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_front_spawn_allies`

Spawn point for FRONT (allies). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_front_spawn_axis`

Spawn point for FRONT (axis). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_koth_spawn`

Spawn point for KOTH (default). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_koth_spawn_allies_start`

Spawn point for KOTH (allies start). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_koth_spawn_axis_start`

Spawn point for KOTH (axis start). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_koth_spawn_secondary`

Spawn point for KOTH (secondary). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_rugby_spawn_allies`

Spawn point for RUGBY (allies). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_rugby_spawn_allies_secondary`

Spawn point for RUGBY (allies secondary). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_rugby_spawn_allies_start`

Spawn point for RUGBY (allies start). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_rugby_spawn_axis`

Spawn point for RUGBY (axis). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_rugby_spawn_axis_secondary`

Spawn point for RUGBY (axis secondary). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_rugby_spawn_axis_start`

Spawn point for RUGBY (axis start). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_sd_spawn_attacker`

Spawn point for SD (attacker). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_sd_spawn_defender`

Spawn point for SD (defender). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_tdm_spawn`

Team deathmatch spawn point. The build needs two or more (contract 15 s3.4).

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter. The build needs two or more (contract 15 s3.4).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_tdm_spawn_allies_start`

Team deathmatch allies start spawn. The build requires it with the axis start (contract 15 s3.4).

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter. The build requires it with the axis start (contract 15 s3.4).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_tdm_spawn_axis_start`

Team deathmatch axis start spawn. The build requires it with the allies start (contract 15 s3.4).

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter. The build requires it with the allies start (contract 15 s3.4).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `mp_tdm_spawn_secondary`

Spawn point for TDM (secondary). Description inferred from the class name.

- **Kind:** point entity.
- **Box:** from (-16, -16, 0) to (16, 16, 72), in map units.
- **Editor colour:** `#33CC33` (RGB 0.2, 0.8, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

## Game modes

10 classes.

### `script_struct_infil_lbravo_alpha`

Infil aircraft start struct (inferred from stock values).

- **Kind:** point entity.
- **Box:** from (-8, -8, -8) to (8, 8, 8), in map units.
- **Editor colour:** `#FFD933` (RGB 1, 0.85, 0.2).
- **Build:** dropped by the converter.
- **Seen in stock maps:** 1 (mp_backlot2).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). Examples: `0 56.9998 0`. |
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `model` | model: a model name, or a brush model index written as \*N | none | no | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). Examples: `veh8_mil_air_lbravo`. |
| `name` | string: any text | none | no | Name key on infil structs (inferred from stock values). Examples: `alpha`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). Examples: `-451.692 -2406.11 264`. |
| `script_noteworthy` | string: any text | none | no | Free-form tag that gametype code branches on (gsc gametypes/arena.gsc:509-527, ammorestock.gsc:22-25). Examples: `infil_lbravo`. |
| `script_team` | string: any text | none | no | Team side, axis or allies (gsc gametypes/arm.gsc:2292, 2300). Examples: `allies`. |
| `spawnflags` | int: a whole number | `0` | no | Bit flags. Scripts test single bits (gsc gametypes/br_quest_util.gsc:1795, gametypes/br_dom_quest.gsc:71); per-class bit meanings are not sourced. Examples: `1`. |
| `target` | targetname: any text, the name other entities use | none | no | targetname of the entity or struct this one points to (gsc ammorestock.gsc:12). Examples: `pf16_auto3180`. |
| `targetname` | targetname: any text, the name other entities use | none | no | Name that other entities use to find this one (gsc ammorestock.gsc:8). Examples: `mp_infil`. |

### `script_struct_infil_van_hackney_alpha`

Infil van start struct (inferred from stock values).

- **Kind:** point entity.
- **Box:** from (-8, -8, -8) to (8, 8, 8), in map units.
- **Editor colour:** `#FFD933` (RGB 1, 0.85, 0.2).
- **Build:** dropped by the converter.
- **Seen in stock maps:** 1 (mp_backlot2).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). Examples: `0 90 0`, `0 84.9999 0`, `0 270 0`. |
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `model` | model: a model name, or a brush model index written as \*N | none | no | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). Examples: `veh_civ_lnd_mercedes_sprinter_01`. |
| `name` | string: any text | none | no | Name key on infil structs (inferred from stock values). Examples: `alpha`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). Examples: `-128 2241 60`, `-121.83 2039.66 3.86057`, `-247.08 -2702.45 3.48289`. |
| `script_noteworthy` | string: any text | none | no | Free-form tag that gametype code branches on (gsc gametypes/arena.gsc:509-527, ammorestock.gsc:22-25). Examples: `infil_van_hackney`. |
| `script_team` | string: any text | none | no | Team side, axis or allies (gsc gametypes/arm.gsc:2292, 2300). Examples: `axis`, `allies`. |
| `spawnflags` | int: a whole number | `0` | no | Bit flags. Scripts test single bits (gsc gametypes/br_quest_util.gsc:1795, gametypes/br_dom_quest.gsc:71); per-class bit meanings are not sourced. Examples: `255`, `251`. |
| `target` | targetname: any text, the name other entities use | none | no | targetname of the entity or struct this one points to (gsc ammorestock.gsc:12). Examples: `pf16_auto3135`, `pf2137_auto3135`, `pf2137_auto3218`. |
| `targetname` | targetname: any text, the name other entities use | none | no | Name that other entities use to find this one (gsc ammorestock.gsc:8). Examples: `mp_infil`. |

### `script_struct_mp_atv`

A Warzone vehicle spawn: a ATV (quad bike) appears here in a Warzone (br) match, as on the stock Warzone maps. The build carries the stock vehicle and its model from mp_br_quarry into your map. Keep the keys as they are; set origin and angles. Not seen in game.

- **Kind:** point entity.
- **Box:** from (-48, -48, 0) to (48, 48, 64), in map units.
- **Editor colour:** `#4CB2FF` (RGB 0.3, 0.7, 1).
- **Build:** kept by the converter. The build carries the stock vehicle and its model from mp_br_quarry into your map.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | vec3: three numbers, x y z | `0 0 0` | no | Which way it faces. Examples: `0 90 0`. |
| `model` | string: any text | `veh8_mil_lnd_atango_physics_mp` | yes | The vehicle model. Examples: `veh8_mil_lnd_atango_physics_mp`. |
| `origin` | vec3: three numbers, x y z | none | yes | Where the vehicle stands (on the ground). Examples: `0 0 0`. |
| `script_gameobjectname` | string: any text | `br` | no | The game types it belongs to. Examples: `br`. |
| `targetname` | string: any text | `atv_spawn` | yes | The name the Warzone scripts look for. Examples: `atv_spawn`. |
| `vehicletype` | string: any text | `atango_physics_mp` | yes | The stock vehicle definition. Examples: `atango_physics_mp`. |

### `script_struct_mp_cargotruck`

A Warzone vehicle spawn: a cargo truck appears here in a Warzone (br) match, as on the stock Warzone maps. The build carries the stock vehicle and its model from mp_br_quarry into your map. Keep the keys as they are; set origin and angles. Not seen in game.

- **Kind:** point entity.
- **Box:** from (-48, -48, 0) to (48, 48, 64), in map units.
- **Editor colour:** `#4CB2FF` (RGB 0.3, 0.7, 1).
- **Build:** kept by the converter. The build carries the stock vehicle and its model from mp_br_quarry into your map.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | vec3: three numbers, x y z | `0 0 0` | no | Which way it faces. Examples: `0 90 0`. |
| `model` | string: any text | `veh8_mil_lnd_mkilo23_physics_mp` | yes | The vehicle model. Examples: `veh8_mil_lnd_mkilo23_physics_mp`. |
| `origin` | vec3: three numbers, x y z | none | yes | Where the vehicle stands (on the ground). Examples: `0 0 0`. |
| `script_gameobjectname` | string: any text | `br` | no | The game types it belongs to. Examples: `br`. |
| `targetname` | string: any text | `cargotruck_spawn` | yes | The name the Warzone scripts look for. Examples: `cargotruck_spawn`. |
| `vehicletype` | string: any text | `mkilo_physics_mp` | yes | The stock vehicle definition. Examples: `mkilo_physics_mp`. |

### `script_struct_mp_jeep`

A Warzone vehicle spawn: a SUV appears here in a Warzone (br) match, as on the stock Warzone maps. The build carries the stock vehicle and its model from mp_br_quarry into your map. Keep the keys as they are; set origin and angles. Not seen in game.

- **Kind:** point entity.
- **Box:** from (-48, -48, 0) to (48, 48, 64), in map units.
- **Editor colour:** `#4CB2FF` (RGB 0.3, 0.7, 1).
- **Build:** kept by the converter. The build carries the stock vehicle and its model from mp_br_quarry into your map.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | vec3: three numbers, x y z | `0 0 0` | no | Which way it faces. Examples: `0 90 0`. |
| `model` | string: any text | `veh8_civ_lnd_decho_vm_dirty_blue_physics_mp` | yes | The vehicle model. Examples: `veh8_civ_lnd_decho_vm_dirty_blue_physics_mp`. |
| `origin` | vec3: three numbers, x y z | none | yes | Where the vehicle stands (on the ground). Examples: `0 0 0`. |
| `script_gameobjectname` | string: any text | `br` | no | The game types it belongs to. Examples: `br`. |
| `targetname` | string: any text | `jeep_spawn` | yes | The name the Warzone scripts look for. Examples: `jeep_spawn`. |
| `vehicletype` | string: any text | `decho_physics_mp` | yes | The stock vehicle definition. Examples: `decho_physics_mp`. |

### `script_struct_mp_littlebird`

A Warzone vehicle spawn: a helicopter appears here in a Warzone (br) match, as on the stock Warzone maps. The build carries the stock vehicle and its model from mp_br_quarry into your map. Keep the keys as they are; set origin and angles. Not seen in game.

- **Kind:** point entity.
- **Box:** from (-48, -48, 0) to (48, 48, 64), in map units.
- **Editor colour:** `#4CB2FF` (RGB 0.3, 0.7, 1).
- **Build:** kept by the converter. The build carries the stock vehicle and its model from mp_br_quarry into your map.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | vec3: three numbers, x y z | `0 0 0` | no | Which way it faces. Examples: `0 90 0`. |
| `model` | string: any text | `veh8_mil_air_lbravo` | yes | The vehicle model. Examples: `veh8_mil_air_lbravo`. |
| `origin` | vec3: three numbers, x y z | none | yes | Where the vehicle stands (on the ground). Examples: `0 0 0`. |
| `script_gameobjectname` | string: any text | `br` | no | The game types it belongs to. Examples: `br`. |
| `targetname` | string: any text | `littlebird_spawn` | yes | The name the Warzone scripts look for. Examples: `littlebird_spawn`. |
| `vehicletype` | string: any text | `lbravo_physics` | yes | The stock vehicle definition. Examples: `lbravo_physics`. |

### `script_struct_mp_rugby_endzone`

Rugby endzone struct (inferred from stock values). Not accepted by the converter.

- **Kind:** point entity.
- **Box:** from (-8, -8, -8) to (8, 8, 8), in map units.
- **Editor colour:** `#FFD933` (RGB 1, 0.85, 0.2).
- **Build:** dropped by the converter. Not accepted by the converter.
- **Seen in stock maps:** 4 (mp_shipment, mp_backlot2).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). Examples: `0 270 0`, `0 34.9994 0`. |
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). Examples: `-556.34 1946.67 96`, `1006 -2013 79`, `160 2488 168`. |
| `spawnflags` | int: a whole number | `0` | no | Bit flags. Scripts test single bits (gsc gametypes/br_quest_util.gsc:1795, gametypes/br_dom_quest.gsc:71); per-class bit meanings are not sourced. Examples: `2`, `1`. |
| `target` | targetname: any text, the name other entities use | none | no | targetname of the entity or struct this one points to (gsc ammorestock.gsc:12). Examples: `axis_endzone`, `allies_endzone`, `pf1_auto3175`. |
| `targetname` | targetname: any text, the name other entities use | none | no | Name that other entities use to find this one (gsc ammorestock.gsc:8). Examples: `rugby_endzone`. |

### `script_struct_mp_rugby_jugg_crate`

Rugby juggernaut crate struct (inferred from stock values). Not accepted by the converter.

- **Kind:** point entity.
- **Box:** from (-8, -8, -8) to (8, 8, 8), in map units.
- **Editor colour:** `#FFD933` (RGB 1, 0.85, 0.2).
- **Build:** dropped by the converter. Not accepted by the converter.
- **Seen in stock maps:** 6 (mp_shipment, mp_backlot2).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). Examples: `0 90.0009 0`, `0 0 0`, `0 330 0`. |
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `model` | model: a model name, or a brush model index written as \*N | none | no | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). Examples: `military_crate_large_stackable_01_jugg`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). Examples: `256 6 57`, `1338 122 62`, `-586 -161 65`. |
| `targetname` | targetname: any text, the name other entities use | none | no | Name that other entities use to find this one (gsc ammorestock.gsc:8). Examples: `rugby_jugg_crate`. |

### `script_struct_mp_tacrover`

A Warzone vehicle spawn: a tactical rover appears here in a Warzone (br) match, as on the stock Warzone maps. The build carries the stock vehicle and its model from mp_br_quarry into your map. Keep the keys as they are; set origin and angles. Not seen in game.

- **Kind:** point entity.
- **Box:** from (-48, -48, 0) to (48, 48, 64), in map units.
- **Editor colour:** `#4CB2FF` (RGB 0.3, 0.7, 1).
- **Build:** kept by the converter. The build carries the stock vehicle and its model from mp_br_quarry into your map.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | vec3: three numbers, x y z | `0 0 0` | no | Which way it faces. Examples: `0 90 0`. |
| `model` | string: any text | `veh8_mil_lnd_tromeo_physics_mp` | yes | The vehicle model. Examples: `veh8_mil_lnd_tromeo_physics_mp`. |
| `origin` | vec3: three numbers, x y z | none | yes | Where the vehicle stands (on the ground). Examples: `0 0 0`. |
| `script_gameobjectname` | string: any text | `br` | no | The game types it belongs to. Examples: `br`. |
| `targetname` | string: any text | `tacrover_spawn` | yes | The name the Warzone scripts look for. Examples: `tacrover_spawn`. |
| `vehicletype` | string: any text | `tromeo_physics_mp` | yes | The stock vehicle definition. Examples: `tromeo_physics_mp`. |

### `trigger_multiple_mp_rugby_endzone`

Brush trigger for a rugby endzone (inferred from the class name). Not accepted by the converter.

- **Kind:** brush entity, no box.
- **Editor colour:** `#E64C4C` (RGB 0.9, 0.3, 0.3).
- **Build:** dropped by the converter. Not accepted by the converter.
- **Seen in stock maps:** 4 (mp_shipment, mp_backlot2).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `model` | model: a model name, or a brush model index written as \*N | none | no | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). Examples: `?25`, `?26`, `?8`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | no | Position x y z in map units (inferred from stock values). Examples: `1160 -1912 134`, `-548 2024 118`, `160 2484 84`. |
| `physasset` | string: any text | none | no | Physics asset name (inferred from stock values). Examples: `triggermodeldummydefault`. |
| `targetname` | targetname: any text, the name other entities use | none | no | Name that other entities use to find this one (gsc ammorestock.gsc:8). Examples: `allies_endzone`, `axis_endzone`, `pf1_auto3175`. |

## Triggers

27 classes.

### `trigger_damage`

Brush trigger with damage. The converter drops it without script_exploder (contract 15 s3.3).

- **Kind:** brush entity, no box.
- **Editor colour:** `#E64C4C` (RGB 0.9, 0.3, 0.3).
- **Build:** kept by the converter. The converter drops it without script_exploder (contract 15 s3.3).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `dmg` | int: a whole number | none | no | Damage applied to what is inside the trigger (inferred from stock values). |
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). |
| `model` | model: a model name, or a brush model index written as \*N | none | no | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). |
| `physasset` | string: any text | none | no | Physics asset name (inferred from stock values). |
| `script_exploder` | string: any text | none | yes | Exploder id, stored as exploderindex (gsc gametypes/btm.gsc:1830-1832). |
| `target` | targetname: any text, the name other entities use | none | no | targetname of the entity or struct this one points to (gsc ammorestock.gsc:12). |
| `targetname` | targetname: any text, the name other entities use | none | no | Name that other entities use to find this one (gsc ammorestock.gsc:8). |

### `trigger_hurt`

Brush trigger that damages what is inside it (inferred from stock values).

- **Kind:** brush entity, no box.
- **Editor colour:** `#E64C4C` (RGB 0.9, 0.3, 0.3).
- **Build:** kept by the converter.
- **Seen in stock maps:** 3 (mp_shipment, mp_backlot2).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `dmg` | int: a whole number | none | no | Damage applied to what is inside the trigger (inferred from stock values). Examples: `10000`. |
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `model` | model: a model name, or a brush model index written as \*N | none | no | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). Examples: `?2`, `?15`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | no | Position x y z in map units (inferred from stock values). Examples: `0 0 -512`, `-142 2949 -516`, `-142 2949 -1028`. |
| `physasset` | string: any text | none | no | Physics asset name (inferred from stock values). Examples: `triggermodeldummydefault`. |

### `trigger_multiple`

Brush trigger volume. Scripts check its classname (gsc bots/bots_gametype_common.gsc:148).

- **Kind:** brush entity, no box.
- **Editor colour:** `#E64C4C` (RGB 0.9, 0.3, 0.3).
- **Build:** kept by the converter.
- **Seen in stock maps:** 78 (mp_shipment, mp_backlot2).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `iw8r_ambient` | string: any text | none | no | Ambient bed of the zone: the name of one of the map's own sounds (&lt;map dir&gt;/sounds/&lt;name&gt;.wav or .mp3), built into the map sound bank and played as a 2D loop while in the zone. Stock aliases cannot be used here (silent in the map bank); use a looping sound_emitter for those. In game NOT VERIFIED. Examples: `wind_bed`, `factory_hum`. |
| `iw8r_ambient_oneshots` | string: any text | none | no | Ambient one-shots of the zone: "&lt;min s&gt; &lt;max s&gt; &lt;sound&gt;[:weight] ..." with the map's own sounds (&lt;map dir&gt;/sounds); one of them plays at random every min..max seconds while in the zone. In game NOT VERIFIED. Examples: `6 12 bird_call:3 dog_bark`. |
| `iw8r_audio_interior` | bool: 0, 1, true or false | `0` | no | 1: the zone uses the stock "interior" occlusion and filter presets (sounds behind walls muffled as indoors); 0: the stock "default" ones. Written to the sound table (research 63). In game NOT VERIFIED. |
| `iw8r_audio_priority` | float: a number | `30` | no | Audio zone priority where zones overlap (higher wins; the map-wide zone is 0). Examples: `30`, `50`. |
| `iw8r_audio_zone` | string: any text | none | no | Audio zone: a lower-case zone name. Inside this trigger's box the zone's reverb (iw8r_reverb) applies; written to the sound table and client triggers (research 63). In game NOT VERIFIED. Examples: `hall`, `tunnel_east`. |
| `iw8r_damage` | string: any text | none | no | Damage zone: "&lt;amount&gt; [interval]". Needs a targetname; a player inside takes amount damage every interval seconds (default 1). Examples: `10 1`, `200`. |
| `iw8r_jump` | string: any text | none | no | Jump pad: "&lt;vx&gt; &lt;vy&gt; &lt;vz&gt;" launch velocity (units per second). Needs a targetname; a player touching the trigger is launched. Examples: `0 0 700`, `400 0 500`. |
| `iw8r_reverb` | string: any text | none | no | Reverb room type (one of the 59 stock types, e.g. iw8_md_room, iw8_ext_open_space_med_1; Help lists them in research 63) and optional dry wet fade: "&lt;room&gt; [dry wet fade]". The build writes the map sound table (in game NOT VERIFIED). Examples: `iw8_ext_open_space_med_1`, `iw8_md_room`, `iw8_lg_room_warehouse`. |
| `iw8r_teleport` | string: any text | none | no | Teleporter: the targetname of the destination (a script_origin). Needs its own targetname; a player touching the trigger moves to the destination's origin and angles. Examples: `tp_exit_1`. |
| `iw8r_vision` | string: any text | none | no | Vision / fog inside this volume: "name=value; ..." Replay vision fields (as worldspawn iw8r_vision). Needs a targetname. A player inside sees it; leaving restores the map's vision. Research 63. Examples: `tonemapExposure=2.5`. |
| `model` | model: a model name, or a brush model index written as \*N | none | no | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). Examples: `?1`, `?3`, `?4`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | no | Position x y z in map units (inferred from stock values). Examples: `1732 -1374 2868`, `-7424 8960 256`, `-9984 8704 512`. |
| `physasset` | string: any text | none | no | Physics asset name (inferred from stock values). Examples: `triggermodeldummydefault`, `TriggerModelStaticDummyDefault`. |
| `script_gameobjectname` | string: any text | none | no | Gameobject or objective id read by gsc gameobjects.gsc:17. Values such as bombzone and sd are inferred from stock values. Examples: `hardpoint`, `grnd`, `sd`. |
| `script_label` | string: any text | none | no | Label for a gameobject trigger (gsc gameobjects.gsc:4432-4435; gametypes/arena.gsc:2165-2177 compares '1', '2', '3'). Examples: `3`, `2`, `1`. |
| `script_linkname` | string: any text | none | no | Link name read by gsc-raw animation_suite.gsc:124; role inferred from stock values. Examples: `pf14_2`, `pf14_3`, `pf14_4`. |
| `script_noteworthy` | string: any text | none | no | Free-form tag that gametype code branches on (gsc gametypes/arena.gsc:509-527, ammorestock.gsc:22-25). Examples: `laundry`, `gas_station`, `destroyed`. |
| `target` | targetname: any text, the name other entities use | none | no | targetname of the entity or struct this one points to (gsc ammorestock.gsc:12). Examples: `hardpoint_03_chevrons`, `hardpoint_02_chevrons`, `hardpoint_01_chevrons`. |
| `targetname` | targetname: any text, the name other entities use | none | no | Name that other entities use to find this one (gsc ammorestock.gsc:8). Examples: `OutOfBounds`, `callout_area`, `hardpoint_zone`. |

### `trigger_multiple_bcs_backalley`

Brush trigger for the 'backalley' area (inferred from the class name). Accepted by prefix in the converter.

- **Kind:** brush entity, no box.
- **Editor colour:** `#E64C4C` (RGB 0.9, 0.3, 0.3).
- **Build:** kept by the converter. Accepted by prefix in the converter.
- **Seen in stock maps:** 2 (mp_backlot2).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `model` | model: a model name, or a brush model index written as \*N | none | no | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). Examples: `?68`, `?69`, `?51`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | no | Position x y z in map units (inferred from stock values). Examples: `604 1320 288`, `256 1972 288`, `-978 -1767 60`. |
| `physasset` | string: any text | none | no | Physics asset name (inferred from stock values). Examples: `triggermodeldummydefault`. |

### `trigger_multiple_bcs_center`

Brush trigger for the 'center' area (inferred from the class name). Accepted by prefix in the converter.

- **Kind:** brush entity, no box.
- **Editor colour:** `#E64C4C` (RGB 0.9, 0.3, 0.3).
- **Build:** kept by the converter. Accepted by prefix in the converter.
- **Seen in stock maps:** 2 (mp_shipment, mp_backlot2).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `model` | model: a model name, or a brush model index written as \*N | none | no | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). Examples: `?70`, `?16`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | no | Position x y z in map units (inferred from stock values). Examples: `544 20 292`, `-348 1984 92`. |
| `physasset` | string: any text | none | no | Physics asset name (inferred from stock values). Examples: `triggermodeldummydefault`. |

### `trigger_multiple_bcs_chickens`

Brush trigger for the 'chickens' area (inferred from the class name). Accepted by prefix in the converter.

- **Kind:** brush entity, no box.
- **Editor colour:** `#E64C4C` (RGB 0.9, 0.3, 0.3).
- **Build:** kept by the converter. Accepted by prefix in the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `model` | model: a model name, or a brush model index written as \*N | none | no | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). Examples: `?59`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | no | Position x y z in map units (inferred from stock values). Examples: `320 -1544 104`. |
| `physasset` | string: any text | none | no | Physics asset name (inferred from stock values). Examples: `triggermodeldummydefault`. |

### `trigger_multiple_bcs_compound`

Brush trigger for the 'compound' area (inferred from the class name). Accepted by prefix in the converter.

- **Kind:** brush entity, no box.
- **Editor colour:** `#E64C4C` (RGB 0.9, 0.3, 0.3).
- **Build:** kept by the converter. Accepted by prefix in the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `model` | model: a model name, or a brush model index written as \*N | none | no | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). Examples: `?55`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | no | Position x y z in map units (inferred from stock values). Examples: `165 1260 180`. |
| `physasset` | string: any text | none | no | Physics asset name (inferred from stock values). Examples: `triggermodeldummydefault`. |

### `trigger_multiple_bcs_construction`

Brush trigger for the 'construction' area (inferred from the class name). Accepted by prefix in the converter.

- **Kind:** brush entity, no box.
- **Editor colour:** `#E64C4C` (RGB 0.9, 0.3, 0.3).
- **Build:** kept by the converter. Accepted by prefix in the converter.
- **Seen in stock maps:** 1 (mp_backlot2).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `model` | model: a model name, or a brush model index written as \*N | none | no | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). Examples: `?71`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | no | Position x y z in map units (inferred from stock values). Examples: `-904 -328 292`. |
| `physasset` | string: any text | none | no | Physics asset name (inferred from stock values). Examples: `triggermodeldummydefault`. |

### `trigger_multiple_bcs_containers`

Brush trigger for the 'containers' area (inferred from the class name). Accepted by prefix in the converter.

- **Kind:** brush entity, no box.
- **Editor colour:** `#E64C4C` (RGB 0.9, 0.3, 0.3).
- **Build:** kept by the converter. Accepted by prefix in the converter.
- **Seen in stock maps:** 4 (mp_shipment, mp_backlot2).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). Examples: `0 0 0`. |
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `model` | model: a model name, or a brush model index written as \*N | none | no | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). Examples: `?73`, `?19`, `?20`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | no | Position x y z in map units (inferred from stock values). Examples: `-996 864 296`, `-348 2544 64`, `-924 1996 96`. |
| `physasset` | string: any text | none | no | Physics asset name (inferred from stock values). Examples: `triggermodeldummydefault`. |

### `trigger_multiple_bcs_courtyard`

Brush trigger for the 'courtyard' area (inferred from the class name). Accepted by prefix in the converter.

- **Kind:** brush entity, no box.
- **Editor colour:** `#E64C4C` (RGB 0.9, 0.3, 0.3).
- **Build:** kept by the converter. Accepted by prefix in the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `model` | model: a model name, or a brush model index written as \*N | none | no | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). Examples: `?62`, `?63`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | no | Position x y z in map units (inferred from stock values). Examples: `-1152 -1216 48`, `760 1464 76`. |
| `physasset` | string: any text | none | no | Physics asset name (inferred from stock values). Examples: `triggermodeldummydefault`. |

### `trigger_multiple_bcs_destroyedbldg`

Brush trigger for the 'destroyedbldg' area (inferred from the class name). Accepted by prefix in the converter.

- **Kind:** brush entity, no box.
- **Editor colour:** `#E64C4C` (RGB 0.9, 0.3, 0.3).
- **Build:** kept by the converter. Accepted by prefix in the converter.
- **Seen in stock maps:** 2 (mp_backlot2).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `model` | model: a model name, or a brush model index written as \*N | none | no | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). Examples: `?72`, `?74`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | no | Position x y z in map units (inferred from stock values). Examples: `1536 408 288`, `1516 -308 288`. |
| `physasset` | string: any text | none | no | Physics asset name (inferred from stock values). Examples: `triggermodeldummydefault`. |

### `trigger_multiple_bcs_dumpster`

Brush trigger for the 'dumpster' area (inferred from the class name). Accepted by prefix in the converter.

- **Kind:** brush entity, no box.
- **Editor colour:** `#E64C4C` (RGB 0.9, 0.3, 0.3).
- **Build:** kept by the converter. Accepted by prefix in the converter.
- **Seen in stock maps:** 2 (mp_shipment).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `model` | model: a model name, or a brush model index written as \*N | none | no | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). Examples: `?17`, `?22`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | no | Position x y z in map units (inferred from stock values). Examples: `172 1576 80`, `-708 2496 72`. |
| `physasset` | string: any text | none | no | Physics asset name (inferred from stock values). Examples: `triggermodeldummydefault`. |

### `trigger_multiple_bcs_electronicstore`

Brush trigger for the 'electronicstore' area (inferred from the class name). Accepted by prefix in the converter.

- **Kind:** brush entity, no box.
- **Editor colour:** `#E64C4C` (RGB 0.9, 0.3, 0.3).
- **Build:** kept by the converter. Accepted by prefix in the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `model` | model: a model name, or a brush model index written as \*N | none | no | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). Examples: `?52`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | no | Position x y z in map units (inferred from stock values). Examples: `-588 1294 76`. |
| `physasset` | string: any text | none | no | Physics asset name (inferred from stock values). Examples: `triggermodeldummydefault`. |

### `trigger_multiple_bcs_forklift`

Brush trigger for the 'forklift' area (inferred from the class name). Accepted by prefix in the converter.

- **Kind:** brush entity, no box.
- **Editor colour:** `#E64C4C` (RGB 0.9, 0.3, 0.3).
- **Build:** kept by the converter. Accepted by prefix in the converter.
- **Seen in stock maps:** 2 (mp_shipment).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `model` | model: a model name, or a brush model index written as \*N | none | no | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). Examples: `?18`, `?23`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | no | Position x y z in map units (inferred from stock values). Examples: `152 2464 76`, `-828 1520 48`. |
| `physasset` | string: any text | none | no | Physics asset name (inferred from stock values). Examples: `triggermodeldummydefault`. |

### `trigger_multiple_bcs_garage`

Brush trigger for the 'garage' area (inferred from the class name). Accepted by prefix in the converter.

- **Kind:** brush entity, no box.
- **Editor colour:** `#E64C4C` (RGB 0.9, 0.3, 0.3).
- **Build:** kept by the converter. Accepted by prefix in the converter.
- **Seen in stock maps:** 1 (mp_backlot2).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `model` | model: a model name, or a brush model index written as \*N | none | no | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). Examples: `?75`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | no | Position x y z in map units (inferred from stock values). Examples: `-4 860 276`. |
| `physasset` | string: any text | none | no | Physics asset name (inferred from stock values). Examples: `triggermodeldummydefault`. |

### `trigger_multiple_bcs_junkyard`

Brush trigger for the 'junkyard' area (inferred from the class name). Accepted by prefix in the converter.

- **Kind:** brush entity, no box.
- **Editor colour:** `#E64C4C` (RGB 0.9, 0.3, 0.3).
- **Build:** kept by the converter. Accepted by prefix in the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `model` | model: a model name, or a brush model index written as \*N | none | no | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). Examples: `?58`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | no | Position x y z in map units (inferred from stock values). Examples: `-1067 356 128`. |
| `physasset` | string: any text | none | no | Physics asset name (inferred from stock values). Examples: `triggermodeldummydefault`. |

### `trigger_multiple_bcs_laundromat`

Brush trigger for the 'laundromat' area (inferred from the class name). Accepted by prefix in the converter.

- **Kind:** brush entity, no box.
- **Editor colour:** `#E64C4C` (RGB 0.9, 0.3, 0.3).
- **Build:** kept by the converter. Accepted by prefix in the converter.
- **Seen in stock maps:** 1 (mp_backlot2).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `model` | model: a model name, or a brush model index written as \*N | none | no | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). Examples: `?76`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | no | Position x y z in map units (inferred from stock values). Examples: `920 1320 288`. |
| `physasset` | string: any text | none | no | Physics asset name (inferred from stock values). Examples: `triggermodeldummydefault`. |

### `trigger_multiple_bcs_mainstreet`

Brush trigger for the 'mainstreet' area (inferred from the class name). Accepted by prefix in the converter.

- **Kind:** brush entity, no box.
- **Editor colour:** `#E64C4C` (RGB 0.9, 0.3, 0.3).
- **Build:** kept by the converter. Accepted by prefix in the converter.
- **Seen in stock maps:** 1 (mp_backlot2).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `model` | model: a model name, or a brush model index written as \*N | none | no | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). Examples: `?77`, `?54`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | no | Position x y z in map units (inferred from stock values). Examples: `960 -308 296`, `-642 -368 82`. |
| `physasset` | string: any text | none | no | Physics asset name (inferred from stock values). Examples: `triggermodeldummydefault`. |

### `trigger_multiple_bcs_rugshop`

Brush trigger for the 'rugshop' area (inferred from the class name). Accepted by prefix in the converter.

- **Kind:** brush entity, no box.
- **Editor colour:** `#E64C4C` (RGB 0.9, 0.3, 0.3).
- **Build:** kept by the converter. Accepted by prefix in the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `model` | model: a model name, or a brush model index written as \*N | none | no | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). Examples: `?57`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | no | Position x y z in map units (inferred from stock values). Examples: `635 -2054 96`. |
| `physasset` | string: any text | none | no | Physics asset name (inferred from stock values). Examples: `triggermodeldummydefault`. |

### `trigger_multiple_bcs_shippingarea`

Brush trigger for the 'shippingarea' area (inferred from the class name). Accepted by prefix in the converter.

- **Kind:** brush entity, no box.
- **Editor colour:** `#E64C4C` (RGB 0.9, 0.3, 0.3).
- **Build:** kept by the converter. Accepted by prefix in the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `model` | model: a model name, or a brush model index written as \*N | none | no | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). Examples: `?60`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | no | Position x y z in map units (inferred from stock values). Examples: `-184 -907 116`. |
| `physasset` | string: any text | none | no | Physics asset name (inferred from stock values). Examples: `triggermodeldummydefault`. |

### `trigger_multiple_bcs_shops`

Brush trigger for the 'shops' area (inferred from the class name). Accepted by prefix in the converter.

- **Kind:** brush entity, no box.
- **Editor colour:** `#E64C4C` (RGB 0.9, 0.3, 0.3).
- **Build:** kept by the converter. Accepted by prefix in the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `model` | model: a model name, or a brush model index written as \*N | none | no | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). Examples: `?56`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | no | Position x y z in map units (inferred from stock values). Examples: `640 -1128 112`. |
| `physasset` | string: any text | none | no | Physics asset name (inferred from stock values). Examples: `triggermodeldummydefault`. |

### `trigger_multiple_bcs_tireshop`

Brush trigger for the 'tireshop' area (inferred from the class name). Accepted by prefix in the converter.

- **Kind:** brush entity, no box.
- **Editor colour:** `#E64C4C` (RGB 0.9, 0.3, 0.3).
- **Build:** kept by the converter. Accepted by prefix in the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `model` | model: a model name, or a brush model index written as \*N | none | no | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). Examples: `?61`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | no | Position x y z in map units (inferred from stock values). Examples: `-1172 -548 108`. |
| `physasset` | string: any text | none | no | Physics asset name (inferred from stock values). Examples: `triggermodeldummydefault`. |

### `trigger_multiple_bcs_tower`

Brush trigger for the 'tower' area (inferred from the class name). Accepted by prefix in the converter.

- **Kind:** brush entity, no box.
- **Editor colour:** `#E64C4C` (RGB 0.9, 0.3, 0.3).
- **Build:** kept by the converter. Accepted by prefix in the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `model` | model: a model name, or a brush model index written as \*N | none | no | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). Examples: `?53`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | no | Position x y z in map units (inferred from stock values). Examples: `1005 218 127`. |
| `physasset` | string: any text | none | no | Physics asset name (inferred from stock values). Examples: `triggermodeldummydefault`. |

### `trigger_multiple_bcs_warehouse`

Brush trigger for the 'warehouse' area (inferred from the class name). Accepted by prefix in the converter.

- **Kind:** brush entity, no box.
- **Editor colour:** `#E64C4C` (RGB 0.9, 0.3, 0.3).
- **Build:** kept by the converter. Accepted by prefix in the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `model` | model: a model name, or a brush model index written as \*N | none | no | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). Examples: `?64`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | no | Position x y z in map units (inferred from stock values). Examples: `-16 72 128`. |
| `physasset` | string: any text | none | no | Physics asset name (inferred from stock values). Examples: `triggermodeldummydefault`. |

### `trigger_radius`

Radius trigger point. Spawned in script with radius and height (gsc gametypes/ball.gsc:163).

- **Kind:** point entity.
- **Box:** from (-8, -8, -8) to (8, 8, 8), in map units.
- **Editor colour:** `#FF8033` (RGB 1, 0.5, 0.2).
- **Build:** kept by the converter.
- **Seen in stock maps:** 21 (mp_shipment, mp_backlot2).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). Examples: `0 90 0`, `0 180 0`, `0 255 0`. |
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `height` | float: a number | none | no | Height in map units. gsc gametypes/ball.gsc:163 passes it as the height argument of spawn('trigger_radius'). Examples: `128`. |
| `nophysics` | bool: 0, 1, true or false | none | no | Disables physics on the entity (inferred from stock values). Examples: `true`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). Examples: `368 1528 43`, `1369 80 55`, `-373 -722.5 59`. |
| `radius` | float: a number | none | yes | Radius in map units. gsc gametypes/ball.gsc:163 passes it to spawn('trigger_radius'); inferred for other classes. Examples: `64`, `100`, `120`. |
| `script_gameobjectname` | string: any text | none | no | Gameobject or objective id read by gsc gameobjects.gsc:17. Values such as bombzone and sd are inferred from stock values. Examples: `ctf`, `grind`, `dom onslaught`. |
| `script_label` | string: any text | none | no | Label for a gameobject trigger (gsc gameobjects.gsc:4432-4435; gametypes/arena.gsc:2165-2177 compares '1', '2', '3'). Examples: `a`, `b`, `_a`. |
| `target` | targetname: any text, the name other entities use | none | no | targetname of the entity or struct this one points to (gsc ammorestock.gsc:12). Examples: `ctf_flag_axis`, `ctf_flag_allies`. |
| `targetname` | targetname: any text, the name other entities use | none | no | Name that other entities use to find this one (gsc ammorestock.gsc:8). Examples: `ctf_trig_axis`, `ctf_zone_axis`, `grind_location`. |

### `trigger_use`

Brush trigger for player use (inferred from stock values).

- **Kind:** brush entity, no box.
- **Editor colour:** `#E64C4C` (RGB 0.9, 0.3, 0.3).
- **Build:** kept by the converter.
- **Seen in stock maps:** 6 (mp_shipment, mp_backlot2).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `model` | model: a model name, or a brush model index written as \*N | none | no | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). Examples: `?38`, `?41`, `?42`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | no | Position x y z in map units (inferred from stock values). Examples: `-864 -144 110`, `960 -24 62`, `224 8 64`. |
| `physasset` | string: any text | none | no | Physics asset name (inferred from stock values). Examples: `TriggerModelStaticDummyDefault`. |
| `script_gameobjectname` | string: any text | none | no | Gameobject or objective id read by gsc gameobjects.gsc:17. Values such as bombzone and sd are inferred from stock values. Examples: `cyber`. |
| `targetname` | targetname: any text, the name other entities use | none | no | Name that other entities use to find this one (gsc ammorestock.gsc:8). Examples: `cyber_emp_pickup_trig`. |

### `trigger_use_touch`

Brush trigger for use on touch. Its classname is removed by gsc gametypes/arm.gsc:312.

- **Kind:** brush entity, no box.
- **Editor colour:** `#E64C4C` (RGB 0.9, 0.3, 0.3).
- **Build:** kept by the converter.
- **Seen in stock maps:** 31 (mp_shipment, mp_backlot2).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `model` | model: a model name, or a brush model index written as \*N | none | no | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). Examples: `?28`, `?29`, `?31`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | no | Position x y z in map units (inferred from stock values). Examples: `-3792 4784 2208`, `-8576 9408 24`, `-1144 -152 114`. |
| `physasset` | string: any text | none | no | Physics asset name (inferred from stock values). Examples: `TriggerModelStaticDummyDefault`. |
| `script_bombmode_original` | bool: 0, 1, true or false | none | no | Bomb-mode flag kept by the IW3 converter (contract 15 s3.2); meaning inferred. Examples: `1`. |
| `script_gameobjectname` | string: any text | none | no | Gameobject or objective id read by gsc gameobjects.gsc:17. Values such as bombzone and sd are inferred from stock values. Examples: `bombzone`, `cyber`, `dd_bombzone`. |
| `script_label` | string: any text | none | no | Label for a gameobject trigger (gsc gameobjects.gsc:4432-4435; gametypes/arena.gsc:2165-2177 compares '1', '2', '3'). Examples: `_a`, `_b`, `_c`. |
| `target` | targetname: any text, the name other entities use | none | no | targetname of the entity or struct this one points to (gsc ammorestock.gsc:12). Examples: `pf1195_auto1`, `pf1197_auto1`, `pf1198_auto1`. |
| `targetname` | targetname: any text, the name other entities use | none | no | Name that other entities use to find this one (gsc ammorestock.gsc:8). Examples: `bombtrigger`, `bombzone`, `pf1195_auto2`. |

## Script

3 classes.

### `script_origin`

Point used by scripts for positions and effects (inferred from stock values). Spawned in script at gsc gametypes/cyber.gsc:598.

- **Kind:** point entity.
- **Box:** from (-8, -8, -8) to (8, 8, 8), in map units.
- **Editor colour:** `#33E6E6` (RGB 0.2, 0.9, 0.9).
- **Build:** kept by the converter.
- **Seen in stock maps:** 7 (mp_shipment, mp_backlot2).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `_color` | vec3: three numbers, x y z | none | no | Editor draw colour r g b, 0 to 1 (inferred from stock values). Examples: `.5 1 1`, `0 1 .5`, `0.501961 0 1`. |
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). Examples: `0 155 0`, `0 50 0`, `0 95 0`. |
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). Examples: `3072 -3840 64`, `-2560 3584 64`, `0 0 2560`. |
| `radius` | float: a number | none | no | Radius in map units. gsc gametypes/ball.gsc:163 passes it to spawn('trigger_radius'); inferred for other classes. Examples: `200`, `500`, `256`. |
| `script_accel` | float: a number | none | no | Acceleration value kept by the IW3 converter (contract 15 s3.2); meaning inferred. Examples: `20`, `15`, `30`. |
| `script_airspeed` | float: a number | none | no | Air speed value kept by the IW3 converter (contract 15 s3.2); meaning inferred. Examples: `30`, `40`, `60`. |
| `script_delay` | float: a number | none | no | Delay value (inferred from stock values; no use found in the decompiled 1.20.4 scripts). Examples: `2`, `3`. |
| `script_targetoffset_z` | float: a number | none | no | Vertical target offset (inferred from stock values). Examples: `55`. |
| `target` | targetname: any text, the name other entities use | none | no | targetname of the entity or struct this one points to (gsc ammorestock.gsc:12). Examples: `auto601`, `auto598`, `auto600`. |
| `targetname` | targetname: any text, the name other entities use | none | no | Name that other entities use to find this one (gsc ammorestock.gsc:8). Examples: `minimap_corner`, `airstrikeheight`, `heli_leave`. |

### `script_struct`

Point struct found by name with getstruct(name, "targetname") (gsc gametypes/ball.gsc:376).

- **Kind:** point entity.
- **Box:** from (-8, -8, -8) to (8, 8, 8), in map units.
- **Editor colour:** `#FFD933` (RGB 1, 0.85, 0.2).
- **Build:** kept by the converter.
- **Seen in stock maps:** 563 (mp_shipment, mp_backlot2).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `_color` | vec3: three numbers, x y z | none | no | Editor draw colour r g b, 0 to 1 (inferred from stock values). Examples: `0 .5 0`, `0.000000 1.000000 0.250980`. |
| `all_modes` | bool: 0, 1, true or false | none | no | Applies in all game modes (inferred from one stock value). Examples: `1`. |
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). Examples: `0 135 0`, `0 45 0`, `0 270 0`. |
| `dom` | int: a whole number | none | no | Flag value (inferred from one stock value). Examples: `0`. |
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `model` | model: a model name, or a brush model index written as \*N | none | no | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). Examples: `mp_fullbody_assault`, `misc_vm_tactical_ladder`, `offhand_wm_grenade_smoke`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). Examples: `1600 -1536 1536`, `-2048 -4096 1536`, `-128 2241 59`. |
| `radius` | float: a number | none | no | Radius in map units. gsc gametypes/ball.gsc:163 passes it to spawn('trigger_radius'); inferred for other classes. Examples: `16000`, `500`. |
| `receivevolumedecals` | bool: 0, 1, true or false | none | no | Receives volume decals (inferred from stock values). Examples: `0`. |
| `script_label` | string: any text | none | no | Label for a gameobject trigger (gsc gameobjects.gsc:4432-4435; gametypes/arena.gsc:2165-2177 compares '1', '2', '3'). Examples: `dz_flare`, `2`, `1`. |
| `script_noteworthy` | string: any text | none | no | Free-form tag that gametype code branches on (gsc gametypes/arena.gsc:509-527, ammorestock.gsc:22-25). Examples: `floor`, `default`. |
| `script_team` | string: any text | none | no | Team side, axis or allies (gsc gametypes/arm.gsc:2292, 2300). Examples: `neutral`, `axis`, `allies`. |
| `target` | targetname: any text, the name other entities use | none | no | targetname of the entity or struct this one points to (gsc ammorestock.gsc:12). Examples: `pf16_auto3126`, `pf1_auto3126`, `pf14_auto3127`. |
| `targetname` | targetname: any text, the name other entities use | none | no | Name that other entities use to find this one (gsc ammorestock.gsc:8). Examples: `pf16_auto3126`, `camera_intro`, `pf16_auto3135`. |

### `script_struct_heli`

Helicopter path point (inferred from stock values: script_linkto, script_goalyaw).

- **Kind:** point entity.
- **Box:** from (-8, -8, -8) to (8, 8, 8), in map units.
- **Editor colour:** `#FFD933` (RGB 1, 0.85, 0.2).
- **Build:** kept by the converter.
- **Seen in stock maps:** 21 (mp_shipment, mp_backlot2).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `_color` | vec3: three numbers, x y z | none | no | Editor draw colour r g b, 0 to 1 (inferred from stock values). Examples: `1.000000 1.000000 0.000000`. |
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). Examples: `0 45 0`, `0 346 0`, `0 188 0`. |
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). Examples: `-4096 -4096 1280`, `-808 -1352 1024`, `-1424 1184 1024`. |
| `radius` | float: a number | none | no | Radius in map units. gsc gametypes/ball.gsc:163 passes it to spawn('trigger_radius'); inferred for other classes. Examples: `256`. |
| `script_goalyaw` | bool: 0, 1, true or false | none | no | Helicopter path goal-yaw flag (inferred from stock values). Examples: `true`. |
| `script_linkname` | string: any text | none | no | Link name read by gsc-raw animation_suite.gsc:124; role inferred from stock values. Examples: `pf16_6`, `pf16_13`, `pf16_11`. |
| `script_linkto` | string: any text | none | no | Name this entity links to: getent() for a trigger link target (gsc-raw door.gsc:283-285); a list of node names for node links (gsc-raw utility/entity.gsc:192-194). Examples: `pf16_6`, `pf16_7`, `pf16_14`. |
| `script_stopnode` | bool: 0, 1, true or false | none | no | Helicopter path stop-node flag (inferred from stock values). Examples: `1`. |
| `targetname` | targetname: any text, the name other entities use | none | no | Name that other entities use to find this one (gsc ammorestock.gsc:8). Examples: `ks_heli_entrance`, `ks_heli_goal`, `ks_heli_path`. |

## Lights and atmosphere

4 classes.

### `light`

Light: an omni, or a spot when it has a target or a fov key; built as a primary light (5.1); optional IES profile for spots.

- **Kind:** point entity.
- **Box:** from (-8, -8, -8) to (8, 8, 8), in map units.
- **Editor colour:** `#FFFF80` (RGB 1, 1, 0.5).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `_color` | vec3: three numbers, x y z | `1 1 1` | no | Light colour r g b, 0 to 1 (or 0 to 255); built into the primary light (compile_iw8.cpp localPrimaryLights). |
| `angles` | vec3: three numbers, x y z | `0 0 0` | no | Aim (pitch yaw roll) when there is no target. |
| `fov_inner` | float: a number | `60` | no | Full inner cone angle in degrees (below fov_outer). |
| `fov_outer` | float: a number | `90` | no | Full outer cone angle in degrees (1 to 170). |
| `intensity` | float: a number | `1` | no | Multiplies the colour. |
| `iw8r_bulb_radius` | float: a number | `1` | no | Bulb radius in map units, above 0 (converter default 1). Written as bulb_radius; widens the light's volume. |
| `iw8r_fade` | string: any text | none | no | Fade range 'near far': two numbers (converter default 0 0). Written as fade_offset. |
| `iw8r_falloff` | float: a number | `0.2` | no | Distance falloff, above 0 (converter default 0.2). Written as distance_falloff. |
| `iw8r_ies` | string: any text | none | no | LM-63 .ies light profile (absolute or relative to the map folder). The build writes it as the spot's own light definition (Replay IES segment, docs/research/38); in-game look not verified. |
| `iw8r_intensity_units` | float: a number | none | no | Intensity in light units, 0 or more. Written as intensity: the converter uses it and skips the radius brightness calibration. Unset: calibrated from the radius. |
| `iw8r_ir_intensity` | float: a number | `0` | no | Thermal vision intensity (converter default 0). Written as ir_intensity. |
| `iw8r_shadow_bias` | float: a number | `0.75` | no | Shadow map bias (converter default 0.75). Written as shadow_bias. |
| `iw8r_shadows` | bool: 0, 1, true or false | `0` | no | 1 gives the light a shadow map. Spot lights only; an omni ignores it. Written as can_use_shadow_map. |
| `iw8r_uv_intensity` | float: a number | `0` | no | UV vision intensity (converter default 0). Written as uv_intensity. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units. |
| `radius` | float: a number | `400` | no | Reach in map units. |
| `target` | string: any text | none | no | Aim the spot at the entity with this targetname. |

### `light_spot`

Spot light: built as a primary light (5.1) aimed at its target or along its angles; optional IES profile.

- **Kind:** point entity.
- **Box:** from (-8, -8, -8) to (8, 8, 8), in map units.
- **Editor colour:** `#FFFF80` (RGB 1, 1, 0.5).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `_color` | vec3: three numbers, x y z | `1 1 1` | no | Light colour r g b, 0 to 1 (or 0 to 255); built into the primary light (compile_iw8.cpp localPrimaryLights). |
| `angles` | angles: three numbers | `0 0 0` | no | Aim (pitch yaw roll) when there is no target. |
| `fov_inner` | float: a number | `60` | no | Full inner cone angle in degrees (below fov_outer). |
| `fov_outer` | float: a number | `90` | no | Full outer cone angle in degrees (1 to 170). |
| `intensity` | float: a number | `1` | no | Multiplies the colour. |
| `iw8r_bulb_radius` | float: a number | `1` | no | Bulb radius in map units, above 0 (converter default 1). Written as bulb_radius; widens the light's volume. |
| `iw8r_fade` | string: any text | none | no | Fade range 'near far': two numbers (converter default 0 0). Written as fade_offset. |
| `iw8r_falloff` | float: a number | `0.2` | no | Distance falloff, above 0 (converter default 0.2). Written as distance_falloff. |
| `iw8r_ies` | string: any text | none | no | LM-63 .ies light profile (absolute or relative to the map folder). The build writes it as the spot's own light definition (Replay IES segment, docs/research/38); in-game look not verified. |
| `iw8r_intensity_units` | float: a number | none | no | Intensity in light units, 0 or more. Written as intensity: the converter uses it and skips the radius brightness calibration. Unset: calibrated from the radius. |
| `iw8r_ir_intensity` | float: a number | `0` | no | Thermal vision intensity (converter default 0). Written as ir_intensity. |
| `iw8r_shadow_bias` | float: a number | `0.75` | no | Shadow map bias (converter default 0.75). Written as shadow_bias. |
| `iw8r_shadows` | bool: 0, 1, true or false | `0` | no | 1 gives the light a shadow map. Spot lights only; an omni ignores it. Written as can_use_shadow_map. |
| `iw8r_uv_intensity` | float: a number | `0` | no | UV vision intensity (converter default 0). Written as uv_intensity. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units. |
| `radius` | float: a number | `400` | no | Reach in map units. |
| `target` | string: any text | none | no | Aim the spot at the entity with this targetname. |

### `reflection_probe`

Reflection probe: built into world.json with its optional mins / maxs volume, priority and feather; captured in the editor (Compile &gt; Capture Reflection Probes).

- **Kind:** point entity.
- **Box:** from (-8, -8, -8) to (8, 8, 8), in map units.
- **Editor colour:** `#994CE6` (RGB 0.6, 0.3, 0.9).
- **Build:** kept by the converter. Reflection probe: built into world.json with its optional mins / maxs volume, priority and feather; captured in the editor (Compile &gt; Capture Reflection Probes).
- **Seen in stock maps:** 1 (mp_backlot2).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). Examples: `0 0 0`. |
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `index` | int: a whole number | none | no | Index value (inferred from stock values). Examples: `0`, `44`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). Examples: `-1521.12 -84.8494 -574.662`, `-1233.66 -6581.78 -125.309`. |
| `script_noteworthy` | string: any text | none | no | Free-form tag that gametype code branches on (gsc gametypes/arena.gsc:509-527, ammorestock.gsc:22-25). Examples: `van_probe`, `van_probe_fallback`. |
| `spawnflags` | int: a whole number | `0` | no | Bit flags. Scripts test single bits (gsc gametypes/br_quest_util.gsc:1795, gametypes/br_dom_quest.gsc:71); per-class bit meanings are not sourced. Examples: `1`. |
| `targetname` | targetname: any text, the name other entities use | none | no | Name that other entities use to find this one (gsc ammorestock.gsc:8). Examples: `pf2_auto4`, `pf0_auto4`. |

### `worldspawn`

World geometry and global map settings. One per map (contract 15 s3.1).

- **Kind:** brush entity, no box.
- **Editor colour:** `#CCCCCC` (RGB 0.8, 0.8, 0.8).
- **Build:** kept by the converter.
- **Seen in stock maps:** 2 (mp_shipment, mp_backlot2).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `__smuid` | string: any text | none | no | Stable entity id written by the tooling (inferred from stock values). Examples: `_e0`. |
| `_color` | vec3: three numbers, x y z | none | no | Editor draw colour r g b, 0 to 1 (inferred from stock values). Examples: `1 1 1`. |
| `_csm_bound_center` | vec3: three numbers, x y z | none | no | Cascaded shadow map bounds centre (inferred from stock values). Examples: `-605.625000 44.500000 -110.500000`. |
| `_csm_bound_halfsize` | vec3: three numbers, x y z | none | no | Cascaded shadow map bounds half size (inferred from stock values). Examples: `10668.125000 9251.250000 1513.250000`. |
| `_primarylightcount` | string: any text | none | no | Primary light counts, four integers (inferred from stock values). Examples: `2 0 0 0`, `1 0 0 0`. |
| `ambient` | float: a number | none | no | Ambient light level (inferred from stock values; kept by the IW3 converter, contract 15 s3.2). Examples: `0`. |
| `audio_compile_umbra` | bool: 0, 1, true or false | none | no | Audio umbra compile flag (inferred from stock values). Examples: `1`. |
| `csm_enable` | bool: 0, 1, true or false | none | no | Cascaded shadow maps enable (inferred from stock values). Examples: `1`. |
| `csm_max_resolution` | int: a whole number | none | no | Cascaded shadow map max resolution (inferred from stock values). Examples: `256`. |
| `csm_sample_size` | int: a whole number | none | no | Cascaded shadow map sample size (inferred from stock values). Examples: `2`, `1`. |
| `dustmaterial` | material: a material name | none | no | Dust material (inferred from stock values). Examples: `skl_sand_dune_01_blend`. |
| `forwardplus` | bool: 0, 1, true or false | none | no | Forward+ lighting enable (inferred from stock values). Examples: `1`. |
| `generatenavmesh` | bool: 0, 1, true or false | none | no | Generate a navmesh at compile (inferred from stock values). Examples: `1`. |
| `highresgpulightgrid` | bool: 0, 1, true or false | none | no | High-resolution GPU light grid (inferred from stock values). Examples: `1`. |
| `iw8r_airspace` | string: any text | `6144 2048` | no | How far the map's visibility cell reaches above (first number) and around (second number) the map's geometry, in units. Aircraft killstreaks and their cameras fly there; with a cell that ends at the geometry they were not drawn. "0" keeps the tight cell. Examples: `6144 2048`, `0`. |
| `iw8r_ambient` | string: any text | none | no | Ambient bed of the zone: the name of one of the map's own sounds (&lt;map dir&gt;/sounds/&lt;name&gt;.wav or .mp3), built into the map sound bank and played as a 2D loop while in the zone. Stock aliases cannot be used here (silent in the map bank); use a looping sound_emitter for those. In game NOT VERIFIED. Examples: `wind_bed`, `factory_hum`. |
| `iw8r_ambient_oneshots` | string: any text | none | no | Ambient one-shots of the zone: "&lt;min s&gt; &lt;max s&gt; &lt;sound&gt;[:weight] ..." with the map's own sounds (&lt;map dir&gt;/sounds); one of them plays at random every min..max seconds while in the zone. In game NOT VERIFIED. Examples: `6 12 bird_call:3 dog_bark`. |
| `iw8r_audio_interior` | bool: 0, 1, true or false | `0` | no | 1: the zone uses the stock "interior" occlusion and filter presets (sounds behind walls muffled as indoors); 0: the stock "default" ones. Written to the sound table (research 63). In game NOT VERIFIED. |
| `iw8r_gametypes` | string: any text | none | no | The game modes the game offers for this map, as ui_gametype names with spaces between them (war dm dom sd ctf ...). Left out, the editor lists the modes the map has spawns and objects for. Written to map.json as gameTypes; needs a client that reads it. Examples: `war dm`, `war dom sd`. |
| `iw8r_reverb` | string: any text | none | no | The map's reverb everywhere outside audio zones. Reverb room type (one of the 59 stock types, e.g. iw8_md_room, iw8_ext_open_space_med_1; Help lists them in research 63) and optional dry wet fade: "&lt;room&gt; [dry wet fade]". The build writes the map sound table (in game NOT VERIFIED). Examples: `iw8_ext_open_space_med_1`, `iw8_md_room`, `iw8_lg_room_warehouse`. |
| `iw8r_sun_shadow_range` | string: any text | `3 2` | no | Sun shadow cascade size multipliers sent to every player (sm_sunCascadeSizeMultiplier1 / 2); "0" turns it off. Longer values push sun shadows further. Examples: `3 2`, `4 3`, `0`. |
| `iw8r_sun_sprite` | string: any text | none | no | Sun sprite and flare: "&lt;size 1..64&gt; [flare alpha 0..1] [blind darken 0..1] [glare lighten 0..1]". Draws a sun disc in the sky in the sundirection (size in 480-line virtual pixels, CoD-style), a lens flare when looking at it, and the blind / glare strength. Written to extracted/sun/&lt;map&gt;.sun (research 39). In game NOT VERIFIED. Examples: `16 0.5`, `20 0.7 0.5 0.3`. |
| `lightmapcount` | int: a whole number | none | no | Lightmap count (inferred from stock values). Examples: `4`. |
| `lmapblurpass` | int: a whole number | none | no | Lightmap blur passes (inferred from stock values). Examples: `0`. |
| `northyaw` | float: a number | none | no | North yaw. The IW3 converter drops it and always writes 90 (contract 15 s3.2, s3.4). Examples: `90`, `180`. |
| `objectgroupcost` | int: a whole number | none | no | Object group cost (inferred from stock values). Examples: `0`. |
| `outdoor_only_maxs` | vec3: three numbers, x y z | none | no | Outdoor-only bounds maximum (inferred from stock values). Examples: `1984 4736 1064`. |
| `outdoor_only_mins` | vec3: three numbers, x y z | none | no | Outdoor-only bounds minimum (inferred from stock values). Examples: `-3016 8 -104`. |
| `precomputed_sky_illumination_bounce_count` | int: a whole number | none | no | Precomputed sky illumination bounce count (inferred from stock values). Examples: `1`. |
| `precomputed_sky_illumination_diffuse_reflectance` | float: a number | none | no | Precomputed sky illumination diffuse reflectance (inferred from stock values). Examples: `0.5`. |
| `rayguidemode` | int: a whole number | none | no | Ray guide mode (inferred from stock values). Examples: `1`, `2`. |
| `skycolor` | vec3: three numbers, x y z | none | no | Sky colour r g b (kept by the IW3 converter, contract 15 s3.2; inferred from stock values). Examples: `1 1 1`. |
| `skydomelighting` | bool: 0, 1, true or false | none | no | Sky dome lighting enable (inferred from stock values). Examples: `1`. |
| `skylight` | bool: 0, 1, true or false | none | no | Sky light enable (inferred from stock values). Examples: `0`. |
| `skyrotation` | angles: three numbers | none | no | Sky rotation (inferred from stock values). Examples: `0 158 0`, `0 -15.4 0`, `0 -46 0`. |
| `smallesthole` | float: a number | none | no | Smallest hole size (inferred from stock values). Examples: `4`, `2`. |
| `smallestoccluder` | float: a number | none | no | Smallest occluder size (inferred from stock values). Examples: `16`, `24`, `64`. |
| `suncolor` | vec3: three numbers, x y z | none | no | Sun colour r g b (kept by the IW3 converter, contract 15 s3.2; inferred from stock values). Examples: `0.992 1 0.996`, `1 0.9764 0.945`, `0.9843 0.9921 1`. |
| `sundirection` | angles: three numbers | none | no | Sun direction (kept by the IW3 converter, contract 15 s3.2; inferred from stock values). Examples: `-44.5 162 0`, `-20.261 -15.4 0`, `-45 -46 0`. |
| `sunisprimarylight` | bool: 0, 1, true or false | none | no | Sun is the primary light (inferred from stock values). Examples: `1`. |
| `sunlight` | float: a number | none | no | Sun intensity (kept by the IW3 converter, contract 15 s3.2; inferred from stock values). Examples: `63.89`, `27.118`, `84.582`. |
| `tu0bakechanges` | bool: 0, 1, true or false | none | no | Stock flag, meaning unknown (inferred from one stock value). Examples: `1`. |
| `uselmapcompression` | bool: 0, 1, true or false | none | no | Lightmap compression enable (inferred from stock values). Examples: `0`. |
| `usemightconnectflag` | bool: 0, 1, true or false | none | no | Might-connect flag (inferred from stock values). Examples: `1`. |

## Effects and sound

2 classes.

### `fx_origin`

Stock effect placement: the map script plays the 'fx' (loadfx + playfx / triggerfx), repeating every 'delay' seconds.

- **Kind:** point entity.
- **Box:** from (-8, -8, -8) to (8, 8, 8), in map units.
- **Editor colour:** `#FF8033` (RGB 1, 0.5, 0.2).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `delay` | float: a number | none | no | Seconds between repeats; empty plays once (effects) or loops (sounds with loop 1). |
| `fx` | string: any text | none | no | Stock effect name (vfx/...), from the Effects browser. |
| `iw8r_damage` | string: any text | none | no | The effect hurts: "&lt;amount&gt; [radius] [interval seconds]". A player within the radius (default 48) takes that damage every interval (default 1 second). No trigger is needed. Examples: `10 48 0.5`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | no | Position x y z in map units (inferred from stock values). |
| `sound_loop` | string: any text | `1` | no | 1: the effect's sound loops. 0: it plays once each time the effect's delay passes (once only without a delay). Examples: `0`, `1`. |
| `soundalias` | string: any text | none | no | A sound played at the effect: one of the map's own sounds (sounds folder) or a stock alias. Loops unless sound_loop is 0. Examples: `fire_loop`. |

### `sound_emitter`

Stock sound placement: the map script plays the 'soundalias' as a loop or a repeating one-shot ('delay').

- **Kind:** point entity.
- **Box:** from (-8, -8, -8) to (8, 8, 8), in map units.
- **Editor colour:** `#33E6E6` (RGB 0.2, 0.9, 0.9).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `delay` | float: a number | none | no | Seconds between repeats; empty plays once (effects) or loops (sounds with loop 1). |
| `loop` | bool: 0, 1, true or false | `0` | no | 1 = a looping sound on a script_origin; 0 = a one-shot repeated every delay seconds. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | no | Position x y z in map units (inferred from stock values). |
| `soundalias` | string: any text | none | no | Stock sound alias, from the Sounds browser. |

## Navigation

4 classes.

### `node_3d_pathnode`

3D pathnode (inferred from stock values). Not accepted by the converter.

- **Kind:** point entity.
- **Box:** from (-8, -8, -8) to (8, 8, 8), in map units.
- **Editor colour:** `#808080` (RGB 0.5, 0.5, 0.5).
- **Build:** dropped by the converter. Not accepted by the converter.
- **Seen in stock maps:** 206 (mp_backlot2).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). Examples: `0 270 0`, `0 0 0`, `0 90 0`. |
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). Examples: `344 -1984 240`, `-88 -392 216`, `8 -392 216`. |

### `node_negotiation_begin`

Start of a traversal; target matches a node_negotiation_end targetname; animscript picks the move (stock: jump_\*, ladder_up). Used by the generated bot navigation.

- **Kind:** point entity.
- **Box:** from (-8, -8, -8) to (8, 8, 8), in map units.
- **Editor colour:** `#808080` (RGB 0.5, 0.5, 0.5).
- **Build:** kept by the converter.
- **Seen in stock maps:** 293 (mp_shipment, mp_backlot2).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). Examples: `0 270 0`, `0 90 0`, `0 0 0`. |
| `animscript` | string: any text | none | no | Animation name for a negotiation node (inferred from stock values). Examples: `ladder_up`, `jump_across_100`, `jump_down_136`. |
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). Examples: `638 532 114`, `-1066 -630 114`, `-62 2352 84`. |
| `target` | targetname: any text, the name other entities use | none | no | targetname of the entity or struct this one points to (gsc ammorestock.gsc:12). Examples: `pf16_auto3178`, `pf16_auto3179`, `pf18_auto2108`. |
| `targetname` | targetname: any text, the name other entities use | none | no | Name that other entities use to find this one (gsc ammorestock.gsc:8). Examples: `traverse`, `pf1194_auto716`, `pf1263_auto716`. |

### `node_negotiation_end`

End of a traversal; targetname matches a node_negotiation_begin target. Used by the generated bot navigation.

- **Kind:** point entity.
- **Box:** from (-8, -8, -8) to (8, 8, 8), in map units.
- **Editor colour:** `#808080` (RGB 0.5, 0.5, 0.5).
- **Build:** kept by the converter.
- **Seen in stock maps:** 293 (mp_shipment, mp_backlot2).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). Examples: `0 270 0`, `0 90 0`, `0 0 0`. |
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). Examples: `638 488 218`, `-1066 -586 284`, `90 2352 84`. |
| `targetname` | targetname: any text, the name other entities use | none | no | Name that other entities use to find this one (gsc ammorestock.gsc:8). Examples: `pf16_auto3178`, `pf16_auto3179`, `pf18_auto2108`. |

### `node_pathnode`

AI pathnode: the converter places it in the generated bot navigation (research 45).

- **Kind:** point entity.
- **Box:** from (-8, -8, -8) to (8, 8, 8), in map units.
- **Editor colour:** `#808080` (RGB 0.5, 0.5, 0.5).
- **Build:** kept by the converter. AI pathnode: the converter places it in the generated bot navigation (research 45).
- **Seen in stock maps:** 2096 (mp_shipment, mp_backlot2).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). Examples: `0 180 0`, `0 90 0`, `0 270 0`. |
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `lgvthinstable` | bool: 0, 1, true or false | none | no | LGV thin-stable flag (inferred from stock values). Examples: `1`, `0`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). Examples: `1223.45 1297.64 98.1145`, `-49.912 -2535.78 88.8043`, `-259.761 -2529.22 91.5244`. |
| `spawnflags` | int: a whole number | `0` | no | Bit flags. Scripts test single bits (gsc gametypes/br_quest_util.gsc:1795, gametypes/br_dom_quest.gsc:71); per-class bit meanings are not sourced. Examples: `68`, `76`. |

## Models and prefabs

6 classes.

### `collmap_scriptable`

Collision-map brush for scriptable objects (inferred from stock values). Not accepted by the converter.

- **Kind:** brush entity, no box.
- **Editor colour:** `#B2B2B2` (RGB 0.7, 0.7, 0.7).
- **Build:** dropped by the converter. Not accepted by the converter.
- **Seen in stock maps:** 13 (mp_shipment, mp_backlot2).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `model` | model: a model name, or a brush model index written as \*N | none | no | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). Examples: `*61`, `*62`, `*63`. |
| `targetname` | targetname: any text, the name other entities use | none | no | Name that other entities use to find this one (gsc ammorestock.gsc:8). Examples: `veh8_civ_lnd_secho`, `veh8_civ_lnd_secho_ai_obstacle`, `veh8_civ_lnd_secho_static_dst`. |

### `misc_model`

Static model placement: built as a world static model (custom or exported stock model) with convex hull collision.

- **Kind:** point entity.
- **Box:** from (-8, -8, -8) to (8, 8, 8), in map units.
- **Editor colour:** `#9999E6` (RGB 0.6, 0.6, 0.9).
- **Build:** kept by the converter.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `ground_lighting` | string: any text | none | no | Ground lighting: the placement is lit by one flat colour instead of the light grid. "1" = the converter's default grey; "r g b" (0..1, or 0..255) sets the colour (model_flags 1 + ground_lighting, research 54). Colour channel order in game NOT VERIFIED. Examples: `1`, `0.4 0.35 0.3`, `90 80 70`. |
| `iw8r_bob` | string: any text | none | no | Bobs without scripting: "&lt;height&gt; [seconds]". The map script moves it up that many units and back down, once every seconds (default 2). A misc_model with this key is built as a live script_model; a missing targetname is filled in. Examples: `16 2`, `48 4`. |
| `iw8r_material` | string: any text | none | no | One material on every surface of this model (drag a material from the Materials browser onto the model). The build makes a copy of the stock model with that material; custom models keep their own materials. Examples: `tm2co/me_curb_stone_01`. |
| `iw8r_no_collision` | string: any text | none | no | 1: the model has no player collision (it stays a live script_model; bullets still hit it). Used by Make Ladder so the ladder model does not stand between the player and the climbable brush. A misc_model with this key is built as a script_model. Examples: `1`. |
| `iw8r_spin` | string: any text | none | no | Spins without scripting: "&lt;seconds per turn&gt; [yaw\|pitch\|roll]". The map script turns it one full circle every that many seconds. A misc_model with this key is built as a live script_model; a missing targetname is filled in. Examples: `4`, `2 roll`. |
| `lod_scale` | float: a number | `1` | no | Scales this placement's LOD switch distances (Replay per-placement table: 0.25 to 4; stock maps often use 4). Written as the placement's lod_scale (research 54). Examples: `2`, `4`. |
| `model` | model: a model name, or a brush model index written as \*N | none | yes | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). |
| `modelscale` | float: a number | none | no | Model scale factor (inferred from stock values). |
| `no_shadow` | bool: 0, 1, true or false | `0` | no | 1: the model casts no shadow: none from local (primary) lights (shadow_lights empty) and none from the sun (the fork's no_sun_shadow; research 54 / 60). Examples: `1`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `misc_prefab`

Editor-only prefab placement (inferred from the class name). Not built.

- **Kind:** point entity.
- **Box:** from (-8, -8, -8) to (8, 8, 8), in map units.
- **Editor colour:** `#8066E6` (RGB 0.5, 0.4, 0.9).
- **Build:** dropped by the converter. Editor-only prefab placement (inferred from the class name). Not built.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). |
| `model` | model: a model name, or a brush model index written as \*N | none | no | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

### `script_brushmodel`

Brush model whose surfaces scripts use, referenced as '\*N' (contract 15 s3.5).

- **Kind:** brush entity, no box.
- **Editor colour:** `#66B2FF` (RGB 0.4, 0.7, 1).
- **Build:** kept by the converter.
- **Seen in stock maps:** 109 (mp_shipment, mp_backlot2).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `iw8r_bob` | string: any text | none | no | Bobs without scripting: "&lt;height&gt; [seconds]". The map script moves it up that many units and back down, once every seconds (default 2). A misc_model with this key is built as a live script_model; a missing targetname is filled in. Examples: `16 2`, `48 4`. |
| `iw8r_brush_visual` | string: any text | `1` | no | Moving brush models are drawn by a generated model that the map script links to them (their own surfaces did not show in game). 0 turns this off for this brush model. Examples: `0`. |
| `iw8r_door` | string: any text | none | no | Door without scripting. "hinge &lt;angle&gt; [time]": hangs on a hinge at one end and swings away from the player who presses Use near it; it stays open until used again. "rotate &lt;angle&gt; [time] [hold]": the same swing, closing by itself after the hold time. "slide &lt;x&gt; &lt;y&gt; &lt;z&gt; [time] [hold]": slides and comes back. Needs a targetname. A trigger_use or trigger_multiple whose target is the door opens it; without one the door gets its own use area (its bounds grown by 40 units). Examples: `hinge 100 0.6`, `rotate 90 1 3`, `slide 0 0 96 1.5 3`. |
| `iw8r_hinge` | string: any text | none | no | Where a hinged door (iw8r_door hinge / rotate) turns. Without it the hinge is the end of the door's longer side with the lower coordinate. "far" takes the other end; "x y z" names the hinge point itself. Examples: `far`, `128 64 0`. |
| `iw8r_lift` | string: any text | none | no | Lift without scripting: "&lt;dz&gt; [time] [wait]". Needs a targetname; the map script moves it up dz units over time seconds, waits, moves it back down and repeats. Examples: `128 2 2`, `256 3 1`. |
| `iw8r_spin` | string: any text | none | no | Spins without scripting: "&lt;seconds per turn&gt; [yaw\|pitch\|roll]". The map script turns it one full circle every that many seconds. A misc_model with this key is built as a live script_model; a missing targetname is filled in. Examples: `4`, `2 roll`. |
| `model` | model: a model name, or a brush model index written as \*N | none | no | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). Examples: `*1`, `*4`, `*5`. |
| `nophysics` | bool: 0, 1, true or false | none | no | Disables physics on the entity (inferred from stock values). Examples: `1`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | no | Position x y z in map units (inferred from stock values). Examples: `-66 0 2432`, `-8544 9312 0`, `-8710 10150 132`. |
| `physasset` | string: any text | none | no | Physics asset name (inferred from stock values). Examples: `ScriptBrushModelDummyDefault`, `ScriptBrushModelDummyFixed`. |
| `script_gameobjectname` | string: any text | none | no | Gameobject or objective id read by gsc gameobjects.gsc:17. Values such as bombzone and sd are inferred from stock values. Examples: `bombzone`, `cyber`, `dd_bombzone`. |
| `script_noteworthy` | string: any text | none | no | Free-form tag that gametype code branches on (gsc gametypes/arena.gsc:509-527, ammorestock.gsc:22-25). Examples: `heli_height_mesh`. |
| `spawnflags` | int: a whole number | `0` | no | Bit flags. Scripts test single bits (gsc gametypes/br_quest_util.gsc:1795, gametypes/br_dom_quest.gsc:71); per-class bit meanings are not sourced. Examples: `1`. |
| `targetname` | targetname: any text, the name other entities use | none | no | Name that other entities use to find this one (gsc ammorestock.gsc:8). Examples: `heli_pilot_mesh`, `care_package_col`, `smoke_grenade_sight_clip_256`. |

### `script_model`

Point model placed by script. The model must exist as xmodel/&lt;name&gt;.json (contract 15 s3.6).

- **Kind:** point entity.
- **Box:** from (-8, -8, -8) to (8, 8, 8), in map units.
- **Editor colour:** `#66B2FF` (RGB 0.4, 0.7, 1).
- **Build:** kept by the converter.
- **Seen in stock maps:** 62 (mp_shipment, mp_backlot2).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). Examples: `0 360 0`, `0 270 0`, `0 90 0`. |
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `iw8r_anim` | string: any text | none | no | Stock animation (xanim name) played on this model in game (scriptmodelplayanim). The model must be live: a targetname that something targets (Brush &gt; Make Scripted). Research 61. Examples: `desktop_fan_fn_01_loop`. |
| `iw8r_anim_loop` | float: a number | `0` | no | Replay the animation every N seconds; 0 plays it once. Examples: `2`. |
| `iw8r_bob` | string: any text | none | no | Bobs without scripting: "&lt;height&gt; [seconds]". The map script moves it up that many units and back down, once every seconds (default 2). A misc_model with this key is built as a live script_model; a missing targetname is filled in. Examples: `16 2`, `48 4`. |
| `iw8r_material` | string: any text | none | no | One material on every surface of this model (drag a material from the Materials browser onto the model). The build makes a copy of the stock model with that material; custom models keep their own materials. Examples: `tm2co/me_curb_stone_01`. |
| `iw8r_no_collision` | string: any text | none | no | 1: the model has no player collision (it stays a live script_model; bullets still hit it). Used by Make Ladder so the ladder model does not stand between the player and the climbable brush. A misc_model with this key is built as a script_model. Examples: `1`. |
| `iw8r_spin` | string: any text | none | no | Spins without scripting: "&lt;seconds per turn&gt; [yaw\|pitch\|roll]". The map script turns it one full circle every that many seconds. A misc_model with this key is built as a live script_model; a missing targetname is filled in. Examples: `4`, `2 roll`. |
| `lgvmerge` | string: any text | none | no | LGV merge group (inferred from stock values). Examples: `bldg20_a4`. |
| `model` | model: a model name, or a brush model index written as \*N | none | yes | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). Examples: `ee_fence_trainyard_gate_left_animated`, `ee_fence_trainyard_gate_right_animated`, `metal_chain_01_wrapped`. |
| `modelscale` | float: a number | none | no | Model scale factor (inferred from stock values). Examples: `1`, `2`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). Examples: `-107 2548.25 59.75`, `-108 2548.25 59.75`, `-107.5 2546.75 89.25`. |
| `receivevolumedecals` | bool: 0, 1, true or false | none | no | Receives volume decals (inferred from stock values). Examples: `1`, `0`. |
| `script_exploder` | string: any text | none | no | Exploder id, stored as exploderindex (gsc gametypes/btm.gsc:1830-1832). Examples: `pf1195_2`, `pf1197_2`, `pf1198_2`. |
| `script_gameobjectname` | string: any text | none | no | Gameobject or objective id read by gsc gameobjects.gsc:17. Values such as bombzone and sd are inferred from stock values. Examples: `ctf`, `bombzone`, `sd`. |
| `sound_csv_include` | string: any text | none | no | Sound CSV include path (inferred from one stock value). Examples: `destruct/destruct_water_barrel`. |
| `spawnflags` | int: a whole number | `0` | no | Bit flags. Scripts test single bits (gsc gametypes/br_quest_util.gsc:1795, gametypes/br_dom_quest.gsc:71); per-class bit meanings are not sourced. Examples: `4`, `0`. |
| `target` | targetname: any text, the name other entities use | none | no | targetname of the entity or struct this one points to (gsc ammorestock.gsc:12). Examples: `pf1195_auto2`, `pf1197_auto2`, `pf1214_auto2`. |
| `targetname` | targetname: any text, the name other entities use | none | no | Name that other entities use to find this one (gsc ammorestock.gsc:8). Examples: `infil_door_left`, `infil_door_right`, `infil_door_chain`. |

### `triggermodeldummydefault`

Listed by the converter as an accepted class (compile_iw8.cpp). In stock data it appears only as a physasset name (inferred). Unclear mapper use.

- **Kind:** point entity.
- **Box:** from (-8, -8, -8) to (8, 8, 8), in map units.
- **Editor colour:** `#CCCCCC` (RGB 0.8, 0.8, 0.8).
- **Build:** kept by the converter. Listed by the converter as an accepted class (compile_iw8.cpp).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). |

## Miscellaneous

8 classes.

### `dynamic_prop`

Physics prop (editor class, 6.15): the build writes it as converter clutter (a dynamic model with a physics preset) that falls and can be knocked around. In-game behaviour not verified yet.

- **Kind:** point entity.
- **Box:** from (-8, -8, 0) to (8, 8, 16), in map units.
- **Editor colour:** `#FF9933` (RGB 1, 0.6, 0.2).
- **Build:** kept by the converter. Physics prop (editor class, 6.15): the build writes it as converter clutter (a dynamic model with a physics preset) that falls and can be knocked around.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | vec3: three numbers, x y z | `0 0 0` | no | Pitch yaw roll in degrees. |
| `iw8r_material` | string: any text | none | no | One material on every surface of this model (drag a material from the Materials browser onto the model). The build makes a copy of the stock model with that material; custom models keep their own materials. Examples: `tm2co/me_curb_stone_01`. |
| `mass` | float: a number | `10` | no | Mass in kilograms; props up to 2 kg are debris. |
| `material` | string: any text | `wood` | no | Physics material word: wood, metal, plastic, glass, paper, rubber, brick, ... |
| `model` | string: any text | none | yes | A custom model (models folder) or an exported stock model. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units. |

### `export_volume`

Animation export volume (inferred from stock values). Not accepted by the converter.

- **Kind:** brush entity, no box.
- **Editor colour:** `#4CE680` (RGB 0.3, 0.9, 0.5).
- **Build:** dropped by the converter. Not accepted by the converter.
- **Seen in stock maps:** 1 (mp_backlot2).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `filterpreset` | string: any text | none | no | Filter preset name (inferred from one stock value). Examples: `Anim_Export_Volumes`. |
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `model` | model: a model name, or a brush model index written as \*N | none | no | xmodel name for point models; '\*N' selects brush model N and is written by the compiler (inferred from stock values). Examples: `*3`, `*1`, `*2`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | no | Position x y z in map units (inferred from stock values). Examples: `-366 -1800 387`, `-24 2904 192`, `-344 -3464 192`. |
| `target` | targetname: any text, the name other entities use | none | no | targetname of the entity or struct this one points to (gsc ammorestock.gsc:12). Examples: `tag_align_mp_infil`. |
| `targetname` | targetname: any text, the name other entities use | none | no | Name that other entities use to find this one (gsc ammorestock.gsc:8). Examples: `mp_backlot2_infil_landing`, `anim_export_infil_axis`, `anim_export_infil_allies`. |
| `volume_intersection_test` | string: any text | none | no | Volume intersection test mode (inferred from one stock value, BOUNDING_BOX). Examples: `BOUNDING_BOX`. |

### `func_group`

Editor-only grouping of brushes. Not a game entity.

- **Kind:** brush entity, no box.
- **Editor colour:** `#808080` (RGB 0.5, 0.5, 0.5).
- **Build:** dropped by the converter. Editor-only grouping of brushes.

This class has no keys.

### `iw8r_decal`

A decal: a picture projected onto the surfaces inside a box (stains, paint, markings). The box is placed at the origin; its forward (X) axis is the direction the picture is projected and must point into the surface. Needs a converter with the decal producer (research 80 and 82). Not seen in game.

- **Kind:** point entity.
- **Box:** from (-8, -8, -8) to (8, 8, 8), in map units.
- **Editor colour:** `#99CCFF` (RGB 0.6, 0.8, 1).
- **Build:** kept by the converter. Needs a converter with the decal producer (research 80 and 82).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | vec3: three numbers, x y z | `0 0 0` | no | Pitch, yaw, roll. Forward is the projection direction: for a floor decal use 90 0 0 (pointing down). Examples: `90 0 0`, `0 90 0`. |
| `draw_order` | int: a whole number | `0` | no | Which decal draws on top where two overlap: -4 to 3, higher on top. Examples: `0`, `2`. |
| `feather` | float: a number | `0` | no | Soft fade toward the front and back of the box, 0 to 1. Examples: `0`, `0.25`. |
| `material` | string: any text | none | yes | The decal picture: a material of your custom library (its alpha is the decal's shape), or the name of a stock decal material the game always has loaded. Examples: `my/arrow_decal`, `i/vfx_decal_surface_scorch_1`. |
| `opacity` | float: a number | `1` | no | How strong the decal is, 0 to 1. Examples: `1`, `0.6`. |
| `origin` | vec3: three numbers, x y z | none | yes | The centre of the box. Examples: `0 0 4`. |
| `size` | vec3: three numbers, x y z | `8 64 64` | no | Depth, width and height of the box in map units. Depth is along the projection direction. Examples: `8 64 64`, `16 128 128`. |
| `uv_rotate` | float: a number | `0` | no | Turns the picture, in degrees. Examples: `0`, `90`. |
| `uv_scale` | string: any text | `1 1` | no | Tiling of the picture across the box: two numbers above 0. Examples: `1 1`, `2 2`. |
| `uv_shift` | string: any text | `0 0` | no | Shifts the picture: two numbers. Examples: `0 0`, `0.5 0`. |

### `iw8r_loot_point`

A loot point: a place where the battle royale scripts may put loot (a weapon, armour, cash, a supply box). On a Warzone stock map, File &gt; Edit Loot Points turns the map's own points into these entities; move, add or delete them and File &gt; Rebuild Stock Zone writes them back. On your own map, place them where loot should appear under the battle royale game type. Not kept in the entity text: the build writes them into the map's loot table. Not seen in game.

- **Kind:** point entity.
- **Box:** from (-6, -6, 0) to (6, 6, 12), in map units.
- **Editor colour:** `#33E659` (RGB 0.2, 0.9, 0.35).
- **Build:** kept by the converter. On a Warzone stock map, File &gt; Edit Loot Points turns the map's own points into these entities; move, add or delete them and File &gt; Rebuild Stock Zone writes them back. Not kept in the entity text: the build writes them into the map's loot table.

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | vec3: three numbers, x y z | `0 0 0` | no | Pitch, yaw, roll of the loot item or box. Examples: `0 90 0`. |
| `loot_cluster` | string: any text | `auto` | no | auto: the build picks the cluster by position (a free point when no cluster box holds it). free: a single point outside every cluster. cache: a loot cache (supply box) point. A number: that cluster of the stock map; a point moved out of its cluster's box is treated as auto. Your own maps have no clusters: only cache matters. Examples: `auto`, `free`, `cache`. |
| `loot_zone` | string: any text | `auto` | no | auto: the build picks the named zone of the stock map by position. A number: that zone (0 = none). Not used on your own maps. Examples: `auto`, `0`. |
| `loot_zone_name` | string: any text | none | no | Information only: the name of the stock zone the point was in when it was read. |
| `origin` | vec3: three numbers, x y z | none | yes | Where the loot lies (on the floor). Examples: `0 0 0`. |

### `iw8r_volume`

Volumetric box (editor class, 5.7): written as a world volumetric (lit fog / light shafts inside the box). Density and falloff units and the in-game look are not verified yet.

- **Kind:** point entity.
- **Box:** from (-8, -8, -8) to (8, 8, 8), in map units.
- **Editor colour:** `#99CCFF` (RGB 0.6, 0.8, 1).
- **Build:** kept by the converter. Volumetric box (editor class, 5.7): written as a world volumetric (lit fog / light shafts inside the box).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | vec3: three numbers, x y z | `0 0 0` | no | Box rotation, pitch yaw roll. |
| `anisotropy` | float: a number | `0` | no | Forward scattering (-1 to 1). |
| `color` | vec3: three numbers, x y z | `1 1 1` | no | Scatter colour r g b, 0 to 1. |
| `density` | float: a number | `0.2` | no | Fog density (0 or more). |
| `falloff` | float: a number | `64` | no | Edge falloff (above 0). |
| `height_fade` | float: a number | `0` | no | Fade with height. |
| `maxs` | vec3: three numbers, x y z | `128 128 128` | no | Opposite box corner relative to the origin. |
| `mins` | vec3: three numbers, x y z | `-128 -128 -128` | no | Box corner relative to the origin. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Box anchor in map units. |

### `mp_global_intermission`

Intermission camera point (inferred from stock values).

- **Kind:** point entity.
- **Box:** from (-8, -8, -8) to (8, 8, 8), in map units.
- **Editor colour:** `#CC33CC` (RGB 0.8, 0.2, 0.8).
- **Build:** kept by the converter.
- **Seen in stock maps:** 2 (mp_shipment, mp_backlot2).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | angles: three numbers | `0 0 0` | no | Pitch yaw roll in degrees. Stock values use the middle (yaw) slot (inferred from stock values). Examples: `16.1056 198.082 -0.896628`, `0 18 0`, `0 232 0`. |
| `footprint_mask_group` | string: any text | `default` | no | Footprint mask group name (inferred from stock values; stock value 'default'). Examples: `default`. |
| `origin` | vec3: three numbers, x y z | `0 0 0` | yes | Position x y z in map units (inferred from stock values). Examples: `678 126 526`, `-864 1584 216`, `1186.12 46.01 730`. |

### `scriptable`

A stock scripted object (a scriptable): the game's own object with built-in behaviour, placed by the name of its stock definition. A definition the game always has loaded is placed by name: hardpoint_chevron (the Hardpoint zone marker). Doors, traffic cones, cars and other world objects live in stock map zones: with Edit &gt; Preferences &gt; Real stock scripted objects turned on the build copies the stock definition with its models, collision and materials into your map; with it off they are left out with a warning. Every stock definition is listed in data/asset_catalog/iw8_scriptables.tsv. Needs a converter with scriptable instances (research 86). The Warzone buy station br_plunder_box (and the few other objects of the game's Warzone file) is placed by name on a map made for the Warzone game type. On a stock map, File &gt; Edit Placed Objects shows the buy stations as these entities: move one, or copy one for an extra station. Not seen in game.

- **Kind:** point entity.
- **Box:** from (-8, -8, -8) to (8, 8, 8), in map units.
- **Editor colour:** `#FFBF33` (RGB 1, 0.75, 0.2).
- **Build:** kept by the converter. Doors, traffic cones, cars and other world objects live in stock map zones: with Edit &gt; Preferences &gt; Real stock scripted objects turned on the build copies the stock definition with its models, collision and materials into your map; with it off they are left out with a warning. Needs a converter with scriptable instances (research 86).

| Key | Type / values | Default | Required | Help |
|---|---|---|---|---|
| `angles` | vec3: three numbers, x y z | `0 0 0` | no | Pitch, yaw, roll. Examples: `0 90 0`. |
| `origin` | vec3: three numbers, x y z | none | yes | Where the object stands. Examples: `0 0 16`. |
| `script_linkname` | string: any text | none | no | Optional link name. |
| `script_linkto` | string: any text | none | no | Optional link target. |
| `script_noteworthy` | string: any text | none | no | Optional script label. |
| `scriptable` | string: any text | `hardpoint_chevron` | yes | The stock definition's name (data/asset_catalog/iw8_scriptables.tsv). Examples: `hardpoint_chevron`. |
| `target` | string: any text | none | no | Optional target name. |
| `targetname` | string: any text | none | no | Name that script uses to find it. Stock Hardpoint chevrons carry the name of their zone's marker group. Examples: `hp_zone_1_chevrons`. |
