# IDA local-type repair, report pass: procedure for agents A101-A135

You are one of 35 helper agents finishing the struct layouts ("Local Types") in the owner's IDA database of the Replay
game exe (`game_dx12_ship_replay.exe`, MW2019 Replay 1.20). An automated pass (`docs/ida_types/ALL_STRUCTS.md`) laid
out every struct it could prove from Replay code. Your structs are the ones it could NOT prove: their current IDA
layout disagrees with Replay accesses, or their Replay size is unknown. Your agent id and structs are in
`E:\IW8-Radiant\docs\ida_types\report_agents.json`. Read this whole file, then `AGENT_PROCEDURE.md` sections 1, 2, 3 and
rule 5 (evidence, storage, tools, keeping IDA responsive). Those rules apply to you too.

**There is no time or token budget for this job.** Do not set one yourself and do not stop early. Finish every struct
in your entry before you report. Stop early only when a tool error stops you, and say so.

## What is different in this pass: you do NOT edit IDA

You only READ IDA (decompile, disassemble, get_xrefs, get_struct_info, idb_query, and `show` / `decl` from
`ida_fill_pad.py`). Never call `fill_pad`, `rename_struct_field`, `set_struct_field_type`, `set_type`, `relayout`,
`remap`, `truncate`, `apply_plan` or anything else that writes. The coordinator applies your result after checking it.

If a struct in your entry has no `packets\S.json` (only `S.json.done`), the coordinator already solved it: skip it.
Wave 1 changed these local types, so they are now right for Replay and can be used as fixed sizes: `TaskItem` 96,
`GenericTask` 88, `FlightDynamics` 1264, `HelicopterDynamics` 2368, `BgVehiclePhysics` 688, `BgVehiclePhysicsGround`
3112, `BgVehiclePhysicsGround__Wheel` 148, `CgSnapshotMP` 405680, `CgVehicleSystem` 1135880, `CgVehicleSystemMP`
1141000, `CgVehicleEventSystem` 14040, `PartyHostDetails` 48, `OnlineMatchmakerOmniscient` 1071120.

## Your starting evidence: the packet

For each struct `S` read `E:\IW8-Radiant\out\ida_types\packets\S.json` (Read tool). Fields:

- `ida_size`, `game_test_size`, `builder_size` (what the automated pass computed), `builder_size_proof`;
- `why_not_applied`: the reason the pass stopped (size not proven, moves not better supported, accesses past the end);
- `unresolved_residuals`: `[field before, field after, residual bytes, candidates]`. Between those two fields the
  Replay layout is that many bytes longer (+) or shorter (-) than game-test, and the pass could not tell which member
  changed. Finding that member is usually the whole job;
- `replay_max_access_end`: the highest byte Replay code reaches through a pointer typed `S` (the size is at least
  this, unless a function's game-test signature types the pointer wrongly);
- `score_now`: [Replay accesses on the right member, on a wrong member] for the current IDA layout;
- `game_test_layout` / `game_test_bases`: `[offset, name, size, type, tag]` from the game-test PDB (names and order);
- `ida_layout`: the current IDA members `[offset, name, size, type, is_base]`;
- `builder_layout`: `[Replay offset or null, name, Replay size, confidence, game-test offset]`. Confidence `anchor` =
  that field's own Replay accesses line up there; `interp` / `fwd` / `bwd` / `absorbed` = laid out from anchors;
  `open` = after the last anchor; `prior` = no evidence; `null` offset = the pass could not place it;
- `anchors_gt_to_replay`: `[game-test offset, Replay offset, functions]` access pairs from functions both builds share;
- `replay_functions`: Replay functions that take `S *` per the game-test signature, with the offsets they touch.

Treat the builder layout and the anchors as hypotheses to check, not as facts. A wrong signature, an inlined base
class or a union can make anchors lie.

## Steps for each struct

1. **Size.** Find Replay evidence of `sizeof(S)`: an allocation (`mov ecx, N` before the allocator, then `S::S`), an
   array stride (`imul`, `base + N * i`, a loop step), a whole-object `memset` / `memcpy` in `S::S`, `Reset`, `Clear`,
   `Init`, a `Load_<S>` stream size, or the next member after `S` in a confirmed parent. For a class with derived
   classes, the derived class's first own member starts right after the base (check its constructor).
2. **Where the layout changes.** For each unresolved residual, read Replay functions that touch the fields on both
   sides (start with `replay_functions`; `gt_layout.py S --uses FIELD` gives game-test functions to look up by name in
   Replay). Find the member whose size or count changed, or the member that was added or removed.
3. **Members.** Place every game-test field you can confirm: Replay offset, width, `function@address` and the line.
   One function usually confirms many fields.

## Output

Write `E:\IW8-Radiant\out\ida_types\proposals\AXXX.json` (Write tool):

```json
{"agent": "AXXX", "structs": [
  {"name": "S", "size": 688, "size_evidence": "BgVehiclePhysics::BgVehiclePhysics@0x140D1A2B0 called after mov ecx, 2B0h",
   "members": [[0, "__vftable", "void *", "ctor@0x140D1A2B0: mov [rcx], rax"],
               [8, "m_physicsId", "int", "BgVehiclePhysics::Reset@0x140D1A400: mov dword ptr [rcx+8], 0FFFFFFFFh"]],
   "changed_members": [["m_wheels", "count 16 -> 12", "BgVehiclePhysicsGround::Setup@...: cmp ebx, 0Ch"]],
   "unconfirmed": ["m_debugName: no Replay access found"],
   "notes": "one or two sentences"}]}
```

- `size`: the proven Replay size, or `null` if you could not prove it (then give the offsets you are sure of).
- `members`: every member you confirmed, `[Replay offset, game-test name, IDA type or null, evidence]`. Use the
  game-test name; `null` type lets the coordinator choose the type. Members you did not confirm are left out (they stay
  padding unless the coordinator can place them between confirmed ones).
- `changed_members`: members whose size, count or type differ from game-test.

Also write a short `E:\IW8-Radiant\out\ida_types\results\AXXX.md` (what you found per struct, problems). Your final
message (at most 15 lines): per struct, proven size or not and how many members confirmed; the most important finding;
anything that went wrong.
