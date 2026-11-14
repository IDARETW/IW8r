# Build pipeline interface: CoD4 Radiant .map to Replay mp_ package

**Legacy mw120rproxy pipeline.** This documents the mw120rproxy / iw8-zonetool route only. The latest IW3/IW7 to IW8 conversion lives in `E:\mw124\x64-zt` and is documented separately; it is out of scope here. The CoD4 compile steps (cod4map, cod4rad, linker_pc) below still apply to the `.map` route.

Date: 2026-11-08. Scope: document the existing pipeline so the editor's Build button can drive it. Read-only research; no builds, deployments, or game runs were performed.

## Provenance and legend

Verified means read in source or observed by running help/usage. Inferred means derived from code but not executed. UNKNOWN means not established.

Path shorthands used below (all absolute):

- `[ZT]` = `E:\mw124\mw120rproxy\iw8-zonetool\src\zonetool\`
- `[TL]` = `E:\mw124\mw120rproxy\mw120rproxy\tools\`
- `[PX]` = `E:\mw124\mw120rproxy\mw120rproxy\`
- `[SIB]` = `E:\mw124\iw8-zonetool\` (sibling tree, outside the named source list; see Key findings)
- `[RP]` = `E:\mw124\mw120rproxy\` (repo root)
- `[RPL]` = `E:\IW8\Builds\1.20-replay\` (Replay install)

Binaries executed (help and usage only):

- `E:\mw124\mw120rproxy\iw8-zonetool\xmake-out\x64\Release\iw8-zonetool.exe` (the named compiler): no arguments (exit 2), `--help` (exit 0), `help` (unknown command, usage, exit 2), `fromdump --help` (unknown command, exit 2), `validate-output --help` (usage, exit 2). Captured text: `%USERPROFILE%\AppData\Local\Temp\claude\40643af5ee92b5b9\f955f107-10f4-450a-a1df-e62aedc1d809\scratchpad\help.txt`.
- `E:\mw124\iw8-zonetool\xmake-out\x64\Release\iw8-zonetool.exe` (sibling, disclosed): `--help` and `fromdump --help` only. This binary is the one `build_mp_test.py` actually calls; I ran it outside the explicitly named binary, for help text only.

## Key findings (read first)

1. **The Python pipeline does not call the compiler you named.** `build_mp_test.py` sets `converter = REPO.parent / "iw8-zonetool/xmake-out/x64/Release/iw8-zonetool.exe"` ([TL]build_mp_test.py:142), which is `[SIB]`, not `[RP]\iw8-zonetool`. `[SIB]` is a different program: its usage line is "offline multi-engine -> IW8 Replay converter" with commands `import`, `fromdump`, `validate-package`. The Python calls `fromdump` at build_mp_test.py:475-491 and `validate-package` at :510. The named binary has no `fromdump` (unknown command, exit 2, verified).
2. **The named compiler's documented route, `build-map`, is not used by `build_mp_test.py`.** The named binary's `build-map` is the prepared-dump route in [RP]iw8-zonetool\docs\USAGE.md. The Python instead produces a `dump/` tree and hands it to `[SIB]`'s `fromdump`.
3. **Defaults are broken in this checkout.** `[RP]custom_map_sources\` and `[RP]external\` do not exist (verified). `local_paths.py` points COD4, Replay, and Unlinker into `external\` unless env vars `MW120R_COD4`, `MW120R_GAME`, `MW120R_UNLINKER` are set; none are set in my shell (verified). `build_mp_test.ps1` passes neither `--cod4` nor `--replay`, so a run from the .ps1 fails at build_mp_test.py:143-147 with FileNotFoundError on the first missing file (`external\CoD4\bin\cod4map.exe`, then the Replay exe). A working run needs `--cod4 "E:\CoD4\game_build\Call of Duty 4 Modern Warfare" --replay "E:\IW8\Builds\1.20-replay\game_dx12_ship_replay.exe"`, or the env vars.
4. **Package contract mismatch (inferred, not run).** `deploy_custom_map.ps1` runs the named binary's `validate-output` on the whole package directory ([TL]deploy_custom_map.ps1, validate call near the top). That validator requires exactly five `.ff` files plus `map.json` (and optional `preview.rgba`) ([ZT]main.cpp:775-778). `build_mp_test.py` leaves `manifest.json`, `glass.bin`, `collision.bin`, `footsteps.bin` in `out\package` ([TL]build_mp_test.py:459-505; footstep_data.py:62). Deployment would therefore refuse the package before copying. Whether `[SIB]` `fromdump` also writes extra files is UNKNOWN.
5. **Zone layout conflict (inferred from code).** The named validator requires `srv_<map>.ff` to contain zero assets ([ZT]main.cpp:798) and the main zone to hold the map ents, col map, com map ([ZT]main.cpp:793-797). `[SIB]` comments say its `srv_` zone holds `map_ents`, `col_map`, `com_map` ([SIB]src\main.cpp:333). The deployed `mp_test` sample has a 172-byte `srv_mp_test.ff` (verified size), which matches the named validator's expectation, not the `[SIB]` comment.
6. **Hard-coded paths** are listed in section 2 (table "Hard-coded paths"). Several point at `E:\CoD4`, `E:\mw124\iw8-zonetool`, and `E:\mw124\atian-cod-tools`, outside the repo.
7. **Decision for the editor.** Two inputs exist: a Radiant `.map` (the `build_mp_test.py` route, the only one that starts from a `.map`) and a CoD4-compiled `.ff` (the `build-iw3` route in [RP]RADIANT_TO_REPLAY.md and [RP]iw8-zonetool\docs\IW3_FASTFILE.md). The Build button must choose one. The `.map` route depends on the CoD4 tools and `[SIB]`.

---

## 1. iw8-zonetool subcommands (named binary unless marked)

### 1.1 Global parsing and exit codes (verified)

- `main` at [ZT]main.cpp:2891. Argument struct `Args` at [ZT]main.cpp:72. `printUsage` at [ZT]main.cpp:122-212. `parseArgs` at [ZT]main.cpp:214-362.
- `parseArgs` treats any token it does not recognise as a positional. Unknown flags therefore shift positionals and produce usage errors, not parse errors.
- Options consumed by `parseArgs` (all take one value unless noted): `-o`, `--replay`, `--oodle`, `--source-exe`, `--patch`/`--fp`, `--report`, `--collision-preview`, `--walk-*`, `--collision`, `--footsteps`, `--unlinker`, `--iw3-root` (repeatable), `--search-path` (repeatable), `--metadata`, `--lighting-profile`, `--sun-intensity-scale` (positive finite float, else usage), `--gsc-tool`, `--acts`, `--stock-cache`, `--style iw3|iw8` (else usage), `--iw3-mode-assets on|off`, `--no-navigation` (no value), `-v`, `-q`.
- Top-level `--help`, `-h`, `-?` print usage and exit 0 ([ZT]main.cpp:2920-2924).
- Any subcommand given `--help` is treated as a positional. Each handler then prints usage and exits 2 (verified for `validate-output --help`). There is no per-subcommand help.
- No arguments: usage, exit 2 (verified).
- Unknown command: `unknown command '<x>'`, usage, exit 2 ([ZT]main.cpp:3286-3288; verified for `help` and `fromdump`).
- Uncaught C++ exception: `conversion failed: <what>`, exit 1 ([ZT]main.cpp:3290-3293).
- Handler convention: 2 for argument shape or map-id errors that print usage; 1 for conversion or validation failure; 0 for success.

### 1.2 Subcommand table

Dispatch line is the `if (args.command == ...)` in `main`. Handler is the function it calls.

| Subcommand | Arguments | Required inputs | Output | Exit behaviour | Parser |
|---|---|---|---|---|---|
| `build-map <dump> <map> [-o <out>] [opts]` | 2 positionals | `<dump>/maps/mp/<map>.d3dbsp.*` (hasMapFiles, [ZT]main.cpp:2013-2016); collision as `<dump>/maps/mp/<map>.d3dbsp.havok` or `--collision` + `--replay`; entities optional | Five `.ff` files + `map.json` in `-o` (default `<dump>\<map>_out`, [ZT]main.cpp:2008). Output is validated in a private sibling stage, then published; a prior output is preserved ([ZT]main.cpp:2165-2267) | 2: usage, bad map id ([ZT]main.cpp:1981), metadata id mismatch (:1999). 1: missing map files, collision missing, conversion or publish failure. 0: published | dispatch [ZT]main.cpp:2925-2927; handler `buildMap` :1971 |
| `build-iw3 <map.ff> [map] [-o <out>] --replay <exe> [opts]` | 1-2 positionals | `--replay` required (else usage, [ZT]main.cpp:2273-2275); `<map>.ff`; sibling `<map>_load.ff` read if present; CoD4 `main`/`raw` via `--iw3-root` or `IW3_GAME_ROOT`/`COD4_ROOT`; `--search-path`; Unlinker (`--unlinker`, or discovered beside the binary, under `tools`, on PATH, or `IW8_ZONETOOL_UNLINKER`, per [RP]iw8-zonetool\docs\IW3_FASTFILE.md) | Five `.ff` + `map.json` in `-o` (default `<fastfile dir>\<map>_iw8`, [ZT]main.cpp:2325, :2376). Ledger `<out>.iw3-assets.tsv` ([ZT]main.cpp:2356). Temp extraction in a private directory beside the output | As build-map. Map id: `metadata.id` overrides the argument ([ZT]main.cpp:2281-2293) | dispatch [ZT]main.cpp:2938; handler `buildIw3` :2271 |
| `build-map-images -o <out> [zone=<name>] <map id>=<preview.rgba>...` | `-o` required; pairs | `preview.rgba` files that must be valid MWRI images ([ZT]main.cpp:2772) | UNKNOWN exact filenames (not traced) | 2: bad pair or missing `-o` ([ZT]main.cpp:2763, :2772) | dispatch [ZT]main.cpp:2942; handler `buildMapImages` :2721 |
| `validate-output <map_output> <map>` (alias `validate-package`) | 2 positionals | Output directory | None (stdout log) | 2: fewer than two positionals. 1: any structural check fails. 0: passes. Checks in section 4 | dispatch [ZT]main.cpp:3282-3285; handler `validate` :2536 -> `validatePackage` :733 |
| `build-stock -o <out> --replay <exe> --acts <acts.exe>` | all three required | Replay exe, ACTS exe | Diagnostic zones holding only the stock copy (UNKNOWN names) | 2 if any missing ([ZT]main.cpp:2862-2866) | dispatch [ZT]main.cpp:2946; `buildStock` :2860 |
| `translate-iw3-script <maps/mp dir> <map> -o <out> [--gsc-tool <exe>]` | exactly 2 positionals and `-o` | `*.gsc` in the directory | `<out>\<map>.gsc` with appended `iw3compat_minimap()` stub; optional compile via `--gsc-tool` ([ZT]main.cpp:2381-2424) | 2 on shape. 1 on read or write failure | dispatch [ZT]main.cpp:2929; `translateIw3Script` :2381 |
| `build-weapon <weapon.json> [-o <out>]` | 1 positional | Weapon JSON | UNKNOWN (handler `iw8::buildWeapon`, not traced) | 2 on shape; 1 on failure | dispatch [ZT]main.cpp:2933-2937 |
| `build-weapon-placeholders <manifest.json> [-o <out>]` | 1 positional | Manifest | UNKNOWN (`iw8::buildWeaponPlaceholders`) | 2 with stderr usage ([ZT]main.cpp:2896-2909) | dispatch [ZT]main.cpp:2896 |
| `build-scene-assets <a> <b>` | 2 args (`argc == 4`), undocumented in usage | UNKNOWN | UNKNOWN | UNKNOWN | dispatch [ZT]main.cpp:2912 |
| `sim <map.sim.json> [render\|pick\|info\|panorama\|serve\|check\|flicker\|rotation]`, `sim <mw19-walk.sim.json> [serve\|walk-check\|walk-replay]` | subcommand list | D3D11 environment (sim_render.cpp) | UNKNOWN (renderer, not traced) | UNKNOWN | dispatch [ZT]main.cpp:2910; usage [ZT]sim\sim_main.cpp:1223 |
| `inspect <file.ff>`, `inspect-iw7 <file.ff>`, `inspect-iw8 <file.ff>` | 1 positional | Fastfile | Stdout dump (not traced) | UNKNOWN | dispatch [ZT]main.cpp:2950-2957; handlers :2425, :2435, :2480 |
| `decompress-iw8 <file.ff> -o <zone.bin> [--patch] [--oodle]` | 1 positional + `-o` | Oodle DLL for compressed zones | Decompressed zone bin | 2 on shape; 1 on failure | dispatch [ZT]main.cpp:2962-2973 |
| MW19 extraction and inspection family: `inspect-mw19-assets`, `inspect-mw19-{mesh,image}-prefix`, `extract-mw19-{image,comworld,glassworld,mesh,streamkey,soundbank,decalvolume,fxtransient,edgelist,xmodel,scriptable,vfx,weapon,transient,script,utility,asset,sfx}-prefix`, `convert-mw19-{comworld,glassworld,lightdef,grading,fog,stringtable,proceduralbones,dynamicbones,pathdata,navmesh,tacgraph,detailcollision}-prefix`, `inspect-mw19-xmodel-span`, `inspect-mw19-xmodelsurfs-span` | 1 positional + `-o` or `--report` (span forms take 4-5) | Replay-1.69 (MW19) fastfiles, `--oodle`, optional `--patch` | Source directories, JSON reports, or `owned_*.ff` (names per command, e.g. `owned_worlds_fog.ff`, [ZT]main.cpp:3043-3049) | 2 on shape; 1 on failure | dispatch [ZT]main.cpp:2974-3148 (per family, see the `if` lines) |
| `compact-havok-tagfile <source.tag> -o <replay.tag>` | 1 positional + `-o` | TAG0 Havok file | `<replay.tag>`; identical existing output returns 0; different existing output is refused | 1 on refusal or I/O error | dispatch [ZT]main.cpp:3169-3201 |
| `normalize-mw19-comworld-span <body.bin> <span-manifest.json> <source.ff> <source.fp> -o <bundle.json>`; `normalize-mw19-mapents-span` (same shape) | 4 positionals + `-o` | Span manifest, source ff and fp | `bundle.json` | 2 on shape; 1 on failure | dispatch [ZT]main.cpp:3202-3227 |
| `normalize-mw19-stream-tree-keys <span-manifest.json> <source.ff> <source.fp> -o <bundle.json>` | 3 + `-o` | As above | `bundle.json` | 2 / 1 | dispatch [ZT]main.cpp:3228-3238 |
| `normalize-mw19-resident-images <span-manifest.json> <source.ff> [source.fp] -o <bundle.json>` | 2-3 + `-o` | As above | `bundle.json` | 2 / 1 | dispatch [ZT]main.cpp:3239-3249 |
| `normalize-mw19-world-geometry <span-manifest.json> <source.ff> <source.fp> -o <bundle.json>` | 3 + `-o` | As above | `bundle.json` | 2 / 1 | dispatch [ZT]main.cpp:3250-3260 |
| `normalize-mw19-materials <span-manifest.json> <source.ff> -o <bundle.json> [--patch]` | 2 + `-o` | As above | `bundle.json` | 2 / 1 | dispatch [ZT]main.cpp:3261-3270 |
| `convert-assets <bundle.json> -o <out> [--collision-preview <dir>] [--walk-preview <dir>] [--walk-profile] [--walk-images] [--walk-image-bundle] [--walk-model-bundle] [--walk-traversal]` | 1 positional + `-o` | Bundle JSON | Converted asset output (UNKNOWN layout) | 2 / 1 | dispatch [ZT]main.cpp:3271-3281 |

Note on the usage text: the named binary's `printUsage` lists every command above ([ZT]main.cpp:122-190), so the table matches the verified `--help` output.

### 1.3 Sibling `[SIB]` converter, commands used by build_mp_test.py (verified help only)

- Its `--help` lists `import`, `fromdump`, `validate-package` and says "offline multi-engine -> IW8 Replay converter".
- `fromdump` dispatch: `[SIB]src\main.cpp:610`; `cmd_fromdump` at `[SIB]src\main.cpp:476`. Accepted flags include `-o`, `--stored`, `--lighting-profile`, `--sun-intensity-scale` ([SIB]src\main.cpp:78-82). It reads `<dump>/<map>_iw8_ents.txt` first, then the parent directory (same rule as build-map, [SIB]src\main.cpp:~489-495).
- `validate-package`: `[SIB]src\main.cpp:612`; usage `validate-package <package_dir> <map>` (static validation).
- Exit codes and internal writes were not traced further. Its `fromdump` writes `srv_<map>.ff` and `<map>.ff`, and writes empty `eng_`, `ww_`, `techsets_` zones ([SIB]src\main.cpp:378-427). Whether it writes any other files into `-o` is UNKNOWN.

---

## 2. build_mp_test.py: the exact sequence

Entry: `[TL]build_mp_test.py`, invoked by `[RP]build_mp_test.ps1` as `python <builder> --source <src> --foliage-mask-size 128 [--deploy] --omit-blended`. `--omit-blended` is added unless `-StrictMaterials` ([RP]build_mp_test.ps1:12-15). The .ps1 writes the success line "Select CoD4 mp_test - Radiant Build from F6 ...".

Arguments (defaults, [TL]build_mp_test.py:626-653): `--cod4` (`local_paths.COD4`), `--source` (`<COD4>\map_source\mp_test.map`), `--replay` (`local_paths.REPLAY`), `--sky chechnya_ft`, `--lighting-profile source` (choices `source`, `aniyah-incursion`), `--sun-intensity-scale 6.0`, `--out` (default `[RP]custom_map_sources\mp_test\builds\<timestamp>`), `--reuse-build`, `--deploy`, `--omit-blended`, `--foliage-mask-size` (128|256|512|1024).

Failure convention: `run()` ([TL]build_mp_test.py:49-57) writes the command line as the first line of its log, runs with stdout/stderr redirected to that log, and raises `RuntimeError` on nonzero exit. The `__main__` block ([TL]build_mp_test.py:654-658) catches `ValueError`, `RuntimeError`, `FileNotFoundError`, prints `BUILD FAILED: ...`, exits 1. Any other exception is an uncaught traceback, also exit 1. Successful run prints the report JSON and exits 0.

Step numbering follows the code.

**S0. Preflight** (build_mp_test.py:124-158)
- Reads `--source`, finds `COD4` (`game`), `raw = game/raw`.
- Requires the sun scale to be finite and positive (:124-125).
- Requires these exist, else FileNotFoundError: `<COD4>\bin\cod4map.exe`, `[SIB]\xmake-out\...\iw8-zonetool.exe` (converter), `--replay` (:143-147).
- Parses dependencies: `radiant_collision.collect(source, source_root)` (:141). Prefabs must live under `<COD4>\map_source`, or `source.relative_to` fails (`radiant_collision.py:109-116`).

**S1. Output directory** (:148-160)
- `out` = `--out` or `[RP]custom_map_sources\mp_test\builds\<YYYYMMDD-HHMMSS-micro>`; created with `exist_ok=False`.
- Writes `out\source.map`, dependency copies to `out\map_source\...` (:154-158), `out\compiled\maps\mp\` created.

**S2. CoD4 map compile** (:183-202)
- Command: `<COD4>\bin\cod4map.exe -platform pc -loadFrom <out>\map_source\mp_test.map <out>\compiled\maps\mp\mp_test`
- cwd `<COD4>`; log `<out>\cod4map.log`.
- Fails if log contains `ERROR:` or `is missing`, or if `<out>\compiled\maps\mp\mp_test.d3dbsp` is absent (:196-202).
- Reads: the .map copy and dependencies. Writes: `mp_test.d3dbsp`.

**S3. CoD4 lighting and link** (`cod4_lighting.py`, called at :203)
- **Light compile:** `<COD4>\bin\cod4rad.exe -platform pc -modelshadow -extra <out>\compiled\maps\mp\mp_test` (cwd `<COD4>`, log `<out>\cod4rad.log`). Fails on `ASSERTBEGIN` in log (cod4_lighting.py:14-27).
- **Staging:** name = `mp_mw120r_light_<pid>_<last 12 chars of out dir name without '-'>`. Refuses if raw, csv, or ff already exist.
  - Copies the BSP to `<COD4>\raw\maps\mp\<name>.d3dbsp`.
  - Writes `<COD4>\zone_source\<name>.csv` with exactly one line: `gfx_map,maps/mp/<name>.d3dbsp`.
- **Link:** `<COD4>\bin\linker_pc.exe -nopause -language english <name>`, cwd `<COD4>\bin`, log `<out>\light_link.log`. Fails on `ERROR:` in the log or if `<COD4>\zone\english\<name>.ff` is absent.
- Copies the ff to `<out>\lighting.ff`.
- **Export:** `[TL]_vendor\OpenAssetTools\build\bin\Release_x86\Unlinker.exe --no-color --skip-obj --include-assets gfxworld -o <out>\lighting <out>\lighting.ff`, cwd `<out>`, log `<out>\light_export.log`. Reads `<out>\lighting\maps\mp\<name>.d3dbsp.replay-world.json`.
- `finally` deletes the raw, csv, and ff staging files.
- Note: `cod4_lighting.py` ignores `--unlinker` and `MW120R_UNLINKER`; it always uses the `_vendor` Unlinker.

**S4. Read compiled BSP and build collision hulls** (:204-207)
- Reads `mp_test.d3dbsp` lumps; `geometry(lumps)`; `radiant_collision.add_compiled_triangles`.
- Writes `<out>\source_collision.json` (rewritten at :339 after glass removal).

**S5. Materials and textures** (:211-236)
- Material names from lump 0. `texture(name)` calls `material_image(raw, name)`; blended materials retried with `allow_blend`. Uses `material_channels.describe(raw, name, raw=raw)`.
- Reads CoD4 `raw\` images and material files. No writes.

**S6. World surfaces** (:248-278)
- From `lit_world["surfaces"]` (lighting JSON from S3). Skips `sky_*`. Writes nothing yet. Junction stitching via `source_tjunctions.stitch` (:257).

**S7. Models** (:279-321)
- For each `misc_model` entity: `load_model(raw, name)`, `transformed(...)`. Blended surfaces raise unless `--omit-blended`; omitted ones are recorded in the report.
- Guards: total render surfaces `> 4096` and any vertex with `|x| > 100000` are errors (:322-325).

**S8. Foliage, decals, glass, collision** (:326-339)
- `classify` each material; `decal_geometry.separate`; `glass_panes.prepare` and `remove_static_collision`. Rewrites `<out>\source_collision.json`.

**S9. Atlases and lightmaps** (:340-361)
- `source_atlases.pack` produces three 4096x4096 atlases (color, normal, response). Lightmaps via `map_lighting.pack_coefficients`. Sky cube via `--sky` from `Images(game, raw)`.
- Writes `<out>\atlas.png` (:431).

**S10. Entities** (:215-216, :392-393)
- `numeric_entities(entities)` emits the IW8 numeric-key list (section 3).
- Writes `<out>\dump\maps\mp\mp_test.d3dbsp.ents` (the raw IW3 entity lump) and `<out>\dump\mp_test_iw8_ents.txt` (the numeric list the converter reads).

**S11. Render JSON, material JSON, atlas RGBA** (:394-430)
- Writes `<out>\dump\maps\mp\mp_test.d3dbsp.render.json` (schema 1; primary material `w/mw120r_test`; additional materials for foliage, glass, sky as enabled).
- **Reads `[RP]custom_map_sources\mp_test\dump\maps\mp\mp_test.d3dbsp.material.json` at :413. This path is missing (verified).** This is a hard failure at this step on the current checkout.
- Writes `mp_test_atlas_{0,1,2}.rgba` and the updated material JSON into `<out>\dump\maps\mp\`.

**S12. Presentation and shader** (:432-453)
- `map_presentation.prepare` writes files into `<out>\dump\maps\mp\` (e.g. `mp_test.d3dbsp.lighting.json`, map_presentation.py:86).
- Writes `<out>\authored_map.hlsl` from `[TL]map_surface_realtime.hlsl`, with `ATLAS_COLUMNS` replaced.
- Runs `python [TL]compile_graybox_shader.py --source <out>\authored_map.hlsl --target-root <out>` (cwd `[RP]`, log `<out>\shader.log`). It loads `d3dcompiler_47` (compile_graybox_shader.py:~20-25). Output file names UNKNOWN.

**S13. Material definitions for foliage, glass, sky** (:454-458)
- `glass_material.create(folder, ...)` writes `<kind>` material and technique JSON into `<out>\dump\maps\mp\` (glass_material.py:90-91).

**S14. GPU light grid** (:460-465)
- If `<light_root>\maps\mp\*.d3dbsp.lightgrid.json` exists, `native_lightgrid.convert` writes `<out>\dump\maps\mp\mp_test.d3dbsp.gpulightgrid.bin` and a `.json` next to it (native_lightgrid.py:12-28). It imports `iw3_native_lightgrid` from `[SIB]\tools` (native_lightgrid.py:17).

**S15. Native collision bake** (`bake_native_collision`, :60-86, called at :466-474)
- `footstep_data.generate(out\package, <REPO>\custom_map_sources\mp_test\extracted, ...)` writes `<out>\package\footsteps.bin` (footstep_data.py:52-62). The source dir is missing (verified).
- Writes `<out>\collision.bin` (`radiant_collision.encode`).
- Runs `python [SIB]\tools\bake_collision.py --replay <replay exe> --collision <out>\collision.bin --footsteps <out>\package\footsteps.bin --output <out>\dump\maps\mp\mp_test.d3dbsp.havok` (cwd `[RP]`, log `<out>\collision-bake.log`). Source of args: bake_collision.py:214-219 (`--replay`, `--collision`, `--output` required; `--footsteps` optional).
- Note the `--output` path is `<out>\dump\maps\mp\...` in code (:82), which is the dump tree the converter reads.

**S16. Converter: fromdump** (:475-491)
- Command: `[SIB]\xmake-out\x64\Release\iw8-zonetool.exe fromdump <out>\dump mp_test -o <out>\package --stored --lighting-profile <source|aniyah-incursion> --sun-intensity-scale 6.0`
- cwd `[RP]`; log `<out>\convert.log`.
- Reads `<out>\dump` (including `mp_test_iw8_ents.txt`, render JSON, material JSON, atlases, `.havok`, lighting JSON, gpulightgrid).
- Writes `<out>\package` (contents UNKNOWN beyond the zone set in `[SIB]` source; see U1).
- `manifest.json` is read at :492 and written at :504 and :597-598, so `fromdump` must produce it. This is inferred from :492 and not verified against the sibling source.

**S17. Manifest, glass, collision, preview** (:492-508)
- Updates `<out>\package\manifest.json` (title "CoD4 mp_test - Radiant Build", collision `convex-v3`, glass `panes-v2`, visibility `native-umbra-v1`, world `replay-1.20-native-v1`).
- Writes `<out>\package\glass.bin` (:501-503), `<out>\package\collision.bin` (:505).
- `map_presentation.preview(package, "mp_test")` (:508) writes `<out>\package\preview.rgba` and `<out>\preview.png` and `<out>\preview_source.json` (map_presentation.py:147-154). Preview source defaults to `[RP]custom_map_sources\mp_test\extracted` (missing, verified; falls back to a blank image per map_presentation.py:89-100 when the `.iwi` is absent).

**S18. Package validation (sibling)** (:510)
- `[SIB]\...\iw8-zonetool.exe validate-package <out>\package mp_test`, log `<out>\validate.log`. Exit nonzero raises.

**S19. Layout verification** (:511-524)
- `python [TL]verify_replay_map_layout.py --game <replay exe> --package <out>\package --out <out>\layout.json`, log `<out>\layout.log`. Map defaults to `mp_test`. Exit nonzero raises. Checks not traced.

**S20. Native loader validation, optional** (:525-549)
- Runs only if `[RP]..\atian-cod-tools\build\bin\Release\acts.exe` exists, i.e. `E:\mw124\atian-cod-tools\build\bin\Release\acts.exe`. **It exists on this machine (verified), so it will run.**
- `python [TL]validate_replay_package_acts.py --acts <acts> --game <replay exe> --package <out>\package --map mp_test --output-dir <out>\acts-validation --log <out>\acts-loader.log --out <out>\acts-validation.json`, log `<out>\acts-validation-wrapper.log`. Nonzero exit raises and fails the build.

**S21. Report** (:550-602)
- Computes footsteps report (`footstep_data.generate` again, :593-595) and SHA-256 of every file in `package`. Writes `<out>\build_report.json` with `game_tested: false`, the materials, models, spawns, and the acts result (or a `skipped` entry, :583-590).

**S22. Deploy, optional** (:603-620)
- If `--deploy`: `pwsh -NoProfile -ExecutionPolicy Bypass -File [TL]deploy_custom_map.ps1 -PackageDir <out>\package`, cwd `[RP]`, log `<out>\deploy.log`. Raises if no PowerShell.
- The .ps1 defaults `-Map mp_test` and `-GameRoot E:\IW8\Builds\1.20-replay`. Details in section 5.

**S23. Print report** (:621), return `out` (exit 0).

### Files read and written, summary

- Reads: `--source` .map and prefab dependencies; `<COD4>\raw\` (materials, images, models); `<COD4>\bin\`, `<COD4>\zone\`, `<COD4>\zone_source\`; the Replay exe (existence and bake); `[TL]` templates (`map_surface_realtime.hlsl`, tool scripts); `[RP]custom_map_sources\mp_test\...` (missing, see below).
- Writes: everything under `<out>` as listed in S1-S21; staged files in `<COD4>\raw\maps\mp\`, `<COD4>\zone_source\`, `<COD4>\zone\english\` (removed in `finally`).

### Hard-coded paths and env coupling

| Location | Value | Status (verified) |
|---|---|---|
| `[TL]build_mp_test.py:44` | `BASE = REPO / "custom_map_sources/mp_test"` | Directory missing. Used at :151, :413. |
| `[TL]build_mp_test.py:142` | converter = `REPO.parent / "iw8-zonetool/xmake-out/..."` (`[SIB]`) | Exists. Different program from the named compiler. |
| `[TL]build_mp_test.py:74` | `REPO.parent / "iw8-zonetool/tools/bake_collision.py"` (`[SIB]`) | Exists. |
| `[TL]build_mp_test.py:469`, `:594` | `[RP]custom_map_sources\mp_test\extracted` | Missing. |
| `[TL]build_mp_test.py:525` | `REPO.parent / "atian-cod-tools/build/bin/Release/acts.exe"` | Exists; step S20 runs. |
| `[TL]local_paths.py:9-17` | `ROOT/external/CoD4`, `ROOT/external/Replay`, `ROOT/external/OpenAssetTools/...`, overridable by `MW120R_COD4`, `MW120R_GAME`, `MW120R_UNLINKER` | `external\` missing; env vars unset in this shell. |
| `[TL]build_mp_test.ps1:3` | default `-Source` `E:\CoD4\game_build\Call of Duty 4 Modern Warfare\map_source\mp_test.map` | File exists. |
| `[TL]footstep_data.py:15` | fallback `E:/CoD4/game_build/Call of Duty 4 Modern Warfare/raw/materials` | Exists per path, not traced further. |
| `[TL]native_lightgrid.py:17` | `parents[3]/iw8-zonetool/tools` (`[SIB]`) | Exists. |
| `[TL]cod4_lighting.py:` (Unlinker call) | `tools/_vendor/OpenAssetTools/build/bin/Release_x86/Unlinker.exe` | Exists. Ignores env override. |
| `[TL]compile_graybox_shader.py:9` | `root = .../custom_map_sources/mp_test` (default for `--source` and `--target-root` only) | Not used when both are given. |
| `[TL]deploy_custom_map.ps1` | default `-PackageDir` `..\..\_direct_mp_test_verify13` (stale); converter `[RP]iw8-zonetool\...` (the named binary); `-GameRoot E:\IW8\Builds\1.20-replay` | Converter exists. Package dir default absent (not verified). |
| `[RP]build_deploy_start_mw120r.ps1:6-7` | `E:\IW8\Builds\1.20-replay` | Exists. |
| `[TL]record_v19..v25_handoff.py:8` | `D:/Games/iw8/...` | Historical scripts, not in the Build path. |

---

## 3. `<map>_iw8_ents.txt` (numeric-key entity list)

### 3.1 Where build-map looks (verified code)

- `[ZT]main.cpp:2021-2022`: candidates in order: `<dump>\<map>_iw8_ents.txt`, then `<dump parent>\<map>_iw8_ents.txt`. First non-empty readable file wins and is used verbatim as the entity string.
- If no file: `[ZT]main.cpp:2093-2106` converts the IW3 `.ents` lump (`dumpsrc::DumpSource::loadEnts`) with `convert::iw3ToIw8EntityString`. If that yields zero entities, build-map fails ("could not convert any source entities", :2101).
- If neither exists: built-in default set `defaultEntities` ([ZT]main.cpp:832-838) and a warning (:2108-2109).
- `convert::validateIw8EntityString(entities)` is then called (:2111).

### 3.2 Syntax (verified against `[ZT]convert\maps.cpp` and the samples)

- Plain text, ASCII. One or more blocks `{ ... }`, one entity per block.
- Inside a block: pairs of `<numeric key> "<value>"`, separated by spaces. Keys are numeric IDs, not names. Values are double-quoted strings. No nesting.
- The first pair in a block is typically the classname (`212`), but the validator only requires that some `212` key exists per block ([ZT]convert\maps.cpp:156-176, regex `\{([^{}]*)\}` and `(\d+)\s+"([^"]*)"`).
- Keys are Replay opaque-string IDs. The table in `[ZT]convert\maps.cpp:24-60` maps IW3 names to IDs, and `iw8::havok::FindOpaqueString` resolves the rest.

Key IDs that appear in the files on this machine (decimal, with the hex value that confirms the name in `maps.cpp`):

| ID | Name | Source |
|---|---|---|
| 80 | angles (`"pitch yaw roll"`) | maps.cpp table 0x0050 |
| 212 | classname | 0x00D4 |
| 652 | model | 0x028C |
| 692 | northyaw (`kNorthYawKey`) | 0x02B4, maps.cpp:14 |
| 709 | origin (`"x y z"`) | 0x02C5 |
| 782 | radius | 0x030E |
| 841 | script_exploder | 0x0349 |
| 845 | script_noteworthy | 0x034D |
| 925 | spawnflags | 0x039D |
| 967 / 968 / 969 | suncolor / sundirection / sunlight | 0x03C7-0x03C9 |
| 1070 | target | 0x042E |
| 1072 | targetname | 0x0430 |
| 2553 | _color | 0x09F9 |
| 43840 | script_gameobjectname | 0xAB40 |
| 37683 | UNKNOWN (not in the 28-row table; probably resolved by `FindOpaqueString`) | |

### 3.3 Required content (verified)

- `validateIw8EntityString` ([ZT]convert\maps.cpp:446-461) requires at least one each of classname `worldspawn`, `mp_tdm_spawn`, `mp_tdm_spawn_allies_start`, `mp_tdm_spawn_axis_start`, `script_model`.
- Beyond that, build-map does not filter classes on this path. The `isReplayEntity` allow-list ([ZT]convert\maps.cpp:178+) applies to the IW3 conversion path only (inferred from main.cpp:2093-2111).

### 3.4 Examples

Shape written by the Python (`numeric_entities`, [TL]build_mp_test.py:89-120). Each spawn is `{ 212 "<class>" 709 "<origin>" 80 "<angles>" }` with numbers formatted `.9g`; the first line is always worldspawn:

```
{ 212 "worldspawn" }
{ 212 "<info_player_start | mp_tdm_spawn | mp_tdm_spawn_allies_start | mp_tdm_spawn_axis_start>" 709 "x y z" 80 "p y r" }
{ 212 "script_model" 709 "0 0 16" 80 "0 0 0" }
```

Built-in fallback set, verbatim from [ZT]main.cpp:833-838 (used only when no entity file exists):

```
{ 212 "worldspawn" }
{ 212 "info_player_start" 709 "0 0 64" 80 "0 0 0" }
{ 212 "mp_tdm_spawn" 709 "0 0 64" 80 "0 0 0" }
{ 212 "mp_tdm_spawn_allies_start" 709 "-64 0 64" 80 "0 0 0" }
{ 212 "mp_tdm_spawn_axis_start" 709 "64 0 64" 80 "0 180 0" }
{ 212 "script_model" 709 "0 0 16" 80 "0 0 0" }
```

The `script_model` line is a required anchor: a model-less `script_model` at `0 0 16` (build_mp_test.py:115-119, comments explain the snapshot baseline). Only worldspawn, `info_player_start`, and `mp_tdm_spawn*` are emitted by the Python; other entity classes are counted as omitted (:110-111). Its `numeric_entities` raises on other spawn classes (:95-102) and on missing required spawns (:112-114).

From the sample on disk (verified; `E:\mw124\iw8-zonetool\evidence\multi_engine_20260908\iw3_office_v3\dump\mp_iw3_office_iw8_ents.txt`, first lines):

```
{ 212 "worldspawn" }
{ 212 "mp_tdm_spawn" 709 "-974 246 22" 80 "0 1.00179e-05 0" }
```

Other samples (verified to exist; this is a wider key set from another pipeline):

- `E:\mw124-conversion-builds\zt-scorch-start-flag-20261027\scorch-mapents.txt` (2107 lines). Example:
  `{ 80 "0 3.18772e-06 0" 1070 "pf8_auto2" 1072 "pf8_auto1" 925 "4" 43840 "bombzone" 841 "pf8_2" 709 "-1204 1344 736" 652 "equipment_bombsite_crate" 212 "script_model" }`
- `scorch-deployed-mapents.txt` in the same folder has the same key counts.
- No `_iw8_ents` file exists under `E:\mw124\mw120rproxy` (verified).

---

## 4. What validate-output checks

Entry: `validate` [ZT]main.cpp:2536 -> `validatePackage` [ZT]main.cpp:733. Exit 0 pass, 1 fail, 2 usage (fewer than 2 positionals, :2538-2540).

Checks, in order (verified):

1. Map id valid: `requireMapId` (:735; 4-15 chars, `mp_` prefix, `[a-z0-9_]`, :364-374).
2. `map.json` present (:480-487), parsed by `readMetadata` (:403-478; JSON object, string fields, `name`/`title` aliases must agree), and its `id` equals the folder map id if present (:496-500).
3. Directory listing: every entry must be a regular file (:746-754). Subdirectories are rejected.
4. If `preview.rgba` present: must be a valid MWRI image (:765-770).
5. Exact file set: `<map>.ff`, `srv_<map>.ff`, `eng_<map>.ff`, `ww_<map>.ff`, `techsets_<map>.ff`, `map.json`, optionally `preview.rgba` (:775-778).
6. Each zone file (`validateZoneFile` :588-731): at least 0x8C + header bytes; magic (`iw8ff::kMagicUnsec`); header version; Replay header/stream policy; stream sizes non-overflowing and consistent; resident framing; root asset list; script-string pointer list and terminators; top-level asset descriptors. Logs asset count (:723).
7. Main zone (`validation[0]`) must contain exactly one each of `GFX_MAP`, `GLASS_MAP`, `IMPACT_FX`, `COL_MAP`, `COM_MAP`, `MAP_ENTS`, and its `NET_CONST_STRINGS` count must equal `replayncs::Count` (:793-797).
8. Server zone (`srv_`) must contain zero assets (:795-800). This is the conflict noted in Key finding 5.
9. Techset zone must contain `MATERIAL`, `TECHSET`, `VERTEXSHADER`, `PIXELSHADER` (non-zero) and no `IMAGE`. Main zone material count must equal techset material count; main zone must contain no `TECHSET`, `VERTEXSHADER`, `PIXELSHADER` (:801-812).
10. `eng_` zone: only `LOCALIZE` and `IMAGE` (:813-818). `ww_` zone: only `IMAGE` (:819-824).
11. Passing prints `passes structural checks` (:827).

Not checked (verified from docs and code): visual parity, collision correctness beyond header and asset presence, runtime loading. The proxy's own status string says "structural checks passed; engine load unverified" ([PX]custom_maps.cpp:506).

A sample deployed package on disk, `E:\IW8\Builds\1.20-replay\mods\mw120r\maps\mp_test\`, contains exactly `mp_test.ff` (258,102,414 bytes), `srv_mp_test.ff` (172 bytes), `eng_mp_test.ff`, `ww_mp_test.ff`, `techsets_mp_test.ff`, `map.json` (44 bytes), all dated Oct 29 (year not shown by `ls`; verified). Its producer is UNKNOWN.

---

## 5. Deployment and in-game launch

### 5.1 Install location (verified)

- Replay root: `E:\IW8\Builds\1.20-replay\` (contains `game_dx12_ship_replay.exe`; verified).
- Custom package root: `<game>\mods\mw120r\maps\` ([PX]custom_maps.cpp:455, `g_gameRoot / L"mods" / L"mw120r" / L"maps"`). `g_gameRoot` comes from the loaded proxy DLL location ([PX]custom_maps.cpp:445-455).
- Package folder: `<game>\mods\mw120r\maps\<map>\`; folder name must equal the map id and `map.json` id ([PX]custom_maps.cpp:312-313, 343-344).

### 5.2 Deploy script (verified source; not run)

`[TL]deploy_custom_map.ps1`:

- Params: `-PackageDir` (default stale), `-Map` (default `mp_test`), `-GameRoot` (default `E:\IW8\Builds\1.20-replay`).
- Refuses map ids not matching `^mp_[a-z0-9_]{1,12}$` or longer than 15 characters.
- Requires the named binary at `[RP]iw8-zonetool\xmake-out\x64\Release\iw8-zonetool.exe` and the game exe.
- Runs `validate-output <package> <map>` on the package. Exit nonzero aborts.
- `AssertGameClosed`: refuses while any `game_dx12_ship_replay*.exe` from this root runs. The script never starts or stops the game.
- Stages the package: copies `<map>.ff`, `srv_<map>.ff`, `eng_<map>.ff`, `ww_<map>.ff`, `techsets_<map>.ff`, `map.json`, and `preview.rgba` if present, into `mods\mw120r\maps\.<map>.stage-<stamp>\`, hashing each.
- Validates the stage again.
- Moves any existing `mods\mw120r\maps\<map>` to `.proxy\backups\mw120r-map-<stamp>\<map>`, moves stage into place, rehashes installed files. Rollback on failure.
- Writes `[RP]evidence\custom_map_package_deployment.json` via `Tee-Object`. The `[RP]evidence\` folder is absent in this checkout (verified), so this may fail after installation (U12).

### 5.3 Proxy and launch path (verified)

- Proxy DLL `XInput9_1_0.dll` in the Replay root is installed by `[RP]build_deploy_start_mw120r.ps1`; that script never starts the game (its final line tells the user to start Replay and test themselves).
- `[RPL]mw120rproxy.ini` (verified) has `console=1`, `developer_ui=1`, `custom_map_loader=1`, `offline_auth=1`. Comments: "Tilde opens the native console; F6 opens the in-game custom-map browser." Also "Only explicitly selected, structurally checked custom package paths are served."
- Launchers in `[RPL]`: `Run_Optimized_Replay.cmd` starts `game_dx12_ship_replay_optimized.exe`; `Run_Replay_DW_Local.cmd` starts `game_dx12_ship_replay_local_dw.exe`. Plain `game_dx12_ship_replay.exe` is the base. Verified file names; the proxy and deploy guard apply to all variants.

Steps for the mapper (from the proxy source; not exercised in game):

1. Start Replay. Go to Multiplayer, then Local Play. Its normal map list gets a `Custom CoD4 map` entry for each valid package, with mode `war` ([PX]custom_map_ui.cpp: entry creation around lines 60-80, "normal map selector ready").
2. Or press F6 (`K_F6` = `0x9F`, [PX]developer_ui.cpp:520-524). This opens the custom-map browser; R refreshes, Up/Down browses, Enter selects, F6/Esc closes ([PX]developer_ui.cpp:427).
3. Selecting a package submits `set NSQLTTMRMP <id>` and `set MOLPOSLOMO war` through the command buffer ([PX]developer_ui.cpp:165-177). `NSQLTTMRMP` is the token for `ui_mapname` and `MOLPOSLOMO` for `ui_gametype` ([PX]dvar_names.h:5026, :2481). The proxy translates readable names at command boundaries ([PX]tests\custom_map_tests.cpp:92-96).
4. Close the browser, then Start Match in the Local Play lobby ([PX]developer_ui.cpp:443 status text).
5. Tilde opens the native console. A typed `set ui_mapname mp_test` and `set ui_gametype tdm` are translated to the tokens. Note the proxy writes `war` for the mode, and its comment says "Package manifests call this mode 'tdm'; the game names it 'war'" ([PX]developer_ui.cpp:173-174).
6. The build script's success text says "Select CoD4 mp_test - Radiant Build from F6 in Replay Local Play" ([RP]build_mp_test.ps1 last line). The title comes from `map.json` (`manifest.title`, [TL]build_mp_test.py:494).

The status string for a package is "structural checks passed; engine load unverified" ([PX]custom_maps.cpp:506). Expect that live behaviour needs an owner test.

---

## 6. Map id rules and constraints

### 6.1 Map id

- Length 4-15 characters; prefix `mp_`; characters `[a-z0-9_]`. Verified in three places: `[ZT]main.cpp:364-374` (`isMapId`), `[PX]custom_maps.cpp:37-44` (`IsMapId`), and `[RP]deploy_custom_map.ps1` regex `^mp_[a-z0-9_]{1,12}$` with length limit 15.
- Must not collide with an installed stock map: `[PX]custom_maps.cpp:351-353` rejects if `<game>\zone\<id>.ff` or `<game>\<id>.ff` exists.
- Zone names: `<id>`, `srv_<id>`, `eng_<id>`, `ww_<id>`, `techsets_<id>` plus `.ff` ([ZT]main.cpp:386-389; [PX]custom_maps.cpp:274-275).
- `map.json` `id` must match the folder and the build id. `name` and `title` are aliases and must agree if both present ([PX]custom_maps.cpp:296-313; [ZT]main.cpp:403-478). Other fields: `description`, `compassRotation` (0/90/180/270), `ladderRungDistance`, `minimapScale` ([ZT]main.cpp:72-110).

### 6.2 Output contract

- Exactly five `.ff` plus `map.json`, optional `preview.rgba` (verified: [ZT]main.cpp:775-778; [ZT]main.cpp:538 `kPreviewFile`).
- Output directory must be empty or contain only this map's files (`prepareOutputDirectory`, [ZT]main.cpp:554-585). Unrelated files are refused rather than deleted.
- Proxy-side: the scan expects the five zones and `map.json` (`[PX]custom_maps.cpp:362-383`). Whether `preview.rgba` is accepted there is UNKNOWN (U13); the deploy script copies it when present.

### 6.3 Content constraints (verified in build_mp_test.py unless noted)

- Entities: spawns `mp_tdm_spawn`, `mp_tdm_spawn_allies_start`, `mp_tdm_spawn_axis_start` required (:112-114); `worldspawn` and `script_model` required by the converter (maps.cpp:446-461). Supported spawn classes in the Python: `info_player_start`, `mp_tdm_spawn`, the two team starts (:95-102).
- Render: at most 4096 render surfaces (:322-323). Vertex coordinates within ±100000 (:324-325). Per-surface vertex limit 60,000 and 16-bit indices (INPUT_FORMAT.md, "Render geometry").
- Foliage mask size: 128, 256, 512, 1024 (:648-653).
- Lighting profile: `source` or `aniyah-incursion` (:635-638; choices as declared at [TL]build_mp_test.py:639-640 area). Sun scale: positive finite (:124-125); Python default 6.0 (:634); converter default 1.0 (`Args::sunIntensityScale`, [ZT]main.cpp:~100).
- Blended surfaces (glass, decals) are omitted unless `-StrictMaterials` on the .ps1; the report lists omissions (:298-306; [RP]build_mp_test.ps1:5-14).
- Navigation: `--no-navigation` skips generated bot aipaths/navmesh/tacgraph ([ZT]main.cpp usage). Build_mp_test report says "no bot navigation" (:495).
- Sky: default cube `chechnya_ft` (:630).
- Replay version: 1.20.4.7623265; `game_dx12_ship_replay.exe` MD5 `1c238fe327f2ecc3b0db924c5b425439` ([RP]README.md). The named binary's `--replay` check is documented as SHA-256 (`[RP]iw8-zonetool\docs\USAGE.md`, "The compiler checks its SHA-256"); the check was not traced (U14).
- Game-tested flag: `"game_tested": False` in every report (:561).
- Local tools: CoD4 `cod4map.exe`, `cod4rad.exe`, `linker_pc.exe`. The `cod4_lighting.py` comment says the supplied 64-bit light tool asserts on this host and the x86 compiler completes (cod4_lighting.py:5-6); the code runs `bin\cod4rad.exe`. Python 3 with PIL; numpy and shapely are vendored under `[TL]_vendor`.

### 6.4 Replay runtime constraints (from docs; not re-verified)

- Replay must be closed for deployment (both scripts).
- Map file count and asset ownership, not visual parity, are what validation proves ([RP]iw8-zonetool\docs\USAGE.md, "Check output").

---

## UNKNOWN list

- U1. Whether `[SIB]` `fromdump` writes extra files (manifest, bins) into its `-o`, and whether its `srv_` zone holds assets (`[SIB]src\main.cpp:333` says yes). The named validator requires `srv_` empty ([ZT]main.cpp:795-800). Both reading and running `[SIB]` fromdump would settle this; I did not run it.
- U2. Whether any run of `build_mp_test.py` has completed with the current code. No `build_report.json` exists under `[RP]` (not searched exhaustively). The deployed `mp_test` (dated Oct 29) has an unknown producer.
- U3. Exit-code semantics of `cod4map.exe`, `cod4rad.exe`, `linker_pc.exe`: only log-string checks are implemented (build_mp_test.py:196-200; cod4_lighting.py:26-27, 43-44).
- U4. Full `linker_pc.exe` flag set and whether a multi-line `zone_source` CSV is needed for lightmaps. The code uses one line and a single zone name.
- U5. Output files of `compile_graybox_shader.py` and whether they are read by `fromdump`.
- U6. Output filenames of `native_lightgrid.convert`, `map_presentation.prepare`, `glass_material.create`, and `source_atlases.pack` beyond those named above.
- U7. Whether `MW120R_COD4`, `MW120R_GAME`, `MW120R_UNLINKER` are set in the environment where the Build button will run.
- U8. Editor route choice: `.map` (build_mp_test.py) versus CoD4 `.ff` (`build-iw3`). `RADIANT_TO_REPLAY.md` documents only the `.ff` route.
- U9. Traced-out-of-scope handlers: `build-map-images` output names, `build-weapon*`, `build-scene-assets`, `sim`, `inspect*`, `build-stock`, the MW19 extraction family, `normalize-*`, `convert-assets`. Usage and dispatch are verified; I did not read their handlers.
- U10. The exact in-game menu labels ("Multiplayer", "Local Play", "Start Match") come from proxy comments and status strings, not from Replay resources. Not verified in game.
- U11. Size limits of fastfiles accepted by Replay. The deployed `mp_test.ff` is 258 MB (verified). No limit found in the code read.
- U12. Whether `deploy_custom_map.ps1` fails at its final `Tee-Object` because `[RP]evidence\` does not exist (inferred, not run).
- U13. Whether the proxy scan accepts `preview.rgba` alongside the five zones and `map.json` ([PX]custom_maps.cpp:362-383 lists only five zones plus map.json as expected; the deploy script copies `preview.rgba`).
- U14. Whether `--replay` SHA-256 is enforced by the named binary (documented in USAGE.md, not traced in code).
- U15. Whether the converter emits a `.iw3-assets.tsv` ledger for `build-map` (ledger code is in `buildIw3`, [ZT]main.cpp:1386, 2356).
- U16. What `iw3_native_lightgrid` (`[SIB]\tools`) requires; the Python path is conditional on a lightgrid JSON that the earlier steps may or may not produce.
