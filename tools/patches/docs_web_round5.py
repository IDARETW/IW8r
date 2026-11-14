import io

# Docs for the vertex and edge tools of the web page (2026-11-14).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('CHANGELOG.md', '''## 2026-11-14

''', '''## 2026-11-14

- **Web: vertex and edge tools.** The web page's toolbar has Tool: Move / resize, Vertices, Edges. With Vertices or
  Edges the selected brushes show a handle on every corner or edge of the 2D view; dragging one moves every corner
  under it (a corner handle in the Top view is the whole upright edge, an edge handle is the whole side), with a live
  preview, snapped to the grid, as one undo step. A move that would leave a brush that is not convex is refused and
  nothing changes. Checked in a real browser with the mouse.

''')
patch('docs/PROGRESS.md', '''  Open on the web list: compile options and Build Budget as a form, vertex and edge tools.
''', '''  Then: POST /api/vertices {moves:[{e,i,from:[[x,y,z]..]}], d} (brushes only; each point is matched to the nearest
  corner within 0.5 units; moveBrushVerts is tried on every brush first, so the request is all or nothing, then
  moveVertexHandles makes the one undo step). Page: #editMode, shapeHandles / shapeHandleAt / shapeOffset in app.js;
  handles merge corners with the same 2D position over all selected brushes. Patches have no web vertex tool.
  Open on the web list: compile options and Build Budget as a form.
''')
patch('docs/USER_GUIDE.md', '''- **Panels.** Entities (with a filter), the Inspector''', '''- **Vertices and edges.** Tool in the toolbar: with Vertices or Edges, the selected brushes show a handle on every
  corner or edge of a Top, Front or Side view. Drag a handle: every corner under it moves, so the faces stay flat. A
  move the brush cannot take is refused with the reason in the status line. Patches are shaped at the editor.
- **Panels.** Entities (with a filter), the Inspector''')
patch('docs/features/FEATURES.md', '''- Not in the browser yet: vertex, edge and face tools, the full Vision Editor (all 218 fields by name), the compile options.''',
      '''- Vertex and edge tools in the 2D views: a handle on every corner or edge of the selected brushes, dragged with a live preview; one undo step, and a move that breaks the brush is refused.
- Not in the browser yet: patch vertex editing, the full Vision Editor (all 218 fields by name), the compile options.''')
patch('docs/HANDOVER.md', '''- Web page: vertex, edge and face tools; the full Vision Editor and the compile options as forms.''',
      '''- Web page: patch vertex editing; the full Vision Editor and the compile options as forms.''')
patch('docs/HANDOVER.md', '''fly controls, drawing and resizing brushes, placing assets, prefabs and entities, the inspector with key help.''',
      '''fly controls, drawing, resizing and reshaping brushes (corner and edge handles), placing assets, prefabs and
entities, the inspector with key help.''')
print('ok')
