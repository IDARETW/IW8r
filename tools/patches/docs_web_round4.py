import io

# Docs for the Surface web form and the asset info in the web Assets panel (2026-11-14).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('CHANGELOG.md', '''## 2026-11-14

''', '''## 2026-11-14

- **Web: Surface form and asset info.** Edit > Surface in the web page edits the texture of the selected brushes:
  width, height, offsets, rotation, lightmap scale, and Fit / Natural / Justify, as one undo step; after Apply the
  fields show the result. Each material, model and effect in the Assets panel has an info button with the text of the
  editor's own info window (an effect that has no export yet is exported; open it again a moment later). Checked in a
  real browser.

''')
patch('docs/PROGRESS.md', '''  Open on the web list: Surface / UV as a form, Material / Effect Info, vertex and edge tools.
''', '''  Then: form "surface" (SurfacePanel::current / apply / applyOp / applyLightmapScale through MainWindow::surface_; a
  findChild lookup did not find the panel in a running editor with a restored layout, the GUI test did not show that),
  forms may set "reload" (the page reads the form again after Apply), GET /api/assetinfo?kind=&name= (hook assetInfo,
  showMaterialInfo / showModelInfo / showEffectInfo with show = false; names with a path are refused). The page takes
  scripts, pictures and links out of that HTML before showing it.
  Open on the web list: compile options and Build Budget as a form, vertex and edge tools.
''')
patch('docs/PLAN_REMAINING.md', '''form), Preferences (7), prefabs in the Assets panel (8). Open: compile options and Build Budget (5), Surface / UV
(9), Material / Effect Info (10).
''', '''form), Preferences (7), prefabs in the Assets panel (8), Surface / UV (9), Material / Model / Effect Info (10, the
info button of the Assets panel). Open: compile options and Build Budget (5).
''')
patch('docs/USER_GUIDE.md', '''  Atmosphere form also takes vision overrides (`name=value; name=x y z`).
''', '''  Atmosphere form also takes vision overrides (`name=value; name=x y z`). Edit > Surface edits the texture of the
  selected brushes (size, offsets, rotation, lightmap scale, Fit / Natural / Justify); Apply keeps the form open and
  shows the result.
''')
patch('docs/USER_GUIDE.md', '''it, a model, effect, sound or prefab to place it; prefabs land in front of the live view) and the Log.''',
      '''it, a model, effect, sound or prefab to place it; prefabs land in front of the live view; the round "i" button
  shows the info text of a material, model or effect) and the Log.''')
patch('docs/features/FEATURES.md', '''and Preferences (without the folders and the host command). The prefab library is in the Assets panel.''',
      '''Preferences (without the folders and the host command) and Surface (texture size, offsets, rotation, lightmap scale, Fit / Natural / Justify on the selection). The prefab library is in the Assets panel, and each material, model and effect there has its info text.''')
patch('docs/features/FEATURES.md', '''- Not in the browser yet: vertex, edge and face tools, the full Vision Editor (all 218 fields by name), the surface tools as a form.''',
      '''- Not in the browser yet: vertex, edge and face tools, the full Vision Editor (all 218 fields by name), the compile options.''')
patch('docs/HANDOVER.md', '''- Web page: vertex, edge and face tools; the full Vision Editor and the surface tools as forms.''',
      '''- Web page: vertex, edge and face tools; the full Vision Editor and the compile options as forms.''')
patch('docs/HANDOVER.md', '''the editor's menus, seven dialogs as forms,''', '''the editor's menus, eight dialogs as forms,''')
print('ok')
