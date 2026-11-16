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
    ('## 2. x64-zt (the IW8 zone writer and the IW3 map converter)\n',
     '''## 2. x64-zt (the IW8 zone writer and the IW3 map converter) (done 2026-11-16)

Done: `src/tools/zonetool` (fork e648a414, 385 files) and `third_party/{zstd,lz4,minizip,nlohmann_json,gsl,glaze,
libtomcrypt,libtommath,directxtex}` copied by `tools/patches/zt_sync_fork.py`; built by `src/tools/zonetool/xmake.lua`
(the fork's settings: C++23, /MT, /O1 with /O2 for IW8 and the converter, no LTO, `/BASE:0x180000000 /DYNAMICBASE:NO`,
without the project-wide /utf-8 and Windows macros) into `bin/<mode>/iw8r-zt.exe`, built with the editor and shipped.
Parity with the fork's zonetool.exe, byte for byte on every file the converter writes:

| Command | Input | Result |
|---|---|---|
| buildextraction | mp_iw8r_fxtest (21 MB extraction) | 33 files identical |
| buildextraction | mp_cinderrail (1.4 GB extraction; extraction unchanged) | 46 files identical, the 394 MB zone included |
| editzone | first-person look (lit filter over 10 shared techset zones) | identical |
| editzone | mp_rust and mp_bm_tut stock edits | identical; a mp_hideout edit fails the same way in both |
| dumpzone, buildzone, verifyzone | mp_hideout with a vision edit (`tools/m2_rebuild_mapents.ps1`) | identical dump and zone, verify passed |

The ACTS exports the converter asks for during a build (`_stock_cache`) differ between any two runs of the same acts.exe;
they are ACTS's output and are checked with ACTS (section 3). Tools: `tools/zt_parity.py`, `tools/zt_editzone_parity.py`.
Defaults switched: Tool Paths `zonetool`, `tools/m2_rebuild_mapents.ps1`, `run_offline_proofs.py`,
`stock_edit_editor_check.py`, `scriptable_offline_test.py`.

Survey (before the port):
'''),
])
rw(R + 'CHANGELOG.md', [
    ('## 2026-11-16\n\n',
     '''## 2026-11-16

- **The zone writer and map converter are part of the project.** `iw8r-zt.exe` is built from `src/tools/zonetool`
  (the x64-zt fork with its 485 IW8 Radiant commits, GPL-3; only IW8, the IW3 map pipeline the editor's builds use, and
  their libraries in `third_party`) and is the default Tool Paths `zonetool`; the package ships it. Proven against the
  fork's zonetool.exe: map builds of the effects test map and Cinderrail (every file the same, the 394 MB zone
  included), stock edits and the first-person look (the same zones), and the dump / build / verify path of the
  mapents and vision edits (the same dump and zone). Details: [docs/PLAN_OWN_TOOLS.md](docs/PLAN_OWN_TOOLS.md).
'''),
])
rw(R + 'docs/PROGRESS.md', [
    ('  - Next: x64-zt (src/tools/zonetool, iw3 + iw8 + convert/iw3_iw8 only) then ACTS (mw19replay handler closure); plan\n'
     '    and parity method in docs/PLAN_OWN_TOOLS.md.\n',
     '''  - x64-zt done: src/tools/zonetool (fork e648a414 via tools/patches/zt_sync_fork.py; src/tools/zonetool/xmake.lua is
    ours and survives a sync), third_party libs, bin/<mode>/iw8r-zt.exe (dep of IW8Radiant), default zonetool path and
    script defaults. Parity tools: tools/zt_parity.py (buildextraction; use a SHORT scratch root like E:/zp, ACTS
    fails on long paths), tools/zt_editzone_parity.py. Results table in docs/PLAN_OWN_TOOLS.md section 2.
  - Next: ACTS (mw19replay handler closure -> src/tools/acts, iw8r-acts.exe); then the setup wizard / Tool Paths
    wording (x64-zt, ACTS rows), THIRD_PARTY.md, LICENSE, README.
'''),
])
print('ok')
