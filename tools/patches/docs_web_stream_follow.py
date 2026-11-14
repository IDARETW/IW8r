import io

# Docs for the streamed world following the browser's live view (plan 3.1, 2026-11-14).

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    write(p, s.replace(a, b))


patch('CHANGELOG.md', '''## 2026-11-14

''', '''## 2026-11-14

- **Web: the Warzone world follows the browser's view.** On Verdansk and Quarry the streamed tiles, the near
  terrain layers and the probes were loaded around the editor's own camera only, so flying away in the web page's
  live view showed the coarse far terrain. They now follow the browser's camera while its view is being drawn, and
  go back to the editor's camera about four seconds after its last picture. One camera leads at a time, so the two
  cannot throw each other's tiles out of the cache. The switch is tested; a flight over Verdansk in a browser was
  not run this round.

''')
patch('docs/PROGRESS.md', '''  The web list of plan 3.3 / 3.4 is done (the Vision Editor form closed it).''', '''  Plan 3.1: MainWindow::followCameraPos() (the last /api/frame camera for webFollowHoldMs_ = 4000 ms, else
  ctx_.cam.pos) is the centre for updateStreamedTiles, updateTerrainLayers, updateProbeSubset and
  streamedTilesReport. GUI test with a 150 ms hold. Not run on Verdansk in a browser.
  The web list of plan 3.3 / 3.4 is done (the Vision Editor form closed it).''')
P = 'docs/PLAN_REMAINING.md'
s = read(P)
nl = '\r\n' if '\r\n' in s else '\n'
a = '(live)"). Left by decision (table "Open points closed by decision"): streamed tiles and terrain layers follow only the editor\'s own camera.'
assert s.count(a) == 1, s.count(a)
s = s.replace(a, '(live)"). Streamed tiles and terrain layers follow the browser\'s view while it draws DONE 2026-11-14.')
start = s.index('| 3.1 streamed tiles and terrain layers around the browser\'s camera |')
end = s.index(nl, start) + len(nl)
s = s[:start] + s[end:]
write(P, s)
patch('docs/USER_GUIDE.md', '''- **Vertices and edges.** Tool in the toolbar:''', '''- **Warzone maps.** While you fly in "Editor view (live)", the detailed world (streamed tiles, terrain layers) loads
  around the browser's camera; a few seconds after you stop it goes back to the editor's own camera.
- **Vertices and edges.** Tool in the toolbar:''')
print('ok')
