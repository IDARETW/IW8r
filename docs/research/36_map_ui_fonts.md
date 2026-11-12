# 36. Map zone UI: fonts, LUI, localized strings, images and HUD from GSC

Date: 2026-11-10. Scope: which UI-related asset types a MAP zone (the five `<map>.ff`, `srv_`, `eng_`, `ww_`, `techsets_` zones built by x64-zt `buildextraction`) can carry and that Replay 1.20 uses, and how the editor would author them. Read-only research: Grep/Read of source and data files, no game run, no ACTS run on game files, no builds. The only file written is this report. `docs/PROGRESS.md` was not updated because the task limited writes to this file.

Path shorthands used below:
- `X/` = `E:\mw124\x64-zt\src\` (the converter, IW8 zone code)
- `A/` = `E:\mw124\atian-cod-tools\` (ACTS)
- `R/` = `E:\IW8-Radiant\`

Asset ids are the x64-zt / ACTS ids (`X/games/iw8/zone/asset_streams.hpp` enum, and `A/config/data/mw19/schema.json` pool ids). The parenthesised numbers in `R/docs/research/05_runtime_evidence_1_20.md` are a different numbering and are not used here.

Verification tags: VERIFIED = read in source or data; INFERRED = follows from code read but not run; UNVERIFIED = needs an in-game or runtime check.

## 0. Conclusions

1. Fonts (`ttf`), Lua files (`luafile`) and localized strings (`localize`) are real IW8 asset types with stream, dump and parse code. Only `localize` and `rawfile` have a writer on the map route, and only the editor-driven title reaches the game through a map-written `localize` entry (the MP intro chyron). A map cannot add a font or a Lua widget through the current map converter. Whether Replay reads a map-zone font or Lua file at all is UNVERIFIED.
2. `ui_map` (id 36) cannot be carried by x64-zt at all: it has no parser or stream.
3. The HUD path that is most realistic now is GSC: `newhudelem` plus `settext`/`setshader`/fields, carried in the map script (`scriptfile`, main zone). All of those names are IW8 builtins in the x64-zt table. The gap is the material: x64-zt only writes a UI material for the compass name (`writeUiMaterial`), and the converter only scans `precachemodel`/`setmodel`/`loadfx` for dependent assets.
4. The map UI image that already works in the game is `compass_map_<map>` (image plus material). The editor already writes that image (`R/src/core/compile_iw8.cpp:1795`). The same route can carry a `hud_*` image with a material, which needs an x64-zt change.
5. The title already reaches the game through the intro chyron (`mp_chyron/<map>_<team>_<n>` localize entries in `eng_<map>`), but only if the in-game chyron reads that zone. Owner test needed.
6. Loadscreen art is a package file (`preview.rgba`), not a zone asset. The converter validates and copies it, but whether Replay shows it for x64-zt output is UNVERIFIED.
7. Font names for `hudelem` are not in any source read. Do not author a `font` value until the owner confirms names in game.

## 1. Asset ids (VERIFIED)

| Pool name | id | Enum / table location |
|---|---|---|
| `rawfile` | 51 | `X/games/iw8/zone/asset_streams.hpp:173` (enum), `:51` (list); table `asset_streams.cpp:10854` |
| `stringtable` | 54 | `asset_streams.hpp:176`; table `asset_streams.cpp:10857` |
| `localize` (LocalizeEntry) | 41 | `asset_streams.hpp:45`, `:163`; table `asset_streams.cpp:10844`; `X/games/iw8/iw8_structs.h:45` (`ASSET_TYPE_LOCALIZE = 41`) |
| `ui_map` | 36 | `asset_streams.hpp:158`; table `asset_streams.cpp:10839` (all null) |
| `luafile` (LuaFile) | 62 | `asset_streams.hpp:62`, `:184`; `iw8_structs.h:53` (`ASSET_TYPE_LUAFILE = 62`); table `asset_streams.cpp:10865` |
| `ttf` (TTFDef) | 69 | `asset_streams.hpp:69`, `:191`; table `asset_streams.cpp:10872` |
| `image` | 19 | `asset_streams.hpp:25`, `:141` |
| `material` | 11 | `asset_streams.hpp:17`, `:133` |
| `scriptfile` | 52 | `asset_streams.hpp:52`, `:174` |
| `hudoutline` | 73 | `asset_streams.hpp:73`, `:195` |

Schema cross-check, `A/config/data/mw19/schema.json`, profile `replay-1.20` (starts at line 1264): `luafile` id 62 at 2878-2905 (default asset `ui/default.lua`, status `replay_stream_crosscheck`, x64-zt crosscheck 2746 assets in `code_post_gfx`); `ttf` id 69 at 3067-3090 (default asset `fonts/dev.ttf`, crosscheck 6 assets in `code_post_gfx`); `ui_map` at 2261-2269 (size 0). A default asset is what the schema uses for an empty slot. It is not evidence that the game has that file.

## 2. Summary table

| Asset / feature | Can a map zone carry it (format) | Does the game use it from a map zone | x64-zt support today | Evidence |
|---|---|---|---|---|
| TTF font (`ttf`, 69) | Yes: TTFDef, stream and shared file | UNVERIFIED. Stock has 6 `ttf` assets, all in `code_post_gfx` (the only zone with names listed in the manifest) | Dump and parse: yes. Stream: yes. Map converter writer: NO. Generic `buildzone`: yes (shared file) | `asset_streams.hpp:69`; `asset_files.cpp:276-294,314`; `manifest.json:65` |
| Font names for HUD elements | n/a | UNVERIFIED. No font name list in any source read | n/a | `R/data/generated/iw8_canonical_keys.tsv:396-397`; `iw8_gsc_builtins.txt:1299` |
| Lua file (`luafile`, 62) | Yes: LuaFile, stream and shared file | UNVERIFIED. Stock has 2746 in `code_post_gfx` | Dump, parse, stream: yes. Map converter writer: NO. Generic `buildzone`: yes | `asset_streams.hpp:62`; `asset_files.cpp:265-274,313`; `manifest.json:63` |
| LUI load from script | n/a | Builtin exists (`loadluifile`). Use of map-zone Lua UNVERIFIED | n/a | `iw8_gsc_builtins.txt:562`; `iw8_gsc_builtins.inc:576` |
| UI map (`ui_map`, 36) | NO: no parser, null stream | NO | Cannot carry | `asset_streams.cpp:10839`; `R/docs/research/14_x64zt_verifyzone_coverage.md:150` |
| Localized strings (`localize`, 41) | Yes | Likely for the intro chyron (source comment). Owner test needed | Map converter: chyron entries into `eng_<map>`. Generic: `localizedstrings/<file>.str|json` | `X/games/iw8/assets/replay_rawfile.cpp:46-76`; `X/convert/iw3_iw8/convert.cpp:2080-2101`; `X/games/iw8/build.cpp:159-193` |
| Map title / description | Via `localize` (chyron) and `map.json` | Title in package `map.json`: yes (file format). In-game display: owner test | Yes | `R/src/qtapp/compile_panel.cpp:660-676`; `X/convert/iw3_iw8/convert.cpp:264-280` |
| Raw file (`rawfile`, 51) | Yes | Yes for `vision/<map>.vision` (converter relies on it). Other use not established | Yes, map converter writes `prepared->rawFiles` | `X/games/iw8/assets/replay_rawfile.cpp:13-44`; `convert.cpp:1771-1772`; `iw3_fastfile.cpp:10959-10961` |
| String table (`stringtable`, 54) | Yes | UNVERIFIED for map zones (legacy proxy used `mp/mapInfo.csv`) | Stream yes. A `writer.add(54)` exists (not traced to a map route) | `asset_streams.cpp:10857`; `R/docs/research/05_runtime_evidence_1_20.md:50` |
| Image (`image`, 19) | Yes | Yes for `compass_map_<map>` (stock layout). Other HUD images: via material only | Yes: every image dump in the dump directory is converted | `X/convert/iw3_iw8/convert.cpp:733-745`; `R/data/asset_catalog/iw8_images.tsv` |
| UI material (`material`, 11) | Yes | Compass material: yes (stock layout). `setshader` material: UNVERIFIED | Writer for the compass name only (`writeUiMaterial`) | `X/games/iw8/assets/write_material.cpp:124-137`; `convert.cpp:750-758`; `R/data/asset_catalog/iw8_materials.tsv` |
| Minimap / compass image | Yes | Yes (stock maps carry `compass_map_<id>` in the map's own zone) | Yes | `convert.cpp:731-758`; `R/docs/research/27_minimap.md:12-16,66` |
| Loadscreen / map preview (`preview.rgba`) | Not a zone. Package file | UNVERIFIED for x64-zt output | Yes: optional, validated, copied | `convert.cpp:305-310,610-623,2132-2140`; `iw3_fastfile.cpp:10868-10874`; `iw3_render_assets.cpp:8646-8662` |
| GSC HUD builtins (`newhudelem` family, `settext`, `setvalue`, `setshader`, `changefontscaleovertime`, `clearalltextafterhudelem`) | Via map script (`scriptfile`, main zone) | UNVERIFIED in game. Names are IW8 builtins | Yes: translator passes IW8 builtins through. `setshader` is not dropped | `R/data/generated/iw8_gsc_builtins.txt:596-598,1299,1310,2651,2686,2724`; `X/convert/iw3_iw8/iw3_scripts.cpp:37-39,58-66,499-513` |
| Objective and waypoint icons (`objective_*`, `setwaypoint*`) | Same route as image + material | UNVERIFIED | Builtins exist; no converter path for objective images | `iw8_gsc_builtins.txt:615-671,2749-2753` |
| Hint and popup text (`sethintstring`, `iprintlnbold`) | Literal text: yes. Localized keys: via `localize` (see above) | UNVERIFIED for localized keys | Literal: yes | `iw8_gsc_builtins.txt:470,1898,2518-2519`; `R/src/core/script_check.cpp:313` |
| Hud outline (`hudoutline`, 73) | Format yes | Stock uses it. Authoring not started | No map writer (stream and dump only) | `asset_streams.cpp:8504,10528`; `X/games/iw8/dump.cpp:484-485` |
| Map GSC (`scriptfile`, 52) | Yes | Path exists. HUD behaviour UNVERIFIED | Yes: compiled by gsc-tool into main zone, with a minimap-only fallback | `X/convert/iw3_iw8/convert.cpp:1954-1985`; `R/src/qtapp/compile_panel.cpp:396-398` |

## 3. Fonts (topic 1)

Format and code (VERIFIED):
- The TTFDef layout is name, fileLen, file, ftFace, size 0x20 (`X/games/iw8/structs.hpp:15104`; static asserts `structs.hpp:25573-25577`).
- Stream: `stream_ttfdef` at `X/games/iw8/zone/asset_streams.cpp:8358` (load/write path at `:10506`).
- Shared file: `dump_ttf` and `parse_ttf` at `X/games/iw8/zone/asset_files.cpp:276-294`. Registered as a shared format with empty folder and extension (`asset_files.cpp:314`). The file is stored as the raw font bytes at the asset name path, with an `.iw8` fallback (`asset_files.cpp:325-331`, `:374-383`).
- Neutral form: `read_ttf` / `write_ttf` at `X/games/iw8/neutral/gameplay.cpp:4496-4507`; `ftFace` must be zero (`:4503`). Writer only in the neutral layer, no map caller.
- ACTS treats `ttf` as raw bytes in its linker (`A/src/core/acts/tools/fastfile/linkers/linker_mw19.cpp:47-49`).
- Dump label: `X/games/iw8/dump.cpp:530-531` (kind "font").

Writers and callers:
- The map converter registers only: grading CLUT, rawfile, xanim, sound bank, and other families (`X/convert/iw3_iw8/convert.cpp:1762-1776`). No TTF register call exists. `grep ASSET_TYPE_TTF` in `X/games/iw8/assets` finds nothing.
- A generic `buildzone` can add a font from a shared file, because `zone_source` rows `<pool>,<name>` go through `add_asset` and `type_of` (`X/games/iw8/build.cpp:195-224`, `:34-46`) and the shared file is looked up first (`asset_files.cpp:374-383`). This route is not the editor route.

Stock data:
- `R/data/stock_assets/code_post_gfx/mw19replay/code_post_gfx/manifest.json:65`: `"ttf": 6`. Names are not exported in the data folder (only the count). Stock fonts are therefore in the always-present common zone, not in map zones. Map-zone fonts: UNVERIFIED.
- An image named `ui_font_lock` exists (`R/data/asset_catalog/iw8_images.tsv`). It is an image, not a font.

Font names for HUD elements:
- `font` and `fontscale` are canonical key ids 414 and 415 (`R/data/generated/iw8_canonical_keys.tsv:396-397`), taken from gsc-tool's IW8 token table. They are field names. The setter form and the accepted values are UNVERIFIED.
- `changefontscaleovertime` is a method (`R/data/generated/iw8_gsc_builtins.txt:1299`).
- No font name appears in `X/convert/iw3_iw8/iw8_gsc_builtins.inc`, `X/games/iw8/assets/gsc_tool_iw8_tokens.inc`, `A/` sources, or `R/` docs/data (searched for `bigfixed`, `smallfixed`, `.ttf`, `fonts/`). Names must come from the owner's in-game test or a stock script dump.

Verdict: a map-carried TTF has the format and the generic build route, but no map writer and no evidence that Replay reads it. Not for the first iteration.

## 4. LUI and Lua files (topic 2)

Format and code (VERIFIED):
- LuaFile is 24 bytes (`X/games/iw8/zone/asset_streams.cpp:10865`; static assert `:10978`). Stream `stream_luafile` at `:7050`; asset path `:10462-10464`.
- Shared file: `parse_luafile` at `X/games/iw8/zone/asset_files.cpp:265-274` (sets `strippingType = 1`), `dump_luafile` before it (`:256-263`). Registered at `:313`.
- Dump label: `X/games/iw8/dump.cpp:536`.
- ACTS linker: raw bytes for `luafile` (`linker_mw19.cpp:47-49`).

Stock data:
- 2746 `luafile` assets in `code_post_gfx` (`manifest.json:63`). Names are not exported in the data folder.
- Schema default asset `ui/default.lua` (`schema.json:2881-2895`). This is a slot default, not proof.

Game side:
- LUI entry from script: `loadluifile` (`R/data/generated/iw8_gsc_builtins.txt:562`), `getindexforluincstring` (`:322`).
- The engine symbol `LUI_RegisterFunction` exists and is unhooked (`R/docs/research/05_runtime_evidence_1_20.md:253`). Map use of Lua is UNVERIFIED.

Writers and callers:
- No `LuaFile` register exists on the map route. Only the generic `buildzone` path can carry it (see section 3, same mechanism).

Risks (not verified, flag for owner):
- Replay may restrict client Lua to stock files. No source read shows a scanner over zone contents. The binary has an `AnticheatScanTable` symbol (`05_runtime_evidence_1_20.md:257`), but its role is unknown and is not a map entry point.
- A custom `luafile` that reuses a stock name could replace menu code. Effect UNVERIFIED.

Verdict: a map-shipped Lua widget is not supported by the editor or the map converter. Out of scope until the owner tests whether Replay loads a map-zone Lua file.

`ui_map` (id 36): `asset_streams.cpp:10839` has all-null functions, `R/docs/research/14_x64zt_verifyzone_coverage.md:150` says "none (null in format table, no parser)". It cannot be carried.

## 5. Localized strings and the title path (topic 3)

Writer (VERIFIED):
- `iw8::localize::Register(writer, name, value)` writes a LocalizeEntry: a 0x10 header (name and value pointers), name and value strings in the virtual block, and a 0x20 postload reservation (`X/games/iw8/assets/replay_rawfile.cpp:46-76`; header `replay_rawfile.h:14-20`). Lowercase keys, e.g. `mp_chyron/mp_shipment_axis_1` (header comment).
- Only caller: `X/convert/iw3_iw8/convert.cpp:2097`.

Title to game (VERIFIED chain):
1. The editor's Map Info dialog writes `iw8r_title` (and description, compass, minimap scale, ladder, exposure) into worldspawn (`R/src/qtapp/compile_panel.cpp:655-735`).
2. `CompilePanel::metadataJson` writes `title` into `metadata.json`, falling back to the map id (`compile_panel.cpp:660-676`, title at `:672-673`). Passed to the converter with `--metadata` (`compile_panel.cpp:389-391`).
3. `readString("title")` (`X/convert/iw3_iw8/convert.cpp:194`) and `writeMetadata` put it in the package `map.json` (`convert.cpp:264-280`).
4. The chyron block (`convert.cpp:2080-2101`) builds four localize entries per team, `mp_chyron/<map>_<allies|axis>_<1..4>`: "Operation: <title>" (with " (Classic)" and " (IW7)" stripped), the clock, "Classified", and the unit name. The entries go to the `english` zone, `eng_<map>`.
5. Clock: from the IW3 sun elevation (`X/convert/iw3_iw8/iw3_fastfile.cpp:10966-10984`). The default style is `iw3` (`iw3_fastfile.h:48`), and the editor does not pass `--style` (`R/src/qtapp/compile_panel.cpp:384-402`), so the clock is generated for editor builds.
6. Units are fixed text: "SAS - Detachment Delta" and "12th Spetsnaz Brigade" (`iw3_fastfile.cpp:11183-11186`). The editor cannot change them.

Conflict to check in game:
- `convert.cpp:2082` says stock keeps every shipped map's chyron in `eng_code_post_gfx`. But the stock `code_post_gfx` manifest has no `localize` entry (`manifest.json` loaded_by_type list), and `R/docs/research/14_x64zt_verifyzone_coverage.md:155` lists localize only in `eng_iw8_cw_*` zones. Which zone the game reads for the chyron is UNVERIFIED.

Zone rule (VERIFIED): `eng_` may hold only LOCALIZE and IMAGE; `ww_` only IMAGE (`convert.cpp:668-679`). Custom strings belong in `eng_`.

Generic route: `localize,<file>` rows read `localizedstrings/<file>.str` or `.json` (`X/games/iw8/build.cpp:159-193`, `:206-210`). The editor does not write these files.

Hint and popup text: `sethintstring` and `sethintstringparams` (`iw8_gsc_builtins.txt:2518-2519`) take a localized key; the key format is UNVERIFIED. `iprintlnbold` takes literal text (`:470`, `:1898`), and the editor already has a snippet using it (`R/src/core/script_check.cpp:313`).

Verdict: the title already reaches the game through the chyron, if the chyron is read from `eng_<map>`. Custom hint strings are feasible through `iw8::localize::Register` with an editor-supplied list; that needs a converter hook (section 8).

## 6. 2D images, materials, loadscreen and minimap (topic 4)

Compass (VERIFIED code, stock layout evidence):
- `compassName = "compass_map_" + map` (`X/convert/iw3_iw8/convert.cpp:731`).
- Every image dump is converted (`convert.cpp:733-745`); an image whose name equals `compassName` gets the map's rotation and sets `hasCompass`.
- Material: `writeCompassMaterial(techsets, compassName)` (`convert.cpp:750`), which calls `writeUiMaterial(zw, name, name)` (`X/games/iw8/assets/write_material.cpp:124-127`). The material goes to the techset zone; the image goes to the main zone.
- `writeUiMaterial` (`write_material.cpp:129`) writes a 2D UI material with the stock techset name `cust_2_2d_rpl_ta0_0_0_0_0_0` (`write_material.cpp:137`). The stock material `black` uses the same techset (`R/data/stock_assets/code_post_gfx/mw19replay/code_post_gfx/assets/material/black.318.asset.json`, field `techniqueSet`). So a UI material needs only an image and a material; the techset is a stock name.
- Stock layout: `compass_map_mp_shipment` is an image in `mp_shipment` (`R/data/asset_catalog/iw8_images.tsv`) and a material in `techsets_mp_shipment` (`R/data/asset_catalog/iw8_materials.tsv`). This matches the validator rule below.
- Editor writes the image: `R/src/core/compile_iw8.cpp:1795` writes `extracted/images/compass_map_<id>.dds`.
- Runtime: `setminimap("compass_map_<m>", ...)` (`R/docs/research/27_minimap.md:66`, from the doc; not re-read in source here).

Validator rules (VERIFIED, `X/convert/iw3_iw8/convert.cpp:630-660`):
- Map render definitions (MATERIAL, TECHSET, VERTEXSHADER, PIXELSHADER) must be in the techset zone; the main zone must not hold TECHSET, VERTEXSHADER or PIXELSHADER.
- Main-zone material count must equal the techset-zone material count.
- Images live in the main zone; each image has one definition across the main and techset zones (`convert.cpp:656-660`).

Loadscreen and preview (VERIFIED code):
- `preview.rgba` is an optional package file in MWRI format, 1..2048 per side (`X/convert/iw3_iw8/convert.cpp:305-310`). Validated in the package (`:610-623`) and copied from the dump directory (`:2132-2140`).
- Source art on the IW3 route: `images/loadscreen_<map>` (`X/convert/iw3_iw8/iw3_render_assets.cpp:8646-8662`), written as `preview.rgba` (`iw3_fastfile.cpp:10868-10874`). Classified as "becomes the map preview / loading image" (`iw3_fastfile.cpp:1989-1990`).
- The IW7 importer comment says the game shows this image while the map loads (`X/convert/iw7_iw8/export_materials.cpp:2789-2791`). That is IW7, not verified for IW8.
- The editor writes no loadscreen image and no `preview.rgba` (no hits in `R/src`). Owner's roadmap: 1.10 done, 1.17 partial (`R/docs/ROADMAP.md:101,108`). `R/docs/research/06_test_assets.md:133` shows a `preview.rgba` in an earlier package.
- Game use for x64-zt output: UNVERIFIED. The legacy proxy doc says whether it is accepted is unknown (`R/docs/research/03_build_pipeline_interface.md:389`, U13).

Stock HUD images and materials that exist in common zones (possible without new assets):
- Materials: `hud_icon_c4` and `compass_icon_objpoint_atk_lb` in `common_base_mp`; `hud_helmet` in `techsets_common` (`R/data/asset_catalog/iw8_materials.tsv`).
- Images: `hud_bomb` in `common_base_mp`; `ui_font_lock` in `code_post_gfx` (`R/data/asset_catalog/iw8_images.tsv`).
- Whether `common_base_mp` is resident for a custom map: UNVERIFIED.

## 7. HUD elements from GSC (topic 5)

Builtins (VERIFIED names in the generated table; source is `X/convert/iw3_iw8/iw8_gsc_builtins.inc`, from `R/tools/build_gsc_builtins.py`):
- Functions: `newhudelem` (`R/data/generated/iw8_gsc_builtins.txt:597`), `newclienthudelem` (`:596`), `newteamhudelem` (`:598`), `loadluifile` (`:562`), `setminimap` (`:841`), `iprintlnbold` (`:470`), `precacheshader` (`:750`).
- Methods: `settext` (`:2686`), `setvalue` (`:2724`), `setshader` (`:2651`), `changefontscaleovertime` (`:1299`), `clearalltextafterhudelem` (`:1310`), `sethintstring` (`:2518`), `sethintstringparams` (`:2519`), `setpulsefx` (`:2631`), `setwaypoint` family (`:2749-2753`), `iprintlnbold` (`:1898`), `objective_*` (`:615-671`).
- No `setfont` or `setscale` method is in the table. Font and scale are therefore field assignments or other names (UNVERIFIED).

Translator (VERIFIED, `X/convert/iw3_iw8/iw3_scripts.cpp`):
- `IsBuiltin` checks the IW8 function and method tables (`:37-39`). IW8 builtins pass through (`:509-513`).
- `DroppedBuiltins` replaces `precacheshader`, `precachestring`, `precacheitem`, and others with `iw3compat_noop` (`:58-66`, `precacheshader` at `:63`; use at `:504`).
- Consequence: `setshader` works through the translator, but a `precacheshader` call in an IW3-style script does nothing.

Dependencies (VERIFIED gap):
- The converter scans the translated script only for `precachemodel`/`setmodel` (model names) and `loadfx` (effects) (`X/convert/iw3_iw8/convert.cpp:1939-1945`). A material named only in `setshader` is not pulled into the zone. The converter must emit it explicitly.

Placement (VERIFIED):
- The map script becomes a `scriptfile` named `scripts/mp/maps/<map>/<map>.gsc` in the main zone (`convert.cpp:1956-1961`), compiled with gsc-tool (`iw3::scripts::Compile`, `:1954`). Fallback: minimap-only startup script (`:1975-1985`).
- Editor generator: `R/src/core/map_script.cpp:84-140` emits `main()`, `maps\mp\_load::main()`, fog, fx and sound placement, the `user_main()` hook from worldspawn `iw8r_script`, and hot update.
- Snippet library: `R/src/core/script_check.cpp` (snippets around line 300-320; `trigger_message` uses `iprintlnbold`). This is where HUD snippets would go.

Verdict: this is the most practical HUD path. An editor snippet using `newhudelem` + `settext` + `setshader` with a stock material (e.g. `hud_icon_c4`) needs no x64-zt change, but in-game rendering is UNVERIFIED. A custom image needs the converter change in section 8.

## 8. Recommendation

### 8.1 The editor can offer now (no x64-zt change)

1. Map title and description: already in Compile > Map Info. Owner to confirm the title shows in the intro chyron (`eng_<map>`).
2. A HUD snippet in the script snippet library (`R/src/core/script_check.cpp`): `newhudelem`, `settext` / `setvalue`, `setshader` with a stock material from `R/data/asset_catalog/iw8_materials.tsv`, placed through the existing user-script hook (`map_script.cpp`). Leave `font` unset until names are verified. Owner test: does the element draw on a custom map.
3. Compass image: already written by the editor (`compile_iw8.cpp:1795`). Nothing to add.

### 8.2 Needs an x64-zt change

1. Custom HUD image plus material. Images in the dump directory already become main-zone images (`convert.cpp:733-745`), but a `hud_*` image gets no material. Add an optional list (from the editor, or a name prefix) and call `writeUiMaterial(techsets, name, name)` (`X/games/iw8/assets/write_material.cpp:129`) plus `RegisterMaterialReference` (as in `convert.cpp:750-758`). Also extend the scan at `convert.cpp:1939-1945` so `setshader("...")` names are gathered. Files: `X/convert/iw3_iw8/convert.cpp` (`addMapAssets` at `:708`, compass block `:731-758`), `X/games/iw8/assets/write_material.cpp`.
2. Custom localized strings (hint text, objective labels). Add an optional table from the editor and register entries with `iw8::localize::Register` (`X/games/iw8/assets/replay_rawfile.cpp:53`) in the `english` writer next to the chyron block (`X/convert/iw3_iw8/convert.cpp:2079-2101`). Keep key names in the stock style (`mp_chyron/...`).
3. Loadscreen: the cheapest test is to place a valid MWRI `preview.rgba` in the folder the build copies from (`convert.cpp:2132-2136`), validated by `validPreview` (`convert.cpp:307-310`). An editor-native loadscreen DDS would need `BuildLoadscreenPreview` (`iw3_render_assets.cpp:8646`) to accept a non-IW3 source. Owner test first.
4. Custom TTF or Lua file: needs a map-route writer. TTF: a `from_neutral` / native path for TTFDef exists only as read (`X/games/iw8/neutral/gameplay.cpp:4507` is write_ttf in the neutral layer, not a map caller). LuaFile: stream exists, no writer. Zone placement: not `eng_` or `ww_` (`convert.cpp:675-678`). Defer until in-game tests show Replay uses a map-zone font or Lua file.

### 8.3 Out of scope, or owner in-game test

- `ui_map` (id 36): no parser in x64-zt. Out of scope.
- Custom Lua widgets: anti-tamper and load behaviour unknown. Owner decision.
- Font names: the owner should confirm valid `hudelem` font names in game (or from a stock script dump run by the owner).
- Test list for the owner: (a) title in the intro chyron; (b) `newhudelem` + `setshader("hud_icon_c4")` on a custom map; (c) custom `hud_*` image with the change in 8.2.1, once made; (d) `preview.rgba` on a custom map select; (e) a map-zone TTF or Lua file, only if the owner wants to test it.

## 9. Unverified (consolidated)

- Game uses a map-zone `ttf` or `luafile` (section 3, 4).
- Font value names and the font setter form for `hudelem` (section 3).
- Map-zone `localize` is read for the chyron, and from which `eng_` zone (section 5).
- `setshader` material from a map zone resolves by name without precache (section 6, 7).
- Stock HUD materials are resident for custom maps (section 6).
- Objective icon argument types (section 2).
- `preview.rgba` is shown for x64-zt packages (section 6).
- Whether `hudoutline` or `stringtable` from a map zone is used (section 2).

## 10. Source conflicts noted

- Chyron zone: `convert.cpp:2082` (stock in `eng_code_post_gfx`) vs. the coverage table (`14_x64zt_verifyzone_coverage.md:155`) and the manifest (no `localize` in `code_post_gfx`).
- ACTS support: `R/docs/research/04_iw8_asset_access.md:72` says the ACTS fastfile linker writes "only 8 pool types". The code read (`linker_mw19.cpp:47-80`) takes any pool by name through the schema profile, and only special-cases `rawfile`, `luafile`, `ttf` and `scriptfile`. The schema profile decides the rest. Not re-checked against the full profile.
- The legacy validator (`R/docs/research/03_build_pipeline_interface.md:326`, `eng_` only LOCALIZE and IMAGE) agrees with `X/convert/iw3_iw8/convert.cpp:675-678`.
