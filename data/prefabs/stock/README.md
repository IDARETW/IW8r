# Stock prefab library

Harvested from the shipped IW8 maps (world_cache exports, IW8 only) by `iw8r stock-prefabs`.

## How it was built

- Placements come from the gfx_map smodels of each zone (src/core/stock_smodels.h); only models marked `ok` in
  data/asset_catalog/iw8_models_status.tsv are used.
- Anchor: an instance whose model matches a category keyword (src/core/stock_prefabs.cpp, category table).
- Cluster: the anchor plus the ok instances within the category radius that are attached-looking (same family
  prefix as the anchor, a frame / trim / handle / glass model, or an extra category of the table). Radius 0 =
  no clusters for that category.
- Relative placement: anchor origin at (0,0,0), anchor yaw removed (anchor faces +X), member pitch and roll kept.
- Dedup: the same multiset of models with placements within 1 inch and 1 degree is one prefab; `count` is the
  number of anchors that produced it and `source_zones` the per-zone counts.
- Singles: one misc_model at the origin per ok model used in a category (`*_single.map`).
- Format: iwmap 4 with an empty worldspawn and misc_model entities (layout of tests/data/iw8_sample_room.map).

Parameters: min-count 1, max-per-category 400.

## Zones

- mp_aniyah_tac: 41895 instances
- mp_backlot2: 17651 instances
- mp_donetsk: 1096414 instances
- mp_emporium: 19337 instances
- mp_hideout: 12367 instances
- mp_m_fork: 8691 instances
- mp_m_king: 2695 instances
- mp_m_speedball: 3476 instances
- mp_rust: 6938 instances
- mp_shipment: 9535 instances
- mp_village2: 15669 instances

Instances seen: 1234668 (not ok: 554692, no category: 341690).

## Counts per category

| category | singles | clusters | candidates | dropped by cap | bytes |
|---|---:|---:|---:|---:|---:|
| barrel | 21 | 379 | 1312 | 912 | 276626 |
| barrier | 25 | 375 | 1162 | 762 | 230233 |
| cover | 29 | 0 | 29 | 0 | 7081 |
| crate | 125 | 275 | 6767 | 6367 | 229047 |
| debris | 182 | 0 | 182 | 0 | 44435 |
| door | 144 | 256 | 2637 | 2237 | 284621 |
| fence | 86 | 314 | 1438 | 1038 | 857309 |
| foliage | 221 | 0 | 221 | 0 | 53912 |
| frame | 69 | 331 | 7560 | 7160 | 330012 |
| furniture | 103 | 297 | 4035 | 3635 | 232677 |
| glass | 8 | 0 | 8 | 0 | 1935 |
| handle | 7 | 0 | 7 | 0 | 1692 |
| ladder | 14 | 67 | 81 | 0 | 38751 |
| lamp | 97 | 303 | 1114 | 714 | 224728 |
| pipe | 115 | 285 | 7349 | 6949 | 255710 |
| railing | 96 | 304 | 2883 | 2483 | 269879 |
| sandbag | 23 | 377 | 2403 | 2003 | 422344 |
| sign | 162 | 0 | 162 | 0 | 38918 |
| stair | 75 | 325 | 2114 | 1714 | 573745 |
| trim | 124 | 0 | 124 | 0 | 30213 |
| vehicle | 43 | 0 | 43 | 0 | 10415 |
| window | 117 | 283 | 3972 | 3572 | 281701 |
| **total** | 1886 | 4171 | 45603 | 39546 | 4695984 |

## Examples (largest clusters)

- `window/tm_window_office_frame_com_01_mp_v01.map`: anchor `tm/window_office_frame_com_01_mp`, 3 pieces, seen 775x
- `railing/tm_cs_barrier_handrail_128_double_v01.map`: anchor `tm/cs_barrier_handrail_128_double`, 2 pieces, seen 520x
- `railing/tm_cs_barrier_handrail_128_double_v02.map`: anchor `tm/cs_barrier_handrail_128_double`, 2 pieces, seen 520x
- `furniture/tm_storage_shelf_beam_wood_plank_v01.map`: anchor `tm/storage_shelf_beam_wood_plank`, 2 pieces, seen 515x
- `furniture/tm_storage_shelf_beam_horizontal_front_v01.map`: anchor `tm/storage_shelf_beam_horizontal_front`, 2 pieces, seen 511x
- `pipe/tm_pipe_metal_painted_straight_128_v01.map`: anchor `tm/pipe_metal_painted_straight_128`, 3 pieces, seen 417x
- `pipe/tm_pipe_metal_painted_bend_90_sml_v01.map`: anchor `tm/pipe_metal_painted_bend_90_sml`, 6 pieces, seen 380x
- `furniture/tm_storage_shelf_beam_horizontal_support_v01.map`: anchor `tm/storage_shelf_beam_horizontal_support`, 2 pieces, seen 340x

Files: 6057 .map prefabs, index in index.tsv.
