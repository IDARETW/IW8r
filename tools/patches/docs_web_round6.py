import io

# Docs for the Layers and Compile web forms (2026-11-14): with these, every item of plan 3.3 and 3.4 has its web part.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('CHANGELOG.md', '''## 2026-11-14

''', '''## 2026-11-14

- **Web: Layers, and Compile Options with the Build Budget.** View > Layers in the web page shows or hides a layer
  (the page's views follow), selects a layer's brushes and patches, moves the selection to a layer and declares a new
  one. Compile > Compile Options and Build Budget has the map id, the three build switches (stock textures, bake
  lightmaps, occlusion bake) and the last build's figures against their limits. A layer hidden from the page also
  unticks its row in the editor's Layers dock. Checked in a real browser on a built map.

''')
patch('docs/PROGRESS.md', '''  Open on the web list: compile options and Build Budget as a form.
''', '''  Then: forms "layers" (LayersPanel::rows / setLayerVisible / selectLayer / moveSelectionToLayer / newLayer; rows()
  reads hiddenLayers_, and setLayerVisible now also sets the row's tick) and "compile" (CompilePanel::setMapId, the
  three check boxes by object name, budgetGauges / budgetWarnings of <build root>/last_build_report.json as a report).
  The web list of plan 3.3 / 3.4 is done except: the full Vision Editor (218 fields; overrides by name are in the
  Atmosphere form), patch vertex editing, face picking with surface fit.
''')
patch('docs/PLAN_REMAINING.md', '''info button of the Assets panel). Open: compile options and Build Budget (5).
''', '''info button of the Assets panel), compile options and Build Budget (5). All ten are in the page.
''')
patch('docs/PLAN_REMAINING.md', '''commands from the web menu. Open: vertex and edge modes, region and layers in the page, face picking with surface fit.''',
      '''commands from the web menu. 2026-11-14 also: vertex and edge handles (POST /api/vertices), layers (the Layers form;
hidden layers leave the page's views), region (View > Region from the web menu; the page draws what the editor
shows). Open: patch vertex editing, face picking with surface fit.''')
patch('docs/USER_GUIDE.md', '''  shows the result.
''', '''  shows the result. View > Layers shows and hides layers, selects a layer's brushes and moves the selection to a
  layer. Compile > Compile Options and Build Budget has the map id, the build switches and the last build's figures
  against their limits.
''')
patch('docs/features/FEATURES.md', '''- Not in the browser yet: patch vertex editing, the full Vision Editor (all 218 fields by name), the compile options.''',
      '''- Layers (show, hide, select, move the selection, declare) and the compile options with the Build Budget are forms too.
- Not in the browser yet: patch vertex editing, the full Vision Editor (all 218 fields by name; overrides by name are in the Atmosphere form).''')
patch('docs/HANDOVER.md', '''- Web page: patch vertex editing; the full Vision Editor and the compile options as forms.''',
      '''- Web page: patch vertex editing; the full Vision Editor (vision overrides by name are in the Atmosphere form).''')
patch('docs/HANDOVER.md', '''the editor's menus, eight dialogs as forms,''', '''the editor's menus, ten dialogs and docks as forms,''')
print('ok')
