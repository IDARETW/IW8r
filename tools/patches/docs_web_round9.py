import io

# Docs for patch control points in the web page (2026-11-14): plan 3.4 has no open point left.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('CHANGELOG.md', '''## 2026-11-14

''', '''## 2026-11-14

- **Web: patch control points.** With Tool: Vertices, a selected patch shows a handle on every control point in the
  2D views; dragging one moves the points under it (one undo step). Locked points have no handle. Checked in a real
  browser with the mouse.

''')
patch('docs/PROGRESS.md', '''  The web list of plan 3.3 / 3.4 is done except:''', '''  GET /api/handles (control points of the selected patches, at most 20,000) and POST /api/vertices moves with
  {e, i, points: [index]} for a patch; app.js patchPoints / loadPatchPoints. Plan 3.4 is done.
  The web list of plan 3.3 / 3.4 is done except:''')
patch('docs/PLAN_REMAINING.md', '''shows), face picking (Ctrl + click in the live view; the Surface form fits and justifies the picked faces). Open:
patch vertex editing.''', '''shows), face picking (Ctrl + click in the live view; the Surface form fits and justifies the picked faces), patch
control points (Tool: Vertices; GET /api/handles). Nothing open.''')
patch('docs/USER_GUIDE.md', '''  move the brush cannot take is refused with the reason in the status line. Patches are shaped at the editor.''',
      '''  move the brush cannot take is refused with the reason in the status line. A selected patch shows its control
  points with Vertices; locked points are left out.''')
patch('docs/features/FEATURES.md', '''- Not in the browser yet: patch vertex editing, the full Vision Editor''', '''- Patch control points are moved the same way (Tool: Vertices).
- Not in the browser yet: the full Vision Editor''')
patch('docs/HANDOVER.md', '''- Web page: patch vertex editing; the full Vision Editor (vision overrides by name are in the Atmosphere form).''',
      '''- Web page: the full Vision Editor (vision overrides by name are in the Atmosphere form).''')
print('ok')
