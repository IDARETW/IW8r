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

## Result

Automated pass (two rounds, backup `out/ida_types/backup/local_types_20261115_before_all_structs.h`), then a report
pass: 35 read-only Haiku agents (A101-A135, `REPORT_AGENT_PROCEDURE.md`) checked the structs the builder could not
prove and wrote proposals (`docs/ida_types/proposals`, reports in `docs/ida_types/results`). The coordinator fed the
proposals back into the builder (proposal members = anchors, measured sizes = proofs; inferred sizes are listed in
`COORD.json` `drop_size` and not used) and applied them through the same score and dry-run checks, four waves, each
with a backup (`local_types_20261115_before_report_wave1..4.h`). Two retype passes gave 97 byte-array members their
struct type back once that type had the same size.

| | start | end |
|---|---|---|
| Replay accesses on the right member / on a wrong member | 19,530 / 5,449 | 24,306 / 3,907 |
| structs with no padding left | 2,394 | 2,895 |
| structs that were only padding | 415 | 26 |
| padding share of all struct bytes | 35.7% | 10.4% |
| bytes in named members | 95.6 M | 125.3 M |
| builder verdict over the 4,410 structs with a game-test counterpart | | 4,380 agree with Replay or have nothing left to place, 20 absent, 4 opaque, 6 partial |

The 135 structs the automated pass could not prove went through two report passes (35 + 74 read-only Haiku agents,
`REPORT_AGENT_PROCEDURE.md`, `REPORT2_AGENT_PROCEDURE.md`): 108 resolved (rebuilt or proven consistent), 18 absent in
this build, 4 opaque Direct3D interfaces, 5 partial (`GfxCmdBuf`, `GfxVertexBufferState`, `Umbra__Tome`,
`bfx__ProbeResults`, `bdClansGroupMembershipProposal`). `ParticleManager` is laid out to +313744 with an UPPER-BOUND
size (the next static starts at +2499504). Every non-normal verdict is a struct comment in IDA and is listed in
`ALL_STRUCTS_REPORT.txt`. 54 misnamed functions / globals were renamed (`renames_applied.json`).

What static analysis cannot settle (the 5 partials): the record base of `GfxDrawCallOutput` (so `GfxCmdBuf`'s exact
size), a pool element that may be one struct or two (`GfxVertexBufferState`), a variable-length blob (`Umbra__Tome`),
a record whose identity differs from its game-test name (`bfx__ProbeResults`), and two same-typed members whose order
the Replay getters and game-test disagree on (`bdClansGroupMembershipProposal`, size proven).

* Fills: 820 structs got names in their pads (about 2,500 fields); the roughly 1,000 fields without Replay evidence
  carry the member comment "game-test layout; no Replay access checks this field yet".
* Rebuilds: 147 structs rebuilt and 112 struct sizes changed, every size change from a planned rebuild with a proof (allocation, stride, memset,
  loader stream size, next global, anchors to the end, derived-class start); no member moved outside a rebuild or a
  kept parent (`check_apply.py`). Examples: `cg_t`-family types, the vehicle physics family (`BgVehiclePhysics` 688,
  `BgVehiclePhysicsGround` 3112, `FlightDynamics` 1264, `HelicopterDynamics` 2368), `CgSnapshotMP` 405680,
  `ClStatic` 175776, `ClSnapshot` 119000, `GfxCmdBufState` 10368, `GfxViewInfo` 23728, `DWWrappers` 1136,
  `Vehicle` 2192, `PartyMember` 488, `TaskItem` 96, and many sizes earlier tools had invented (`m_unk...` members).
* Parents that hold a resized struct by value but have no proof of their own kept their offsets (the bytes stay
  padding): `PartyData`, `BgVehiclePhysicsManager` (later proven), `TaskManager`, `ParticleManager`, `Online_Friends`,
  `RB_BackendDataCopier` (later proven), `GVehicles`.
* `ALL_STRUCTS_REPORT.txt`: the 135 structs still unproven, each with the reason; their agent reports say what is
  known (lower bounds, confirmed members, conflicts). Open conflicts are listed at the end of `APPLIED.md`.
* 397 local types have no game-test counterpart (Windows SDK / CRT / COM / anonymous types from IDA's libraries,
  HLSL helper types, and a few `Replay_*` types an earlier tool made up).

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
