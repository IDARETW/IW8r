import io

# PROGRESS handoff note, 2026-11-14 16:15: everything of the owner's afternoon queue is built, proven offline and
# deployed; what is left.

ROOT = 'E:/IW8-Radiant/'
p = 'docs/PROGRESS.md'
s = io.open(ROOT + p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = '  STATE 2026-11-14 16:00 (read this first; everything below it in this block is older detail).'
assert s.count(a) == 1
note = '''  STATE 2026-11-14 16:15 (read this first; the 16:00 block below lists the same work as "not built": it is all built
  now). Converter a0a50852 is committed in the worktree and LIVE (E:\\mw124\\x64-zt fast-forwarded and rebuilt 16:06;
  old exe out/zt_backup/zonetool_1d3ff9c0.exe). Suites: core 181,925 checks / 1,377 tests, GUI 1,082, smoke ok, 0
  failures. 25 offline proofs pass against a0a50852 (the two live-vs-new ones rerun after the deploy). Stand-alone
  proof tools/scene_depth_build_test.py: Cinder Rail Toon package 26 of 26 lit techniques read the scene depth;
  Shipment (--stock --min 300) 365 techniques, ACTS 621 tested 0 failed.
  DEPLOYED 16:01-16:04 at the owner's request: mp_cinderrail_t (built from out/agent/mp_cinderrail_toon with
  --compile --deploy; old package in <game>\\.proxy\\backups\\mw120r-map-20261114-160421; haze desert 0.7, sky
  sky_mp_aniyah_tac x 3, exposure +1, baked lightmaps and occlusion as the owner set) and Shipment's
  techsets_mp_shipment.ff (out/agent/toon_ship/edit3, 3,694,056 bytes, md5 76E8FCF2...; 215 of 217 lit shaders with
  the screen filter; the zone installed before it is in out/agent/toon_ship/game_backup_20261114b; stock in
  <game>\\iw8r_stock, restore with --restore-stock mp_shipment). Recipe for Shipment: IW8Radiant.exe
  --shader-filter-program data\\shaders\\toon.iw8shader.hlsl --out toon_filter3.cso (also writes .screen.cso), then
  zonetool -game iw8 -path <game> editzone mp_shipment edits3.json edit3, then --install-stock edit3.
  NOT SEEN IN GAME: all of it. docs/TEST_CHECKLIST.md has three new rows (toon lines, Shipment lines, haze).
  OPEN after this round: the toon on the viewmodel (shared techset zones: ask the owner first); the viewport's
  preview of a stock map with the screen filter is wired (IW8R_FILTER_SCREEN) but no picture of it was taken; the
  viewport volumetric march is still not compared with the game's numbers (research 109 section 2.7); volumetric
  boxes' masks; fog spline (t120) for see-through surfaces; mount edges (research 125); the installer is not
  rebuilt since f082308.
'''.replace('\n', nl)
s = s.replace(a, note + a)
io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
