import io

# Docs for Brush > Paint Mesh on Faces (ROADMAP 5.9 / 8.11, 2026-11-14).

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


def replace_row(p, start_text, new_row):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    assert s.count(start_text) == 1, (p, s.count(start_text))
    start = s.index(start_text)
    end = s.index(nl, start)
    write(p, s[:start] + new_row + s[end:])


patch('CHANGELOG.md', '''## 2026-11-14

''', '''## 2026-11-14

- **Blend painting on brush surfaces: Brush > Paint Mesh on Faces.** A brush face has only its corners, so there
  was nothing to paint a blend on; you had to turn the brush into patches and then into a terrain mesh by hand. The
  new command lays a mesh patch with a control point every N units (you choose; 32 by default) on the faces you
  selected with Ctrl+Shift+click, or on every drawn face of the selected brushes. The patch has the face's material
  and texture placement. The face under it becomes caulk, so the brush keeps its shape and its collision and only
  the patch is drawn. It is one undo step, and Vertex Paint Mode starts right after. The camera view no longer
  draws a tool face that lies wholly under a flat patch (the two fought for every pixel). A map with such a mesh
  compiles (test); how the game's layered materials use the painted colours is still not seen in the game.

''')
patch('docs/PROGRESS.md', '''  Plan 1.8 white balance: core white_balance (gameWhiteBalanceMatrix; research 120), GamePostFx::whiteBalance,''',
      '''  ROADMAP 5.9 / 8.11: core convert_ops brushFacePaintMeshes / addPaintMeshesToBrushFaces (face selection, else the
  selected brushes' drawn faces; caulk under the patches; one undo step), MainWindow::addPaintMeshes, command
  PaintMeshOnFaces (setting paintMesh/spacing). CameraGeometry::Impl::findCovers / toolFaceCovered: a brush tool
  face wholly under a flat patch is not drawn (hash in the global key; IW8R_NO_PATCH_COVERS = test switch).
  Plan 1.8 white balance: core white_balance (gameWhiteBalanceMatrix; research 120), GamePostFx::whiteBalance,''')
patch('docs/PLAN_REMAINING.md', '''  - layered blend painting (5.9 / 8.11): vertex paint on patches is built; painting layer blends on brushes is not.''',
      '''  - layered blend painting (5.9 / 8.11): vertex paint on patches is built; brush surfaces get a paint mesh with
    Brush > Paint Mesh on Faces (done 2026-11-14). How the game's layered materials use the colours: game.''')
replace_row('docs/ROADMAP.md', '| 5.9 | Layered materials: vertex-colour blend painting on brushes/patches |',
            '| 5.9 | Layered materials: vertex-colour blend painting on brushes/patches | IW8 | partial: patches are painted with Patch > Vertex Paint Mode (see 8.11); brush surfaces get a paint mesh with Brush > Paint Mesh on Faces (2026-11-14: a mesh patch with a control point every N units on the selected faces, caulk under it, one undo step; core `convert_ops`, compile test). How Replay\'s layered materials use the colour is UNVERIFIED in game | L |')
patch('docs/USER_GUIDE.md', '''- **Film grain.** On a stock map the picture has the game's fine grain,''',
      '''- **Painting a blend on a brush surface.** Select the faces (Ctrl+Shift+click) or whole brushes and run Brush >
  Paint Mesh on Faces. It asks how far apart the paint points should be (32 units is a good start; smaller is finer)
  and lays a mesh on each face with the same material and texture placement. The face under the mesh becomes caulk:
  the brush still blocks the player, only the mesh is drawn. Vertex Paint Mode starts right after (colour, alpha,
  radius, strength); click on the mesh to paint. Ctrl+Z takes the whole step back.
- **Film grain.** On a stock map the picture has the game's fine grain,''')
print('ok')
