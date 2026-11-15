# 134. Lobby crash on a map with carried stock scriptables: the preload mark stack holds two

Owner report 2026-11-14: selecting Cinder Rail (`mp_cinderrail`, the 20:15 package) in the private-match lobby gives
`Fatal Error 0x0000000140d97add 7596524 0xc0000005 game_dx12_ship_replay_local_dw.exe`. The toon build
`mp_cinderrail_t` (18:42) loads.

## What the client log shows (VERIFIED)

`replay-dw-client.log` ends with `DW_ZONE_REQUEST load=0x00000002 mp_cinderrail:0x00100000`: the lobby loads the
map zone when the map is selected, and the crash follows.

## The faulting code (VERIFIED, read from the exe with capstone; IDA was not reachable)

`game_dx12_ship_replay_local_dw.exe`, function at `0x140D97A80`:

```
eax  = [0x145DA48B8]              ; the counter
rdx  = 0x145DA4830 + eax * 0x44   ; a record of 8 block positions (+0x00..) and 8 zeros (+0x20..), 0x44 bytes
[rdx + 0x20] = 0                  ; 0x140D97ADD: the fault
[rdx + 0x00] = position of block 0 ...
[0x145DA48BC] = counter; [0x145DA48B8] = counter + 1     ; no bound check
```

A neighbouring function (ending `0x140D97A71`) also pushes onto the same counter. The function has 211 call sites,
all inside the asset `Load_*` range (`0x140D9CEEA` .. `0x140DE8068`): the map preload marks pushed by each
preloaded asset pointer (the converter's `preload_block`, x64-zt `writer.cpp`).

The stack is **two records**: record 2 starts at `0x145DA4830 + 0x88 = 0x145DA48B8`, the counter itself. A third
nested push writes a block position over the counter, and the next push indexes the table with that position and
faults: exactly the reported address.

## What nested three deep (VERIFIED, the converter's own walk)

An instrumented x64-zt writer (counting nested preloaded assets per list entry) refused the same build inputs with:

```
scriptable "scriptable_door_wooden_panel_03_painted_mp" > xmodel "door_wooden_panel_03_painted_left_mp"
  > physicsasset "door_wooden_panel_03_painted_left_mp"
```

The carried stock door (mp_backlot2) was the first asset to use its door model, so the model was written inline
inside the scriptable, and the model's physics asset inline inside the model. In stock zones the models come
before their scriptables in the asset list, so a scriptable only points at them (two levels at most: list asset and
one inline). Maps without carried scriptables never reached three, which is why every earlier map loaded.

## Fix (converter, iw8radiant/scriptables)

`write_zone` runs its collecting pass, records every list entry whose preloaded assets nest three deep, lists the
middle asset (the model) just before that entry, and collects again until none is left; the writing pass refuses a
zone that would still nest three deep. The log says `zone writer: N asset(s) listed before their users`.

## Owner action

Rebuild Cinder Rail with the fixed converter (F7) and deploy (F6). The installed 20:15 package crashes the lobby
and should be replaced (or removed) before the map is selected again.
