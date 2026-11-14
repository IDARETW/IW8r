import io

# Docs for plan 3.3 part 2 (web forms).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('CHANGELOG.md', '''## 2026-11-13

''', '''## 2026-11-13

- **Web: dialogs as forms in the browser.** Compile > Sun & Sky and Compile > Atmosphere open as forms in the web
  page, and Lightmap Bake Settings and Map Info are in the web Compile menu too. A form shows the map's values; OK
  or Apply sends only the fields you changed, through the same code as the desktop dialog, as one undo step. A value
  the editor cannot take is refused with the reason. Fixed on the way: "Sun & Sky" lost its "&" in the web menu.
  API: `GET /api/forms`, `GET /api/form?id=`, `POST /api/form`. Checked in a real browser (open, change the sun
  pitch, apply, undo).

''')
patch('docs/PROGRESS.md', '''## Latest (2026-11-13)

''', '''## Latest (2026-11-13)

- Plan 3.3 part 2: web forms in src/qtapp/main_window_web.cpp (webFormList / webForm / applyWebForm /
  webFormForCommand): sunSky (MapSunSky, via currentSunSky / applySunSky), atmosphere (MapAtmosphere, via
  currentAtmosphere / applyAtmosphere), bakeSettings (CompilePanel::applyBakeSettings + the bake key, one group),
  mapInfo (the seven iw8r_* keys with the dialog's ranges). Field types number / text / choice / color / bool; the
  page sends only changed fields. webCommandList marks a command with "form" (no longer blocked) and appends
  "form:<id>" entries for forms without a menu command; plainMenuText keeps "&&" as "&". Server: GET /api/forms,
  GET /api/form, POST /api/form. Page: formShade / formDialog, openForm, sendForm. Browser check on 8791.
  Next forms in owner-scope order: Game Modes, loot points / placed objects, stock-edit actions, compile options,
  Check Map, Preferences.
''')
patch('docs/PLAN_REMAINING.md', '''/api/command) and the generated menu bar with search. Open: the web forms for the commands that open dialogs.
''', '''/api/command) and the generated menu bar with search. Part 2 DONE 2026-11-13: web forms (GET /api/forms, GET
/api/form, POST /api/form) for Sun & Sky, Atmosphere, Lightmap Bake Settings and Map Info. Open: the forms 2 to 10
of the list below (Vision of item 1 too).
''')
patch('docs/USER_GUIDE.md', '''  the first match. A command that would open a dialog on the editor's screen is greyed, with that reason.
''', '''  the first match. A command that would open a dialog on the editor's screen is greyed, with that reason.
  Sun & Sky, Atmosphere, Lightmap Bake Settings and Map Info (all in the web Compile menu) open as forms in the
  page instead: change what you want, then OK or Apply. Only the changed fields are written, as one undo step.
''')
print('ok')
