# IDA local-type repair, second report pass: procedure for agents A136 onwards

You are a helper agent finishing the last struct layouts ("Local Types") in the owner's IDA database of the Replay
game exe (`game_dx12_ship_replay.exe`, MW2019 Replay 1.20). An automated pass and 35 earlier agents laid out about
4,240 structs from Replay code. Your structs are among the 135 that are still unproven. Your agent id and structs are
in `E:\IW8-Radiant\docs\ida_types\report2_agents.json`. Read this whole file, then `AGENT_PROCEDURE.md` sections 1, 2,
3 and rule 5 (evidence, storage, tools, keeping IDA responsive). Those rules apply to you.

**There is no time or token budget for this job.** Do not set one yourself and do not stop early. Finish every struct
in your entry before you report. Stop early only when a tool error stops you, and say so.

**You only READ IDA** (decompile, disassemble, get_xrefs, get_struct_info, get_function_info, idb_query, scan_disasm,
and `show` / `decl` from `ida_fill_pad.py`). Never call anything that writes (`fill_pad`, `rename_struct_field`,
`set_struct_field_type`, `set_type`, `relayout`, `remap`, `truncate`, `apply_plan`, ...). The coordinator applies your
result after checking it.

## Your packet

`E:\IW8-Radiant\out\ida_types\packets2\S.json` for each struct `S`. Besides the fields described in
`REPORT_AGENT_PROCEDURE.md` (game-test layout, current IDA layout, builder layout with confidences, anchors, Replay
functions that take `S *`), it has:

- `first_pass`: what the earlier agent found (`entry`: its proposal; `report`: its notes). Start from it; do not
  redo what it proved, and look first at what it could not settle;
- `deleting_dtor_lead`: sizes passed to the sized `operator delete` in a Replay function NAMED as this class's
  deleting destructor, plus a game-test check (`validated` = the game-test function of that name frees exactly the
  game-test sizeof). WARNING: Replay names came from an earlier matching tool and deleting destructors look alike, so a
  name can be wrong (example: the function named `CgSnapshotMP::scalar_deleting_dtor` frees 528 bytes, but
  `CgSnapshotMP` is proven 405680). Trust the size only after you show the destructor sits in this class's own vtable;
- `typed_globals`: Replay globals already typed `S` and the distance to the next named global;
- `embedded_in`: IDA structs that hold `S` by value, the member, its offset, the next member and whether that parent
  was already rebuilt from Replay evidence (then the distance to the next member is a size bound or the size);
- `related_replay_functions_by_name`: Replay functions named `S::...` or `S_...`.

## The goal: a verdict for every struct

Each struct must end with exactly one verdict:

- `resolved`: the Replay size is proven (one of the size methods below) and every member that Replay code touches is
  placed at its Replay offset. Game-test fields that no Replay instruction touches may be listed only when they fill
  the bytes between two confirmed members exactly (same total size, same order); mark those `"by elimination"`.
- `absent`: the type does not exist in this build. Show it: no allocation, no global, no by-value use inside a struct
  that Replay uses, no function that takes or returns it, and say what Replay uses instead if you can (example: Replay's
  loopback uses three 32832-byte rings, not `NetBuffer` objects).
- `opaque`: an external object that Replay only reaches through an interface pointer (Direct3D 12 / COM interfaces
  such as `ID3D12Device`): only the vtable pointer at +0 is real, its size cannot and need not be known. Show that
  every Replay access is `[obj]` (vtable load) followed by an indirect call.
- `partial`: you proved some offsets but could not prove the size. Say exactly what blocks it and give the best lower
  and upper bounds with evidence.

## Size methods (Replay code only)

1. **Allocation**: the caller of the constructor: `mov ecx/edx/r8d, N` before the allocator call
   (`Mem_HunkUser_AllocInternal`, `bdMemory::allocate`, `operator new`, `MemoryManager::Malloc`, ...), then the result is
   passed to `S::S` or gets `S`'s vtable stored at +0. Also `N * count` hunk allocations.
2. **Deleting destructor**: `mov edx, N` before the sized delete (`operator delete`, `bdMemory::deallocate*`,
   `__global_delete`). Prove the function is `S`'s: it sits in the vtable that `S`'s constructor stores at +0, or in the
   vtable whose other slots are `S`'s own methods.
3. **Whole-object clear or copy**: `memset(this, 0, N)` / `memcpy(dst, this, N)` / a loop of 16-byte moves that covers
   the whole object in a constructor, `Reset`, `Clear`, `Copy`, `operator=`.
4. **Array stride**: `base + N * i`, `imul reg, reg, N`, a loop pointer step `add reg, N`, where the elements are known
   to be `S` (the parent member's type, or the function using them is `S`'s).
5. **Global extent**: find the global by its game-test name (`python E:/IW8-Radiant/tools/ida_types/gt_layout.py
   --global NAME`, or `idb_query` on names); the next global's address bounds the size; with the highest Replay
   access and the struct's alignment it often fixes it.
6. **Parent**: the distance from `S`'s start to the next member of a parent rebuilt from Replay evidence.
7. **Derived class**: the first own member of a class derived from `S` starts at `sizeof(S)` (aligned).
8. **Self-reported size**: Demonware classes have `sizeOf()` / `getObjectSize()` methods that return the size; asset
   types have `Load_S` with `Load_Stream(..., varS, N)`.
9. **Stack local**: in a function that keeps `S` as a local, the next local in the frame bounds the size
   (`get_stack_frame`).

A builder size or a game-test size is never proof on its own.

## Misnamed functions and globals (owner: "allowed to rename any misnamed functions/symbols")

Many Replay names came from an earlier name-matching tool. When Replay code proves that a function or global you meet
is misnamed, propose the rename in `renames` (do not rename it yourself; the coordinator applies them in one batch):

- proof examples: a function named as `A`'s deleting destructor sits in `B`'s vtable (the one `B::B` stores); a
  function named `X` logs a string or calls a sequence that game-test's `Y` has, and game-test's `X` does something
  else; a global named `g_a` is passed to functions of the type its game-test namesake does not have;
- give the address, the current name, the new name (game-test spelling, `Class::Method`; game-test names via
  `gt_layout.py` or the atlas), and the proof in one line;
- names with an address or size baked in (`BgAntiLag_vt1423BD2D0_scalar_deleting_dtor_37848b`, `sub_...`,
  `dword_...`) should get their real name when you know it;
- never rename on a guess. "Probably" belongs in `notes`, not in `renames`.

## Output

Write `E:\IW8-Radiant\out\ida_types\proposals\AXXX.json` (Write tool, valid JSON: no trailing commas):

```json
{"agent": "AXXX", "structs": [
  {"name": "S", "verdict": "resolved", "size": 688,
   "size_evidence": "method 1: G_Foo@0x140D1A2B0 mov ecx, 2B0h -> Mem_HunkUser_AllocInternal -> S::S",
   "members": [[0, "__vftable", "void *", "S::S@0x140D1A2B0: mov [rcx], rax"],
               [8, "m_id", "int", "S::Reset@0x140D1A400: mov dword ptr [rcx+8], 0FFFFFFFFh"],
               [12, "m_flags", null, "by elimination between m_id and m_next"]],
   "changed_members": [["m_wheels", "count 16 -> 12", "S::Setup@...: cmp ebx, 0Ch"]],
   "unconfirmed": ["m_debugName: no Replay access found"],
   "notes": "one or two sentences"}],
 "renames": [["0x140D1A2B0", "CgSnapshotMP::scalar_deleting_dtor", "Foo::`scalar deleting destructor'",
              "only xref is Foo's vtable slot 0, which Foo::Foo@0x... stores at +0"]]}
```

- `size`: the proven Replay size, or `null` (verdict `partial`, `absent` or `opaque`).
- `members`: `[Replay offset, game-test name (or a new descriptive name for a Replay-only member), IDA type or null,
  evidence]`. List every member you confirmed, also the ones that are already right in IDA.
- For `absent` / `opaque`, explain the proof in `notes` and leave `members` empty or with the vtable only.

Also write a short `E:\IW8-Radiant\out\ida_types\results\AXXX.md`. Your final message (at most 15 lines): per struct,
the verdict and size; the most important finding; anything that went wrong.
