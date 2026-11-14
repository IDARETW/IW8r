# 116. Why a patch of a stock zone stops the game, and how an edited stock zone is installed instead

**Why.** The owner installed the toon Shipment patch of research 115 (`techsets_mp_shipment.fp`, copied into the
game's `zone` folder) and the game stopped in the pre-game lobby with
`Disc read error [3.1]: 'techsets_mp_shipment.fp'`. Every stock edit of this editor was installed as such a patch
file (research 88), and none had been run in the game before. This note finds the cause in the game's code and
records the install method that replaces it.

**Answer in one paragraph.** The game's stock zones are signed (`IWffa100`). The game decides whether a patch file is
signed from the copy of the zone's header inside the patch, and that copy must equal the zone's real header: so the
patch of a signed zone is always read as signed data, which must start with a block signed with the game's own key.
Our patch holds unsigned data, the first block fails the check, and the reader reports error 3 with code 1. A valid
signature cannot be made. The owner's client does load whole **unsigned zone files** under any name, stock names
included. So an edited stock zone is now installed as the whole unsigned zone in place of the stock file; the stock
file (and its stock patch) is moved to `<game>\iw8r_stock` and moved back on restore. **Not seen in game.**

**Labels.** VERIFIED = read in the game's code (IDA database of `game_dx12_ship_replay.exe`, names from its symbols)
or measured on files. INFERRED = follows from those, not run in the game.

## 1. What the error message says (VERIFIED)

`DB_AsyncIWFileLoad_DirtyDiscError(fileLoad, a, b)` (0x140D86F00) raises `Disc read error [a.b]: '<file>'`. Eight
places call it:

| a | Caller | When |
|---|---|---|
| 1 | `DB_AsyncIWFileLoad_ReadBufferFinish` (0x140D871E0) | the file is not signed (b = 0) |
| 2 | `DB_AsyncIWFileLoad::BatchRead` | a read request could not be queued |
| 3 | `DB_AsyncIWFileLoad_Update` (0x140D87571) | **b = the result of `DB_AsyncIWFileLoad_PumpDecodeQueue`** |
| 4 | `DB_AsyncIWFileLoad_Update` | a read buffer failed |
| 6 | `DB_BinaryPatch_CopyDataInternal` | the patch copy ran out of data |
| 7, 8 | `Shutdown`, `WaitForAsyncCompletion`, `ReadHeader` | an asynchronous read failed |

`DB_AsyncIWFileLoad_PumpDecodeQueue` (0x140D87E40) returns 1 in two cases: for an unsigned file, when the decoder
fails (`DB_CmpLoad_Decompress`: the data must start with a type byte and `IWC`, type 1 stored or 2 in blocks); for a
signed file, when a 0x4000-byte block fails `DB_AsyncIWFileLoad_VerifyChunkHash` or the decoder fails after it.

## 2. A signed file's first block (VERIFIED, `DB_AsyncIWFileLoad_VerifyChunkHash`, 0x140D87AF0)

- bytes 0..7 are `IWffs100`;
- bytes 12..43 are the SHA-256 of bytes 300..6511;
- bytes 44..299 are an RSA signature of that hash, checked with the game's public key
  (`DB_AsyncIWFileLoad_VerifyChunkSignature`, `rsa_verify_hash_ex`);
- bytes 300..363 are the file's name without its extension;
- the rest of that range holds the SHA-256 of each following block group, and every later 0x4000-byte block is
  checked against them.

Only the holder of the private key can write such a block.

## 3. Which files count as signed (VERIFIED, `DB_ReadFastfileHeaderData`, 0x1411A7D30)

- `DB_CheckFastfileHeaderVersionAndMagic(header, file)` sets the file's signed flag to "the header's magic is
  `IWffa100`" (`IWffc100` is the unsigned magic; anything else is error 6033).
- For a zone file the header is its own first 0x88 bytes.
- For a patch file (`IWffd100`, header version 6, diff version 4) the function is called twice on the same file: first
  with the target header (at 0xC0), then with the base header (at 0x38). **The base header decides.** The base header
  must also equal the zone's real header byte for byte (error 6042 otherwise).
- So: a patch of a signed zone is signed data, always. All 38 stock patch files with data in the game's zone folder
  start their data with `IWffs100`; the other 110 stock patch files hold headers only (no data is read from them).
  The one file with unsigned data on a signed base was ours.
- The patch suffixes are tried in the order `.fp`, `.fc`; a second patch must start from the first one's target
  header (error 6041).

## 4. What the owner's client accepts unsigned (VERIFIED, its source `integration/replay/DB_ResetFileReader.cpp`)

The client replaces `DB_AsyncIWFileLoad_ReadBufferFinish`. For a file whose signed flag is 0 it starts the unsigned
decoder instead of raising error 1 when:

- a `.ff` file has the magic `IWffc100`, header version 11, zone version 0xFF7, no compression or encryption flag,
  transient type 0 or 1, and equal decoded sizes that are not 0 (one more rule covers the merged collision zone
  `*_cg_ls_tr.ff`); the file's name does not matter;
- a `.fp` file is a patch whose base **and** target headers are such unsigned headers.

The client's own files use this: `mw120r_map_images.ff` with its `.fp`, `techsets_mp_frontend6.ff`,
`mp_donetsk_cg_ls_tr.ff` (1 GB, a stock name) are all unsigned zones in the zone folder. The converter writes its
zones with exactly this header (`out/agent/toon_ship/edit/techsets_mp_shipment.ff`: `IWffc100`, 11, 0xFF7, flags 0,
both decoded sizes 0xBF1C3A).

A patch of a signed stock zone cannot pass: its signed flag is 1 (section 3), the client leaves the signed reader in
place, and section 2 applies.

## 5. The install method now (core `stock_install`, editor File > Install Edited Stock Map Into Game)

- The rebuild's whole zones (`<work>/out/<zone>.ff`, `techsets_<zone>.ff`) are installed; the converter is asked for no
  patch files (`"patch": false`).
- Before anything is changed, every source file is checked with the client's rule (`unsignedReplayZone`).
- The stock `<zone>.ff` is moved (a rename on the same drive: no copy, hard links stay) to `<game>\iw8r_stock\`. The
  stock `<zone>.fp` goes with it: it belongs to the stock header only and would stop the load of any other file
  (error 6042).
- A patch file with unsigned data on a signed base (`unsignedPatchOfSignedZone`: ours, from before this note) is
  removed, not kept. Patch installs recorded by the old method are taken out first.
- The edited zone is copied to `<game>\zone\<zone>.ff`; a marker in the editor's backup folder records it.
- File > Restore Stock Map In Game deletes our zone and moves the stock zone and patch back. When the kept stock
  file is missing, nothing is changed for that zone and the message says so.
- While an edit is installed, the editor opens the stock map from the kept file (`stockZoneForReading`), and a rebuild
  tells the converter to read it there: editzone `"stock_zones": {"<zone>.ff": "<kept file>"}` (x64-zt 5bb98fb9,
  `xfile::use_zone_file`; the patch beside the kept file is the one applied).
- With no rebuild in the session, the install offers a folder dialog: the `out` folder of an earlier rebuild can be
  installed (for the Shipment toon zone: `out/agent/toon_ship/edit`).

Checked offline: core tests (install, second install, restore, refusals, old-method cleanup, a missing kept file,
the stray patch); the converter with `stock_zones` pointing at a copy of the stock techset zone writes the same
Shipment toon zone byte for byte, while the game folder still held our old patch beside the stock zone.

## 6. Limits and open questions

- **In game (INFERRED, not run):** that the client loads an unsigned zone under the name of a stock map's zone as it
  loads its own unsigned zones (section 4 shows no rule that depends on the name, and three stock-named unsigned
  zones exist).
- The unsigned rule wants the decoded size below 4 GB. Measured on the stock zone headers (VERIFIED): the largest map
  zones are mp_donetsk 573 MB, mp_br_quarry 571 MB, mp_aniyah_tac 390 MB, mp_shipment 385 MB; common_mp is 820 MB.
  All fit.
- A stock zone loads only part of its data at once (mp_shipment: 195 of 385 MB in the header's always-loaded size).
  An unsigned zone must declare all of it always loaded, so an edited map zone holds more memory while its map runs
  (INFERRED from the two header fields; not measured in game).
- **Streamed tiles cannot be replaced.** The client takes an unsigned zone only with transient type 0 or 1; the
  Warzone tiles (`mp_donetsk_NNNNN_tr.ff` and the like) have types 2 to 13. An edit that would have to change a tile
  (taking out streamed world geometry, plan 2.3) has no install route without a client change.
- The installed copy needs the edited zone's size on the game's drive (the stock file itself is only moved).
- A patch route would need a client change (accept unsigned data for a patch whose target header is unsigned). That
  is the owner's decision and build; nothing here depends on it.
- The pipeline-state file `main\techsets_<map>.psob` of a stock map does not hold the new hashes of a filtered techset
  zone (research 115, section 6): the game builds them at load, as for a converter-built map. Not seen in game.

## 7. Seen in the game (2026-11-14)

The owner ran the Shipment toon zone (`out/agent/toon_ship/edit/techsets_mp_shipment.ff`) in the game twice.

1. **With our old patch file still beside it** (the edited zone copied in by hand, `techsets_mp_shipment.fp` of the
   first attempt left in the `zone` folder): the map loaded, the world was invisible, the minimap showed the engine's
   default pattern, effects and the weapon were drawn. No error was shown. The cause is the game's check 6041
   (section 3): the patch's base header is not the header of the zone beside it, so the zone is refused, and none of
   its 945 materials exist. Shown offline: ACTS, which uses the game's loader logic, refuses exactly that pair of
   files ("The patch file is not for this fast file") and loads the zone alone with 0 failed.
2. **Without the patch file**: Shipment loads and shows the filter (the light in steps, stronger colours).

So, VERIFIED in the game: the client loads a whole unsigned zone under a stock map's zone name; the engine builds
the pipeline states of the new hashes at load although `main\techsets_mp_shipment.psob` is the stock one (section 6
had both as INFERRED). And the install rule of section 5 that removes a patch beside the zone is necessary, not
tidiness: a hand copy that leaves it gives an invisible map without any message.

The install is also a command now, for a deploy without the editor's window:
`IW8Radiant.exe --install-stock <folder with the edited zones> [--out <report folder>]` and
`IW8Radiant.exe --restore-stock <map>` (the same code and folders as the two menu commands; the report
`stock-install.txt` lists every file removed, moved and installed). The zone above was installed with it on
2026-11-14: the stock zone is in `<game>\iw8r_stock\`, and a second copy of the stock zone and of the removed patch
is in `out/agent/toon_ship/game_backup_20261114/`.
