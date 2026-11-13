import io


def edit(p, a, b):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if b in s:
        return
    assert s.count(a) == 1, (p, a[:60])
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


edit('docs/PROGRESS.md', '''## Latest (2026-11-13)

''', '''## Latest (2026-11-13)

- M2.1 DONE (2026-11-13, helper; research 110): core umbra_tome (extractUmbraTomeFromGfxMapJson streams the tome out of
  the gfx_map export; UmbraTome::parse / validate; tiles (k-d tree, cells 36 B, portals 16 B, the point-to-cell tree),
  LOD tiles, clusters, objects with user IDs (class in bits 28-30); queryPoint / queryView -> visible cells, objects and
  user IDs by class). Verified structurally on shipment, hideout, aniyah_tac, backlot2, rust, village2, emporium,
  m_speedball and Verdansk; portal bit fields and camera quantisation confirmed in the DW exe's code. Query gaps: finest
  tiles only (the coarse-tile rule is INFERRED), gates always open, no contribution cull, screen-rectangle clipping (only
  ever shows more). 2-25 ms per all-direction query on MP maps, 70 ms on Verdansk. Tests umbra_tome_tests (1,267 tests).
  Crash work: bad_alloc in setReferenceExtras (a group's vertices concatenated by a doubling vector) -> batches are
  uploaded straight into a DEFAULT buffer; setReferenceTextured reserves its total; abort() writes abort-<tick>.txt
  (the C++ exception in flight and module+offset frames; tools/symbolize_trace.py resolves them against the PDB); the
  crash report keeps a 64 MB reserve and writes its text before the dump; the bench appends memory.log every 2 s.
''')
edit('docs/PLAN_REMAINING.md', '''**2.1 Decode the tome.**''', '''**2.1 Decode the tome.** DONE 2026-11-13 (research 110, core umbra_tome with a visibility query and tests on nine
stock zones).''')
print('ok')
