# Research reports

Evidence gathered for IW8 Radiant Suite. Each report cites file:line or tool output. Items marked UNKNOWN are
not guessed. The target build throughout is MW2019 Replay 1.20.4.7623265
(`game_dx12_ship_replay.exe`, MD5 `1c238fe327f2ecc3b0db924c5b425439`).

| # | Report | Use |
|---|---|---|
| 01 | [iw3xo-radiant feature audit](01_iw3xo_feature_audit.md) | Parity reference: which mapper features iw3xo added and which it inherits from CoD4 Radiant |
| 02 | [Radiant text grammar (`iwmap 4`)](02_iwmap4_format.md) | Grammar validated on 2,182 files; the editor's source container |
| 03 | [Legacy mw120rproxy build pipeline](03_build_pipeline_interface.md) | Older IW8 writer and its CoD4-based route (superseded; not used) |
| 04 | [IW8 asset access](04_iw8_asset_access.md) | ACTS is the stock Replay 1.20 reader; where the stock zones live |
| 05 | [Replay 1.20 runtime evidence](05_runtime_evidence_1_20.md) | RVAs proven by the proxy's hooks; live-link entry points |
| 06 | [Test assets and in-game evidence](06_test_assets.md) | Fixtures and owner-recorded in-game loads |
| 07 | [Editor feel reference](07_editor_feel_reference.md) | Stock camera, selection, grid, hotkey and undo behaviour |
| 08 | [x64-zt IW8 side](08_x64zt_iw8_conversion.md) | IW8 dump/build commands, `.iw8` container, MapEnts dump, extraction contract |
| 09 | [IW8 render features](09_iw8_render_features.md) | IW8-only lighting/render data the editor must author |
| 10 | [IW8 entity inventory](10_iw8_entity_inventory.md) | Stock classes and keys; `data/iw8_entity_classes.json` is now rebuilt by `iw8r iw8-classes` |
| 11 | [ACTS Replay baseline](11_acts_replay_baseline.md) | ACTS build/test procedure and the before-numbers for the layout work |
| 12 | [ACTS research switches review](12_acts_research_switches_review.md) | Review of the opt-in `ACTS_*` switches (findings fixed in the fork) |
| 13 | [iw8 level maker (third-party reference)](13_iw8_level_maker_reference.md) | Extra reference only, mixed reliability (see below) |
| 14 | [x64-zt verifyzone coverage](14_x64zt_verifyzone_coverage.md) | Per-pool real-zone evidence for the ported Replay layouts |
| 29 | [Probes, static models and sky](29_probes_models_sky.md) | Reflection probes per entity, `misc_model` placements and sky limits in the x64-zt extraction |
| 32 | [Game-mode objects](32_gamemode_objects.md) | DOM, hardpoint, S&D, CTF classnames/keys from the stock map_ents; backs `src/core/gamemode_templates.*` |
| 33 | [Sky route](33_sky_route.md) | What x64-zt needs for `world.sky` (legacy cube DDS), the stock hemi-octahedral BC6H skies, conversion and build check |
| 94 | [Stock lightmaps](94_stock_lightmaps.md) | Where a stock map's lightmap lives, its three planes and per-vertex UVs |
| 95 | [Lit shader math](95_lit_shader_math.md) | The stock world shader, the post chain, and the converter's world shader for editor maps (Game Lighting) |
| 96 | [Stock world removal](96_stock_world_removal.md) | Removing stock world triangles from drawing, Havok collision and the sun shadow |
| 97 | [Stock layered materials](97_stock_layered_materials.md) | How tw4 / twc4 blend up to 8 texture layers: weights, reveal ramp, constants |
| 98 | [Stock decals and displacement](98_stock_decals_displacement.md) | Blended world decals (sortKey 10-17) and `dn` displacement layers |
| 99 | [Stock decal volumes](99_stock_decal_volumes.md) | Projected box decals (paint stripes, stencils): layout, projection, opacity |
| 100 | [Stock ground clutter](100_stock_ground_clutter.md) | GPU clutter (Verdansk terrain only); MP grass is placed props and how the game shades them |

## Third-party "iw8 level maker" (report 13)

Used purely as extra reference, with each claim cross-checked:
- **Agrees** with our evidence: canonical key ids 212/709/80/652/1070/1072/845 (re-confirmed in stock `mp_shipment`), the
  five-zone package layout, and the `lighting.json` sun schema.
- **Do not copy:** its entity text is CRLF without the NUL terminator and puts each entity on one line, while stock Replay
  1.20 is LF, NUL-terminated and multi-line. It writes spawns as entities, while stock IW8 keeps them in `spawnList`.
  It uses 5-number classic `.map` faces and a solid-colour BC6H probe.
- No licence in the folder. Its bundled `gsc-tool.exe` is GPLv3 and is not vendored.
