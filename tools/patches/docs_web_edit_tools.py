import io

# Docs for the browser's editing tools (plan 3.4).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('CHANGELOG.md', '''## 2026-11-13

''', '''## 2026-11-14

- **Web: building in the browser.** "Draw brush" in the page, then a drag in a Top, Front or Side view, makes a
  brush with the current material (64 units in the third direction). A selected brush shows a box with a handle on
  each side: drag one to resize along that side while the opposite side stays. The Inspector shows what the entity's
  class is, the help text of each key (required keys are marked), suggests the keys the class knows, and lists what
  Check Map says about the entity. "Entity class to place" under the entity list places a point entity of any
  class: on the surface at the centre of the live view, or at the 2D view's centre. API: `POST /api/brush`,
  `POST /api/resize`, `GET /api/entityclasses`, `GET /api/entitydef`, `POST /api/entity`. Checked in a real browser.

## 2026-11-13

''')
patch('docs/PROGRESS.md', '''## Latest (2026-11-13)

''', '''## Latest (2026-11-13)

- 2026-11-14, plan 3.4: POST /api/brush (createBoxBrush), POST /api/resize (scaleSelectionAbout), GET
  /api/entityclasses + /api/entitydef (checkMapEntityDefs(), a new getter in find_dialog; "problems" = validate),
  POST /api/entity (point classes; at or view). Page: drawBrush button and drag mode "draw", selectionBox / handleAt
  and drag mode "resize", inspector classHelp / entityProblems / keyList, addEntity form. Browser check on 8791.
  Open in 3.4: vertex and edge modes, region, layers in the page (clone, delete, copy, paste, rotate run from the
  web menu as commands).
''')
patch('docs/PLAN_REMAINING.md', '''**3.4 Editing tools.**
''', '''**3.4 Editing tools.** DONE 2026-11-14 for: drawing a brush, resize handles, placing any entity class (on the
surface in the live view), the inspector with entity definitions; clone / delete / copy / paste / rotate run as
commands from the web menu. Open: vertex and edge modes, region and layers in the page, face picking with surface fit.
''')
patch('docs/USER_GUIDE.md', '''- **Panels.** Entities (with a filter), the Inspector (edit, add and remove keys), Assets (click a material to apply''', '''- **Building.** **Draw brush**, then drag a rectangle in a Top, Front or Side view: a brush with the current
  material, 64 units in the third direction. A selected brush has a handle on each side of its box: drag one to
  resize. Clone, delete, copy, paste and rotate are in the page's Edit and Brush menus.
- **Entities.** Type a class under the entity list and press Place: a point entity of that class, on the surface at
  the centre of the live view or at the 2D view's centre. The Inspector shows what the class is, each key's help
  (hover the key; required keys are marked), suggests the class's keys, and lists what Check Map says about it.
- **Panels.** Entities (with a filter), the Inspector (edit, add and remove keys), Assets (click a material to apply''')
patch('docs/features/FEATURES.md', '''- Not in the browser yet: creating and resizing brushes by dragging, vertex and face tools, the Vision Editor, Preferences, the prefab and surface tools as forms.''',
      '''- Building in the page: Draw brush (drag a rectangle in a 2D view), resize handles on the selected brushes, placing a point entity of any class, and an inspector with the entity definitions (class description, key help, required keys, Check Map's findings for the entity). (tested in a browser)
- Not in the browser yet: vertex, edge and face tools, the Vision Editor, Preferences, the prefab and surface tools as forms.''')
p = ROOT + 'docs/TEST_CHECKLIST.md'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = '- [ ] A big map (Cinder Rail) opens in the page within a few seconds.' + nl
assert s.count(a) == 1
s = s.replace(a, a + '- [ ] Draw brush, drag a rectangle in the Top view: a brush appears in the editor too. Drag a handle of its box:' + nl +
              '      it grows on that side only. Select a spawn: the Inspector shows what the class is and help for its keys.' + nl)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
