# 51. Reference stock assets instead of carrying copies (roadmap 4.15 / 4.9)

Date: 2026-11-10. Owner request: "during compilation, use references to other zones' materials to preserve space".

## How a name-only reference works

x64-zt can write a stock asset as a name-only reference (`,name` header, `RegisterNameReference`, research 18 §2.5). The
game resolves the name at load time against the zones that are loaded at that moment. For a custom MP map those are the
map's own five zones plus the always-loaded common zones (`code_*`, `common_mp`, `techsets_common*`, `ww_common*`, the
patch zones). Another stock map's zone (`mp_backlot2.ff`, `techsets_mp_donetsk.ff`, ...) is not loaded while a custom map
plays, and a map zone cannot ask for it.

## What the showcase map would gain

Measured on the dev showcase build (`samples/dev/iw8build/mp_iw8r_dev_sho`, script `refshare.py` in the session
scratchpad, catalogs `data/asset_catalog/iw8_materials.tsv` / `iw8_models.tsv`):

| Carried stock copies | In an always-loaded zone |
|---|---|
| 45 materials (`iw8r_stock/<name>`) | 4, all in `common_br_mp` (the Warzone common zone; whether MP matches load it is UNVERIFIED) |
| 17 models (`iw8r_stock_<name>`) | the zone report (core `zone_deps.*`) lists common_br_mp 13, common_mp 3, code_post_gfx 2 assets for the whole build |

Every other stock asset the showcase uses lives only in other maps' zones. Referencing them by name would make them
missing in game (white or invisible), so the build must keep carrying copies. The zone dependency window
(Compile > Zone Dependencies) shows the per-asset picture for any map.

## Conclusion

- Name-only references are worth it only for assets in always-loaded zones: stock HUD icons (done: the level `mat`
  table, fork 719151ff), FX materials and models (the converter already does this), and the few common-zone props.
- For the bulk of world materials and props there is no space to win this way. The package size is set by what the
  converter writes into the zones; extraction-side DDS size (BGRA8 for custom images) does not reach the zone because the
  converter encodes the zone images itself.
- Cheaper wins that remain: smaller stock texture copies (Compile > Stock textures already caps colour at 1024 and normal
  maps at half that; a lower cap is a one-line option), and sharing one image between materials that use the same stock
  image (UNVERIFIED how much the converter already dedupes by digest: its resident-image cache keys on image digests,
  `iw3: resident images: ... digests` in the compile log).
- Common-zone props by name (research 18 §4.1, script_model / misc_model whose model is in `ww_common*` / `common_mp`)
  remain a small, safe follow-up in the fork.
