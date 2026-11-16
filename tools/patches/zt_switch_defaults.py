import io

# The editor, the package and the tools scripts use the project's own zone writer (bin/<mode>/iw8r-zt.exe, built from
# src/tools/zonetool) instead of the x64-zt fork checkout.


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


R = 'E:/IW8-Radiant/'
rw(R + 'src/qtapp/main_window.cpp', [
    ('        {"zonetool", "E:/mw124/x64-zt/build/bin/x64/Release/zonetool.exe"},',
     '        // The project\'s own zone writer and map converter (src/tools/zonetool, the x64-zt fork), beside the editor.\n'
     '        {"zonetool", QCoreApplication::applicationDirPath() + "/iw8r-zt.exe"},'),
    ('{"zonetool", "x64-zt (zonetool.exe)"}', '{"zonetool", "Zone writer (empty = iw8r-zt.exe beside the editor)"}'),
])
rw(R + 'tools/m2_rebuild_mapents.ps1', [
    (r"    [string]$ZoneTool = 'E:\mw124\x64-zt\build\bin\x64\Release\zonetool.exe',",
     r"    [string]$ZoneTool = '',  # empty: bin\release\iw8r-zt.exe (src/tools/zonetool)"),
    ("$iw8r = Join-Path $root 'bin\\release\\iw8r.exe'\n",
     "$iw8r = Join-Path $root 'bin\\release\\iw8r.exe'\nif (-not $ZoneTool) { $ZoneTool = Join-Path $root 'bin\\release\\iw8r-zt.exe' }\n"),
])
OURS = "os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'bin', 'release', 'iw8r-zt.exe')"
rw(R + 'tools/run_offline_proofs.py', [(r"exe = r'E:\mw124\x64-zt\build\bin\x64\Release\zonetool.exe'", 'exe = ' + OURS)])
rw(R + 'tools/stock_edit_editor_check.py', [
    (r"zone, exe = 'mp_shipment', r'E:\mw124\x64-zt\build\bin\x64\Release\zonetool.exe'", "zone, exe = 'mp_shipment', " + OURS)])
rw(R + 'tools/scriptable_offline_test.py', [(r"LIVE = r'E:\mw124\x64-zt\build\bin\x64\Release\zonetool.exe'", 'LIVE = ' + OURS)])
rw(R + 'xmake.lua', [('    add_deps("iw8r_core", "iw8r_viewport", "qtads", "iw8r-gsc")',
                      '    add_deps("iw8r_core", "iw8r_viewport", "qtads", "iw8r-gsc", "iw8r-zt")')])
rw(R + 'tools/package_release.ps1', [("'iw8r-gsc.exe', 'qtadvanceddocking.dll'",
                                       "'iw8r-gsc.exe', 'iw8r-zt.exe', 'qtadvanceddocking.dll'")])
print('ok')
