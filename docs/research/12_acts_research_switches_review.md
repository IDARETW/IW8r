# ACTS research switches: review of uncommitted diff

Scope: `E:\mw124\atian-cod-tools`, branch `codex/mw2019-complete`, `git diff --ignore-submodules`. Five files, +108 / -7. Read-only review; nothing was built or run. Line numbers refer to the working-tree files.

## Problems first

### P1. Preload call target does not match its own comment (block the PRELOAD hunk until verified)

`src/core/acts/tools/fastfile/handlers/handler_game_mw19_replay.cpp:538-541`

The comment says `sub_140DCBF80`. That is a VA with image base 0x140000000, so its RVA is `0xDCBF80`. The code calls `(*state.module)[0xdcbfd0]`, which is `0xDCBF80 + 0x50`. Either the comment is wrong or the call lands 0x50 bytes into a different function. The XAsset and header slots (`0x5d414b0`, `0x5d414d0`) are written as RVAs, so the comment mixes VA and RVA. Check the address in IDA before committing this hunk. The default path does not execute this code.

### P2. `PlanEnd` throws C++ exceptions from a native-called hook

`handler_game_mw19_replay.cpp:307-309`, `:323` (`_ReturnAddress`)

`PlanEnd` is installed with `Redirect` on native 0x11B24B0 and runs inside the native preload call. Its underflow check throws `std::runtime_error`. That exception unwinds through game frames and the Redirect trampoline. If the trampoline has no `.pdata` unwind info, the process terminates instead of reporting an error. Replace the throw with a counter or a logged error and a return. The `_ReturnAddress()` use at `:323` depends on the same assumption as `LoadStream` (`:34`): the Redirect must be a direct entry patch so the return address is the native caller. The hook installer was not located in this checkout, so this was not verified.

### P3. `ACTS_REPLAY_PRELOAD` turns on for any value, including `0`

`handler_game_mw19_replay.cpp:808`

The check is `std::getenv(...) != nullptr`. `ACTS_REPLAY_PRELOAD=0` enables the mode. The other switches require `== '1'`. Use the same rule, or document the footgun clearly.

### P4. `ACTS_REPLAY_PAYLOAD_MB` does not affect the Replay exporter

`src/core/acts/tools/mw19/mw19_pools.cpp:398-408` changes the budget only for the `mw19pools` path. The Replay exporter still passes the literal `128 * 1024 * 1024` at `handler_game_mw19_replay.cpp:618-621`. The comment and name suggest the Replay path. Values below 128 are silently ignored and fall back to 128. The `<< 20` on 4096 overflows a 32-bit `size_t`; this is fine only for x64 builds.

### P5. `ACTS_NATIVE_BYTES` has no size bound and parses differently

`src/core/acts/tools/mw19/mw19_schema.cpp:696-705`

- `std::vector<uint8_t> native(size)` is allocated for every struct node with no check against `limits.maxBytes`. Nested structs copy their bytes again, so the total is roughly depth times size. A corrupt size field can trigger a very large allocation.
- `$native` is added to every struct in the inspection JSON. Each hex dump is two characters per byte. The field JSON files grow sharply.
- Enabling uses `*value != '0'`, while the other switches use `== '1'`. Any value other than `0` enables it.
- A failed read silently omits `$native`. Nothing records the omission.
- Not verified: whether `ExportStructuredAsset` treats a `$native` key as a field.

### P6. `ACTS_REPLAY_STRUCTURED` raises budgets and can hide partial assets

- `handler_game_mw19_replay.cpp:613-630`: the switch sends every type, including types with a payload exporter, through the structured path. It raises `maxBytes` to 2047 MiB and `maxNodes` to 60 million. This increases peak memory.
- `src/core/acts/tools/mw19/mw19_payload.cpp:111-112`: structured exports with `read_errors` are accepted. The issues stay in the manifest. Not verified: whether the journal or the manifest `success` flag still reports these assets as clean. If it does, a partial asset looks complete.
- `mw19_payload.cpp:111` reads the variable on every call. The handler caches it in a static. The two readers could drift. Low risk today.

### P7. Plan-file handle and counters on error paths

- `handler_game_mw19_replay.cpp:893` opens `preload-plan.jsonl`. The error path in the catch block (about line 1040) closes only `streamTrace`, not `state.plan`. The handle stays open until `State` is destroyed.
- `:996-998` writes the preload counters only after a successful loop. A failed run has no plan counts in the manifest.
- `:542-543`: when a native fault occurs (`code != 0`), `planDepth` is not reset. The run aborts, so no later asset sees the stale depth. This is not silent corruption today.
- `:303-305`: `planDepth` is incremented before `Offsets()`. If `Offsets()` throws (see P8), depth stays incremented.

### P8. Minor

- `mw19_ff_stream.hpp:64-67`: `Offsets()` calls `Sync()` (`:45-51`), which throws "cursor escaped". It is called from the hook path, so it has the same exposure as P2.
- `mw19_pools.cpp:398-408` uses `std::getenv` and `std::strtoull` without including `<cstdlib>` in the diff. It relies on a transitive include.
- `mw19_payload.cpp:1`: `#include <cstdlib>` sits above the `#ifndef MW19_SCHEMA_STANDALONE` guard. Harmless, but the guard convention is broken.

## Per-switch summary

| Variable | Default | Enabled by | Code | Output |
|---|---|---|---|---|
| `ACTS_REPLAY_STRUCTURED` | off | `== "1"` | handler `:613-630`; payload `:111-112` | existing manifest and field files |
| `ACTS_REPLAY_PAYLOAD_MB` | 128 MiB | integer 128..4096 | pools `:398-408` (mw19pools only) | none new |
| `ACTS_NATIVE_BYTES` | off | any non-empty value except `0` | schema `:696-705` | `$native` key in field JSON |
| `ACTS_REPLAY_PRELOAD` | off | any set value (including `0`) | handler `:808-812`, `:537-546`, `:888-895`, `:993-998`, `:1026-1028` | `preload-plan.jsonl`; manifest keys `preload_plan*`; log line |

## Default behaviour when unset

Confirmed by reading the code: with no variable set, every executed operation matches the committed behaviour.

- Stream `Offsets()` (`mw19_ff_stream.hpp`) is new and only called from the preload hooks.
- The loader call (`handler_game_mw19_replay.cpp:544-547`) runs the same statements in the same order as before. The only difference is that `fault` and `code` are declared earlier.
- `structuredOnly` is false, so the 256 MiB, 4,000,000-node and `HasPayloadExporter` branch is unchanged.
- The pools budget defaults to `128 << 20`, which is the old literal.
- `nativeBytes` is false, so no `$native` key is added.
- Redirects for `0x11b2540` and `0x11b24b0` are installed only when preload is set.
- The run-start reset (`:888-895`) and the stream-plan close only touch new state. They write no output unless preload is on.

No path changes default output.

## The removed loader line

The line `*reinterpret_cast<void***>((*state.module)[it->global]) = header;` was not deleted. It moved into the `else` branch (`:544-547`), after the `fault` and `code` declarations. Default behaviour is the same.

In preload mode (`:537-541`), the statement is replaced by three actions:

1. Store `header - 8` into the slot at RVA `0x5d414b0`. `header` is the `void**` pointing at `asset.header`, and `Asset` is 16 bytes with `header` at +8 (`:18-21`). So `header - 8` is the `XAsset*` base.
2. Store `header` into the slot at RVA `0x5d414d0`.
3. Call RVA `0xdcbfd0` (see P1) instead of `it->loader`.

The binding lookup (`it`) still runs and still throws for types without a loader. `it->global` is not written in preload mode. Whether `0x5d414d0` is the same slot as `it->global` for every type was not verified.

## Static initialisation, threading, `_ReturnAddress`, handles

- Static locals (`structuredOnly`, `payloadBudget`, `nativeBytes`) use C++11 magic statics. They are thread-safe, and their initialisers do not depend on other static objects.
- `planDepth`, `planStart` and `plan` are plain fields on the shared `State`. They are safe only if the native loader runs on one thread. This was not checked.
- `_ReturnAddress()` at `:134`, `:190`, `:236`, `:323` and `:70` depends on how Redirect is installed (see P2).
- File handle: see P7.

## Blocking status

Nothing in the default (unset) path needs fixing before commit. Default behaviour is unchanged. Before committing the PRELOAD hunk, verify P1 in IDA. P2 and P3 should be fixed or documented. P4 to P7 are non-blocking for an opt-in research commit.

## build-native/ and build-workstation/

Both are untracked (`git status`). Both contain CMake output (`CMakeCache.txt`, `.vcxproj` files). They should be ignored. The root `.gitignore` already ignores `/build/` (line 52) but has no rule for these two directories. Recommended addition (not applied, `.gitignore` was not edited):

```
/build-*/
```

## Doc change

`docs/mw19/fastfile.md` received one new section, `## Research switches (opt-in)`, appended after `## Validation`. It is the only file edited in the ACTS repo. The working copy showed CRLF line endings at the first check and LF at the last. The committed file uses LF, and the new lines are LF.
