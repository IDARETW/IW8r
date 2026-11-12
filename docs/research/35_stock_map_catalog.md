# 35 Stock map catalogue (roadmap 11.1)

Status: done for the Replay 1.20 install at `E:\IW8\Builds\1.20-replay`. Read-only: the scan stats file names only and opens no fastfile.
Output: `data/asset_catalog/iw8_stock_maps.tsv` (93 rows). Builder: `tools/build_stock_map_catalog.py`.

## Re-run

```
py -3 -I tools\build_stock_map_catalog.py
py -3 -I tools\build_stock_map_catalog.py --install "E:\IW8\Builds\1.20-replay" --out data\asset_catalog
```

Run time on this machine: about 2 s (scan 1.8 s for 207,312 files, total wall 2.1 s). Stdlib only, no network.

## Columns

`map_id, kind, main_ff, companion_ffs, xpaks, total_bytes, can_open_in_editor, notes`. Paths are install-relative. `can_open_in_editor` is `yes` when a main `.ff` exists (File > Open IW8 Stock Map uses ACTS on that `.ff`). Notes are generated from the file layout (see Method).

## Summary

| kind | map ids | can open (main .ff) | xpak-only |
| --- | --- | --- | --- |
| mp | 66 | 37 | 29 |
| cp | 13 | 0 | 13 |
| br | 0 | 0 | 0 |
| other | 14 | 0 | 14 |
| total | 93 | 37 | 56 |

- `br` is empty: no file uses a `br_` prefix. Battle royale maps are `mp_br_*` and fall under `mp` (`mp_br_quarry`, `mp_br_tut`).
- Of the 37 openable mp maps, 25 come from the stock set in `zone/`. The other 12 are outside `zone/` (see Surprises).

## Openable MP maps (37)

Stock set, `zone/` (25):

- Gameplay: `mp_aniyah_tac`, `mp_backlot2`, `mp_donetsk`, `mp_emporium`, `mp_hideout`, `mp_m_fork`, `mp_m_king`, `mp_m_speed`, `mp_m_speedball`, `mp_rust`, `mp_shipment`, `mp_village2`, `mp_br_quarry`
- Frontend menu scenes (7): `mp_frontend`, `mp_frontend1`, `mp_frontend2`, `mp_frontend3`, `mp_frontend6`, `mp_frontend7b`, `mp_frontendc3s5`
- Training/tutorial ranges (5): `mp_bm_tut`, `mp_br_tut`, `mp_t_gun_course`, `mp_t_reflex`, `mp_t_sn_reflex`

Outside `zone/` (12):

- Mod folder `mods\mw120r\maps` (7): `mp_4doffice`, `mp_discovery`, `mp_discovery_8`, `mp_iw7_brkneck`, `mp_mp_nuketowwn` (the folder is `mp_nuketowwn`, the file is `mp_mp_nuketowwn.ff`), `mp_nuketown_c`, `mp_test`
- `.proxy\backups` (3): `mp_nuketown`, `mp_iw8_sample_r`, `mp_iw8r_dev_sho`
- Review build `replay-compiler-frontend-dw-review\zone` (2): `mp_frontend8`, `mp_frontend9`

## xpak-only (no main .ff)

56 rows: 29 mp (for example `mp_aniyah`, `mp_crash2`, `mp_hackney_yard`, `mp_runner`, `mp_piccadilly`, `mp_quarry2`, `mp_raid`, `mp_vacant`, and the `_gw`, `_am`, `_pm` variants), 13 cp (`cp_so_*`, `cp_landlord*`, `cp_smuggler*`, `cp_arms_dealer`, `cp_armsdealer_2`, `cp_dwn_twn_2`), and 14 other (`captive`, `embassy`, `estate`, `highway`, `hometown`, `lab`, `marines`, `piccadilly`, `proxywar`, `safehouse`, `safehouse_finale`, `stpetersburg`, `townhoused`, `tunnels`). Full list in the TSV.

All 56 xpak-only rows have 32 KB packs (the `xpaks are 32 KB header-size files` note, from stat). That is the same size as `mp_shipment.xpak`, which sits beside a 187 MB `.ff`. So the size does not show whether bulk data exists; it is consistent with header-only packs. Not checked with ACTS.

## File naming patterns

- Map id = file stem with a companion prefix (`techsets_`, `ww_`, `eng_`, `srv_`) or suffix (`_tr`, `_cg`, `_load`) removed.
- Per map, the main fastfile is `<id>.ff`. Companions are `srv_<id>.ff`, `techsets_<id>.ff`, and `eng_`/`ww_` variants. Streaming packs are `<id>.xpak` and `techsets_<id>.xpak`.
- Localized companions live in `zone\english\` and `zone\worldwide\` (`eng_`/`ww_` `.ff` and `.xpak`), not in `zone\` itself. The scan includes them in the zone's set.
- Variant maps are separate ids: `mp_aniyah` is xpak-only, `mp_aniyah_tac` has the `.ff`. Same for `mp_spear`/`mp_spear_pm`, `mp_runner`/`mp_runner_pm`, and the `_gw`, `_am` ids.
- `mp_av_*` (1,526 stems: attachments, viewmodels, cosmetics) are excluded as asset packs, not maps.
- `_dw_*`, `dw_*`, `common*`, `global*`, `ui*`, `code_*`, `comms_mp`, `iw8_cw_*`, `mw120r_map_images` are engine, weapon or tool zones and are excluded.

## Surprises

1. Stray copy of `mp_shipment.ff` (187,760,776 bytes) at the install root, next to `zone\`. The catalogue prefers `zone\`, so this one is not picked, but it is counted under notes.
2. `mods\mw120r\maps` is a link to `D:\mw124-conversion-builds\game_maps`. The walk followed it, so those 7 mod maps are on another drive.
3. `replay-compiler-frontend-dw-review\zone` is a near-complete second copy of the zone (six or seven copies per stock map). `mp_frontend8` and `mp_frontend9` exist only there.
4. `.proxy\backups` holds old copies of `mp_nuketown`, `mp_iw8_sample_r` and `mp_iw8r_dev_sho`. The user's sample map `mp_iw8_sample_r` also has a copy under `mods\mw120r\maps\`. The tool picks by path order when there is no `zone\` copy, so its row points to the `.proxy` backup. The tool only reads these files; nothing was written.
5. Every stock campaign/CP zone (`cp_*` and the 14 "other" zones) is xpak-only. No CP or SP zone has a main `.ff` in the install.
6. `mp_m_*` maps (12 ids, e.g. `mp_m_speed`, `mp_m_fork`) are not classified by name. Some are openable and some are xpak-only.

## Method and limits

- Scan: `os.walk` over the install, `os.stat` on `.ff`/`.xpak` names only. No file is opened.
- Source per map: `zone\` first, then the copy with a main `.ff`, then path order. Notes name the source when it is not `zone\`.
- `kind` comes from the prefix. `other` needs a pack named for the zone itself, and must sit in `zone\` and outside the engine families.
- Notes are heuristic (name suffixes like `_tut`, `mp_frontend`, `mp_t_`, and `mp_br_`). They are labels, not verified facts.
- Names only: nothing is verified with ACTS, so "openable" means "a main `.ff` exists". The editor's open path and load success are not tested here.
- `.sabs` and `.fp` files are not listed; only `.ff` and `.xpak` count.
