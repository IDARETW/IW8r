# Release package check (roadmap 12.11)

Checks that `tools/package_release.ps1` ships every data file or folder the editor loads at run time.
Package folder: `out\package\IW8RadiantSuite` (zip beside it, `IW8RadiantSuite-20261111.zip`).
Size: folder 73.9 MB, zip 25.6 MB. Editor data under `data\` in the package: 52 MB.

## How the editor finds data

Almost every lookup walks up from `QCoreApplication::applicationDirPath()` (up to 3 or 4 folders) until it finds
the file, so the same code works in `bin\release` (repo `data\` is two folders up) and in the package (`data\` beside
the exe). The core helper `material_tints.cpp` walks up from the same start folder, and the CLI `iw8r.exe` walks up
from its own path to find `data\generated\iw8_canonical_keys.tsv`.

## Table

"In package" is the result of the package build after the change in this check (`yes` = copied before and after,
`added` = copied only after this change, `no` = not copied and not needed, with the reason).

| Data file or folder | Loaded by (source, first line) | How it is found | In package |
|---|---|---|---|
| `data/web/` (index.html, app.js, view3d.js, style.css) | `src/qtapp/web_server.cpp:60` | walk up, `data/web/index.html` | yes |
| `data/asset_catalog/` (all `iw8_*.tsv`, incl. `*_status.tsv`) | `asset_browser.cpp:159,383,401`; `asset_audit.cpp:28`; `material_cache.cpp:189,320`; `model_cache.cpp:291`; `stock_sky.cpp:26`; `main_window.cpp:2752,6006`; `web_server.cpp:74`; `find_dialog.cpp:332,372`; `compile_panel.cpp:84`; `asset_info.cpp:43`; `src/core/build_preflight.cpp:412`; `src/core/material_tints.cpp:54` | walk up / `repoData()` | yes (22 MB) |
| `data/asset_catalog/iw8_material_tints.tsv` | `src/core/material_tints.cpp:54` | walk up from exe | yes (inside asset_catalog) |
| `data/asset_catalog/iw8_streamed_images.tsv` | `asset_info.cpp:45`; `compile_panel.cpp:86` | walk up | yes (inside asset_catalog) |
| `data/generated/iw8_canonical_keys.tsv` | `src/qtapp/main.cpp:232` (walk up 3); `src/cli/main.cpp:501` | walk up | yes |
| `data/generated/iw8_gsc_builtins.txt` | `compile_panel.cpp:816`; `script_panel.cpp:155` | walk up 4 | yes |
| `data/generated/entity_reference.html` | `main_window.cpp:3452` (Help > entity reference) | walk up 4 | yes |
| `data/iw8_entity_defs.json` | `main_window.cpp:440` (Check Map); `panels.cpp:73` | walk up 3 | yes |
| `data/iw8_entity_classes.json` | `panels.cpp:53`; `brush_tools.cpp:402` | walk up 3 | added |
| `data/shaders/` (`toon.iw8shader.hlsl`, `stock.iw8shader.hlsl`) | `main_window.cpp` (Shader Editor templates, map shader lookup); `compile_panel.cpp` (build) | walk up 3, `data/shaders` | added 2026-11-14 (was missing: the installed editor had no toon shader) |
| `data/hotkey_presets.json` | `main_window.cpp:878` (hotkey presets, on start) | walk up 4 | added |
| `data/prefabs/` (stock + iw8r) | `prefab_browser.cpp:314` | beside exe | yes (17 MB) |
| `data/ies/` (.ies profiles, README) | `ies_browser.cpp:84` | beside exe | yes |
| `data/fx/iw8r_soft_particle.png` (default soft sprite for compile) | `compile_panel.cpp:786-789` | walk up 4 | added |
| `data/stock_assets/code_post_gfx/` (ACTS thumbnails and colour index, 12 MB, 708 files) | `material_cache.cpp:51-55` (`repoDataDir`); `model_cache.cpp:46`; `asset_browser.cpp:465` | walk up 4 for `data/stock_assets` | added |
| `docs/USER_GUIDE.md`, `TUTORIAL.md`, `ENTITY_REFERENCE.md`, `AGENT_MAP_GUIDE.md`, `TEST_CHECKLIST.md`, `FEATURE_MATRIX.md` | `help_panel.cpp:19,31` (search); `main_window.cpp:3385,3466` | walk up 3 to 4 | yes (in `docs\`) |
| `docs/ROADMAP.md` | listed in package script, not loaded at run time | n/a | yes (kept as before) |
| `tutorial\iw8r_tutorial.map` and `tutorial\prefabs\` | docs/TUTORIAL.md (opened by the user) | n/a | yes |
| `sample_room.map` | tests and first-run sample (`tests\data\iw8_sample_room.map`) | n/a | yes |
| `licenses\` (Qt, QtADS, stb notices) | legal | n/a | yes |
| `data/web_tls/cert.pem`, `key.pem` (HTTPS for Tools > Web Server, only when HTTPS is on) | `main_window.cpp:3953-3956`; the checkbox text at line 3169 | beside exe | no: a private key is not shipped; the user makes one with `tools/make_web_cert.ps1` |
| `data/stock_dumps/` (5.6 MB) | no run-time reference in `src/` | n/a | no: not loaded |
| `data/iw8_entity_defs_overrides.json` | read only by the generator (`tools/gen_iw8_entity_defs.py`) | n/a | no: not loaded |
| Thumbnail packs (`%LOCALAPPDATA%\IW8 Radiant Suite\IW8 Radiant Suite\thumbs\*.pack`) | `main_window.cpp:348-355`, written by the editor at run time | per user | no: created on first use; sizes on this machine: `materials.full.pack` 71 MB, `models.full.pack` 13 MB, `materials.pack` 0.3 MB, `models.pack` 0.4 MB, `images.pack` under 0.1 MB. None is over 200 MB. The full packs are built from game assets, so they are not shipped |
| `tools\*.ps1` (`m2_rebuild_mapents.ps1`, ACTS/zonetool wrappers) | `main_window.cpp:4110,6123` (Relight / rebuild stock entities) | walk up 4 for `tools/` | no: developer features that need ACTS and game files |
| `IW8Radiant.ini`, `IW8Radiant_layout.ini`, `docs\test_results.json` | written by the editor | beside exe | no: user state, created at run time |

## Changes to `tools/package_release.ps1`

Two lines only, in the same style as the existing copies:

- The folder copy list gained `stock_assets` and `fx`.
- The single-file copy list gained `iw8_entity_classes.json` and `hotkey_presets.json`.

Nothing else in the script changed. `build_installer.ps1` was not changed.

## Run and verification

- Lock: taken once `out\build.lock` was free (another helper held it for about three minutes), released after the run.
- The script only deletes and rewrites `out\package\IW8RadiantSuite` and its zip. It does not rebuild the editor, so
  it was run as is. The exe copied is the existing `bin\release\IW8Radiant.exe` (same timestamp, 2026-11-11); the
  editor was not rebuilt or started for this check.
- Each needed file was checked at its package path (see the table): all present. The three absent items are the ones
  marked "no" above.

## Decisions (settled 2026-11-14 with these defaults; say so if you want another)

1. HTTPS: the package ships no certificate and HTTPS is off by default. A certificate is made on the machine that
   needs one with `tools/make_web_cert.ps1` (Tools > Web Server Settings names the files). The installer makes none:
   a key made at install time would sit in a folder every later update overwrites.
2. Thumbnail packs: not shipped. They are pictures of game assets (84 MB for the two full packs), and the package
   holds nothing made from game files. The editor makes them on the machine (Tools > Thumbnail Packs).
3. `docs\test_results.json` and `IW8Radiant.ini` are written beside the exe. The installer therefore installs for
   the current user only (`%LOCALAPPDATA%\Programs\IW8 Radiant Suite`, no administrator rights); the "all users"
   choice, which would put the suite under Program Files where those writes fail, is no longer offered
   (`tools/installer/iw8radiant.iss`).
4. ACTS-dependent features (relight, stock entity rebuild, thumbnail packs from ACTS) only work where ACTS and the
   game files are installed. The package does not include them, which is intended.
