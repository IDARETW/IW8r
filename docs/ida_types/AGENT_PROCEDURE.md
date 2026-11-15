# IDA local-type repair: procedure for every agent

You are one of up to 100 helper agents repairing struct layouts ("Local Types") in the owner's IDA database of the
Replay game exe (`game_dx12_ship_replay.exe`, MW2019 Replay 1.20). Your brief gives your agent id (`A001` to `A100`).
Your structs, byte ranges, hints and notes are in `E:\IW8-Radiant\docs\ida_types\agents.json` under that id. Read this
whole file before you start, then read your entry.

## 1. What counts as evidence

1. **Replay code is the only authority for offsets and sizes.** A field is CONFIRMED only when you have seen a Replay
   instruction or pseudocode line that reads or writes it at that offset (or a Replay loader / net-field table entry
   for it). Write down the function name, its address and the line.
2. **Game-test** (`E:\1-game-test`: PDB + MAP of an older Xbox build of the same game) gives names, types and field
   order. Replay is newer, so offsets and sizes often differ. Never copy a game-test offset without Replay evidence.
   Read it with `python E:\IW8-Radiant\tools\ida_types\gt_layout.py <Type>`.
3. **Game-test decompile** (`E:\idapluginforcod\sources\cleaned\code_source\src`) tells you which functions touch a
   field, so you can decompile the function with the same name in Replay. It is a guide, not evidence.
4. **Not references at all:** the x64-zt headers (`E:\mw124\x64-zt`), any other IW8 headers under `E:\mw124`, the
   IW8-Radiant source, the Replay SQLite / crossref plugin data, and any other tool built from this database. They
   were made from this same database and repeat its mistakes. The current local types are what you are checking.

## 2. How these structs are stored

- Every struct is `pack(1)` and not fixed-layout: each offset is the running sum of the member sizes before it.
  Changing a member's width, or the size of a struct used by value inside another struct, silently moves every later
  member. That is how many of these layouts went wrong.
- Unknown bytes are `_padXXX` members (`unsigned __int8[n]`, `XXX` = hex offset).
- Most sizes were copied from game-test and were never checked against Replay. Example: `gentity_s` is 1456 bytes in
  the type, but Replay indexes `g_entities` with a stride of 1440 (`GetEntity`: `g_entities + 1440 * n`).
- Game-test flattens base classes into the derived struct. If your entry has `gt_base`, the first bytes belong to
  that base (another agent's struct). Work only on the fields after it, and propose `<Base> baseclass_0` as the first
  member.

## 3. Tools

- Load the IDA tools first, in one call: ToolSearch `select:mcp__ida__get_struct_info,mcp__ida__find_type_by_name,mcp__ida__decompile,mcp__ida__disassemble,mcp__ida__get_xrefs,mcp__ida__idb_query,mcp__ida__get_function_info,mcp__ida__scan_disasm,mcp__ida__rename_struct_field,mcp__ida__set_struct_field_type,mcp__ida__idascript`.
- `decompile` accepts function names. `idb_query` runs SQL over `functions`, `names`, `xrefs`, `strings`, e.g.
  `SELECT name, start, size FROM functions WHERE name LIKE '%InitGentity%'`.
- Game-test, from Bash (read-only):
  - `python E:/IW8-Radiant/tools/ida_types/gt_layout.py gentity_s` : the game-test layout
  - `python E:/IW8-Radiant/tools/ida_types/gt_layout.py gentity_s --uses birthTime` : game-test functions using a field
  - `python E:/IW8-Radiant/tools/ida_types/gt_layout.py gentity_s --funcs` : game-test functions using the struct most
  - `python E:/IW8-Radiant/tools/ida_types/gt_layout.py --global g_entities` : type of a game-test global
- The edit helper, inside `mcp__ida__idascript` (one call per action, each call starts with the `exec` line):

```python
exec(open(r"E:\IW8-Radiant\tools\ida_types\ida_fill_pad.py", encoding="utf-8").read())
show("gentity_s", 0x290, 0x2B0)        # members in [lo, hi)
decl("gentity_s")                      # full C declaration
fill_pad("gentity_s", [(0x298, "missileTargetEnt", "EntHandle")], lo=0, hi=0x5B0)              # dry run
fill_pad("gentity_s", [(0x298, "missileTargetEnt", "EntHandle")], lo=0, hi=0x5B0, apply=True)  # write
```

`fill_pad` only places new members inside existing pads. It refuses (and changes nothing) if a field is not wholly
inside one pad, is outside your range, overlaps another, reuses a name, or if the rebuilt struct would change size or
any offset. Pass your own range as `lo` / `hi`.

## 4. Rules

1. Edit only the structs, and only the byte ranges, in your agents.json entry. Other agents are working at the same
   time; a shared struct is split between agents by range. If your range does not reach the struct's end, the size
   question belongs to the agent whose range does.
2. Edits you may apply once CONFIRMED:
   - add a field inside a `_pad` member: `fill_pad` (dry run first, then `apply=True`);
   - rename a member whose name is a placeholder or wrong (`field_*`, `unknown_0x31`, `runtime_0x298`, ...) when
     Replay code and the game-test name agree: `rename_struct_field`;
   - change a member's type only when the new type has exactly the same size (`unsigned int` to `EntHandle`,
     `__int64` to `XModel *`): `set_struct_field_type`. Never use it on a pad. Check with `get_struct_info` afterwards
     that the struct size and the next member's offset did not change.
3. Never do these yourself. Write them as PROPOSALS in your report: change a struct's total size; move or delete a
   member; change a member's width; resize or retype a by-value child struct; add a base class; edit a union or a
   struct with bitfields; edit any struct or range that is not yours; change function prototypes or global variables.
4. Never guess. A game-test field you cannot find in Replay code stays in the pad. "Probably" is fine for a proposal
   marked UNCONFIRMED, never for an edit.
5. Keep IDA responsive. It is the owner's live session and every agent shares its main thread.
   - Check `get_function_info` before decompiling; do not decompile functions over 40,000 bytes (for example
     `VM_Execute`): use `disassemble` on a small range or `scan_disasm` with a pattern instead.
   - `idascript` only for the helper above. Never loop over all functions, strings or types in a script.
   - If a tool reports "Exception in timer callback", or IDA stops answering, stop and report it.
6. Do not save the database, do not use undo (`database_state`), do not touch the owner's plugins or IDA settings.
7. Write files only under `E:\IW8-Radiant\out\ida_types\` (your report and the before-copies). Do not edit repository
   files, do not build, do not commit. Use the Write tool, not shell redirection.

## 5. Steps for each struct

**Step 0, save the current declaration.** `decl("S")`, then write the text to
`E:\IW8-Radiant\out\ida_types\before\S.h` (for a shared struct: `S.AXXX.h`).

**Step 1, read both layouts.** `get_struct_info` (or `show("S", lo, hi)` for big structs) and `gt_layout.py S`.
Note which members match a game-test name at the same offset, which pads cover game-test fields, and which members
have placeholder names.

**Step 2, size** (only if your range reaches the struct's end). Find Replay evidence of the struct size:
- an array stride: `base + N * i`, `imul reg, reg, N`, a loop pointer step of N (example above: 1440 for gentity_s);
- an allocation or clear of the whole struct: `MT_Alloc`, `Hunk_*`, `memset` / `memcpy` with size N;
- for assets: `Load_Stream(..., var<S>, N)` in `Load_<S>`, or `i += N` in the parent's loader loop;
- in a confirmed parent: the distance from S's start to the next confirmed member.

Start with `replay_loaders` and `size_evidence_candidates` in your entry. A size that differs from the type is a
PROPOSAL; say where the difference is if you can find it.

**Step 3, members** (only inside your range). For each existing member and each game-test field that sits in a pad:
- find a Replay function that uses it: `replay_functions_to_read` in your entry, or `gt_layout.py S --uses FIELD`
  and then decompile the same name in Replay;
- in the Replay pseudocode find the access: typed code shows `p->member` or `p->_pad298[4]`, untyped code shows
  `*(_DWORD *)(p + 664)`. Match it by meaning: the same constant, call, string or flag as in the game-test function;
- record one evidence row per field: field, Replay offset, width, `function@address`, the line;
- one function often confirms many fields: read it once and take everything it shows;
- if a run of members all disagree with Replay by the same delta, the shift starts at the first disagreeing member:
  an earlier member or child changed size. Find it and propose the fix.

**Step 4, apply** the allowed edits from rule 2 (dry run first). Then `get_struct_info` again: the size must be
unchanged. Decompile one evidence function again: the access should now show the field name.

**Step 5, report.** Write `E:\IW8-Radiant\out\ida_types\results\AXXX.md`:

```
# AXXX
## <Struct> [range]
- Size: type <n>; Replay <m> from <function@addr: line>  -> OK | PROPOSAL | not checked (range does not reach the end)
- Applied: <field> +0x<off> <type>  (<function@addr>: `<line>`)
- Renamed / retyped: <old> -> <new>  (<evidence>)
- Proposals: <what>, CONFIRMED | UNCONFIRMED, <evidence>
- Left in pads (no Replay evidence found): <fields>
- Problems: <refusals, tool errors>
```

Your final message (at most 15 lines): how many fields applied, how many proposals, the three most important findings,
and anything that went wrong.
