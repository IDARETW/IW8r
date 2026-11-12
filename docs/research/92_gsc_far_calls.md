# 92. How Replay 1.20 links far calls between script files

Date: 2026-11-12. Read-only study of `E:\IW8\Builds\1.20-replay\game_dx12_ship_replay_dw.exe` through its linker map
(`tools\dw_disasm.py`, plus a scratch copy of it that does not stop at the map's `gl_*` labels). Follows research 87
section 5 and research 91 section 4 ("the far-call question"). Nothing was run in the game, and nothing under
`E:\IW8\Builds`, `E:\DW\IW8`, `E:\mw124` or `C:\zsw-iw8r` was written. A one-line test script was compiled with the
configured gsc-tool binary into the session scratch folder (not the project) to see its real output.

Labels: **VERIFIED** = read in the exe's code (function + address), in a named source file, or in exported data.
**INFERRED** = follows from verified facts but was not read directly or seen at run time.

Sources: `[WS]` = `E:\IW8-Radiant\out\warzone_study\` (ACTS `scriptfile` exports, research 87). `[GT]` =
`E:\mw124\acts-perf\deps\gsc-tool\src\gsc\`. `[GTX]` = the editor's configured gsc-tool,
the gsc-tool binary the build is configured with (prints "GSC Tool 1.6.2.364"). `[G120]` = the 1.20.4
decompile named in research 87.

---

## Short answer

- Far calls are resolved when the script is loaded, by **asset name**. The engine reads the file reference from the
  script's data. A number becomes the decimal string (`1857`). A string is lower-cased and `\` becomes `/`. Nothing
  else is added, so no `.gsc` is appended. The engine looks up a `scriptfile` asset with exactly that name in all
  loaded zones, loads it if it is not loaded yet, and links the call to the function in that file's function table.
- Every function in a file can be called this way. There is no export list.
- Function names must match **in kind**. A numeric token id only matches a function the target defines with that
  id. A name written as a string only matches a function the target defines as a string. Names written as strings
  get ids from 0x14730 up. Numeric token ids are below 0x14730.
- A missing target file or function is **fatal**. Either the match drops (`Com_Error`, "Unknown function") or the
  process exits (`Sys_Error`, "unknown function"). The call does not have to run: the whole file is linked when it
  is loaded.
- **`scripts\mp\globallogic::init()` works** (INFERRED, with strong evidence). gsc-tool writes file id 0x741 and
  function id 0x204. The engine looks up asset `1857` (in `common_base_mp`), and that file's first function is
  defined with id 0x204. The converter's other whitelist entries work the same way: they name numbered files.
- **`scripts\mp\gametypes\br_gametypes::disablefeature()` does not work** (INFERRED: each step is VERIFIED, but it was
  not run). The file part is fine: `[GTX]` writes `scripts/mp/gametypes/br_gametypes.gsc`, which is the stock asset
  name. But `[GTX]` writes the function as numeric id 0xF04F, while stock 1.20 `br_gametypes.gsc` defines
  `disablefeature` as a string. The ids cannot match, so loading the map script fails. The editor emits this call
  today when the "no circle" switch is set (`src\core\map_script.cpp:446`).

---

## 1. What a far call looks like in a compiled script

### 1.1 gsc-tool

- `[GT]assembler.cpp:375-413` (`assemble_call_far`, IW8 has no `props::farcall`): writes the file as a u32 token id.
  If the id is 0, the path string follows. Then the function as a u32 token id, with the name string following if
  that id is 0. In the bytecode it writes a 3-byte placeholder (`u8 0`, `u16 0`), plus a parameter-count byte for
  thread calls. The id-0 path gets `.gsc` appended only when the engine has `props::extension` (`:390-395`). In this
  source tree, IW8 does not set it (`[GT]engine\iw8.cpp:16`).
- `[GT]lexer.cpp:396-417`: a path token is letters, digits, `_` and `\`, and each `\` is stored as `/`.
  `[GT]context.cpp:670-682` lower-cases the path. A `.` cannot be part of a path, so source code cannot write
  `.gsc` itself.
- `[GT]context.cpp:551-566` (`token_id`): the id comes from the IW8 token table (`engine\iw8_token.cpp`, 83,760
  lines, `max_string_id = 0x1472F`). A name that is not in the table gets id 0 and is written as a string.
- **The configured binary differs from that source on one point (VERIFIED by compiling).** `[GTX]` compiled
  `scripts\mp\gametypes\br_gametypes::disablefeature("circle"); scripts\mp\globallogic::init();` to this
  stack: `[0]"scripts/mp/gametypes/br_gametypes.gsc"[0xF04F]` and `[0x741][0x204]`. So 1.6.2 does append `.gsc` to
  an id-0 path for IW8.

### 1.2 Stock 1.20 scripts (VERIFIED in `[WS]` data)

- `.gscbin` = `GSC\0`, then u32 compressed stack size, u32 inflated stack size, u32 bytecode size, then the zlib
  stack, then the bytecode. Checked on `mp_br_tut.gsc` (5911 / 21421 / 17394) and `br.gsc`. Which header field maps
  to which asset field is INFERRED from `DWBS_ProcessScriptFile` (asset +8, +0xC, +0x10; section 2.2).
- Stock string far calls always carry `.gsc`. Every `scripts/...` string in the 1.20 script strings has it:
  2,815 in `common_base_mp`, 22 in `global_mp`, 36 in `mp_donetsk`, 20 in `mp_br_tut`, and none without it.
  Decoded entries look like `[u32 0]"scripts/mp/gametypes/br_gametypes.gsc\0"[u32 func]`. For example, `mp_br_tut.gsc`
  calls `br_gametypes.gsc` and `br_pickups.gsc` with function id 0, so with name strings. `br.gsc` calls
  `br_respawn.gsc` with ids 0x9973 and 0x9968.
- There are two kinds of stock script asset names. 235 of the 550 in `common_base_mp` are path names ending in
  `.gsc` (`scripts/mp/gametypes/br.gsc`, ...). 315 are bare numbers (`1857`, `1833`, `16`, ...; ACTS writes
  `<name>.<index>.gscbin`). The numbers are the gsc-tool token ids of the paths. Each whitelist target exists under
  its number: `scripts/mp/damage` 0x729 = `1833`, `scripts/mp/weapons` 0x10 = `16`, `scripts/mp/globallogic`
  0x741 = `1857`, `scripts/mp/gameobjects` 0x73B = `1851`, `scripts/mp/utility/game` 0x95E = `2398`
  (all in `common_base_mp`); `scripts/engine/trace` 0x70D = `1805` and `scripts/engine/scriptable_door`
  0x70C = `1804` (in `global_mp`). No `scripts/mp/gametypes/*` path has a token id (`[GT]engine\iw8_token.cpp`, grep).

## 2. The engine's link step

### 2.1 Call chain (VERIFIED)

`GScr_MainMP_LoadScripts` (0x14129ECB0) calls `Scr_LoadScriptWithTempBuffer` (0x14134E400) for each queued script.
That calls `Scr_GetOrLoadScriptObject` (0x14134E460), which calls `Scr_LoadScriptPrecacheList` (0x14134CA20), which
calls `DW_BotStyle_ProcessScriptFile` (0x1413592F0). That function calls `CO_FindAsset(0x34 scriptfile, name, 1)` and
then `DWBS_ProcessScriptFile` (0x141359350), unless `DB_IsXAssetDefault` says the asset is a default one
(0x141359318).

`DWBS_ProcessScriptFile` is the linker. It inflates the stack (`DB_GetRawBuffer`, 0x1413593B6) and walks the
bytecode one opcode at a time (jump table at 0x141359878 / 0x141359830, opcode - 1, at most 0xBC), taking stack
entries in order:

- **Function start** (0x141359401-0x141359474): when the code pointer reaches the next function, it reads the i32
  size and a u32 name id. If the id is 0, it reads the name string and calls `SL_GetCanonicalString` (0x14134CBD0).
  Then `Scr_SetThreadPosition_Precompiled` (0x14134C9D0) stores name -> code position in the current file's
  function table (`[ctx+0xC48]`). Every function is registered; there is no export flag.
- **Far calls**: handler 0x14135968E takes opcodes 0x60, 0x75, 0x92 and 0x96. Handler 0x141359723 takes 0x05, 0x17,
  0x65 and 0x8A. Those are `OP_ScriptFarFunctionCall`, `...Call2`, `OP_GetFarFunction` and `OP_ScriptFarMethodCall`,
  then the far thread, method-thread and child-thread calls (`[GT]engine\iw8_code.cpp:18,36,109,114,130,151,159,163`).
  Each handler calls `Scr_ReadScriptFileString` (0x14135C4F0) for the file, reads the u32 function id (id 0 means
  name string + `SL_GetCanonicalString`), and calls `Scr_EmitFunction_Precompiled` (0x14134C800). It then skips the
  3-byte placeholder (4 bytes for thread calls).

### 2.2 File name (VERIFIED)

- `Scr_ReadScriptFileString` (0x14135C4F0): reads a u32. If it is not 0, the name is `CO_Format("%d", id)` (format
  at 0x1427829CC). If it is 0, the name is the string that follows. Either way it goes through `SL_GetString`.
- `Scr_GetOrLoadScriptObject` canonicalizes the name with `Scr_GetCanonicalFilenameString` (0x1413522F0), which calls
  `Scr_CreateCanonicalFilenameInternal` (0x141352360). That removes leading `\` and `/`, lower-cases, turns `\` into
  `/`, and errors past the length limit ("Filename '%s' exceeds maximum length of %d", 0x142514878). No extension is
  added.
- `CO_FindAsset` (0x1411D4890) lower-cases and turns `\` into `/` again, then calls
  `DB_AssetEntryTable::FindAsset`: an exact name lookup across all loaded zones (the override rules are in research
  91 section 1).

### 2.3 Loading the target (VERIFIED)

- `Scr_EmitFunction_Precompiled` (0x14134C800) gets or creates the target file's function table (`[ctx+0xC64]`)
  and its pending-call list (`[ctx+0xC68]`). If the file is not known yet, the name is pushed onto the precache list
  (`[ctx+0xC50]`, 0x14134C835).
  - If the target is already loaded (its `[ctx+0xC60]` entry has a value, 0x14134C8CD), the function is looked up at
    once. A miss gives `Com_Error(1, "Unknown function")` (`s_scrEmitFunctionUnknownMsg`, 0x14134C8E8). A hit writes
    the relative offset into the 3-byte placeholder (0x14134C906-0x14134C91B).
  - Otherwise the call site is added to the pending list under the function name (0x14134C923-0x14134C9A4).
- After the walk, `Scr_LoadScriptPrecacheList` loads each name on the precache list with
  `Scr_GetOrLoadScriptObject` (0x14134CB02). So a target is **loaded by name, on demand, recursively**, from
  whatever zone holds it. More than 0x800 entries gives "ScriptCompile: MAX_PRECACHE_ENTRIES exceeded"
  (0x14134CAA9). A return of 0 gives `Com_Error(1, "Could not find script '%s'")` (0x14134CBAC).
- When a file finishes loading, `Scr_FixupPrecompiledCallOffsets` (0x14134C5F0) goes through its pending list and
  patches each call site from the file's function table. This only happens when `[ctx+0x34D8] == 0`, 0x14134CB5E;
  what that field means was not determined. A function that is not in the table gives
  `CompileError2("unknown function")` (0x14134C767 and 0x14134C6DE). `CompileError2` (0x14134F0E0) calls
  `Sys_Error`, which calls `CrashReport_RaiseFatalException` and `Sys_ShutdownAndExit` (0x141441890, 0x1414418A9).
  That is a process exit, not a drop.
- **Missing file.** In the missing-asset path, `CO_FindAsset` prints "Missing Server Asset ... Not stalling" when the
  flags allow it, then creates a default entry with zone index 0 (`DB_CreateDefaultEntry`, 0x1411D4F52, then
  `AddToTable`, 0x1411D5005). `DB_IsXAssetDefault` (0x1411D64A0) is true for zone index 0, so the file is not
  processed and its function table stays empty. Every call into it then ends in the fatal "unknown function" above.
  "Could not find script" is not reached on this path: `Scr_GetOrLoadScriptObject` returns the table object, which
  is never 0 there (0x14134E56E-0x14134E5F6). That a missing file always takes this path is INFERRED; not every
  branch of `CO_FindAsset` was traced.

### 2.4 Function names (VERIFIED)

- A u32 id is used as it is. A string goes through `SL_GetCanonicalString` (0x14134CBD0). That function first checks
  a fixed table of 36 names at 0x1554BEC00, which return fixed ids: `getintorzero` 63011 (= 0xF623, also its gsc-tool
  id), `waittill_any_2` 71506 (= 0x11752, also its gsc-tool id), `stringtovec3`, `ispointinsidecircle`,
  `array_convert_keys_to_ints`, and the `waittill_any*` family. It then checks a hash table built at run time
  (`s_scrCanonicalStringBuffer`, cleared by `Scr_InitCanonicalStringBuffer`, 0x141351990). A name found there
  returns index + 0x14730 (0x14134CD14). A new name is added and returns slot + 0x14730 (0x14134CD85).
- So a function written as a string always gets an id of 0x14730 or more, apart from those 36 names. gsc-tool's
  numeric ids are 0x1472F or less. The walker uses the same limit for other u32 references
  (`cmp ecx, 0x1472F`, 0x1413597BE). Its value matches gsc-tool's `max_string_id`.
- The match therefore needs the caller and the target to use the same form of the name. Stock code does this,
  because one compiler built both files.

### 2.5 The whitelist and the two calls in the brief

Checked in `[WS]` stacks by looking for `[i32 size][u32 id]` function headers (scratch scan; header layout from
section 2.1):

| Far call | gsc-tool writes | Asset looked up | How the target defines it | Result |
|---|---|---|---|---|
| `scripts\mp\globallogic::init` | 0x741 / 0x204 | `1857` | first header: size 503, id 0x204 (VERIFIED) | links |
| `scripts\mp\damage::playerkilled_internal` | 0x729 / 0x98E9 | `1833` | header id 0x98E9, size 123 | links (INFERRED) |
| `scripts\mp\weapons::deleteexplosive` | 0x10 / 0x3B71 | `16` | header id 0x3B71, size 111 | links (INFERRED) |
| `scripts\engine\trace::_bullet_trace` | 0x70D / 0x9F1 | `1805` | header id 0x9F1, size 194 | links (INFERRED) |
| `scripts\engine\scriptable_door::system_init` | 0x70C / 0xC4EE | `1804` | header id 0xC4EE, size 54 | links (INFERRED) |
| `scripts\mp\utility\game::getgametype` | 0x95E / 0x58C9 | `2398` | header id 0x58C9, size 8 | links (INFERRED) |
| `scripts\mp\gametypes\br_gametypes::disablefeature` | 0 + `scripts/mp/gametypes/br_gametypes.gsc` / **0xF04F** | `scripts/mp/gametypes/br_gametypes.gsc` (found) | **string** `disablefeature` (header `[20][0]"disablefeature"` at stack +862, VERIFIED) | **fails: id 0xF04F is not the canonical id of `"disablefeature"`** |

"INFERRED" in the table means one header-shaped `[size][id]` match with a size no larger than the file's bytecode.
It was not a full parse. The target function names in `1857`, `1833` and `2398` also appear as `init()`,
`playerkilled_internal(...)` and `getgametype()` in `[G120]1857.gsc:4`, `1833.gsc:2838` and `2398.gsc:1351`.

For the failing row, what happens depends on whether `br_gametypes.gsc` is already loaded. If the game type is `br`,
its scripts are loaded, and the map script then gets `Com_Error` "Unknown function": the match drops. With any
other game type, the map script loads the file on demand, and fixup ends in `Sys_Error`: the game exits. Both are
INFERRED from the code paths; neither was seen in the game.

---

## What this means for the editor

1. **Bug to fix now: the "no circle" Warzone switch.** `src\core\map_script.cpp:446` emits
   `scripts\mp\gametypes\br_gametypes::disablefeature("circle")`, and the converter passes it through
   (`C:\zsw-iw8r\scriptables\src\convert\iw3_iw8\iw3_scripts.cpp:146`). The compiled map script will then fail to
   link (section 2.5). The map line is VERIFIED; the failure is INFERRED, not seen in the game. Until the function
   reference is fixed, drop the call and keep only `level.br_circle_disabled = 1`. That loses the circle feature
   switch: `mp_br_tut` sets both (research 87 section 5). Remove the converter whitelist entry too, or mark it as
   needing the fix in item 3.
2. **Calls into numbered stock scripts work as compiled.** That covers `scripts\mp\globallogic`, `gameobjects`,
   `damage`, `weapons`, `utility\game`, and `scripts\engine\*`. gsc-tool's token table matches 1.20 for the ids
   checked: 7 function headers and 2 fixed engine names. Any new whitelist entry should be checked the same way:
   find the target's `[size][id]` header in the `[WS]` export, or use the `[G120]` file named by the number. A new
   game type script in a map zone (research 91 option 2) can call `scripts\mp\globallogic::init()` and the other
   numbered helpers.
3. **Calls into named stock scripts (`scripts/mp/gametypes/*.gsc`, `scripts/cp_mp/*.gsc`, ...) need a check per
   function.** The file name is fine with `[GTX]` 1.6.2, which writes the `.gsc` name. The gsc-tool source in
   `E:\mw124\acts-perf` would not, so keep using 1.6.2 or a later version. The function id is the problem: 1.20
   defines many of these functions as strings, while gsc-tool's table (from a later build) knows them as numbers.
   Two ways to make such calls link:
   - **Post-process the compiled `.gscbin`** before it goes into the zone. Inflate the stack, rewrite the function
     id of each far call into a named file, re-deflate, and fix the two stack sizes in the header. Use `[u32 0]` plus
     the name string where the target defines the name as a string, and keep the number where the target uses one.
     The engine reads the stack strictly in order, with no offsets into it (section 2.1), so changing string lengths
     is safe (INFERRED). The bytecode does not change. The target's form comes from its function headers in the
     `[WS]` export.
   - Or generate wrapper functions in the map's own script and avoid far calls into named files.
4. **Your own files.** A second script in the map zone with a new path name (for example
   `scripts/mp/gametypes/<mode>.gsc`) is found by name like any stock file. Calls between your own files compile with
   the same gsc-tool, so the name forms match. A missing file or function kills the match at load, whether or not
   the call ever runs. The editor's build preflight should check every far call against a list of known targets and
   their function forms.
5. **Note for research 91 section 2.2 (INFERRED).** A missing required game type script probably does not give
   "Could not find script". `Scr_GetOrLoadScriptObject` returns a non-zero object for a default (missing) asset, so
   the error more likely comes from the next check: "Could not find label '%d' in script '%s'". It is still a drop
   error.

## Not determined

- What `[ctx+0x34D8]` is. When it is non-zero, the fixup step is skipped (0x14134CB5E).
- Whether the order in which scripts are queued at map start puts the game type's scripts before the map script.
  That decides between the drop and the exit in section 2.5.
- Why the gsc-tool source in `E:\mw124\acts-perf` (no `.gsc` for IW8) differs from the 1.6.2 binary (writes `.gsc`).

## Built on this (2026-11-12)

- `data/generated/iw8_stock_scripts.tsv` (`tools/build_stock_script_index.py`): the 632 stock GSC files of
  `common_base_mp` and `global_mp`, 391 stored by number (path from gsc-tool's token table) and 241 by name.
- New game type scripts (roadmap 13.6, core `gametype_script.*`): worldspawn `iw8r_gametype_script` names a GSC file
  beside the map; its file name is the game type name. The build refuses it (and says why, with line numbers) when the
  name is not new or not 1 to 11 of a-z 0-9 _, when there is no `main()`, or when a far call goes to a stock file stored
  by name or to no stock file. Converter (x64-zt c5e182f3) compiles `extracted/scripts/mp/gametypes/<name>.gsc` as it is
  into the map zone; `map.json` lists the name. Tools > Game Modes > New game type script writes a template (team kills
  with the Team Deathmatch spawns) that calls only numbered files.
- The "no circle" switch no longer writes the `br_gametypes::disablefeature` call, and the converter's stock call list
  no longer has it (x64-zt e57497bd).
- Proof: core tests `gametype_script_*`; the sample room with the template built through `IW8Radiant.exe --compile`:
  "compiled game type script scripts/mp/gametypes/teamkills.gsc (1225 bytes gscbin)", ACTS loads the map zone with 0
  failed and exports both scripts; the compiled file holds no path string and the numeric ids 1857 (globallogic, 2
  calls), 1851, 1852, 1912 (spawnlogic, 25) and 2398 (utility/game, 12). Whether the lobby offers the name and the
  match runs is not seen in game.

