import io

# Docs: coplanar overlaps, watertight seams, Game Lighting brush outlines, View > Umbra Visibility (plan M2.2).


def patch(p, a, b):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:60])
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('E:/IW8-Radiant/CHANGELOG.md', '''## 2026-11-13

''', '''## 2026-11-13

- **No more z-fighting where brushes overlap.** Two brush faces lying in the same plane on top of each other used to
  be drawn both, flickering against each other in the viewport and in game (Cinder Rail had 2,636 such pairs). Now
  only one is drawn where they overlap: the smaller face, so a panel sits on its wall. The build says how many faces
  it left out because they were wholly covered.
- **No lines at the slices of a wall.** Toon lines never follow the joins between brushes that make one surface, and
  hidden faces (inside or against another brush) carry no edge. The seams between touching brushes are also tight
  now: no dark pixels can show through.
- **Game Lighting hides the editor's brush outlines.** That view shows what the game shows; the outlines made every
  join between brushes look like a line. Selected brushes keep theirs. View > Game Lighting Options > Brush Outlines
  brings them back.
- **View > Umbra Visibility.** For a stock map: Cull by Tome draws only what the game itself would draw from where
  the camera is (its occlusion data hides the rest), and Show Cells and Portals draws the camera's visibility cell,
  its portals and the visible cells around it. The status bar shows how many surfaces, models, lights and probes are
  visible. Both choices are remembered.

''')
patch('E:/IW8-Radiant/docs/PROGRESS.md', '''## Latest (2026-11-13)

''', '''## Latest (2026-11-13)

- Owner (2026-11-13 night): lines at the slices of walls, z-fighting on square structures. (1) Game Lighting no
  longer draws the editor's dark outlines of brush faces (scene.gameOutlines, View > Game Lighting Options > Brush
  Outlines, game/brushOutlines, off). (2) Core face_edges now always shapes brush faces in both hosts
  (edgeBrushMeshes withEdges false = no edge data): coplanarCovers cuts a face away under a coplanar, same-facing,
  overlapping drawn face of another brush of its set (the smaller face wins, the later brush of two equal ones);
  Cinderrail had 2,636 such pairs. (3) Seams between brushes share vertices (allNear: the neighbours' piece corners
  are added to this brush's edges; the viewport key covers the neighbours' neighbours); spans cut where a neighbour
  ends land exactly on its plane. (4) An edge counts only where its face is in the open beside it: no edge where the
  face is inside or against another drawn brush (hidden side faces of overlapping brushes fought with the wall plane
  along a line of dark pixels). Folds under 2 degrees between brushes are seams. Not done: hidden faces are still
  built and drawn (removing them needs to know which brushes are opaque).
- Plan M2.2 (Umbra visibility view) part 1: class 1 user IDs are static model collection indices (VERIFIED,
  tests/umbra_tome_tests.cpp umbra_static_model_ids_on_shipment: 9,336 of 9,413 origins inside their object's box;
  as instance rows 1); StockModelInstance::collection. StockWorld::triSurface (GfxWorld surface of every triangle);
  reference vertices carry it (referenceSurfaceTag in GpuTexVertex::edge, top byte 0). Renderer:
  setReferenceTomeVisibility (surface bits in a Buffer<uint> at VS t30, wind.w = 1 while on; hidden surfaces go off
  screen in vs_tex, still cast shadows; props culled in cullTexInstances by InstancedTexMesh::ids). Core umbra_view
  (umbraTomeIndex, umbraViewVisibility: bits set for visible or unknown to the tome; umbraOverlayLines). Qt:
  main_window_umbra.cpp (tome read and view queries in workers, one at a time, only when the camera pose changed),
  View > Umbra Visibility > Cull by Tome / Show Cells and Portals (view/umbraCull, view/umbraOverlay), status bar
  counts, scene.tomeLines. Bench: IW8R_UMBRA=cull|cells|both adds an "umbra" report line. Open in 2.2: lights, probes
  and decal collections are counted but not culled; displaced terrain has no surface ids.
''')
patch('E:/IW8-Radiant/docs/USER_GUIDE.md', '''## 6. Lighting and atmosphere''', '''### What the game draws from here (Umbra visibility)

- Every stock map carries occlusion data (an Umbra "tome") that tells the game what can be seen from where. **View >
  Umbra Visibility > Cull by Tome** makes the editor draw a stock map the same way: surfaces and models the game
  would not draw from the camera's spot are hidden. Use it to see why something pops in or out in game, or what an
  edit would expose.
- **Show Cells and Portals** draws the camera's visibility cell (yellow), its portals (orange; doors that can close in
  magenta) and the visible cells around it (blue).
- The status bar shows the counts, for example "Umbra: 312 / 1,024 surfaces, 1,204 / 9,413 models". When the camera
  is outside the map's data or inside a wall, nothing is culled and the status bar says so.
- Hidden things still cast their sun shadows, as in game. Lamps and probes are counted but not hidden yet.

## 6. Lighting and atmosphere''')
patch('E:/IW8-Radiant/docs/TEST_CHECKLIST.md', '''- [ ] Colours are stronger than on the normal Cinder Rail, and shade is a cool darker tone, not black.''',
      '''- [ ] Colours are stronger than on the normal Cinder Rail, and shade is a cool darker tone, not black.
- [ ] No lines run across a wall where it is made of several brushes, and nothing flickers (z-fights) on the square
      structures or anywhere two brushes overlap.''')
P = 'E:/IW8-Radiant/docs/PLAN_REMAINING.md'
s = io.open(P, encoding='utf-8', newline='').read()
a = '**2.2 A visibility view in the editor.**'
assert s.count(a) == 1
s = s.replace(a, '**2.2 A visibility view in the editor.** Part 1 DONE 2026-11-13 (View > Umbra Visibility: cull by tome for surfaces\n'
                 'and props, cells and portals overlay, status bar counts, saved settings). Open: culling lights, probes and decal\n'
                 'collections; surface ids on displaced terrain.')
io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('ok')
