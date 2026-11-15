# 134. Lobby crash on a map with carried stock scriptables: DB_PatchMem's asset stack holds two

Owner report 2026-11-14: selecting Cinder Rail (`mp_cinderrail`, the 20:15 package) in the private-match lobby gives
`Fatal Error 0x0000000140d97add 7596524 0xc0000005 game_dx12_ship_replay_local_dw.exe`. The toon build
`mp_cinderrail_t` (18:42) loads.

## What the client log shows (VERIFIED)

`replay-dw-client.log` ends with `DW_ZONE_REQUEST load=0x00000002 mp_cinderrail:0x00100000`: the lobby loads the
map zone when the map is selected, and the crash follows.

## The faulting code (VERIFIED: the local_dw exe and its PDB)

`game_dx12_ship_replay_local_dw.exe` is the `_dw` build with 1,024 bytes changed; both carry the PDB GUID of
`<game>\game_dx12_ship_replay_dw.pdb`. Named with `llvm-symbolizer` (the base exe's IDA database has the same
functions at other addresses, e.g. `DB_PatchMem_PostLoad_PushAsset` at `0x140D8D860`):

| local_dw address | name | what it does |
|---|---|---|
| `0x140D97A80` | `q_DB_PatchMem_PushAsset` | records the 8 block positions at `table[counter]` (0x44 bytes each), then `counter++`; **no bound check** |
| `0x140D97860` | `DB_PatchMem_PostLoad_PushAsset` | records only when `counter == 0`, then `counter++` |
| `0x140D97850` | `DB_PatchMem_PopAsset` | `counter--` |
| `0x140D971DA` | `DB_PatchMem_BeginLoad` | resets the counter |
| `0x140D97EF3` | `DB_PatchMem_TryDiscard` | uses the table only when `counter < 2` |

The table is `0x145DA4830` and the counter `0x145DA48B8 = table + 2 * 0x44`: the stack holds **two** records. A
push at depth 2 writes a block position over the counter, and the next push indexes the table with that position
and faults at `0x140D97ADD`, the reported address.

**Who pushes:** 210 call sites of `q_DB_PatchMem_PushAsset` in the asset loaders of nearly every type
(`Load_Material`, `Load_MaterialTechniqueSet`, `Load_GfxImage`, `q_Load_XModel`, `Load_XAnimParts`,
`Load_PhysicsAsset`, `Load_ScriptableDef`, `Load_ParticleSystemDefPayload`, `Load_MapEdgeList`, `Load_GfxWorld`,
...), the map preload (`Preload_*`) and, through `DB_PatchMem_PostLoad_PushAsset`, the postload (`Postload_*`). So
the limit is: a list entry (depth 1) may load assets inline (depth 2), but an asset loaded inline in an inline
asset (depth 3) crashes, whatever the types.

## What nested three deep

First fix (converter 51312871) counted only the map-preloaded types and found `scriptable
"scriptable_door_wooden_panel_03_painted_mp" > xmodel "door_wooden_panel_03_painted_left_mp" > physicsasset`; the
package rebuilt with it still crashed (installed 20:32, crashed 20:33): other chains of other types remained. The
second fix counts every type (see below). Stock zones list every asset before its users, so a stock asset only
points at earlier list entries; the carried scriptables (doors, cars) were written with their models, effects,
materials and animations inline.

## Fix (converter, iw8radiant/scriptables)

`write_zone` runs its collecting pass, records every list entry that loads an asset of any type three deep inline,
lists the middle asset just before that entry, and collects again until none is left; the writing pass refuses a
zone that would still nest three deep. The log says `zone writer: N asset(s) listed before their users` and names
each chain.
