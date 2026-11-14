import io

# Docs for plan 3.1 (the browser's live editor view) and a web section in the user guide (plan M3 / M4 docs).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('CHANGELOG.md', '''## 2026-11-13

''', '''## 2026-11-13

- **Web: the editor's own 3D view in the browser.** The page's View box has "Editor view (live)": the picture is
  drawn by the editor's renderer from a camera the browser owns, so it shows exactly what the desktop camera view
  shows (textures, lit preview, Game Lighting, the stock map, selection and gizmo) and each browser looks where it
  wants. Drag to look, W A S D and Q E to fly (Shift is faster), wheel to move forward and back, right-drag to
  slide, click to select (Shift adds). It starts at the editor's camera. A 1280 x 720 picture of Cinder Rail takes
  about 20 ms (lighter ones while moving); nothing is drawn for a hidden tab. API: `GET /api/frame`, `POST /api/pick`.
  Checked in a real browser against a running editor.

''')
patch('docs/PROGRESS.md', '''## Latest (2026-11-13)

''', '''## Latest (2026-11-13)

- Plan 3.1: WebCamera; hooks renderFrame / pickFrame / camera; GET /api/frame?pos=&yaw=&pitch=&fov=&w=&h=&q= (JPEG of
  CameraView::renderImage with the browser's camera; renderImage got keepGeometry so a frame does not remake the scene
  geometry: 250-440 ms -> 19-28 ms at 1280 x 720 on Cinder Rail, 10 ms at 768 x 432), POST /api/pick (scene.pick
  through app::cameraRay), /api/state "camera". MainWindow::webRenderFrame / webPick / webCamera. Page: view option 4,
  img#viewLive, data/web/viewlive.js (own camera, one request at a time, light frames while moving, hidden tab asks
  nothing). Browser check on 8791: flying, pick, Game Lighting from the web menu. Open in 3.1: a continuous stream
  (now a frame per change), terrain / streamed tiles following a browser's camera.
''')
patch('docs/PLAN_REMAINING.md', '''**3.1 Remote 3D view.**
''', '''**3.1 Remote 3D view.** DONE 2026-11-13 as frames on demand (GET /api/frame, POST /api/pick, the page's "Editor view
(live)"). Open: streamed tiles and terrain layers follow only the editor's own camera.
''')
s = io.open(ROOT + 'docs/USER_GUIDE.md', encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
head = '## 12. Known limits'
assert s.count(head) == 1 and 'Using the editor from a browser' not in s
section = '''### Using the editor from a browser

**Tools > Web Server** serves the editor to a browser on the same machine (`http://127.0.0.1:8780`; **Web Server
Settings** has the port, a login for other machines and HTTPS). The page works on the same live map as the window:
one undo history, and each side sees the other's edits at once.

- **Menu bar.** The editor's own menus, with ticks and shortcuts. **Find a command** searches them all; Enter runs
  the first match. A command that would open a dialog on the editor's screen is greyed, with that reason.
- **View box.** Top, Front and Side are drawn in the browser (drag to pan or move the selection, wheel to zoom,
  click to select). **3D** is a simple view drawn in the browser. **Editor view (live)** is the editor's own camera
  picture, drawn by the editor for your browser: drag to look, W A S D and Q E to fly (Shift is faster), wheel to
  move forward and back, right-drag to slide, click to select. Camera modes, Lit Preview and Game Lighting from the
  View menu apply to it.
- **Panels.** Entities (with a filter), the Inspector (edit, add and remove keys), Assets (click a material to apply
  it, a model, effect or sound to place it) and the Log.
- Big maps load: models are sent as boxes for the drawn views, and after the first load only changes travel.

'''.replace('\n', nl)
i = s.index(head)
s = s[:i] + section + s[i:]
io.open(ROOT + 'docs/USER_GUIDE.md', 'w', encoding='utf-8', newline='').write(s)
print('ok')
