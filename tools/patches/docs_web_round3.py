import io

# Docs for the web round of 2026-11-14: Preferences form, vision overrides in the Atmosphere form, prefabs in the
# Assets panel; the offline proofs rerun.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('CHANGELOG.md', '''## 2026-11-14

''', '''## 2026-11-14

- **Web: Preferences, vision overrides and prefabs.** Edit > Preferences opens in the web page with the editing,
  build and F5 settings (the asset folders and the F5 host command stay at the editor: paths and a command line on
  the editor's machine are not for a browser). The Atmosphere form has the map's vision overrides, checked by the
  build's own parser. The Assets panel lists the prefab library with pictures; a click places one in front of the
  live view. Checked in a real browser (a stock door prefab placed and undone).
- **The installed editor has its shader files.** The release package left out `data\\shaders`, so an installed copy had
  no toon shader and no Shader Editor templates. The package now carries them and its build stops when a file the
  editor needs is missing; the packaged editor passes its start-up test and compiles the toon filter from its own
  folder.
- All 19 end-to-end proofs pass again against the current converter (`tools\\run_offline_proofs.py`).

''')
patch('docs/PROGRESS.md', '''## Latest (2026-11-13)

''', '''## Latest (2026-11-13)

- 2026-11-14: handover round. docs/HANDOVER.md rewritten for the current state; TEST_CHECKLIST starts its In game
  part with "the short list" (six items, most important first). tools/package_release.ps1 copies data\\shaders (was
  missing from every package so far) and throws when a run-time file is missing; packaged exe smoke test ok.
  run_offline_proofs.py: 19 of 19 pass with converter 24d1aec7. Web forms: preferences (currentPreferences /
  applyPreferences, build/realScriptables, build/collisionLayout, DevmapRunner play options; no folders, no host
  command), vision overrides in the atmosphere form (visionOverridesJson decides), prefabs in the Assets panel (hooks
  prefabs / placePrefab over PrefabBrowser rows; thumbnails by row; the dock's list is loaded on the first request).
  Open on the web list: Surface / UV as a form, Material / Effect Info, vertex and edge tools.
''')
patch('docs/PLAN_REMAINING.md', '''are plain commands and run from the web menu already. Open: Vision (item 1), compile options and Build Budget (5),
Preferences (7), Prefabs (8), Surface / UV (9), Material / Effect Info (10).
''', '''are plain commands and run from the web menu already. 2026-11-14 also: vision overrides (item 1, in the Atmosphere
form), Preferences (7), prefabs in the Assets panel (8). Open: compile options and Build Budget (5), Surface / UV
(9), Material / Effect Info (10).
''')
patch('docs/USER_GUIDE.md', '''  Warzone start entities. Tools > Check Map shows the findings and can apply the automatic fixes.
''', '''  Warzone start entities. Tools > Check Map shows the findings and can apply the automatic fixes. Edit > Preferences
  has the editing, build and F5 settings (asset folders and the F5 host command are set at the editor only). The
  Atmosphere form also takes vision overrides (`name=value; name=x y z`).
''')
patch('docs/USER_GUIDE.md', '''- **Panels.** Entities (with a filter), the Inspector (edit, add and remove keys), Assets (click a material to apply
  it, a model, effect or sound to place it) and the Log.''', '''- **Panels.** Entities (with a filter), the Inspector (edit, add and remove keys), Assets (click a material to apply
  it, a model, effect, sound or prefab to place it; prefabs land in front of the live view) and the Log.''')
patch('docs/features/FEATURES.md', '''- Dialogs as forms in the page: Sun & Sky, Atmosphere, Lightmap Bake Settings, Map Info, Game Modes (a preset and what to do with it) and Check Map (the findings, and Fix all).''',
      '''- Dialogs as forms in the page: Sun & Sky, Atmosphere (with vision overrides), Lightmap Bake Settings, Map Info, Game Modes (a preset and what to do with it), Check Map (the findings, and Fix all) and Preferences (without the folders and the host command). The prefab library is in the Assets panel.''')
patch('docs/features/FEATURES.md', '''- Not in the browser yet: vertex, edge and face tools, the Vision Editor, Preferences, the prefab and surface tools as forms.''',
      '''- Not in the browser yet: vertex, edge and face tools, the full Vision Editor (all 218 fields by name), the surface tools as a form.''')
patch('docs/HANDOVER.md', '''- `py -3 -I tools\\run_offline_proofs.py <work dir>`: end-to-end proofs. Each one edits or builds a real map with the''',
      '''- `py -3 -I tools\\run_offline_proofs.py <work dir>`: 19 end-to-end proofs (all pass, 2026-11-14). Each one edits or builds a real map with the''')
patch('docs/HANDOVER.md', '''- Web page: vertex, edge and face tools; forms for the Vision Editor, Preferences, prefabs and surface tools.''',
      '''- Web page: vertex, edge and face tools; the full Vision Editor and the surface tools as forms.''')
patch('docs/HANDOVER.md', '''**From a browser.** Tools > Web Server: the editor's menus, six dialogs as forms, the editor's own 3D picture with
fly controls, drawing and resizing brushes, placing assets and entities, the inspector with key help.''',
      '''**From a browser.** Tools > Web Server: the editor's menus, seven dialogs as forms, the editor's own 3D picture with
fly controls, drawing and resizing brushes, placing assets, prefabs and entities, the inspector with key help.''')
print('ok')
