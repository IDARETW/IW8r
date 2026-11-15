import io

# The bench's settle wait only looked at loads the per-frame budget had postponed (texturesPending). Images still being
# exported or resolved (terrain layers, prop layers, decal volumes) and a terrain patch being rebuilt did not hold it:
# a Verdansk run took its still frames without the near terrain layers (2026-11-15). The settle loop now runs every
# time and also waits for the reference's unresolved materials and the terrain patch build (capped).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if s.count(a) == 0 and b in s:
        return  # already applied
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


CH = 'src/qtapp/camera_view.h'
patch(CH, '''    bool texturesPending() const { return moreToLoad_ || refMoreToLoad_; }''',
      '''    bool texturesPending() const { return moreToLoad_ || refMoreToLoad_; }
    // The stock reference still has materials whose images or layers are not resolved (exports, decodes, layer images).
    bool referencePending() const { return refTexPending_ || texInstPending_; }''')
MH = 'src/qtapp/main_window.h'
patch(MH, '''    bool streamedTilesSettled() const;''', '''    bool streamedTilesSettled() const;
    bool terrainLayersBusy() const { return terrainLayerBusy_; }  // the near-camera terrain patch is being built''')
M = 'src/qtapp/main.cpp'
patch(M, '''        if (!w.stockCacheIdle()) {
            while (wt.elapsed() < 600000 && !w.stockCacheIdle()) {
                cam->renderNow();
                QApplication::processEvents(QEventLoop::AllEvents, 50);
            }
            QElapsedTimer settle;
            settle.start();
            while (settle.elapsed() < 3000 || cam->texturesPending()) {
                cam->renderNow();
                QApplication::processEvents(QEventLoop::AllEvents, 50);
                if (wt.elapsed() > 660000) break;
            }
        }''', '''        while (wt.elapsed() < 600000 && !w.stockCacheIdle()) {
            cam->renderNow();
            QApplication::processEvents(QEventLoop::AllEvents, 50);
        }
        // Then what that work set going: textures, the reference's unresolved images and layers, the terrain patch.
        {
            QElapsedTimer settle;
            settle.start();
            while (settle.elapsed() < 3000 || cam->texturesPending() || cam->referencePending() || w.terrainLayersBusy() || !w.stockCacheIdle()) {
                cam->renderNow();
                QApplication::processEvents(QEventLoop::AllEvents, 50);
                if (wt.elapsed() > 660000 || settle.elapsed() > 180000) break;
            }
        }''')
patch(M, '''            if (!w.streamedTilesSettled() || cam->texturesPending()) calm.restart();''',
      '''            // The tiles, then what they set going: their light grids and lightmaps, the terrain patch at the bench
            // camera, the images and layers of what they added.
            if (!w.streamedTilesSettled() || cam->texturesPending() || cam->referencePending() || w.terrainLayersBusy() || !w.stockCacheIdle())
                calm.restart();''')
print('ok')
