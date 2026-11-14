import io

# Docs for the web Undo History form and the log filters (2026-11-14): plan 3.2 has no open point left.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('CHANGELOG.md', '''## 2026-11-14

''', '''## 2026-11-14

- **Web: Undo History and a log you can filter.** Edit > Undo History in the web page lists the document's steps;
  choosing one undoes or redoes to it. The Log panel loads the lines from before the page was opened, and shows all
  lines, warnings and errors, or errors only, and only the lines that hold a text you type. Fixed on the way: with
  more than one web server object every log line was kept several times.

''')
patch('docs/PROGRESS.md', '''  The web list of plan 3.3 / 3.4 is done except:''', '''  Web form "history" (doc.history / undoPosition; undo or redo to the chosen step) and GET /api/log with level,
  source and q; log lines carry their level; the kept lines are filled by one process-wide slot (was one per
  WebServer object). The page: logLines / renderLog / addLogLine, #logLevel, #logFilter. Plan 3.2 is done.
  The web list of plan 3.3 / 3.4 is done except:''')
patch('docs/PLAN_REMAINING.md', '''**3.2 Protocol.** Part 1 DONE 2026-11-13 (lean model boxes; `GET /api/map?since=` differences). Open: the log backlog
with filters, an undo history list.''', '''**3.2 Protocol.** DONE: part 1 2026-11-13 (lean model boxes; `GET /api/map?since=` differences), the log backlog
with filters and the undo history list 2026-11-14.''')
patch('docs/USER_GUIDE.md', '''  shows the result. View > Layers shows and hides layers,''', '''  shows the result. Edit > Undo History lists the steps; choose one to go back or forward to it. View > Layers shows
  and hides layers,''')
patch('docs/features/FEATURES.md', '''- Layers (show, hide, select, move the selection, declare) and the compile options with the Build Budget are forms too.''',
      '''- Layers (show, hide, select, move the selection, declare), the compile options with the Build Budget and the Undo History are forms too. The Log panel keeps the editor's earlier lines and filters by level and text.''')
patch('docs/HANDOVER.md', '''the editor's menus, ten dialogs and docks as forms,''', '''the editor's menus, eleven dialogs and docks as forms,''')
print('ok')
