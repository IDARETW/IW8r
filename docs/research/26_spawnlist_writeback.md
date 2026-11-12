# 26. Writing spawnList back into a stock MapEnts (M2.2)

Evidence for roadmap 6.1 / M2.2. Target: MW2019 Replay 1.20.4.7623265. x64-zt was read only (not built or edited).
Stock dump: `out/m2-x64zt/dump/mp_shipment/map_ents/maps/mp/mp_shipment.d3dbsp.iw8` (3,185 records, 985,564 bytes).
Paths below are relative to `E:\mw124\x64-zt` unless stated otherwise.

## 1. In-memory layout

- `MapEnts` is 0x408 bytes. `spawnList` is at +0x128, 16 bytes
  (`src/games/iw8/structs.hpp:10937`, `static_assert` at `structs.hpp:22848`; `src/games/iw8/iw8_map_structs.h:206-214`).
- `SpawnPointRecordList` is `{ uint16 spawnsCount; SpawnPointEntityRecord* spawns; }` (`structs.hpp:10378-10382`,
  asserts `structs.hpp:22521-22524`). So the count is the u16 at MapEnts +0x128 and the pointer is at +0x130.
- **Conflict:** `iw8_map_structs.h:209-212` declares `{ uint32 count; uint32 _pad; void* records; }`. Both agree on the offsets
  and on the low 16 bits of the count. `structs.hpp` is the one with static asserts, and the stock dump agrees with it.
  This report uses `structs.hpp`.
- `SpawnPointEntityRecord` is 40 bytes (`structs.hpp:10368-10374`, asserts `structs.hpp:22514-22520`):

| Offset | Size | Field | Note |
|---|---|---|---|
| 0x00 | u16 | `index` | position in the array |
| 0x02 | u16 | (padding) | not a field in the struct; zero in stock; kept raw |
| 0x04 | u32 | `name` | `scr_string_t`, script string id |
| 0x08 | u32 | `target` | `scr_string_t` |
| 0x0C | u32 | `script_noteworthy` | `scr_string_t` |
| 0x10 | float[3] | `origin` | |
| 0x1C | float[3] | `angles` | pitch yaw roll |

- The Replay loader reads the same thing (`src/games/iw8/zone/replay_spawns.h:1-24`): "ushort count, pointer at +8", and
  `ReplaySpawnRecord` is 40 bytes with `uint16 index, padding; uint32 name, target, noteworthy; float origin[3], angles[3]`.
- `name` is the entity **classname** (key 212), not the targetname. `replay_spawns.h:41-69` builds spawns from the entity
  string: classname starts with `mp_` and contains `_spawn`; `target` from key 1070, `script_noteworthy` from key 845,
  `origin` from key 709 (required), `angles` from key 80. `index` is the position. The same key ids are in
  `src/core/map_text.cpp:447-459` (targetname is key 1072 per research 13 and the stock ents).

## 2. Dump container (x64-zt IW8D)

- Record kinds (`src/games/iw8/zone/streams.hpp:345-357`): 0 null, 1 data, 2 runtime, 3 offset, 4 string, 5 asset,
  6 script_string, 7 temp. The enum matches `src/core/iw8_dump.h`.
- Traversal order is the stream order (`src/games/iw8/zone/asset_streams.cpp:3054-3067` and `3495`).
  `stream_spawnpointrecordlist` does:
  1. `inline_array(data.spawns, 3, count)`, where the count is the u16 at +0x128. It emits **one data record** holding
     all `40 * N` bytes of the array (`streams.hpp:623`, `add_data`).
  2. For each spawn in order, `script_string(name)`, `script_string(target)`, `script_string(script_noteworthy)`.
     Each emits one **script_string record** (kind 6; `streams.hpp:533-544`). Empty text is kind 6 with 0 bytes. Stock has no kind 0 in the block; the null branch (`streams.hpp:536-541`) is unverified for spawns.
- `dump_stream::clear_field` (`streams.hpp:607`) zeroes every field that holds a script id or pointer inside the
  already-copied data record. In the dump, the name, target and noteworthy u32s are therefore 0, and the text lives in the
  script_string records. The origin and angles and index are stored as-is.
- Offset records (kind 3) point at an earlier record by **record index** (`streams.hpp:659-675`, `record.value = it->second`).
  A record can only point backwards.
- Record kinds for the header: record 0 is the 1032-byte header (data), record 1 is runtime (value 0), record 2 is the
  entity string (data, 62,892 bytes = numEntityChars).

### Stock mp_shipment, measured (`probe` in the scratch folder, Python 3, read only)

- Header: `spawnsCount` (u16 at +0x128) = 611. The pointer at +0x130 is 0. Spline count at +0x138 = 0.
- Spawn data record: index **163**, 24,440 bytes (= 40 × 611). Then records 164..1996 (1,833 = 3 × 611) are all kind 6
  (script_string). 1,210 of them are empty strings.
- Records 0..162 are unaffected. Records 1997..3184 (1,188 records) follow the block.
- Offset records: 96 in total. None point into the block (164..1996) or to any record before index 163. Those after the block
  point at records after the block (for example 2766 -> 2764).
- Spawn 0: index 0, name `mp_dm_spawn_start`, origin (-904, 2016, 36), angles (0, 269.999, 0). Spawn 1: index 1, origin
  (-808, 2000, 32). Spawn 2: index 2, origin (-340, 2372, 40).
- ACTS `loadActsMapEnts` on the same zone reports `spawnsCount` 611 and 611 values with the same indices, names and origins
  (`out/m2-script-check/acts_ents/.../mp_shipment.d3dbsp.0.asset.json`).

## 3. Important: stock spawns are NOT in the entity string

- `out/m2-x64zt/shipment_stock.ents` (the 62,892-byte entity string) has 611 entity blocks, but none has classname
  `mp_*_spawn*`. The string `spawn` appears only in `worldspawn`. The spawns exist only in `spawnList`. This agrees with
  `docs/research/08_x64zt_iw8_conversion.md:135` and with `docs/research/13...` ("stock IW8 keeps them in spawnList").
- Consequence for the editor. An edit to `spawnList` is a separate write from the entity string. The M5 extraction path
  (`map_zone.h:1343-1366`) regenerates `spawnList` from `mp_*_spawn*` entities in the `.ents` text. That path does not
  apply to a stock dump. The dump path (`src/games/iw8/zone/asset_files.cpp:433`, `parse_stream`) uses the dumped records.

## 4. Writing the list back (design)

- The dumped records are a sequence, so the list can change length. There are three constraints:
  1. The count is a u16 in the header (max 65,535).
  2. The 40-byte array is one data record of exactly `40 * N` bytes, so it grows and shrinks with `N`.
  3. The script_string records are exactly `3 * N` in order, so they grow and shrink too.
- When the block grows by `3 * (M - N)` records, every offset record whose target index is at or after the block end must
  move by the same amount. Offsets into the block cannot exist (they would reference an array that is not a whole record),
  so a write is refused if one does.
- The block is located by search: the first data record with `40 * N` bytes whose index field at `40 * i` equals `i`, and
  whose following `3 * N` records are kind 6 or null. When `N` is 0 the block cannot be found, so an insert is refused.
- Untouched spawns keep their 40 bytes and their three script records byte-identical. A changed spawn gets its 40 bytes
  rewritten (index = position, name/target/noteworthy = 0, origin, angles, padding kept) and three new kind 6 records.

## 5. Result

- Implemented in `src/core/spawnlist.{h,cpp}`, CLI `iw8r iw8d-spawns` in `src/cli/spawns_cmd.cpp` (wiring into `main.cpp`
  is pending), tests in `tests/spawnlist_tests.cpp`.
- `iw8r_tests`: 19,240 checks, 0 failures, 222 tests. The mp_shipment cases: 611 spawns with 0 mismatches against the ACTS
  decode; write-back of the unchanged list is byte-identical; moving spawn 5 by +16 X changes one byte of its origin.x in the
  data record and nothing else; adding a spawn re-points the 96 offsets behind the block with no wrong target, and removing it
  restores the stock bytes.
- Not verified in game, and the rebuilt zone was not run through the M2 pipeline. `tools/m2_rebuild_mapents.ps1` only writes
  the entity string, so a spawn test needs a separate script.
