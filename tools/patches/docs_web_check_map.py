import io

# Docs for the Check Map web form (plan 3.3, form 6) and the web section of the feature list.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('CHANGELOG.md', '''## 2026-11-13

''', '''## 2026-11-13

- **Web: Check Map in the browser.** Tools > Check Map opens in the web page as a report with the same findings, in
  the same order, as the editor's dialog, and a "Fix all findings marked [fix]" box that applies the automatic fixes
  as one undo step.

''')
patch('docs/PROGRESS.md', '''## Latest (2026-11-13)

''', '''## Latest (2026-11-13)

- Plan 3.3 form 6: checkMap web form (a CheckMapDialog that is never shown gives itemTexts / fixableCount / fixAll;
  field type "report" = read-only text). FEATURES.md web section rewritten (menu bar, forms, live view, differences).
  Web forms so far: sunSky, atmosphere, bakeSettings, mapInfo, gameModes, checkMap.
''')
patch('docs/PLAN_REMAINING.md', '''action; presets are made in the desktop dialog). Open: the forms 3 to 10 of the list below (Vision of item 1 too).
''', '''action; presets are made in the desktop dialog) and Check Map (report + fix all). Items 3 and 4 of the list below
are plain commands and run from the web menu already. Open: Vision (item 1), compile options and Build Budget (5),
Preferences (7), Prefabs (8), Surface / UV (9), Material / Effect Info (10).
''')
patch('docs/USER_GUIDE.md', '''  Tools > Game Modes is a form too: pick a mode and whether to carry it in the map, use it for F5, or insert the
  Warzone start entities.
''', '''  Tools > Game Modes is a form too: pick a mode and whether to carry it in the map, use it for F5, or insert the
  Warzone start entities. Tools > Check Map shows the findings and can apply the automatic fixes.
''')
patch('docs/features/FEATURES.md', '''- The browser client has 2D top, front and side views, an entity list, an inspector, select and move, key editing, undo, redo, save, compile and a log. It fits a phone-width screen. (not yet seen in game)
- Live updates reach the browser as the document changes.
- A 3D view in the browser draws the textured scene with thumbnails.
- The browser can compile, deploy, build and play, load in game, and place assets.
- The web access work is paused. The owner asked for the desktop app to come first, on 2026-11-10. The server stays on for the owner.''', '''- The browser client has 2D top, front and side views, an entity list, an inspector, select and move, key editing, undo, redo, save, compile and a log. It fits a phone-width screen.
- Live updates reach the browser as the document changes. After the first load only the changed items travel, and a model is sent as its box, so big maps open (Cinder Rail: 3 MB once, then a few hundred bytes per change). (tested in a browser)
- The editor's own menu bar is in the page, built from the editor's command list, with ticks, shortcuts and a command search. A command runs in the editor as its menu entry would. Commands that open a dialog on the editor's screen are greyed with that reason; exit, the web server's switches and installs into the game stay with the person at the editor. (tested in a browser)
- Dialogs as forms in the page: Sun & Sky, Atmosphere, Lightmap Bake Settings, Map Info, Game Modes (a preset and what to do with it) and Check Map (the findings, and Fix all). A form writes only the fields that were changed, through the dialog's own code, as one undo step. (tested in a browser)
- "Editor view (live)": the editor's own camera picture in the browser, drawn by the editor's renderer from a camera each browser owns. It shows what the desktop camera view shows (textures, lit preview, Game Lighting, the stock map, selection). Drag to look, W A S D and Q E to fly, wheel to move, click to select. About 20 ms per 1280 x 720 picture on Cinder Rail. (tested in a browser)
- A simple 3D view drawn in the browser itself draws the textured scene with thumbnails (models as boxes).
- The browser can compile, deploy, build and play, load in game, and place assets.
- Not in the browser yet: creating and resizing brushes by dragging, vertex and face tools, the Vision Editor, Preferences, the prefab and surface tools as forms.''')
print('ok')
