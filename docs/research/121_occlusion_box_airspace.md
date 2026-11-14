# 121. The box of a map's occlusion data, and the airspace of a built map

**Why.** Owner playtest 2026-11-11 (ROADMAP F.14): on a built map the UAV's model was invisible, and players were
invisible in the Chopper Gunner view. Research 75 found no per-map key for it and left one lead: a built map's one
visibility cell ends at the map's geometry, and aircraft and their cameras are outside it. On 2026-11-12 the build
began to grow that cell (worldspawn `iw8r_airspace`), as a guess. Writing the offline proof for it on 2026-11-14
showed that the guess never reached the data the game asks: the built zone's occlusion data (the Umbra tome) still
ended at the geometry.

**Labels.** VERIFIED = read in the Replay 1.20 exe (Hex-Rays) or measured on stock zones. INFERRED = follows from
verified facts, not read or run. Nothing was run in the game.

## 1. What the game does with a camera outside the occlusion data (VERIFIED, Replay 1.20)

| Step | Function | What it does |
|---|---|---|
| 1 | `Umbra::PortalCuller::init` `0x141F9FBB0` | Reads the tome's box (tome `+0x150` / `+0x15C`; `+0x14` / `+0x20` for an old or flagged tome) and tests the camera's volume against it. Outside: error **5** and no walk. Also error 5 when the walk finds no start cell. |
| 2 | `Umbra_PortalCuller_initTileState` `0x141F9D330` | Clears the query's occlusion buffer (0x4000 bytes of zero) **before** it calls `init`, and returns the error. |
| 3 | `Umbra::Query::queryPortalVisibility` `0x141F91860` | Returns that error (6 = no tome, 3 = bad argument, 2 = out of memory, 5 from step 1). |
| 4 | `R_Umbra_QueryStaticVisibilityCmd` `0x1418CDD20` | On any error it calls `R_Umbra_ResetStaticVisibilityMasks`. |
| 5 | `R_Umbra_ResetStaticVisibilityMasks` `0x1418F1EE0` | Sets every bit: all world surfaces, static models, primary lights, reflection probes, volumetrics and decal collections are visible. |
| 6 | `R_Umbra_CullSceneEnts` `0x1418CC4A0` | Every scene entity (players, vehicles, script models) is tested with `R_Umbra_IsBoxVisible` `0x1418CCF20` against `s_umbraOcclusionBuffer[view]`. Only a null buffer means "visible"; nothing asks whether the query succeeded. |

So from a camera outside the tome's box the static world is drawn in full, and the entities are tested against a buffer
that step 2 cleared. INFERRED: a cleared buffer hides every box (the buffer holds what the portal walk found open, and
the walk did not run). That is the owner's picture exactly: the map is there, the players are not.

INFERRED for the other symptom (an aircraft seen from the ground): the camera is inside, the query runs, and the
aircraft's box lies past the outer faces of the cells, where no portal leads. The buffer has nothing open there.

## 2. How far stock occlusion data reaches (VERIFIED, measured)

`tests/umbra_bake_tests.cpp` `umbra_zone_airspace_report` (set `IW8R_TEST_GFXMAP_JSON` and `IW8R_TEST_AIR_PROBE=1`)
prints a tome's box, the box of its geometry objects, and how far above and beside them a point still lies in a cell.

| Zone | Tome box (min .. max) | Above the geometry's top | In a cell 40,000 up / out |
|---|---|---|---|
| mp_shipment | -131328 -128736 -130976 .. 130784 133376 131136 | 126,008 | yes / yes |
| mp_hideout | -262144 -302208 -131072 .. 288768 524288 194944 | 185,812 | yes / yes |
| mp_rust | -524288 -524288 -131072 .. 524288 524288 131392 | 129,607 | yes / yes |
| mp_backlot2 | -262144 -262144 -130928 .. 262144 131072 131216 | 124,395 | yes / yes |
| mp_m_speedball | -131072 -131072 -130752 .. 131072 131072 131392 | 128,816 | yes / yes |
| mp_aniyah_tac | -196608 -196608 -130528 .. 196608 196608 131584 | 116,858 | yes / yes |
| mp_village2 | -524288 -586624 -131072 .. 624256 582912 131712 | 127,806 | yes / yes |
| mp_emporium | -131744 -131072 -130304 .. 131072 262144 131808 | 106,208 | yes / yes |
| mp_m_king | -131968 -131104 -130960 .. 130160 131040 131168 | 110,688 | yes / yes |

Every stock tome reaches at least 131,072 units from the origin on every axis, below the map as well as above, and its
cells fill that box: no killstreak camera can leave it. (The probe stops at 40,000 units.)

Stock maps also have exactly one `GfxCell`, and its bounds equal `GfxWorld.bounds` (Shipment: half size 77553 x 79081
x 29681, Speedball the smallest read: 45357 x 48675 x 13825).

## 3. What a built map had (VERIFIED, read back from built zones)

- The converter sized the tome by the drawn geometry and the static models (`replayrender::BuildUmbraTome`,
  `m.sceneBounds`). The sample room's tome ended at its walls.
- The editor's airspace (2026-11-12) went into `dpvs.cells[0].bounds` of the extraction. The converter writes that into
  the `GfxCell` and its surface tree, and never into the tome. `tools/airspace_build_test.py` on the build of
  2026-11-14 before the fix: 0 of 3 air points in a cell.
- The default (6144 up, 2048 around) was also smaller than the Chopper Gunner's own distance (about 9,000 units,
  research 75).

## 4. What is built now

- **Editor** (`compile_iw8.cpp`): the one cell reaches 131,072 units above, around and below the geometry by default,
  the reach of a stock tome. Worldspawn `iw8r_airspace "<up> [out]"` sets it (`out` is around and below; `"0"` keeps
  the tight cell; more than 524,288 is cut to that and said). The world bounds, the light grid and the surface tree
  keep the map's own size.
- **Converter** (x64-zt, `BuildUmbraTome`): the tome's box also holds every cell's bounds. The one-cell tome's cell and
  objects span that box, as before; the occlusion bake (research 118) gets it as its free space, so the air becomes
  cells that reach the map through portals.
- The built sample's tome box is now -131360 -131360 -131104 .. 131360 131360 131344 (Speedball's: -131072 ..
  131072), and the cell coordinate scale is about 4.0 units, as in stock tomes (Shipment 3.9995).

## 5. Proof (offline)

`tools/airspace_build_test.py` builds the sample four ways through the editor and reads each zone's occlusion data
back with ACTS and the core reader:

| Build | Result |
|---|---|
| default | 6 of 6 air points (1,500 / 5,500 / 30,000 above, 9,000 / 30,000 beside, 2,000 below) are in a cell and see all 9 objects; points 200,000 away are outside |
| `iw8r_airspace "16384 8192"` | inside up to those distances, outside past them |
| `iw8r_airspace "0"` | the air points are outside every cell (every build before the fix) |
| default with the occlusion bake | 5 of 5 air points are in a cell and see all 9 objects |

Core test `compile_iw8_airspace_cell` covers the key (default, `0`, two numbers, one number, too large, not a number).

## 6. Not determined

- Nothing here was seen in the game. The chain in section 1 is read from the exe; that the cleared buffer hides the
  entities (step 6) and that this was the whole cause of F.14 are INFERRED.
- What reads `GfxCell.bounds` in Replay was not found. Stock cells are as large as their world; a built map's cell is
  now as large as its occlusion data, and its world bounds stay small.
- The UAV's missing sweep and pings on the minimap (the other half of F.14) are a compass matter (research 75
  section 4.1), not this.
