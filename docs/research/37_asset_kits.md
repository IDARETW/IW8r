# 37 Asset kits (roadmap 4.23)

Status: built for the Replay 1.20 stock lists. Keyword rules only; no network, no game files opened.
Output: `data/asset_catalog/iw8_kits.tsv` (61,397 rows: 15,521 models, 45,876 materials). Builder: `tools/build_asset_kits.py`.

## Re-run

```
py -3 -I tools\build_asset_kits.py
```

Reads `iw8_models.tsv`, `iw8_models_status.tsv`, `iw8_materials.tsv`, `iw8_materials_status.tsv`, `iw8_materials_status_b.tsv`, `iw8_stock_maps.tsv`. Writes only `iw8_kits.tsv` and prints per-kit counts, the buildable split, the unclassified share and the stock-map usage per kit. Stdlib only.

## Columns

`kit, kind (model|material), name, buildable (yes|no|unknown)`. The file opens with `#` comment lines, then a header row.

- **Buildable, models:** `ok` gives `yes`, `missing` gives `no`, a name not in the status file gives `unknown`.
- **Buildable, materials:** `ok` gives `yes`, `missing` or `grey` gives `no`, not listed gives `unknown`. `grey` is read as "present but not exported" (the status reasons say `not exported`), so it counts as not buildable. This is a judgement; change `buildable_value()` if `grey` should count as usable.
- **Name markers:** a leading `~` or `#` is a catalog marker, not part of the name. It is stripped for output and for status lookups. 39 models and 259 materials appear both plain and marked; each becomes one row. The meaning of the markers is not documented in the inputs. Confirm with the source before relying on them.

## Rules

`RULES` in the script is ordered; the first rule whose pattern matches (and whose exclude pattern does not) wins. A name lands in one kit only. Patterns match the lower-cased full name, including folder prefixes such as `tm/`. A keyword matches only as a whole token: a letter may not sit directly before or after it, so digits and `_ / - .` count as boundaries. "Start" rules match at the start of the name or of its basename after a folder.

In words, in order:

1. **Excluded, kept visible but outside the prop kits.**
   - `excluded_ui`: ui, hud, snapshot, waypoint, reticle, crosshair, minimap, icon.
   - `excluded_engine`: prepass, shadowcaster, depthtest, debug, techset, gfx, fontcache, rasterized, msaa, culling; names starting `white` or `black`.
   - `excluded_characters`: names starting body, head, hat, hero, sas, cosmetic, parts_wristwear, charm, clothing, vm_mp, mp_western, mp_eastern, mp_codl, dw_operator, viewhands, zmb; any helmet.
   - `excluded_weapons`: names starting attachment, weapon, iw8_cw, offhand, perk, ks, killstreak, lm_heal, tool_execution; any vm, wm, viewmodel, grenade, magazine, mag, silencer, scope, receiver, pistolgrip, bomb and similar weapon-part words. Not excluded when the name also has vfx, fontcache, wall.
2. **Gameplay and map geometry:** `gameplay_objects` (art, dz, carepackage, trophy, loot, flare, respawn, scriptable, explode); `world_chunks` (names starting `donetsk_`).
3. **Decals and debris:** `decals` (names starting `decal`); `debris` (debris, rubble, chunk, shard, splinter, casing, gib, dismembered, junk, litter, scrap, fragment, trash). Excluded when the name has food, container, trash_can.
4. **Vehicles and effects:** `vehicles_military` (veh8_mil, helicopter, humvee, apc); `vehicles_civilian` (veh8_civ, car, truck, van, sedan, taxi, motorcycle, scooter, trike, bicycle); `vehicles_other` (veh8, vehicle, wreck, plane, boat, yacht, ship, wheel, tire); `effects_vfx` (vfx).
5. **Street furniture:** hydrant, bollard, bench, parking, meter, bus_stop, mailbox, trash_can, bin, newspaper, vending, kiosk, atm, streetlamp, recycling. Excluded when the name has food.
6. **Props:** `decals` (decal, graffiti, stencil, sticker, mural, splat, label); `lights_fixtures` (light, lamp, lantern, sconce, chandelier, bulb, spotlight); `electronics` (tv, monitor, screen, computer, router, phone, radio, speaker, server, printer, camera, console, outlet, switch, keypad, alarm); `pipes_wires` (pipe, conduit, wire, cable, hose, duct, gutter; not barbed); `water`; `foliage_bushes`; `foliage_trees` (tree, palm, spruce, birch, pine, oak, cypress); `foliage_misc` (foliage, leaf, moss, bark); `doors_windows` (door, gate, window, shutter, hatch, garage, blinds, curtain; not cabinet, shelf); `glass` (glass, windshield; not sunglasses, glasses, bottle); `signs`; `barriers` (barrier, sandbag, jersey, traffic cone, barricade; not stairs); `fences` (fence, railing, handrail, picket, chainlink, barbed; not stairs); `containers` (container, dumpster, barrel, drum, canister, jerrycan, tank; not cinder); `crates_boxes` (crate, box, pallet, tote, cardboard, carton, wrapped; not cinder); `furniture` (chair, table, desk, sofa, bed, shelf, cabinet, stool, rug, carpet, appliance, fridge, stove, sink, toilet, radiator, locker); `machinery_industrial` (machinery, pulley, hoist, crane, generator, compressor, reservoir, pump, valve, conveyor); `terrain` (terrain, mountain, mud, ground, asphalt, pavement, road, sidewalk, curb, hill, trench; not wall, buildup, plaster); `rocks` (rock, boulder, cliff, pebble, riverbed).
7. **Soft goods, range, tools:** `soft_goods` (fabric, tarp, blanket, flag, sweater, clothes, clothing; models only); `range_targets` (shooting, target, targets); `tools_hardware` (hardhat, saw, broom, mop, bucket, ladder, hammer, shovel, drill, wrench, tool, crowbar, axe).
8. **Materials, surface families (material scope only):** `surface_concrete_masonry` (concrete, masonry, brick, cinder, stone, marble); `surface_wood` (wood, timber, plywood, boards); `surface_metal` (metal, steel, iron, corrugated, rust); `surface_plaster_paint` (plaster, stucco, drywall, wallpaper, paint, paper, ceramic); `surface_fabric` (fabric, cloth, canvas, tarp, leather, denim); `surface_plastic_rubber` (plastic, rubber, vinyl, pvc, foam).
9. **Building parts:** wall, roof, floor, ceiling, stair, pillar, column, beam, trim, catwalk, balcony, brick, cinder, plaster, siding, shingles, cornice, corner, support, plank, scaffold, slab, bridge, building, house, tile, masonry, concrete.
10. **Small props:** food, bottle, cup, can, jar, toy, ball, bag, knife, utensil, plate, book, magazine, pen, clock, medical, syringe, bread, snack, soda, candy, cookie, pot, pan, kettle, vase, basket, shoe, dish, kitchen, office, toiletries, cigarette, mask.
11. **Unclassified:** everything else.

## Counts

Models first, then materials. `buildable` split is yes/no/unknown.

| kit | models | materials | models yes/no/unknown |
| --- | ---: | ---: | --- |
| excluded_ui | 10 | 365 | 8/1/1 |
| excluded_engine | 6 | 166 | 5/1/0 |
| excluded_characters | 696 | 2,508 | 92/10/594 |
| excluded_weapons | 8,610 | 11,062 | 247/59/8,304 |
| gameplay_objects | 21 | 33 | 14/7/0 |
| world_chunks | 235 | 584 | 235/0/0 |
| decals | 2 | 408 | 2/0/0 |
| debris | 373 | 260 | 334/39/0 |
| vehicles_military | 87 | 419 | 82/5/0 |
| vehicles_civilian | 157 | 643 | 157/0/0 |
| vehicles_other | 47 | 166 | 47/0/0 |
| effects_vfx | 50 | 573 | 47/2/1 |
| street_furniture | 42 | 96 | 42/0/0 |
| lights_fixtures | 179 | 360 | 160/19/0 |
| electronics | 175 | 266 | 174/1/0 |
| pipes_wires | 372 | 175 | 372/0/0 |
| water | 35 | 124 | 34/1/0 |
| foliage_bushes | 220 | 207 | 220/0/0 |
| foliage_trees | 46 | 166 | 46/0/0 |
| foliage_misc | 9 | 19 | 9/0/0 |
| doors_windows | 500 | 678 | 500/0/0 |
| glass | 13 | 162 | 13/0/0 |
| signs | 179 | 363 | 179/0/0 |
| barriers | 102 | 151 | 102/0/0 |
| fences | 80 | 122 | 80/0/0 |
| containers | 190 | 211 | 190/0/0 |
| crates_boxes | 204 | 247 | 202/2/0 |
| furniture | 336 | 538 | 336/0/0 |
| machinery_industrial | 67 | 182 | 67/0/0 |
| terrain | 98 | 269 | 98/0/0 |
| rocks | 106 | 221 | 106/0/0 |
| soft_goods | 126 | (model only) | 126/0/0 |
| range_targets | 40 | 35 | 36/4/0 |
| tools_hardware | 73 | 92 | 73/0/0 |
| surface_concrete_masonry | (material only) | 704 | |
| surface_wood | (material only) | 306 | |
| surface_metal | (material only) | 703 | |
| surface_plaster_paint | (material only) | 384 | |
| surface_fabric | (material only) | 161 | |
| surface_plastic_rubber | (material only) | 159 | |
| building_parts | 1,118 | 387 | 1,118/0/0 |
| small_props | 407 | 260 | 406/0/1 |
| unclassified | 510 | 20,941 | 492/13/5 |

Unclassified share: **models 3.3%** (510 of 15,521), **materials 45.6%** (20,941 of 45,876). The model target (under 25%) is met. Most unclassified materials are hashed or generic names such as `2913n_1276n` and `mo/mtl_...`. They were left unclassified on purpose. Materials are not all covered by the target, and the material share should not be read as a model failure.

## Spot-checks (20 random names per big model kit)

- **building_parts:** `tm/stairs_industrial_handrail_bars_01a_straight`, `tm/mout_catwalk_support_brace_ibeam_96`, `cinderblock_01_destr_s1_f3`, `tm/building_roof_lip_01_tan_corner_top`.
- **doors_windows:** `school_locker_door_01`, `tm/window_frame_160_160`, `tm/storage_shipping_container_door_right_wet`.
- **pipes_wires:** `tm/ee_pipe_05_valve_01`, `tm/wire_hanging_01_096x16`, `electrical_conduit_set_electrical_box_big`.
- **furniture:** `tm/ee_stool_wood_01`, `tm/appliance_store_freezer_cabinet_01`, `decor_radiator_01`.
- **small_props:** `kitchen_01_teapot_01_destr_s1_f4`, `bathroom_toiletries_bottle_01`, `glass_beer_bottle_fn_01`.
- **debris:** `vfx_debris_metal_shrapnel_a_05`, `debris_bullet_casing_50_cal`, `com_barrel_shard5`.
- **containers:** `tm/container_barrel_plastic_01_closed_white_top_bottom`, `tm/oil_drum_big_01`, `tm/storage_shipping_container_20ft_closed_green_wet`.
- **signs:** `tm/ee_sign_frame_01_128`, `ee_sign_street_stop_01`, `tm/me_signs_industrial_fire_hazard`.
- **foliage_bushes:** `tm/foliage_grass_tall_02`, `flower_shop_planter_05`, `tm/un_foliage_vines_ivy_01_taper`.
- **vehicles_civilian:** `veh8_civ_lnd_palfa_rhd_infil`, `tm/decor_bicycles_01_clean_blue`, `tm/periph_city_uk_truck_01`.

## Known misclassifications

Found in the spot-checks and not fixed. They are kept because fixing each one would need a rule that risks more misses elsewhere.

- `tm/furniture_cafe_rail_system_stair_64_*`: furniture, though it is a stair rail.
- `tm/un_military_weapon_locker_02`: furniture (a locker, not a piece of furniture).
- `tm/ee_sign_hanging_plant_nursery`: foliage_bushes (matches "plant").
- `space_suit_chunks_02`: debris (matches "chunks").
- `tm/ee_military_shooting_range_target_clay_circle_chunk_04`: debris, not range_targets (chunk comes first).
- `tm/container_electrical_box_01_small`, `tm/container_soil_bag_stack_*`: containers, though they are an electrical box and bag stacks.
- `military_loot_crate_01_br_scavenger`: gameplay_objects (matches "loot"), not crates.
- `tm/building_wire_hole_wall_03`: pipes_wires (matches "wire").
- `tm/uk_house_chair_rail_01_8_window_b`: doors_windows (matches "window").
- Materials: `tmo/ee_buildings_industrial_radiotower_ground_01` is terrain ("ground"); `twc/dirt_dry_compacted_01_light_rvl` is lights ("light" as a colour word); `mo/london_coffee_machine_glass_inside` is glass.

## Decisions and deviations

- **Stock-map usage is a diagnostic, not a classifier.** The script prints the number of stock maps and the top zones for each kit (from `iw8_models.tsv` zones and the map kinds in `iw8_stock_maps.tsv`). Zone membership does not say what a model is: containers, rocks and signs appear in the same mp maps. Using zones as a rule would have forced matches, so the kit assignment uses name keywords only.
- Viewmodel arms and gloves (`vm_*`, `mo/vm_*`) are in `excluded_weapons`, not `excluded_characters`. The `vm` token is ambiguous and this keeps viewmodel assets out of the prop kits.
- `donetsk_*` map geometry is `world_chunks`, not `terrain`. These are map-level meshes, not props.
- `soft_goods` applies to models only. Materials with fabric words go to `surface_fabric`.

## Method

Rules were iterated against the output: first pass, then spot-checks of the largest kits, then fixes to misfires (for example `pile` pulling in cash stacks, `rail` pulling in crane rails, `soil` pulling in bag stacks, `dirt` pulling in crates). Each change was followed by a re-run and a count check. The rule table is in `tools/build_asset_kits.py`; edit it there and re-run.
