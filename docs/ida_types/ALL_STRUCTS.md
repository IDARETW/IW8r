# Every local struct laid out from Replay logic (2026-11-15)

Owner request: "get ALL of the structs laid out." This pass generalises the `cg_t` method (CG_T.md) from one struct to
every struct at once. Game-test (PDB / MAP) gives names, field order and base types; only Replay code decides offsets
and sizes. Tools that were built from this database (x64-zt headers, the plugin, the SQLite) are not used.

## Pipeline (tools/ida_types, all outside IDA except the last step)

1. `gt_signatures.py`: struct-pointer arguments of every game-test function from the atlas (game-test MAP):
   89,187 functions; Replay functions carry the same names, so 17,880 Replay functions get typed arguments.
2. `gt_types_dump.py`: all 94,217 game-test struct / class / union layouts from `E:\1-game-test\types.txt`.
3. `scan_types.py`: capstone scan of both exes. Every typed argument register seeds a pointer of that struct; copies,
   spills and `lea` / `add` sub-pointers are followed; every access `[reg + disp]` and every immediate is recorded in
   instruction order (Replay: 15,126 functions with accesses; game-test: 74,214).
4. `align_types.py`: for each function both builds share and each struct it reaches, Needleman-Wunsch alignment of
   the two access lists; each aligned pair votes "game-test offset G = Replay offset R". Constants are aligned the same
   way (game-test sizeof -> Replay sizeof).
5. `scan_alloc_sizes.py`: `mov ecx, SIZE ; call alloc ; call T::T` sites in both exes; a site whose game-test size equals
   the game-test sizeof validates the Replay size at the same caller (118 classes).
6. `replay_layouts.py`: per struct, children before parents:
   * anchors = aligned pairs on the struct's fields (pairs inside a by-value child are passed down to the child through
     the parent's placement, three passes); members the agent waves placed from Replay code count as anchors;
   * fields are laid out between anchors with their Replay sizes and natural alignment; a residual is absorbed only by
     the one field whose size can change (array / unproven child); otherwise the fields in between stay padding;
   * base classes are clamped to the room before the next member (virtual-base storage sits at the end), explicit
     alignment (HK_ALIGN16 etc.) is read from the game-test tail padding, unions and small value types keep their size;
   * every field gets a confidence: anchor / interp / fwd / bwd / absorbed / open / prior (no Replay evidence).
7. `make_apply_plan.py` + `ida_fill_pad.apply_plan()`: the decision per struct:
   * **fill**: name game-test fields that sit wholly inside `_pad` members (no size change, no member moves). Fields
     without Replay evidence get the member comment "game-test layout; no Replay access checks this field yet";
   * **relayout**: only when the new layout puts more Replay accesses on the right member than the current one (and
     at most half as many on a wrong one), the new size is proven (allocation site, unambiguous sizeof votes, anchors
     to the end, agent-verified) and no Replay access reaches past it;
   * **restore**: a parent that holds a resized struct by value but is not itself rebuilt keeps its offsets (the bytes
     stay padding), so no unproven ripple reaches it (`PartyData` / `PartyMember`, `BgVehiclePhysicsManager`);
   * agent-verified structs (snapshot in out/ida_types/before) only get fills of anchored / interpolated fields;
   * everything else goes to the report.
8. `check_apply.py`: diff of two `dump_udts()` dumps + the Replay access score.

## Result (two rounds applied, backup `out/ida_types/backup/local_types_20261115_before_all_structs.h`)

| | before | after |
|---|---|---|
| Replay accesses on the right member / on a wrong member | 19,530 / 5,449 | 22,517 / 5,260 |
| structs with no padding left | 2,394 | 2,833 |
| structs that were only padding | 415 | 31 |
| padding share of all struct bytes | 35.7% | 20.9% |

* 836 structs got names in their pads (2,340 fields; 911 of them game-test-only fields, marked by the comment).
* 47 structs rebuilt from Replay evidence, e.g. `Shake` 1008 (cg_t already used 1008), `GfxViewInfo` 23728, `BgMissile`
  24, `BgPlayer_Asm` 32 (allocation), `NetConnection` 32, `PartyMember` 488 (133 sizeof votes), `DLogContext` 352,
  `BgVehiclePhysicsZodiac` 1912, `Online_Invitation` 80, `TransientCostCalc` 33816, the Havok constraint atom sets.
  Several of these replaced sizes that earlier local tools had invented (`m_unk...`, `m_arrayAt...` members).
* `ALL_STRUCTS_REPORT.txt`: the 275 structs whose Replay layout differs from IDA but is not proven yet (size not proven,
  moves not better supported, accesses past the computed size). They still got their safe fills.
* 397 local types have no game-test counterpart (Windows SDK / CRT / anonymous types from IDA's own libraries).

Rerun (a few minutes in total):

```
python tools/ida_types/gt_signatures.py out/ida_types/scan/sigs_gt.json
python tools/ida_types/gt_types_dump.py out/ida_types/scan/gt_types_all
python tools/ida_types/scan_types.py --exe <replay exe> --functions out/ida_types/scan/functions.json --sigs out/ida_types/scan/sigs_gt.json --by name --out out/ida_types/scan/replay_types
python tools/ida_types/scan_types.py --exe E:\1-game-test\1-game_test.exe --functions out/ida_types/scan/functions_gt.json --sigs out/ida_types/scan/sigs_gt.json --by start --out out/ida_types/scan/gt_types
python tools/ida_types/align_types.py --replay out/ida_types/scan/replay_types.jsonl --gt out/ida_types/scan/gt_types.jsonl --replay-functions out/ida_types/scan/functions.json --gt-functions out/ida_types/scan/functions_gt.json --out out/ida_types/scan/all_align
python tools/ida_types/scan_alloc_sizes.py ... (both exes; see the file header) ; alloc_sizes.json keeps the validated ones
IDA: dump_udts(r"E:\IW8-Radiant\out\ida_types\scan\ida_udts.json")
python tools/ida_types/replay_layouts.py --out out/ida_types/scan/replay_layouts --passes 3
python tools/ida_types/make_apply_plan.py out/ida_types/scan/apply_plan
IDA: backup_all(...); apply_plan(plan, apply=False); apply_plan(plan, apply=True, log=...)
```
