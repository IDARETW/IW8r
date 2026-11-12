import io

def load(p):
    s = io.open(p, encoding='utf-8', newline='').read()
    return s, ('\r\n' if '\r\n' in s else '\n')

def save(p, s):
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

p = 'docs/ROADMAP.md'
s, nl = load(p)
lines = s.split(nl)
hit = [i for i, l in enumerate(lines) if l.startswith('| 3.11 |')]
assert len(hit) == 1
cells = lines[hit[0]].split(' | ')
assert len(cells) == 5, len(cells)
text = ('Update (2026-11-11 late, research 85): the camera view keeps its geometry between frames in per-item vertex slices and uploads only the changed byte ranges; lines and overlays are no longer copied each frame; the sun shadow map is redrawn only when a caster or the sun changes. '
        'Measured (total ms per frame, before to after): 10,000 brushes still 66.5 to 10.5, select 1,073 to 11.1; 50,000 brushes still 165 to 35.2, select 2,434 to 38.5, peak memory 8.1 to 5.3 GB. All 66 comparison screenshots are identical to the old renderer. '
        'Same day: models inside placed prefabs and bundles draw as real models (nested prefabs to 8 levels), a placed dynamic_prop draws its model, a scriptable draws its definition model, and editor markers (stock-prop cubes, boxes inside prefabs) cast no shadow and stay out of probe captures. '
        'Left for later: outline lines are the largest still-frame cost on very large maps, and instancing of repeated models; both change the picture and need a decision.')
assert '|' not in text
cells[3] = cells[3].rstrip() + ' ' + text
lines[hit[0]] = ' | '.join(cells)
save(p, nl.join(lines))

p = 'docs/TEST_CHECKLIST.md'
s, nl = load(p)
a = '## In game: decals and hardpoint markers (2026-11-11 late), not seen in game'
assert s.count(a) == 1
sec = [
 '## In game: camera view speed and prefab models (2026-11-11 late), editor only, not seen',
 '',
 '- [ ] Restart the editor and open a large map. Selecting and moving things in the camera view is immediate.',
 '- [ ] Place the bundle "Jump pad with effect and sound" from the Prefabs dock: its manhole cover is drawn as the',
 '      real model, not as a small box.',
 '- [ ] A physics prop (dynamic_prop) is drawn as its model.',
 '',
]
s = s.replace(a, nl.join(sec) + nl + a)
save(p, s)

p = 'docs/PROGRESS.md'
s, nl = load(p)
a = '  - Still open: impacts (F.12), the script-played loop of an own sound (F.6), reverb, probes, game modes, killstreak'
assert s.count(a) == 1
add = [
 '  - STATE AT THE END OF 2026-11-11 (handover build):',
 '    Viewport (research 85, two helper agents): kept geometry with ranged uploads; prefab / bundle models, a',
 '    dynamic_prop and a scriptable draw as models; editor markers out of shadows and captures. New files:',
 '    src/viewport/camera_geometry.cpp, dirty_ranges.h, reference_markers.h, scriptable_model.h,',
 '    src/qtapp/qt_gui_test_prefab_models.*; benchmark tools tools/run_viewport_bench.py, make_perf_maps.py,',
 '    compare_bench_images.py, viewport_bench_table.py (data in out/perf, 1.4 GB, can be deleted).',
 '    Known limits from the helper: a nested prefab resolves its model path like a top-level one; a prefab',
 '    placement modelscale moves but does not resize inner models (same as the build); brush faces inside prefabs',
 '    are untextured in the camera.',
 '    Tests on the final build: core 1114 tests / 139,154 checks / 0 failures, GUI 879 / 0. Headless builds:',
 '    mp_scrtest (3 scriptable instances) and mp_iw8r_show2 (decal built) ok, every loader line 0 failed; the',
 '    packaged editor builds its sample map.',
 '    RELEASE: out/package/IW8RadiantSuite, out/package/IW8RadiantSuite-20261111.zip,',
 '    out/installer/IW8RadiantSuite-Setup-0.1.0.exe, all rebuilt after the last source change.',
 '    NOT DONE, needs the game: real stock doors / cones / cars (research 86 step F3); everything marked',
 '    "not seen in game" in docs/TEST_CHECKLIST.md.',
]
s = s.replace(a, nl.join(add) + nl + a)
save(p, s)
print('ok')
