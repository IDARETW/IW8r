import io
p = 'docs/PROGRESS.md'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = '## Latest (2026-11-13)' + nl
b = a + nl + '''- Later on 2026-11-13: displacement drawn (research 101); Warzone super terrain drawn (ACTS fork 8b5ee11d deployed:
  build\\bin\\Release\\data\\mw19\\schema.json; research 102; core stock_super_terrain); stand-in boxes out of the game views;
  prop screen culling at 4 px (Verdansk 37 -> 15.5 ms). Frame times with Game Lighting: Speedball 4.3-5.7, King 4.0,
  Shipment 5.2, Rust 5.8, Speed 5.7, Backlot 7.1, Hideout 7.2, Emporium 7.3, Fork 7.4, Village 11.4, Verdansk 15.5,
  Aniyah Tac 18 ms. Installer rebuilt (no ini).
- Known limits: GPU clutter on Verdansk not drawn (its density masks are streamed per surface; core stock_clutter can
  scatter it once they are exported); terrain layer textures not blended (the baked albedo is used); prop LODs not
  used (screen culling instead).
'''.replace('\n', nl)
assert s.count(a) == 1
s = s.replace(a, b, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
