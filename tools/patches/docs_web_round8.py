import io

# Docs for face picking in the web page's live view (2026-11-14).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('CHANGELOG.md', '''## 2026-11-14

''', '''## 2026-11-14

- **Web: pick a single face.** Ctrl + click in the web page's live view selects the brush face under the pointer
  (Ctrl + Shift + click adds or removes one). Edit > Surface then works on the selected faces: fit one face, justify
  it, set its texture fields.

''')
patch('docs/PROGRESS.md', '''  The web list of plan 3.3 / 3.4 is done except:''', '''  POST /api/pick {face: true} (hook pickFace, MainWindow::webPickFace: scene.pickFace, selectFace / toggleFace);
  viewlive.js sends it on Ctrl + click.
  The web list of plan 3.3 / 3.4 is done except:''')
patch('docs/PLAN_REMAINING.md', '''shows). Open: patch vertex editing, face picking with surface fit.''',
      '''shows), face picking (Ctrl + click in the live view; the Surface form fits and justifies the picked faces). Open:
patch vertex editing.''')
patch('docs/USER_GUIDE.md', '''  shows the result. Edit > Undo History lists the steps;''', '''  shows the result. To work on one face, Ctrl + click it in the "Editor view (live)" first (Ctrl + Shift + click
  adds another). Edit > Undo History lists the steps;''')
patch('docs/features/FEATURES.md', '''- Not in the browser yet: patch vertex editing,''', '''- Ctrl + click in the live view picks a single brush face for the Surface form.
- Not in the browser yet: patch vertex editing,''')
print('ok')
