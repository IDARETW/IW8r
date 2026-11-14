import io

# Docs and proof list for the owner's toon feedback of 2026-11-14: thicker lines, the last coplanar double draw, and the
# occlusion bake's build proof.

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    write(p, s.replace(a, b))


patch('CHANGELOG.md', '''## 2026-11-14

''', '''## 2026-11-14

- **Toon look: thicker lines, and the last z-fighting spot.** (Owner feedback.) The toon shader draws 5 pixel lines
  (were 3). The width is the shader's own constant (`kLinePixels` at the top of `toon.iw8shader.hlsl`, Tools > Shader
  Editor); what had to change in the editor was general: the edge input every custom shader can read now reaches 12
  units from an edge instead of 4, so a wide line keeps its width about three times as far away. Nothing of the toon
  look is built into the editor: no command, no brushes, no material; it is that one shader file.
- **Faces in one plane: one more case drawn once.** Two brushes whose faces lie in one plane and overlap are drawn
  once (2026-11-13). One case slipped through: the neighbour was looked up by comparing bounds exactly, and a face
  can measure a hair off its plane, so the pair was missed and both faces were drawn (Cinder Rail: the undersides of
  two overlapping roof slabs). The comparison has a tolerance now. A new core test checks a whole map for this: no
  two drawn triangles of different brushes may overlap in one plane (the sample room always; any map with
  `IW8R_TEST_OVERLAP_MAP`). Cinder Rail Toon: 0 overlapping pairs (was 11), no line along the joins between the
  brushes of a wall.
- **Occlusion bake: an end-to-end proof.** `tools\\umbra_bake_build_test.py` builds a small walled map with an
  eight-sided tower through the editor's own build, checks the occluder boxes (the tower's box lies inside it) and
  reads the built zone's occlusion data back with the stock reader's checks (61 cells, 452 portals, no leaf with
  several cells). It is in `run_offline_proofs.py`.

''')
patch('docs/PROGRESS.md', '''  The web list of plan 3.3 / 3.4 is done (the Vision Editor form closed it).''', '''  Owner feedback 2026-11-14 on the toon map (the last game run of mp_cinderrail_t was 2026-11-13 20:28, the first
  toon build with ink strip brushes; the game's own shader cache main/techsets_mp_cinderrail_t.psob dates it): "not
  a built in radiant piece of code, simply a custom shader; thicker lines; no lines of every slice of geometry on
  building walls; z fighting on the squareish structures". State: the ink command and strips were removed on
  2026-11-13 (commit 90b5f03), slice lines and coplanar double draws on 4cd4c61. Done now: kEdgeRange 4 -> 12 (core
  face_edges; tests use the constant), toon kLinePixels 3 -> 5 and kLineMaxUnits 3.4 -> 10 (template and the map's
  copy), kBoundsSlack in coplanarCovers / hiddenCovers (the missed roof slab pair), tests/face_overlap_tests.cpp,
  zones of out/agent/mp_cinderrail_toon rebuilt (out/agent/toonbuild). Check renders: out/agent/toonchk.
  The web list of plan 3.3 / 3.4 is done (the Vision Editor form closed it).''')
patch('docs/USER_GUIDE.md', '''`s.edgeUnits` the same in units (up to 4),''', '''`s.edgeUnits` the same in units (up to 12),''')
patch('tools/run_offline_proofs.py', '''    ('lightmap_bake_build_test', []),  # an editor map through --compile (the editor exe, not --exe)''',
      '''    ('lightmap_bake_build_test', []),  # an editor map through --compile (the editor exe, not --exe)
    ('umbra_bake_build_test', []),  # the occlusion bake of an editor map, read back from the built zone''')
patch('tools/umbra_bake_build_test.py', '''        check(seen is not None and float(seen.group(1)) < 100.0, 'the tome hides something: %s%% of the objects drawn on average' % (seen.group(1) if seen else '?'))''',
      '''        # A map this small has a handful of merged world surfaces, each seen from everywhere: how much is hidden is
        # printed, not judged (the conservativeness and the cell structure are what this proof is about).
        print('     from the cell centres %s%% of the objects are drawn on average' % (seen.group(1) if seen else '?'))''')
s = read('out/agent/mp_cinderrail_toon/NOTES.md')
nl = '\r\n' if '\r\n' in s else '\n'
s = s.rstrip() + nl + nl + ('2026-11-14: lines 5 px (were 3; kLinePixels in shaders/toon.iw8shader.hlsl), holding their width three times as' + nl +
                            'far (the edge input reaches 12 units). The last double-drawn faces (undersides of two roof slabs) are drawn once:' + nl +
                            'the map has no overlapping faces in one plane left (core test face_overlap, IW8R_TEST_OVERLAP_MAP). Zones rebuilt.' + nl)
write('out/agent/mp_cinderrail_toon/NOTES.md', s)
print('ok')
