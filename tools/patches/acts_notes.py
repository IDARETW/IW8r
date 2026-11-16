import io


def rw(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    s = s.replace('\r\n', '\n')
    for a, b in pairs:
        if b in s:
            continue
        assert s.count(a) == 1, (p, a)
        s = s.replace(a, b, 1)
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))


R = 'E:/IW8-Radiant/'
rw(R + 'docs/PLAN_OWN_TOOLS.md', [
    ('## 3. ACTS (reading Replay zones)\n',
     '''## 3. ACTS (reading Replay zones) (done 2026-11-16)

Done: `src/tools/acts` (fork 834568b9, 94 files, about 1 MB: the mw19replay handler, the mw19 Replay code, the
fastfile option and handler types, the IW decompressor, the flexible header reader and the ACTS core pieces they
include), copied by `tools/patches/acts_sync_fork.py` (with `acts_missing_headers.py` / `acts_add_files.py` to follow
the include closure). Ours in `src/tools/acts/iw8r/`: the trimmed umbrella header, the fastfile command
(`fastfile_replay.cpp`, the fork's driver without CASC, linkers, asset pools, REPL, GSC, translations and game ids),
`hashutils::Add` / `ExtractTmp` and the tool registration (`acts_support.cpp`), `main.cpp` (`--noUpdater`, `-l <level>`,
then `fastfile`), and `replay_schema.hpp`: the Replay schema now lives in `data/mw19/schema.json` (shipped with the
editor's data) and is found beside the exe or in a data folder above it. Built by `src/tools/acts/xmake.lua`
(C++20, /source-charset:windows-1252, /O2, static ACTS API) into `bin/<mode>/iw8r-acts.exe` (2 MB; the fork's
acts-common.dll alone is 19 MB). SHA-256 from hash-library (`third_party/hash_library`).

Parity (`tools/acts_parity.py`): ACTS's output is not the same from run to run (payload dumps carry run-time
addresses, manifests carry timings), so the fork runs twice and ours must equal it wherever its two runs agree.

| Case | Result |
|---|---|
| `--test` on mp_shipment, common_mp (patched, 10,479 assets) and mp_donetsk (116,307 assets) | same journal, same failures and unavailable payloads, same messages and exit code |
| map_ents, vfx, rawfile + lightdef + com_map, xmodel, xmodelsurfs `--geometry` with the xpaks, images with the xpaks, decal volumes, gfx_map + gfx_map_trzone, a Verdansk streamed tile, streamkey, soundbank with `ACTS_REPLAY_STRUCTURED=1`, the converter's stock export (vfx, physicsfxshape, surfacefx, lightdef: 456 files), `-d --noAssetDump` | every stable file and value the same |
| `--serve` with three requests (map_ents, model geometry, image) | same answers (the zone kept and reused), same files |

End to end with only the helper exes (Tool Paths defaults, no `E:\\mw124` path): `tools/minimap_build_test.py`
(two map builds through the editor: iw8r-gsc, iw8r-zt, iw8r-acts validation and read-back: RESULT ok),
`tools/stock_edit_editor_check.py` (editzone of mp_shipment from the GUI test's files: entities and 611 spawns equal
the stock ones, the zone loads: PASS), the editor's material and model cache exports into scratch roots.

Survey (before the port):
'''),
])
rw(R + 'CHANGELOG.md', [
    ('## 2026-11-16\n\n',
     '''## 2026-11-16

- **The Replay reader is part of the project: no outside tools are needed.** `iw8r-acts.exe` is built from
  `src/tools/acts` (the ACTS fork's Replay 1.20 reader, MIT; only the mw19replay handler and what it uses) and is the
  default Tool Paths `acts`, as `iw8r-zt.exe` and `iw8r-gsc.exe` are for the zone writer and the script compiler;
  all three are built with the editor and shipped in the package, and the Replay schema ships in `data/mw19`. The
  editor, its headless tests and the tools scripts use them by default; the setup wizard names them as installed with
  the editor and saves them empty (the default), so a moved install keeps working. Proven against the fork's acts.exe
  on 18 export, test and serve cases (every stable output the same) and end to end: map builds, a stock zone edit, the
  material and model caches. Details: [docs/PLAN_OWN_TOOLS.md](docs/PLAN_OWN_TOOLS.md).
'''),
])
rw(R + 'docs/PROGRESS.md', [
    ('''  - Next: ACTS (mw19replay handler closure -> src/tools/acts, iw8r-acts.exe); then the setup wizard / Tool Paths
    wording (x64-zt, ACTS rows), THIRD_PARTY.md, LICENSE, README.
''',
     '''  - ACTS done: src/tools/acts (fork 834568b9 via tools/patches/acts_sync_fork.py; iw8r/ and xmake.lua there are
    ours), bin/<mode>/iw8r-acts.exe, data/mw19/schema.json; defaults switched in main_window, the cache tests and 20
    tools scripts (parity tools keep the fork as reference). tools/acts_parity.py is noise-aware (fork runs twice).
    Setup wizard / Tool Paths: "Replay reader" / "Zone writer", saved empty when equal to the bundled default.
  - zt build: xmake "smallest" adds /GL, so zt_flags uses /O1 with optimize "fork_size" (no flag); "fastest" = /O2.
  - Next: LICENSE (GPL-3), THIRD_PARTY.md, README (building, what the user provides), package licence notes.
'''),
])
print('ok')
