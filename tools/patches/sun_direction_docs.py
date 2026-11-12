import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b, 1)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('docs/ROADMAP.md', [
 ('''tools/stock_sky_swap_test.py: mp_shipment with the sky of mp_backlot2, ACTS 0 failed). Open: the sun direction | M, **R** |''',
  '''tools/stock_sky_swap_test.py: mp_shipment with the sky of mp_backlot2, ACTS 0 failed). Sun direction (2026-11-12, x64-zt f9c14d9f): worldspawn iw8r_sun_direction ("pitch yaw", File > Relight Stock Map Sun); Rebuild Stock Zone turns the sun light, re-bakes the stock sun shadow from the stock world and its shadow-casting props (stock shadow format read in full: per-tile depth ranges, empty-child depths), turns the vision's fogSunDir and rewrites worldspawn sundirection. tools/stock_sun_direction_test.py on mp_shipment: new shadow agrees with the world geometry (99.3 % of hidden samples shadowed, roofs 9 / 9), a re-bake at the stock direction agrees with the stock bake on 95-96 % of samples, everything else unchanged, ACTS as stock. Stays with the old sun: lightmaps and light grid (bounce light), a sun painted into the sky; cut-out props (fences, leaves) do not cast in the re-bake. Warzone (streamed sun shadow) refused. Not seen in game. | M, **R** |'''),
])
patch('CHANGELOG.md', [
 ('''Warzone and stock maps (later the same day):
''', '''Warzone and stock maps (later the same day):

- **Turn a stock map's sun.** File > Relight Stock Map Sun takes a direction (pitch yaw). Rebuild Stock Zone turns
  the sun, re-bakes its shadows from the stock world and props, and turns the sun-tinted fog with it. The baked
  bounce light of the lightmaps keeps the old sun. Proven offline on Shipment (tools/stock_sun_direction_test.py).
  Not seen in game.
- **Bake lightmaps checkbox** in the Compile panel (worldspawn `iw8r_bake_lightmaps`).
'''),
])
patch('docs/PROGRESS.md', [
 ('''  holds the patched copy once. The exported scriptable lists are identical (62 / 62): not a loss.''',
  '''  holds the patched copy once. The exported scriptable lists are identical (62 / 62): not a loss.
- Then stock sun direction (x64-zt f9c14d9f, live; old exe out/zt_backup/zonetool_8ed35d9a.exe; the live checkout's
  gitignored build/convert-iw3-iw8.vcxproj got stock_sun.cpp by hand, as the worktree's): editzone sun.direction,
  src/convert/iw3_iw8/stock_sun.*, replay_sunshadow Bake tolerance; editor iw8r_sun_direction, Relight dialog field;
  tools/stock_sun_direction_test.py in run_offline_proofs.py. Compile panel Bake lightmaps checkbox.'''),
])
print('ok')
