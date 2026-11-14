import io

# The release package lacked data\shaders (the toon and stock shader files: Tools > Shader Editor's templates, the
# map shader lookup and the stock-map filter all read them). The package script now copies the folder and stops when
# a file the editor needs at run time is missing from the package.

p = 'E:/IW8-Radiant/tools/package_release.ps1'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = "foreach ($d in 'web', 'asset_catalog', 'generated', 'prefabs', 'ies', 'stock_assets', 'fx') {"
assert s.count(a) == 1
s = s.replace(a, "foreach ($d in 'web', 'asset_catalog', 'generated', 'prefabs', 'ies', 'stock_assets', 'fx', 'shaders') {")
b = "# Docs for the mapper."
assert s.count(b) == 1
check = ('''# Files the editor needs at run time: stop when one did not reach the package (the shaders folder was once left out).
foreach ($need in 'data\\shaders\\toon.iw8shader.hlsl', 'data\\shaders\\stock.iw8shader.hlsl', 'data\\web\\index.html', 'data\\web\\app.js',
                  'data\\web\\viewlive.js', 'data\\iw8_entity_defs.json', 'data\\iw8_entity_classes.json', 'data\\hotkey_presets.json',
                  'data\\generated\\iw8_canonical_keys.tsv') {
    if (-not (Test-Path (Join-Path $dest $need))) { throw "package is missing $need" }
}

''').replace('\n', nl)
s = s.replace(b, check + b)
io.open(p, 'w', encoding='utf-8', newline='').write(s)

r = 'E:/IW8-Radiant/docs/RELEASE_CHECK.md'
t = io.open(r, encoding='utf-8', newline='').read()
nl2 = '\r\n' if '\r\n' in t else '\n'
row = '| `data/iw8_entity_classes.json` | `panels.cpp:53`; `brush_tools.cpp:402` | walk up 3 | added |'
assert t.count(row) == 1
t = t.replace(row, row + nl2 + '| `data/shaders/` (`toon.iw8shader.hlsl`, `stock.iw8shader.hlsl`) | `main_window.cpp` (Shader Editor templates, map shader lookup); `compile_panel.cpp` (build) | walk up 3, `data/shaders` | added 2026-11-14 (was missing: the installed editor had no toon shader) |')
io.open(r, 'w', encoding='utf-8', newline='').write(t)
print('ok')
