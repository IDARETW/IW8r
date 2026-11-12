# IW8 Radiant Suite: architecture

## 1. Target build

**Call of Duty: Modern Warfare (2019), "Replay" 1.20.4.7623265**, `E:\IW8\Builds\1.20-replay\game_dx12_ship_replay.exe`.

| Evidence | Where | What it shows |
|---|---|---|
| Exe hash | MD5 `1c238fe327f2ecc3b0db924c5b425439`, computed this session | Matches the build required by both existing IW8 map writers (`E:\mw124\mw120rproxy\README.md`, x64-zt `docs/IW8_CONVERSION.md`) |
| Symbols | `E:\IW8\Binaries\1.20-replay\game_dx12_ship_replay.exe.map` (57 MB) and an IDA database | The only IW8 build here with a named symbol map, so addresses can be cited instead of guessed ([research/05](research/05_runtime_evidence_1_20.md)) |
| Image base | `0x140000000` (PE header) | RVAs below are relative to it |
| Asset formats | x64-zt `tools/iw8_format.json`: 111 asset types whose readers and writers are generated from this build's `Load_*` routines | IW8 layouts are tied to this exe, not inferred ([research/08](research/08_x64zt_iw8_conversion.md)) |
| Fastfile version | ACTS manifest of stock `mp_hideout`: `xfile_version` 4087 (0xFF7), profile `replay-1.20` | Stock zones on disk read cleanly (18,642 assets, 0 failures) |
| Runtime hooks | `mw120rproxy` (XInput proxy) already hooks this build: `DB_FindXAssetHeader` RVA `0x11AA890`, `DB_LoadFastfiles` `0x11ACD30`, `Com_GameInfo_LoadMapInfoList` `0x10C7E70`, `Cbuf_AddText` `0x15C1C20`, `Sys_OpenFileReliable` `0x13F4D50` | Live-link entry points are proven in a running game, not just named ([research/05](research/05_runtime_evidence_1_20.md) lines 61, 70, 92, 106, 216) |
| In-game map loads | Owner-recorded loads of converted maps (mp_iw7_frontier, mp_iw7_scorch, mp_iw7_brkneck), Oct–Nov 2026 | A custom-map path into this build exists ([research/06](research/06_test_assets.md) section 4). The checklist never records the exe hash, so the attribution to 1.20.4.7623265 is by context |

Other builds on disk (1.30 retail, 1.64, 1.69 BNet) have no symbol map and no writer targeting them, so they are out of scope until a writer exists.

## 2. What was reused, and the standalone-vs-extend decision

| Existing material | Role in the suite |
|---|---|
| **iw3xo-radiant** (`reference/iw3xo-radiant`, commit `7c3a409`) | Feature-parity reference only ([research/01](research/01_iw3xo_feature_audit.md), [research/07](research/07_editor_feel_reference.md)). It is a 32-bit D3D9 hook DLL into `CoD4Radiant.exe`. It cannot host IW8 assets (BC6H/BC7 images, IW8 techsets, Havok collision, 64-bit address space), so extending it would tie IW8 work to IW3 tooling. **Not extended.** |
| **x64-zt fork** (`E:\mw124\x64-zt`, `IDARETW/x64-zt` branch `iw8-conversion`) | IW8 asset readers and writers for this build. It is the back end for asset browsers (`-game iw8 dumpzone/dumpasset`) and for building fastfiles (`buildzone`, plus the map builder `convert_extraction` that consumes the extraction contract). Only its IW8 side is used, with no IW3/IW7 front end ([research/08](research/08_x64zt_iw8_conversion.md)). |
| **ACTS** (`E:\mw124\atian-cod-tools`, `acts fastfile -r mw19replay -a <type>`) | Read-only stock dumps as structured JSON (MapEnts, xmodels, images). Used now for stock `mp_hideout`, `mp_shipment` and `mp_backlot2` MapEnts ([research/04](research/04_iw8_asset_access.md), [research/10](research/10_iw8_entity_inventory.md)). |
| **mw120rproxy** (`E:\mw124\mw120rproxy\mw120rproxy`) | The in-game side: custom-map registration, F6 local play, and console injection. It will host the live-link server ([research/05](research/05_runtime_evidence_1_20.md)). |
| **mw120rproxy iw8-zonetool** | Older IW8 writer. Superseded by x64-zt for building; kept only as a cross-check ([research/03](research/03_build_pipeline_interface.md)). |
| **gsc-tool IW8 token table** | Canonical key ids for IW8 entity strings, generated locally (`tools/gen_iw8_keytable.py`). GPLv3, so it is not vendored. |

**Decision: a standalone x64 editor.** It owns its document model and UI, and drives IW8 tooling (x64-zt, ACTS, the proxy) over
narrow, file-based or IPC interfaces.

## 3. Components

```
                 ┌──────────────────────────── IW8Radiant.exe (Qt 6.8 + Qt-ADS) ─────────────────────────────┐
  mapper  ─────► │ Camera (D3D11)  Grid (QPainter)  Entity  History  IW8 Spawns  Hotkeys  [Asset browsers M4] │
                 └──────────────┬────────────────────────────────────────────────────────────┬───────────────┘
                                │ EditorContext (signals)                                    │
                 ┌──────────────▼──────────────┐      ┌──────────────────────┐   ┌───────────▼────────────┐
                 │ iw8r_core (no UI)           │      │ iw8r_viewport        │   │ iw8r.exe CLI           │
                 │  map_text: lossless model   │◄─────┤  SceneRenderer (D3D) │   │  info/roundtrip/diff/  │
                 │  document: selection, grid, │      │  SceneCache + picking│   │  edit/iw8-ents         │
                 │   undo/redo transactions    │      │  Camera math         │   └────────────────────────┘
                 │  iw8_mapents: ACTS MapEnts  │      └──────────────────────┘
                 │  KeyTable: canonical ids    │
                 └──────────────┬──────────────┘
                                │ M2+: file/IPC boundaries only
     ┌──────────────────────────┼───────────────────────────────┬────────────────────────────────┐
     ▼                          ▼                               ▼                                ▼
 ACTS (stock dumps,     x64-zt -game iw8                 IW8 brush compiler (M5)        mw120rproxy live link (M7)
  read-only)            dumpzone / buildzone /           → extraction contract →        in Replay 1.20: Cbuf_AddText,
                        verifyzone (M2)                  x64-zt convert_extraction      camera, entity hot update
                                                         → 5 zones + map.json
```

- **`iw8r_core`** has no UI or platform dependencies except the atomic file replace. Every shell and tool links it.
- **Lossless model** (`src/core/map_text.*`). The file is partitioned into owned pieces: header, entity prefix/open/close
  text, key/value lines, primitive line blocks, opaque lines, and trailer. A self-check at load rejects any partition that
  does not reproduce the input exactly. Edits regenerate only the changed lines: a moved brush rewrites only its three
  plane points per face and keeps the rest of each line verbatim. A moved patch rewrites only the `v x y z` tokens.
  A key edit keeps the key's original spelling (quoted, canonical id, or `0 name`).
- **Undo** (`src/core/document.*`) is a list of transactions of snapshot ops (replace/insert/remove item or entity).
  Undo restores the exact prior objects, including their text, so edit → undo → save is byte-identical. An interactive
  drag previews without history and commits one transaction on release. The undo limit defaults to 1,024, and the
  "modified" flag survives trimming.
- **Viewport** (`src/viewport`): one D3D11 device per process. Each camera view renders offscreen, then copies to its own
  flip-model swap chain, which is recreated when docking re-parents the native window. The render cache rebuilds only
  items whose `(id, rev)` changed. Picking uses ray-vs-brush planes, patch quads, and entity boxes.
- **Qt shell** (`src/qtapp`): Qt Widgets with Qt Advanced Docking System. Commands are `QAction`s with application-wide
  shortcuts, stock CoD Radiant defaults, rebinding persisted to `IW8Radiant.ini`, and native file dialogs. Views
  subscribe to `EditorContext` signals.

## 4. IW8 format facts used by milestone 1 (tied to Replay 1.20.4.7623265)

| Fact | Value | Evidence |
|---|---|---|
| MapEnts asset pool | `map_ents`, type 29 (0x1D) | ACTS `assets.jsonl` (`type_id` 29); x64-zt `.iw8` header type index 0x1D ([research/08](research/08_x64zt_iw8_conversion.md) §3) |
| MapEnts fields | `name`, `entityString`, `numEntityChars`, `trigger`, `clientTrigger`, `spawnList`, `splineList`, `havokEntsShapeData`, `cmodels`, … (52 fields) | ACTS dump of stock `mp_hideout`; x64-zt `structs.hpp:10929` |
| Entity string | LF line endings, NUL-terminated (`numEntityChars` counts the NUL), `{` / `}` blocks, one `key "value"` per line | Stock `mp_hideout`: 270,980 bytes, 2,660 entities, 12,961 key lines, byte-identical round trip in `iw8r_tests` |
| Key spelling | canonical script-string ids: `212`=classname, `709`=origin, `80`=angles, `652`=model, `1070`=target, `1072`=targetname, `845`=script_noteworthy. Also accepted by the grammar: `"name" "value"` and `0 name "value"` | gsc-tool `iw8_token.cpp` (0xD4, 0x2C5, 0x50, 0x28C, 0x42E, 0x430, 0x34D); x64-zt `src/games/iw8/zone/replay_spawns.h:53,74` and `src/core/formats/mapents.cpp` |
| Canonical id range | ≤ 0xE2C0 compiled into Replay 1.20 | x64-zt `gsc_tool_iw8_tokens.inc` header comment |
| Spawn points | Not entities. `MapEnts::spawnList` holds `SpawnPointEntityRecord` (40 bytes: index, name/target/noteworthy script strings, origin, angles) | x64-zt `replay_spawns.h` (`Load_SpawnPointRecordList` at 0xE0DB80, static_assert 40); stock `mp_hideout` 501 records, `mp_shipment` 611, `mp_backlot2` 430 |
| Map registration in game | Proxy `Com_GameInfo_LoadMapInfoList` hook plus `set ui_mapname` / `ui_gametype` via `Cbuf_AddText`; package routed by `Sys_OpenFileReliable` hook | [research/05](research/05_runtime_evidence_1_20.md) lines 92, 96, 106, 216 |

Nothing in this table is guessed. Every offset or address the editor will rely on later must come with the same kind of
citation, and the IW8 builder's hand-stamped GfxWorld offsets (`write_maps.cpp`) are explicitly not copied ([research/09](research/09_iw8_render_features.md) §4).

## 5. Source format

IW8 never shipped mod tools, so there is no public IW8 map source format. The editor's source document uses the
**Radiant text grammar** (`iwmap 4`: entities, brushes with three-point planes, `curve`/`mesh` patches, layers),
because IW8 mappers know it and it parses losslessly. The grammar is validated against 2,182 real Radiant files with
zero unrecognised lines ([research/02](research/02_iwmap4_format.md)). The *content* is IW8: material names are
Replay 1.20 material assets (e.g. `tm2co/me_plaster_barrier_fence_wall`), xmodels are IW8 xmodels, classnames are
IW8 classes, and builds go through the IW8 compiler (M5), never through CoD4 tools. The editor also opens IW8 runtime
entity strings directly (`.ents`, or an ACTS MapEnts dump) and preserves their numeric key spelling.

Per-face texture parameters keep the Radiant field layout (material, 6 numbers, lightmap material, 6 numbers). Their
IW8 meaning (UV projection for IW8 world surfaces, and lightmap density for the IW8 baker) is defined by the M5
compiler, not by CoD4 behaviour.

## 6. GUI stack

Qt 6.8.3 LTS (Widgets) with Qt Advanced Docking System 4.4.1 replaced the first Dear ImGui prototype at the owner's
request. Qt gives real dock layouts, model/view tables for asset browsers, property editing, a shortcut editor,
native dialogs, IME, and high-DPI. 3D viewports are native child windows with their own D3D11 swap chains. The
editor core is toolkit-independent, and the same 22-check GUI test drives the Qt shell through Qt's offscreen platform.

## 7. Licensing

- Qt: LGPLv3, dynamically linked and deployed beside the exe by windeployqt.
- Qt-ADS: LGPL-2.1, built as `qtadvanceddocking.dll` (dynamic) from the unmodified 4.4.1 sources in `third_party/qtads`.
- gsc-tool token data (GPLv3): generated on the mapper's machine into `data/generated/` and never distributed.
  Seven key ids with two independent sources each are built in.
- No game executable, stock asset, or dump is part of the editor's sources. `data/stock_dumps` holds local reference
  dumps for tests.

## 8. Known unknowns

- In-game loading of anything the editor writes is unverified (tracked in [MILESTONES.md](MILESTONES.md)).
- No IW8 lightmap, light-grid, probe-capture, DPVS-cell or Umbra-tome baker has been located ([research/09](research/09_iw8_render_features.md)).
  x64-zt writes these assets from converted data but does not bake them. M6 must solve baking or ship an
  explicit "unbaked" mode.
- x64-zt's `PrepareFastfile` requires a readable `.ff` path even for an external extraction (`iw3_fastfile.cpp:10432`).
  The native compiler path needs that check relaxed in the fork.
- Proxy names `SetOrigin` / `SetViewAngles` disagree with the symbol map ([research/05](research/05_runtime_evidence_1_20.md) §12).
  The live link must verify their semantics before relying on them.
