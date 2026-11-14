import io

# Docs for custom shaders on stock maps (research 115).


def patch(p, a, b):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:60])
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('E:/IW8-Radiant/CHANGELOG.md', '''## 2026-11-13

''', '''## 2026-11-13

- **Custom shaders on stock maps.** A shader file can now hold a second function, `iw8rFilter`, for surfaces that keep
  the game's own lighting. Set a shader for an opened stock map (Tools > Shader Editor > Use for This Map) and the
  viewport shows its world and props through the filter; File > Rebuild Stock Zone builds the filter into the map's
  own lit shaders and writes the patch files as usual. The toon shader has such a filter: vivid colour and the light
  in steps. Limits: no ink lines on stock maps (their geometry has no edge data), and characters, weapons and effects
  keep their look (their shaders are shared by every map). Checked offline: the edited Shipment zone loads in the
  game's own loaders and Direct3D accepts all 217 changed shaders. Not seen in game (research 115).

''')
patch('E:/IW8-Radiant/docs/PROGRESS.md', '''## Latest (2026-11-13)

''', '''## Latest (2026-11-13)

- Owner (2026-11-13 late night): "make a patch ff to make shipment use the toon shader". Research 115. Core
  dxbc_splice (DXBC container, SHEX tokens, checksum = modified MD5: reproduces all 218 stock checksums;
  dxbcSpliceFilter moves a filter shader's instructions before a shader's final ret). custom_shader: iw8rFilter
  (customShaderHasFilter / HasSurface, customShaderFilterProgramSource, customShaderFilterViewportSource). Viewport:
  SceneRenderer::setReferenceFilterShader (IW8R_FILTER variant of ps_tex on the reference world and props, the
  filter on colour x game0.y), compileProgram, pixelShaderAccepted; CameraView::setReferenceFilterLookup. Stock
  rebuild: lit_filter.cso + stockLitFilterJson -> editzone "lit_filter". Converter x64-zt 14bff0d5 (worktree, deployed
  to E:/mw124/x64-zt, old exe out/zt_backup/zonetool_7bc751ba.exe): dxbc_splice.hpp (copied by
  tools/patches/zt_lit_filter.py), editzone "lit_filter" + "techsets_only". CLI --shader-filter-program; bench
  IW8R_BENCH_SHADER. Shipment: 366 sets / 369 techniques / 217 shaders changed, patch
  out/agent/toon_ship/edit/patch/techsets_mp_shipment.fp (140 KB, toon filter), ACTS loads the edited zone, a D3D11
  device accepts all 217 (GUI test). NOT seen in game: pipeline states for the new hashes are the open risk.
''')
patch('E:/IW8-Radiant/docs/USER_GUIDE.md', '''- The example map Cinder Rail (Toon) uses it. Custom shaders are built and checked offline but not yet seen in game.''',
      '''- The example map Cinder Rail (Toon) uses it. Custom shaders are built and checked offline but not yet seen in game.
- **Stock maps.** A stock map's surfaces keep the game's own lighting, so a shader changes them through a second
  function: `float3 iw8rFilter(float3 colour)`, which gets the finished colour of a pixel and returns the new one.
  Open the stock map, choose the shader in Tools > Shader Editor (Use for This Map), and the viewport shows the world
  and props through it. File > Rebuild Stock Zone builds it into the map's lit shaders; File > Install Edited Stock
  Map Into Game installs the patch files as for any stock edit. The filter cannot use textures or the edge inputs,
  and it does not reach characters, weapons or effects.''')
patch('E:/IW8-Radiant/docs/TEST_CHECKLIST.md', '''## What is not expected to work yet
''', '''## In game (optional, 2026-11-13): Shipment with the toon shader's filter

Patch: `out/agent/toon_ship/edit/patch/techsets_mp_shipment.fp` (or rebuild it: open Shipment, Tools > Shader Editor,
toon, Use for This Map, File > Rebuild Stock Zone, File > Install Edited Stock Map Into Game). Game closed while
installing; File > Restore Stock Map In Game (or deleting the copied `.fp`) takes it out again.

- [ ] Shipment loads and plays (this is the first stock map whose lit shaders were replaced: if it does not load, or
      the world is black or invisible, restore and tell me).
- [ ] The world and the containers have stronger colours and the light falls in steps instead of smooth gradients.
- [ ] Characters, weapons and effects look as always.
- [ ] The first load may take longer (the game builds the changed shaders' pipelines).

## What is not expected to work yet
''')
R = 'E:/IW8-Radiant/docs/research/README.md'
s = io.open(R, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
i = s.index('114_custom_pixel_shader_interface.md')
j = s.index(nl, i) + len(nl)
line = s[s.rfind(nl, 0, i) + len(nl):j]
print(repr(line[:200]))
new = line.replace('114_custom_pixel_shader_interface.md', '115_stock_lit_shader_filter.md')
import re
new = re.sub(r'^(\W*)(\[?)114', lambda m: m.group(1) + m.group(2) + '115', new)
# Replace the description after the link with ours, keeping the line's shape (list item or table row).
if '|' in line:
    cells = line.rstrip('\r\n').split('|')
    cells[-2 if cells[-1].strip() == '' else -1] = ' A custom shader on a stock map: a colour filter spliced into the game\'s compiled lit pixel shaders (DXBC splice, checksum, editzone lit_filter; Shipment) '
    cells = [c.replace('114_custom_pixel_shader_interface.md', '115_stock_lit_shader_filter.md').replace('114', '115', 1) if k == 1 else c for k, c in enumerate(cells)]
    new = '|'.join(cells) + nl
else:
    k = new.find(')')
    new = new[:k + 1] + ' - a custom shader on a stock map: a colour filter spliced into the game\'s compiled lit pixel shaders (DXBC splice, checksum, editzone lit_filter; Shipment)' + nl
s = s[:j] + new + s[j:]
io.open(R, 'w', encoding='utf-8', newline='').write(s)
print(repr(new[:220]))
