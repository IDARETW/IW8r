import io


def edit(p, a, b):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if b in s:
        return
    assert s.count(a) == 1, (p, a[:60])
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


edit('CHANGELOG.md', '''## 2026-11-13

''', '''## 2026-11-13

- **Your maps' props and the gun take their light from where they stand.** The build's light grid (what lights
  models, players and the viewmodel in game) was one flat colour at 70 % of the sun everywhere, so the gun was as
  bright in a closed room as outside. It is now baked from the map itself: open sky where it is visible, light bounced
  off the walls and floors around, dark indoors (plan 1.11, ROADMAP 5.20). Worldspawn `iw8r_flat_light_grid 1` keeps
  the old flat colour. Not seen in game.
''')
edit('docs/PROGRESS.md', '''## Latest (2026-11-13)

''', '''## Latest (2026-11-13)

- M1.11 part 1 (2026-11-13): light grid from the map (ROADMAP 5.20). Core LightGridInput::samplesAt (56 samples in the
  converter's direction order, lightGridSampleDirections = the 4x4x4 shell, z / y / x), palette per cell
  (paletteCellXY 4 x paletteCellZ 1 lattice points, the kept point nearest the cell centre), multithreaded,
  deduplicated, u16 indices, color_count in the JSON; test light_grid_palette_from_samples. compile_iw8: RayBvh of the
  non-sky render surfaces; per cell 96 Fibonacci rays: escape upward = sky (iw8r_bake_sky or 0.55 0.65 0.8 x 0.35) /
  pi, hit = 0.45 x (sun x N.L x visibility + half the sky) / pi; E(n_s) per sample, 255 = 1.0 (the bake's units).
  iw8r_flat_light_grid 1 = the old flat 0.7 x sun colour. Dev showcase: 286,652 points, 10,690 colours; the converter
  reads them ("10690 palette colours (4096x304 image)", 2.0 s).
''')
edit('docs/PLAN_REMAINING.md', '''**1.11 Lighting of a compiled custom map.**''', '''**1.11 Lighting of a compiled custom map.** Part 1 DONE 2026-11-13: the light grid is baked from the map (sky and bounce
per cell, 56 directional samples). Open: the bake items below, probes, the offline parity proof.''')
edit('docs/TEST_CHECKLIST.md', '''## In game: Game Lighting against the game (2026-11-13), the editor next to your in-game shots''', '''## In game: your map's model light (2026-11-13), not seen in game

- [ ] Build the dev showcase (F7) and walk from outside into a closed room: the gun and nearby props get darker inside
      and take the sky's colour outside (before, they were equally bright everywhere). The build log says "light grid:
      ... colour(s) baked from the map".

## In game: Game Lighting against the game (2026-11-13), the editor next to your in-game shots''')
print('ok')
