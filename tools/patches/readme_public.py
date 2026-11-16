import io

# README for the public repository: building from a clone (submodules, any Qt folder), the three tools that are built
# with the editor, the key table from the repository's own gsc-tool copy, the current test gate, the licence.


def rw(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    s = s.replace('\r\n', '\n')
    for a, b in pairs:
        if b in s:
            continue
        assert s.count(a) == 1, (p, a)
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))


rw('E:/IW8-Radiant/README.md', [
    ('''- **Build**: F7 builds the map into the game's zones (x64-zt converter), with collision in one piece or as the game's
  streamed tiles for big maps, and tests every zone with the game's own loaders (ACTS); F5 plays it.''',
     '''- **Build**: F7 builds the map into the game's zones (the converter in `src/tools/zonetool`), with collision in one
  piece or as the game's streamed tiles for big maps, and tests every zone with the game's own loaders (the Replay
  reader in `src/tools/acts`); F5 plays it. Everything it runs is built from this repository.'''),
    ('''Prerequisites (all present on the development machine):
''', '''Prerequisites:
'''),
    ('''- Qt **6.8.3** `msvc2022_64` at `E:\\Qt\\6.8.3\\msvc2022_64`. To install it the same way:''',
     '''- Qt **6.8.3** `msvc2022_64` (any folder; `E:\\Qt` below). To install it with aqtinstall:'''),
    ('''- Optional, for full IW8 key names: a local gsc-tool checkout (here `E:\\mw124\\atian-cod-tools\\deps\\gsc-tool`).

From `E:\\IW8-Radiant`:

```bash
xmake f -m release --qt=E:\\Qt\\6.8.3\\msvc2022_64 -y
```''',
     '''Clone with the submodules (Qt Advanced Docking System, bcdec):

```bash
git clone --recursive https://github.com/IDARETW/IW8r.git
```

From the repository folder:

```bash
xmake f -m release --qt=E:\\Qt\\6.8.3\\msvc2022_64 -y
```'''),
    ('''Outputs land in `bin\\release\\`. The editor build runs `windeployqt`, so `IW8Radiant.exe` starts without Qt on `PATH`.''',
     '''Outputs land in `bin\\release\\`. The editor build runs `windeployqt`, so `IW8Radiant.exe` starts without Qt on `PATH`.
It also builds the three tools the editor runs, beside it: `iw8r-gsc.exe` (the GSC compiler), `iw8r-zt.exe` (the zone
writer and map converter) and `iw8r-acts.exe` (the Replay zone reader); see [THIRD_PARTY.md](THIRD_PARTY.md). The
editor needs nothing else but the game: *Tools → Tool Paths* (or the first-run setup) asks for the Replay folder.'''),
    ('''```bash
py -3.11 -I tools\\gen_iw8_keytable.py E:\\mw124\\atian-cod-tools\\deps\\gsc-tool\\src\\gsc\\engine\\iw8_token.cpp -o data\\generated\\iw8_canonical_keys.tsv
```''',
     '''```bash
py -3.11 -I tools\\gen_iw8_keytable.py src\\tools\\gsc\\src\\gsc\\engine\\iw8_token.cpp -o data\\generated\\iw8_canonical_keys.tsv
```'''),
    ('''Open a stock IW8 map's entities (Replay 1.20 `mp_hideout`, ACTS dump):

```bash
bin\\release\\IW8Radiant.exe E:\\mw124-conversion-builds\\_ents_stock\\hideout\\mw19replay\\mp_hideout\\assets\\map_ents\\maps\\mp\\mp_hideout.d3dbsp.0.asset.json
```''',
     '''Open a stock map (its world, props and entities, read from the game with the Replay reader): *File → Open IW8 Stock
Map*.'''),
    ('''Build a map (needs the x64-zt `buildextraction` build, branch `iw8radiant/build-extraction`, and ACTS; set the paths in
*Tools → Tool Paths*): open the map, press **F7**, and watch the Compile dock. Zones go to the x64-zt output folder
(default `%LOCALAPPDATA%\\IW8 Radiant Suite\\...\\x64zt_output\\<map id>`).''',
     '''Build a map: open it, press **F7**, and watch the Compile dock. Zones go to the output folder (default
`%LOCALAPPDATA%\\IW8 Radiant Suite\\...\\x64zt_output\\<map id>`).'''),
    ('''One command builds everything and runs every milestone-1 check (core tests, CLI edit/diff, stock IW8 MapEnts
round trip, end-to-end GUI test on Qt's offscreen platform, Direct3D 11 smoke test with screenshot):

```bash
pwsh -NoProfile -File tools\\run_milestone1_checks.ps1
```

Reports go to `out\\milestone1-<timestamp>\\` (`summary.json`, `core-tests.txt`, `gui-test-report.txt`,
`smoke-test.txt`, `screenshot.png`). The GUI test never shows a window or moves the mouse, so it can run while
you use the editor. Nothing in the test suite starts the game or writes into the Replay install.''',
     '''One command builds everything and runs the core tests, the end-to-end GUI test on Qt's offscreen platform and the
Direct3D 11 smoke test, with one summary line per suite:

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File tools\\run_all_tests.ps1
```

The GUI test never shows a window or moves the mouse, so it can run while you use the editor. Nothing in the test
suite starts the game or writes into the Replay install. The tools built from `src/tools` are checked against the
programs they came from with `tools/acts_parity.py`, `tools/zt_parity.py`, `tools/zt_editzone_parity.py` and the
compiler fixtures in `tests/data/gsc` ([docs/PLAN_OWN_TOOLS.md](docs/PLAN_OWN_TOOLS.md)).'''),
    ('''src/cli/        iw8r command line tool''', '''src/cli/        iw8r command line tool
src/tools/      the tools the editor runs: gsc (iw8r-gsc), zonetool (iw8r-zt), acts (iw8r-acts)'''),
    ('''third_party/    Qt Advanced Docking System 4.4.1 (LGPL-2.1, built as a DLL)''',
     '''third_party/    libraries (THIRD_PARTY.md); Qt Advanced Docking System and bcdec are submodules'''),
    ('''## Third-party

- Qt 6.8.3 (LGPLv3, dynamically linked, deployed by windeployqt).
- Qt Advanced Docking System 4.4.1 (LGPL-2.1), `third_party/qtads`, built as `qtadvanceddocking.dll`.
- The IW8 key table is generated locally from gsc-tool (GPLv3) and is not distributed with the editor.''',
     '''## Licence

GNU General Public License v3 ([LICENSE](LICENSE)). The tools and libraries this repository contains, with their own
licences (gsc-tool and x64-zt: GPL-3; ACTS: MIT; the libraries: MIT, BSD, zlib, public domain; Qt and the Qt
Advanced Docking System: LGPL, dynamically linked), are listed in [THIRD_PARTY.md](THIRD_PARTY.md). No game files
are part of the repository or the installer: the editor reads the game you own.'''),
])
print('ok')
