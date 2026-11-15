# cg_t in Replay: how it is reached and how its layout was recovered (2026-11-15)

## How Replay reaches cg_t

1. **Pointer table only.** `CG_AllocLocalClientGlobals` (0x1417CA150) allocates `677040 * clientCount` bytes from a hunk,
   runs `cg_t::cg_t` on each block, sets a vtable at +0 and stores the block in `LocalClientGlobals[i]`
   (0x14F26F940, one slot in this build). Code reaches the struct only as `LocalClientGlobals[localClientNum]->...`
   (2238 references in 1685 functions); nothing else holds a typed pointer to it.
2. **Subclass `this`.** The 677040-byte block is a `CgGlobalsMP` / `CgGlobalsSP`, which derive from `cg_t` (599712).
   Their methods reach cg_t members through `this` (e.g. `CgGlobalsSP::GetCharacterInfo` = `this + 662704 + 13720 * i`).
3. **Self-keyed "secure" members.** Pose origins (`cpose_t.origin` / `prevOrigin`) are obfuscated:
   `CG_ViewMP_InitRandomOffsets` (Replay; game-test calls it `cg_t::ResetLocalClientGlobalsObufscation`) picks one of
   34 decoders (`% 0x22`) per member with `SetObfuscatedFunction`, and stores its pointer XOR-ed with the member's own
   address and a global key: `fn = *(cg + X) ^ (cg + X + 8) ^ s_comObfuscationKey126/128`; the payload is at X + 16
   and the "precise" variant stores the origin * 4096 as ints. Code calls through the decoded pointer, so these
   members are only ever seen as `lea` addresses and qword reads. The same function pins the four poses in cg_t:
   origin blocks at 102424 / 102704 / 103024 / 103384 = `pose + 0x20` -> poses at 102392 (`viewModelPose`),
   102672 (`viewModelPoseLeftHand`), 102992 (`firstPersonLegsPose`), 103352 (`firstPersonHelmetPose`).

`LocalClientGlobals` is now typed `cg_t *LocalClientGlobals[1]`, so decompiles read
`LocalClientGlobals[localClientNum]->inKillCam` instead of raw offsets.

## How the layout was recovered (tools/ida_types)

1. `scan_struct_access.py` disassembles the exe file (byte-identical to the IDA input) with capstone, outside IDA.
   A register is a cg_t pointer when it is loaded from the table, is `this` in `cg_t::` / `CgGlobalsMP::` /
   `CgGlobalsSP::` methods, is the return value of an accessor (`--returns`, game-test only), or was passed in from a
   tracked caller (3 levels). It follows copies, spills, `lea` / `add` sub-pointers, and resolves incremental-link
   thunks (`--thunks`, game-test). Every `[reg + disp]` is recorded with width, read / write / address, function.
   Replay: 2715 offsets; game-test: 3155.
2. `align_by_function.py` aligns, for each of the 282 functions both builds share, the two instruction-ordered access
   lists (Needleman-Wunsch on width and kind). Each aligned pair votes that a game-test field (named by the game-test
   PDB) starts at a Replay offset; a weighted increasing chain keeps fields in game-test order. 58 fields placed; the
   chain's final delta is -207888 = 599712 - 807600 exactly.
3. `build_layout.py` lays the other game-test fields out from each anchor in order, with the IDA local-type sizes
   (already corrected against Replay) and natural alignment; a residual at the next anchor is absorbed by the largest
   field of unknown or wrong size (it becomes a named byte array), never by a type whose Replay size is proven.
   Gaps with a small residual and several candidates are left as pads. Base types (float / int / bool / ...) come from
   the game-test type dump `E:\1-game-test\types.txt`.

Checks against independent evidence (all match): `m_mlgSpectatorPtr` +599688 (`cg_t::cg_t` store),
`m_weaponHand` +599608 (A036), `xCam` +0x74B8 (A040), `refdef` +0x6800 (A036's `g_activeRefDef`), `coneTargetingState`
+290368 (A037), `patternCacheList` +534512 (A039, `cg_t::cg_t`), the four poses above, and `predictedPlayerState`
= 21368 bytes (the corrected `playerState_s`).

Result: 251 named members, 82% of the 599712 bytes. Local types the layout proves wrong for Replay (their members in
cg_t are byte arrays of the Replay size until fixed): `refdef_t` (3232, type 69536), `ClientPlayerWeaponInfo`
(68480, type 202520), `ClientVisionSetData` (59568, type 59768), `Shake` (1008, type 1112), `AdvancedSwayState`
(288, type 384); `scores` is 16288 (game-test 22400), `teamScores` 428 (812).

Rerun (about a minute each):

```
python tools/ida_types/scan_struct_access.py --exe <replay exe> --functions out/ida_types/scan/functions.json --table LocalClientGlobals=0x14F26F940 --this-prefix cg_t:: CgGlobalsMP:: CgGlobalsSP:: --max-size 677040 --out out/ida_types/scan/cg_t
python tools/ida_types/scan_struct_access.py --exe E:\1-game-test\1-game_test.exe --functions out/ida_types/scan/functions_gt.json --thunks out/ida_types/scan/thunks_gt.json --table LocalClientGlobals=0x151149090 --this-prefix cg_t:: CgGlobalsMP:: CgGlobalsSP:: --returns CG_GetLocalClientGlobals CgGlobalsMP::GetLocalClientGlobals CgGlobalsSP::GetLocalClientGlobals CG_GetLocalClientGlobalsForEnt --max-size 955184 --out out/ida_types/scan/gt_cg_t_scan
python tools/ida_types/align_by_function.py --replay out/ida_types/scan/cg_t --gt out/ida_types/scan/gt_cg_t_scan --replay-functions out/ida_types/scan/functions.json --gt-functions out/ida_types/scan/functions_gt.json --gt-type cg_t --gt-type-tail CgGlobalsMP --replay-size 599712 --out out/ida_types/scan/cg_t_byfunc
python tools/ida_types/build_layout.py --align out/ida_types/scan/cg_t_byfunc.json --size 599712 --local-sizes out/ida_types/scan/local_sizes.json --verified playerState_s PlayerViewState cpose_t Weapon XCamData WeaponHand --out out/ida_types/scan/cg_t_layout
```

## CgGlobalsMP and CgGlobalsSP

Sizes from the two allocators: `CG_MainMP_AllocateClientMemory` (0x14178FDF0) allocates `747296 * clientCount`, steps
747296 between `CgGlobalsMP_ctor(obj, localClientNum, characterInfos, omnvarSlots, characterCount)` calls and sets every
system's `ms_allocatedType = 2`; MP characters (`characterInfo_t`, 13720 each) are a separate allocation passed in by
pointer. `CG_AllocLocalClientGlobals` (677040, `ms_allocatedType = 1`) is the SP allocator; SP embeds its one character
(`CgGlobalsSP::GetCharacterInfo` = `this + 662704`).

- `CgGlobalsMP` (747296): base `cg_t` + the game-test MP tail at a constant -207888. Both builds have a 147584-byte tail;
  all 182 Replay tail accesses (exe scan to +747296) land inside game-test tail fields at that shift, 83 exactly on a
  field start, none outside. 73 tail members with real base types.
- `CgGlobalsSP` (677040, type was 952512): base `cg_t`, `pipViews` 62936 (game-test 129440; inner layout unknown),
  `pipViewParams` 24, `m_skyOverride` 28, `m_characterInfo` 13720 at +662704, `m_clientCharacterInfo` +676424,
  `m_charAnimState` 608 at +676428 (direct SP access), 4 pad bytes.
- `cg_t` is a real base class of both (IDA base-class flag), and `this` is typed in the 11 class methods and
  `CgGlobalsMP_ctor`. Decompiles: `&this->m_characterInfo[13720 * characterIndex]`,
  `&this->predictedPlayerState.hudElems[0].type`, `this->localClientNum`.

The same pipeline works for any struct reached through a pointer table or `this` (next: `characterInfo_t` 13720,
`centity_t`).
