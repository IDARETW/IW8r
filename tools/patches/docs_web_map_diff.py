import io

# Docs for plan 3.2 part 1 (the browser's map data: lean and incremental).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('CHANGELOG.md', '''## 2026-11-13

''', '''## 2026-11-13

- **Web: big maps open in the browser.** The map data the page loads was every triangle of every model: 64 MB for
  the small showcase map and 248 MB for Cinder Rail, which no browser tab survived. A model is now sent as its box
  (the 2D views and picking need no more), and after the first load the page asks only for what changed. Cinder Rail:
  3 MB once (0.15 s), then 121 bytes when nothing changed, and a moved entity comes as one item. Checked in a real
  browser: Cinder Rail draws, a move and an undo from the page arrive as differences.

''')
patch('docs/PROGRESS.md', '''## Latest (2026-11-13)

''', '''## Latest (2026-11-13)

- Plan 3.2 part 1: WebServer::syncWebMap (per item / entity: the JSON last sent, mapRev_ at its last change, gone
  lists; resynced when doc revision x scene generation changes) and mapJson(since, epoch): GET /api/map?since=&epoch=
  gives {full, mapRev, epoch, items, entities, removed, removedEntities}; an entity item with more than 48 faces is
  its box ({box, faces}). app.js applyMap. Cinder Rail 248 MB -> 3.07 MB (151 ms), unchanged diff 121 bytes (2 ms).
  Browser check on 8791 (move + undo through diffs). Open in 3.2: log backlog filters, undo history list; the 3D
  page view still draws boxes for models until 3.1 (the streamed editor render).
''')
patch('docs/PLAN_REMAINING.md', '''**3.2 Protocol.** Send incremental scene diffs by item revision instead of re-sending the whole `/api/map` on every
revision.''', '''**3.2 Protocol.** Part 1 DONE 2026-11-13 (lean model boxes; `GET /api/map?since=` differences). Open: the log backlog
with filters, an undo history list. Send incremental scene diffs by item revision instead of re-sending the whole `/api/map` on every
revision.''')
print('ok')
