# 141. Sound alias audio: from a stock alias to playable samples

Scope: research for previewing a stock Replay 1.20 sound alias in the Sounds panel. Read-only on the game folder; no
code was changed. Everything below was run on this machine on 2026-11-15 unless marked otherwise. Built afterwards by
another session: `src/qtapp/sound_preview.{h,cpp}` (section H compares it with these findings).

Sources:

- **ACTS**: `E:\mw124\atian-cod-tools`, `src/core/acts/tools/mw19/mw19_sound.cpp` (SAB reader, `RestoreFlac`),
  `mw19_sound_cli.cpp` (`mw19soundtest`), `mw19_payload.cpp` (sound-bank payload export, error at line 166),
  `fastfile/handlers/handler_game_mw19_replay.cpp` (the 128 MiB payload budget, line 821), `docs/mw19/README.md`
  ("Sound banks and FLAC samples"), schema `build/bin/Release/data/mw19/schema.json` (types `SndBank`, `SndAliasList`,
  `SndAlias`, `SndBankStreamInfo`).
- **Game files** (read only): `E:\IW8\Builds\1.20-replay\zone\common_mp.ff` / `.fp` / `.sabs`, `patch_common_mp.sabs`,
  `english\eng_common_mp.sabs`, `code_post_gfx.*`, `mp_shipment.*`.
- **Scripts** (read-only Python, kept): `E:\IW8-Radiant\out\agent_sound\py\` - `sab.py` (bank index), `alias_map.py`
  (alias -> bank entry), `sk_to_sab.py` (stream-key JSON -> bank), `extract.py` (one alias -> .flac + .wav),
  `decode_all.py` (decode every entry). Reference pairs: `out\agent_sound\samples\` (section D).

## A. Alias -> key -> bank entry

1. The zone's `soundbank` asset (`SndBankResident` = `SndBank`, 512 bytes; common_mp's is `common_mp.all`) holds
   `alias` = `SndAliasList[aliasCount]` (32 bytes: `aliasName`, `id`, `head` -> `SndAlias[count]`).
2. Each `SndAlias` variant (232 bytes) has `assetFileName` (+24, a string such as
   `level\mp_hackney\emt_smokestack_hackney.SL.75.44100.all`) and `assetId` (+40, u32). Also `secondaryAliasName`,
   `volMin/Max`, `distMin/Max`, `flags`, `probability`.
3. `assetId` is the **key** of an entry in a SAB v10 bank. Checked on common_mp: 79,253 of the 79,285 variants that
   have an `assetFileName` resolve to a bank entry by key (the other 32: section E). No variant without an
   `assetFileName` resolves.
4. `SndBank.streamInfo` (+496) `loadedStreamKey` names the loaded bank's StreamKey (common_mp: `common_mp_all_loaded`).

The two test aliases:

| Alias | Key | assetFileName | Bank, entry | Format |
|---|---|---|---|---|
| `2pop` | `0x8fbf12b2` | `level\test_tones\pop1k.LN.75.44100.all` | loaded bank `common_mp.all` (inside common_mp.ff), entry 0 | 44,100 Hz mono, 1,322 frames, no loop |
| `emt_smokestack_lp` | `0xd9ef4063` | `level\mp_hackney\emt_smokestack_hackney.SL.75.44100.all` | `zone\common_mp.sabs`, entry 28 | 44,100 Hz stereo, 992,808 frames (22.5 s), loop |

### The type letters in assetFileName

The fourth-from-last dot field (`LN`, `SL`, ...) matches where the sound is, on every common_mp variant:

| Letter 1 | Meaning (from where the key is found) | common_mp variants |
|---|---|---|
| `L` | loaded bank only | LN 77,191, LL 200 |
| `S` | streamed `.sabs` only | SN 1,093, SL 78 |
| `P` | "primed": the loaded bank has a FLAC **prefix**, the full sample is in a `.sabs` | PN 581, PL 14 |
| `H` | loaded bank, entry with `hybridPcmBytes` 1024 | HN 96 |

Letter 2 is the bank entry's loop flag (`L` = looping, `N` = not): 100% agreement.

## B. SAB v10 bank layout

Header (688 bytes, little endian; offsets as `mw19_sound.cpp` reads them, values checked on the common_mp banks):

| Offset | Field | Value |
|---|---|---|
| 0 | magic | `0x23585532` (`2UX#`) |
| 4 | version | 10 |
| 8 | index entry size | 44 |
| 12 | checksum entry size | 16 |
| 20 | entry count | |
| 28 | (ACTS: "build") | 16 |
| 32 | file size (u64) | equals the file size |
| 40 | index offset (u64) | |
| 48 | checksum table offset (u64) | |

Index entry (44 bytes): +0 key, +4 size (encoded bytes), +8 seekBytes, +12 frames (per channel), +16 hybridPcmBytes,
+20 offset (u64), +28 sample rate, +32 channels (u8), +33 looping (u8), +34 format (u8). The encoded sample is
`size` bytes at `offset + seekBytes`; `[offset, offset + seekBytes)` is a seek table (not decoded here). For hybrid
entries the `hybridPcmBytes` follow the FLAC data. ACTS notes that a fastfile-resident index can be in storage order,
not sorted by key (`mw19_sound.cpp`, comment before the sort); a lookup should hash or sort.

## C. Codec

**Every entry is format 8 = FLAC frames without the `fLaC` marker or STREAMINFO.** Checked by header on all entries:

| Bank | Entries | Formats | First frame of every entry |
|---|---|---|---|
| loaded `common_mp.all` | 13,859 | 8 only | sync `FFF8` (fixed blocking), block code 10 (1,024 samples), 16-bit, frame 0 |
| `common_mp.sabs` | 1,500 | 8 only | same |
| `patch_common_mp.sabs` | 126 | 8 only | (decoded, not surveyed by header) |
| `code_post_gfx.all` + `.sabs` + `patch_` | 167 keys used by aliases | 8 only | (decoded) |

- common_mp.sabs rates: 44,100 (1,240), 36,000 (168), 48,000 (80), 22,050 (9), 32,000 (3); channels 2 (1,042), 1 (458).
- No Ogg, Opus, XMA, ADPCM or raw PCM bank entries were seen.
- **Hybrid entries** (87 in the loaded bank, `hybridPcmBytes` 1024): the FLAC part alone decodes to every declared frame
  (87 of 87, counted from frame headers and with libsndfile); the 1,024 PCM bytes are an exact copy of the first 256
  decoded frames (16-bit stereo), checked on the first one.
- **A playable file** = `fLaC` + one STREAMINFO block (42 bytes: min/max block 1024, the entry's rate, channels, 16-bit,
  the frame count, MD5 zero) + the frames unchanged, as ACTS `RestoreFlac` does. The files written by `extract.py` are
  byte-identical to ACTS's validated payload: `emt_smokestack_lp.flac` 1,324,130 bytes CRC32 `bd3166af`,
  `2pop.flac` 1,014 bytes CRC32 `26c87ebb` (`acts.exe --noUpdater mw19soundtest <bank> <key> -o report.json`, which
  checks the frame headers, CRC-8 / CRC-16 and the sample count; 0.15 s each).
- Decoded with libsndfile (below): `2pop` = 30 ms, peak 21,157, zero crossings at about 984 Hz (the file is "pop1k");
  `emt_smokestack_lp` = 22.5 s stereo, RMS 5,192.

## D. Decoders on this machine

- `ffmpeg` / `ffprobe`: not installed (`where ffmpeg`).
- **libsndfile**: `E:\mw124\atian-cod-tools\build\python\_soundfile_data\libsndfile_x64.dll` with pysoundfile 0.14.0
  (`E:\mw124\atian-cod-tools\build\python`, the test-only dependency in ACTS `docs/mw19/README.md`), run with
  `py -3.14 -I`. Used for every decode check in this report.
- Not found under `E:\mw124`, `E:\IW8-Radiant\third_party`, `E:\Qt`: `dr_flac.h`, libFLAC, any Opus library.
  `E:\mw124\mw120rproxy\_research_kisakcod\deps\dr_libs` holds only `dr_wav.h` and `dr_mp3.h`; `stb_vorbis.c` copies
  are Vorbis only; `avcodec-61.dll` exists only inside PyQt6 virtual environments under `E:\mw124\mw164proxy`.
- Qt 6.8.3 (`E:\Qt\6.8.3\msvc2022_64`) has **no Qt Multimedia** (no `Qt6Multimedia.dll`, no `plugins\multimedia`).
- Reference pairs for a decoder test (libsndfile output, 16-bit WAV with a 44-byte header), in
  `E:\IW8-Radiant\out\agent_sound\samples\`: `2pop`, `emt_smokestack_lp`, `exec_shocker_09` (a `PN` sound, full
  sample from common_mp.sabs entry 1252, 125,649 frames) and `mp_hit_tank_3d` (an `HN` hybrid entry, loaded bank
  entry 4754, 48 kHz stereo, 33,367 frames).

## E. common_mp's banks, lookup order and coverage

| Bank | Where | Bytes | Entries |
|---|---|---|---|
| loaded `common_mp.all` | **inside common_mp.ff**: StreamKey `common_mp_all_loaded`, `residentDataGPU` (xpakInfo key 0, so no XPak) | 702,136,704 | 13,859 |
| `zone\common_mp.sabs` | on disk | 1,838,915,072 | 1,500 |
| `zone\patch_common_mp.sabs` | on disk | 256,708,352 | 126 |
| `zone\english\eng_common_mp.sabs` | on disk | 4,096 | 0 |

- Shared keys: common_mp.sabs and the loaded bank 482 (358 with the same size and frames), patch and loaded 44,
  common_mp.sabs and patch 6.
- **Order: `patch_common_mp.sabs`, `common_mp.sabs`, then the loaded bank.** Loaded-first is wrong: 137 loaded entries
  (all `PN` / `PL`) are a FLAC prefix of the `.sabs` sample (example `0x7e5fee14`: 43,119 bytes in the loaded bank,
  55,247 in the .sabs, the same 75,728 frames, the loaded bytes equal to the start of the .sabs bytes). ACTS
  `mw19soundtest` on such a key fails "FLAC frame count does not match sound-bank entry"; libsndfile loses sync. All
  526 `P` keys of the loaded bank are also in a .sabs.
- With that order: **14,873 of the 14,880 distinct keys** named by common_mp aliases decode to their full declared
  length with libsndfile, 0 fail, 7 are in none of these banks; decoding all of them in memory took 37.6 s.
- Aliases: 11,304 in `common_mp.all` (exactly the 11,304 `common_mp` rows of `data/asset_catalog/iw8_common_sounds.tsv`).
  10,029 have at least one playable variant; 1,249 have no `assetFileName` on any variant (nothing to play, e.g.
  `beam`, `accent_plr_bells`); 26 name a file whose key is not in the common_mp banks (e.g. `hit_marker_3d`,
  `ui_br_map_open`).
- The other 403 catalog aliases are not in `common_mp.all` (230 `code_post_gfx`, 173 `common_base_mp`).

## F. ACTS runs and timings

`G=E:/IW8/Builds/1.20-replay`, base command
`acts.exe --noUpdater fastfile -r mw19replay -g $G/game_dx12_ship_replay.exe --oodle $G/oo2core_7_win64.dll`.
Timings are wall clock with the zone files probably in the OS cache.

| Run | Result | Time |
|---|---|---|
| `-a soundbank -o <out> $G/zone/mp_shipment.ff` | `mp_shipment.all.0.sabl`, 36,106,112 bytes, 223 entries | 0.63 s |
| same for `code_post_gfx.ff` | `code_post_gfx.all.0.sabl`, 12,406,912 bytes | 0.29 s |
| same for `common_mp.ff` (with or without the 14 populated `--xpak`) | **failed** `Invalid payload count` | 1.3-1.8 s |
| `ACTS_REPLAY_STRUCTURED=1` + `-a soundbank` on common_mp | `common_mp.all.0.asset.json`, 116,904,445 bytes (aliases, section A) | 8.3 s |
| same on mp_shipment / code_post_gfx | 24 MB (887 alias lists, 16,961 variants) / 0.6 MB | 1.9 s / 0.29 s |
| `ACTS_REPLAY_STRUCTURED=1` + `-a streamkey -n common_mp_all_loaded` on common_mp | 1,404,274,590-byte JSON holding the bank as one hex string; `sk_to_sab.py` writes the 702 MB bank in 1.7 s | 7.4 s |
| `-d --noAssetDump -a soundbank` on common_mp | `common_mp.ff.dec` 820,155,826, `common_mp.fp.dec` 57,196,321, `common_mp.fp.patched.dec` 820,155,826 bytes | 10.5 s |
| `mw19soundtest <bank> <key> -o r.json` | report only (no FLAC written), validates one sound | 0.15 s |

- **Why common_mp fails**: the soundbank payload export is given a 128 MiB budget
  (`handler_game_mw19_replay.cpp:821`); the resident bank is 702,136,704 bytes, so `Reader::Count`
  (`mw19_payload.cpp:166`) throws "Invalid payload count". The same cause explains the `streamkey common_mp_all_loaded`
  failure in research 11. A larger budget in ACTS would let `-a soundbank` write the bank directly (not tried).
- **The loaded bank in the zone dump**: exactly one valid SAB v10 header in each `.dec`, at offset **78,740,312**, size
  from header +32; the bytes are identical in `common_mp.ff.dec` and `common_mp.fp.patched.dec` and identical to the
  stream-key JSON copy (CRC32 `f4a1a2c9` for all three).
- In mp_shipment's own bank, 263 of its 16,961 variants resolve to the shipment banks (246 loaded, 17 in
  `mp_shipment.sabs`); most of the rest name sounds of common_mp's banks (not followed further).
- code_post_gfx: 203 variants with a file, all 203 resolve (188 loaded, 8 `code_post_gfx.sabs`, 7 `patch_`); all 167
  distinct keys decode. `common_base_mp` has no `.sabs` on disk; not tested.

Per alias, after the one-time steps: index lookup + read + header about 0.01 s, libsndfile decode 0.16-0.18 s
(including its load), 0.27-0.29 s per `extract.py` call including Python start-up.

## G. End-to-end recipe (what was run)

1. Aliases: `ACTS_REPLAY_STRUCTURED=1` + `-a soundbank` on `<zone>.ff` -> alias, variant `assetFileName`, `assetId`.
2. Loaded bank: `-a soundbank` (small zones) or, for common_mp, the bank copied from the `-d` dump at its `2UX#`
   header (or from the structured stream-key JSON).
3. Per alias: `assetId` -> first hit in `patch_<zone>.sabs`, `<zone>.sabs`, loaded bank -> `size` bytes at
   `offset + seekBytes` -> STREAMINFO header -> FLAC decode -> 16-bit PCM.
   `py -3.14 -I py\extract.py <map.tsv> <alias> <out> <zone>\patch_common_mp.sabs <zone>\common_mp.sabs common_mp.all.sabl`.

## H. Against `src/qtapp/sound_preview.cpp` (as read on 2026-11-15)

Agrees with the findings: the bank order (patch .sabs, .sabs, loaded); format 8 only; hybrid entries allowed and only
`size` bytes read (the PCM copy is skipped); `maxFrames` = the entry's frames; the alias scan keeps only variants with a
non-null `assetFileName` and a non-zero `assetId` (the same rule as section A); the FLAC decoder's block-size, rate,
channel-decorrelation, wasted-bits, residual-escape and LPC-precision rules match RFC 9639; the structured
`-a soundbank` and `-d --noAssetDump` steps are the runs timed in section F.

Notes and possible conflicts (none changed):

1. `extractLoadedBank` copies the bank out of `<zone>.ff.dec`, the **unpatched** dump. For common_mp this is safe (the
   bank is byte-identical in `.ff.dec` and `.fp.patched.dec`). For another zone whose `.fp` changes the bank this was
   not checked; `.fp.patched.dec` is what the game loads.
2. The `-d` step writes about 1.7 GB (`.ff.dec` + `.fp.dec` + `.fp.patched.dec`) into the cache folder before the
   702 MB bank is copied and the dump removed: about 2.4 GB free space is needed at the peak.
3. `play()` is always called with zone `common_mp`, so the 403 catalog aliases of `code_post_gfx` and `common_base_mp`
   report "no sound data". code_post_gfx would work with the same steps (its plain `-a soundbank` export succeeds).
4. Frame CRCs are not checked. All stock entries used by common_mp aliases decode cleanly, so this matters only for
   damaged files.
5. If a zone's `.sabs` were missing, a `P` sound would decode from its loaded prefix, stop early and still play
   (`decodeFlacFrames` returns true when any frame decoded). Not the case for common_mp (all `P` keys are in a .sabs).
6. The variant is picked uniformly at random; `SndAlias.probability` exists and how the game weights variants was not
   checked.
7. The `-d` run in sound_preview has no `-a soundbank`; its time without it was not measured.
