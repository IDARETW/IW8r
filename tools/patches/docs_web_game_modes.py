import io

# Docs for the Game Modes web form (plan 3.3, form 2).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('CHANGELOG.md', '''## 2026-11-13

''', '''## 2026-11-13

- **Web: Game Modes in the browser.** Tools > Game Modes opens as a form in the web page: choose a mode (the
  built-in ones, Warzone presets included, and your own) and what to do with it: carry it in the map, use it for F5,
  or insert the Warzone start entities. Making your own presets stays in the editor's dialog. Restart in Safe Mode is
  now one of the commands only the editor itself runs. Checked in a real browser (Warzone Trios carried and undone).

''')
patch('docs/PROGRESS.md', '''## Latest (2026-11-13)

''', '''## Latest (2026-11-13)

- Plan 3.3 form 2: MainWindow::webGameModesForm / applyWebGameModes (main_window_game_modes.cpp; actions carry / f5 /
  warzoneStart through bakeGameMode, DevmapRunner::setModePreset, insertWarzoneStart); forms may set "sendAll" and
  choices may carry "choiceLabels"; RestartSafeMode is editor-only. Browser check on 8791.
''')
patch('docs/PLAN_REMAINING.md', '''/api/form, POST /api/form) for Sun & Sky, Atmosphere, Lightmap Bake Settings and Map Info. Open: the forms 2 to 10
of the list below (Vision of item 1 too).
''', '''/api/form, POST /api/form) for Sun & Sky, Atmosphere, Lightmap Bake Settings, Map Info and Game Modes (preset +
action; presets are made in the desktop dialog). Open: the forms 3 to 10 of the list below (Vision of item 1 too).
''')
patch('docs/USER_GUIDE.md', '''  Sun & Sky, Atmosphere, Lightmap Bake Settings and Map Info (all in the web Compile menu) open as forms in the
  page instead: change what you want, then OK or Apply. Only the changed fields are written, as one undo step.
''', '''  Sun & Sky, Atmosphere, Lightmap Bake Settings and Map Info (all in the web Compile menu) open as forms in the
  page instead: change what you want, then OK or Apply. Only the changed fields are written, as one undo step.
  Tools > Game Modes is a form too: pick a mode and whether to carry it in the map, use it for F5, or insert the
  Warzone start entities.
''')
print('ok')
