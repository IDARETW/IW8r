import io

# Docs for the background texture decode and the stock map open through the shared ACTS runner (2026-11-14).

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

- **Textures are decoded off the main thread.** The camera view read and decoded every texture file while it built
  a frame (up to 12 ms of each frame while a map loaded). Worker threads do that now; the view goes on without a
  texture that is not ready and picks it up a few frames later, and only the upload to the graphics card is left
  for the main thread. Hideout: the same picture (2,979 files decoded by the workers), and all its textures were up
  within 20 s of the run where they took between 20 and 30 s before. Screenshots, tests and the build's own captures
  still wait for each file, so they are unchanged. How much smoother the view feels while a big map loads was not
  measured.
- **Opening a stock map loads its zone once.** The entities were read by an ACTS run of their own, which loaded the
  whole zone; the world export that followed loaded it a second time. Both now go through the editor's shared ACTS
  server, so the second one finds the zone in memory (test: two exports, the second from the loaded zone). A zone
  file from outside the game folder, and the view of a map with an installed edit, still run on their own.
- **The start-up check makes its report folder.** `--smoke-test ... --out <folder>` ended with exit code 0 and no
  report when the folder did not exist.

''')
patch('docs/PROGRESS.md', '''  ROADMAP 5.9 / 8.11: core convert_ops brushFacePaintMeshes / addPaintMeshesToBrushFaces''',
      '''  Background texture decode: core keyed_job_pool.h (KeyedJobPool<Result>: take(key, job, out, wait)),
  CameraView::decodedTexture (modes 0 blocks-else-preview, 1 preview, 2 image file) used by textureFor, normalFor
  and emissiveFor; pending = return -1 with moreToLoad_. It waits (decodes in place) when captureW_ > 0, a clean
  capture, scene.captureMode, or CameraView::setBackgroundTextureDecode(false) (main.cpp: gui test, bench, smoke,
  feature shots; IW8R_BACKGROUND_DECODE keeps the workers on there). The bench report has background_decodes.
  MainWindow::exportStockMapEnts: map_ents through ActsRunner::shared() for the game's own zone file.
  ROADMAP 5.9 / 8.11: core convert_ops brushFacePaintMeshes / addPaintMeshesToBrushFaces''')
patch('docs/PROGRESS.md', '''  - Not done: preview decode still runs on the GUI thread (bounded per frame); `compile_panel` validation and the
    `map_ents` export still start one process per run.''',
      '''  - Not done then: preview decode on the GUI thread and one process per `map_ents` export. Both DONE 2026-11-14
    (worker decode; the export goes through the shared runner). `compile_panel` validation still starts one process
    per built zone, by design: a built zone is a new file each time, and the server keeps zones by name.''')
P = 'docs/PLAN_REMAINING.md'
s = read(P)
nl = '\r\n' if '\r\n' in s else '\n'
a = '  - preview decode off the GUI thread, one process per validation run: not built; neither blocks a workflow' + nl + '    (decode is budgeted per frame, validation runs per zone).'
assert s.count(a) == 1, s.count(a)
s = s.replace(a, '  - preview decode off the GUI thread: done 2026-11-14 (worker threads; captures and tests still wait per file).' + nl +
              '  - one process per run: the stock map\'s entity export goes through the shared ACTS server (done 2026-11-14).' + nl +
              '    Validation of a built zone stays one process per zone: the zone is a new file after every build.')
write(P, s)
print('ok')
