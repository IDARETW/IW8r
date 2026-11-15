# IDA local-type repair: coordinator log

Changes the coordinator (main session) applied or checked, with the Replay evidence. Agents' own field edits are
listed in their reports under `out/ida_types/results/`. Backups: `out/ida_types/backup/`.

## Wave 1

### Applied by the coordinator

| Struct | Change | Evidence |
|---|---|---|
| `gentity_s` | size 1456 -> 1440: tail pad `_pad4A0` 272 -> 256 bytes, no member moved | `GetEntity` 0x14125ACA0 returned `g_entities + 1440 * n`; `G_FreeEntity` 0x1410342A0 compares `entity - g_entityArrayBase >= 1440`. Members up to `scripted` +0x498 already matched Replay (`G_FreeEntity`, `G_StopAnimScripted`). After: `GetEntity` reads `&g_entities[entityRef]`. |

### Agent edits re-checked

| Agent | Edit | Check |
|---|---|---|
| A010 | `EntityLoDs.lodIndex` int +0x0 | OK: `MSG_ReadDeltaEntityLoD` 0x1412D1D00 writes `*(_DWORD *)dest = lodIndex`, then 64 DWORDs from +4 |
| A006 | `SprintState` +0x00..+0x1C (7 fields) | OK for +0x10, +0x18, +0x1C: `PM_RestartSprintTimers` 0x141124DB0 writes `ps+812/820/824` with `sprintState` at `ps+0x31C`; +0x20 is the sprint meter (`ps+828`) |
| A003 | `Weapon` size 60 (no edit) | OK: `BG_PlayerToEntity_SetStowedWeaponMP` 0x141111130 `memcmp(rec+2, rec+2, 0x3C)` in 62-byte records |

### Confirmed proposals waiting to be applied

| Struct | Proposal | Evidence |
|---|---|---|
| `EntityLoDs` | size 256 -> 260 (mask is 64 DWORDs at +4) | `MSG_ReadDeltaEntityLoD` copies 2 x 128 + 4 bytes and resets dest+4..dest+260 |
