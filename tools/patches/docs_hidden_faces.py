import io

# Docs for the hidden-faces rule (core face_edges hiddenCovers).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('CHANGELOG.md', '''## 2026-11-13

''', '''## 2026-11-13

- **Brush faces nobody can see are not drawn and not built.** A face that lies inside another brush, or flat against
  one, is left out when that brush is opaque: in the viewport and in the built map. A brush with a cut-out or
  see-through material (glass, fences, foliage cards) hides nothing, and neither does a material the editor does not
  know yet. Cinder Rail (Toon) builds with 109,224 world triangles instead of 147,754, and the toon shader's lines no
  longer show where such a face met a wall. Checked: the built map passes the game's loaders; the viewport picture
  with and without the rule (bench switch `IW8R_NO_HIDE_FACES`) differs in at most 0.16 % of the pixels, all thin
  lines at such joints. Not seen in game.

''')
patch('docs/PROGRESS.md', '''## Latest (2026-11-13)

''', '''## Latest (2026-11-13)

- Hidden faces (follow-up of the owner's "lines at slices / z-fighting" request): core face_edges hiddenCovers +
  EdgeBrush::opaque + edgeBrushOpaque (a brush whose drawn faces are all opaque). A face piece inside or flat against
  an opaque brush is dropped in edgeBrushMeshes (build and viewport share it). Build: opaque from the material library
  (no alphaTest, no blend, blendMode 0). Viewport: Scene::opaqueFor (main_window.cpp: custom material flags, else
  materialBlendFromTechset == Opaque; unknown = not opaque; invalidateSceneSoon on materialReady; bench switch
  IW8R_NO_HIDE_FACES). Test face_edges_hidden_faces_are_not_drawn. Cinder Rail (Toon): 147,754 -> 109,224 world
  triangles, 5 zones pass ACTS (out/agent/toon_build). Viewport A/B with the real material cache (out/agent/hf_on1/2
  vs hf_off1/2): 0.04-0.16 % of pixels differ, thin lines at joints only (out/agent/hf_cmp_1.png). Full suite passed
  (core 180,478 checks / 1,297 tests, GUI 1,016).
''')
print('ok')
