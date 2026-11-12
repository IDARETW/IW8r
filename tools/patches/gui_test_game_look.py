import io

p = 'src/qtapp/qt_gui_test.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
anchor = '''    r.section("camera modes");'''
assert s.count(anchor) == 1
block = '''    r.section("game lighting (View > Game Lighting)");
    if (CameraView* cv = w.cameraView()) {
        // The preview bake runs in the background; the camera shades with the game's world shader and post chain.
        QAction* game = w.action("ToggleGameLighting");
        r.check(game && game->isCheckable(), "View > Game Lighting is a checkable command");
        const bool texWas = ctx.texturedView;
        if (!texWas && w.action("CameraModeTextured")) w.action("CameraModeTextured")->trigger();
        ctx.cam.pos = iw8r::Vec3{0, -200, 120};
        ctx.cam.yaw = 90;
        ctx.cam.pitch = -25;
        ctx.cameraMoved();
        const QImage before = cv->renderImage(320, 180, true, 0, 300);
        if (game) game->trigger();
        r.check(ctx.scene.gameLook && game && game->isChecked(), "Ctrl+Alt+L turns game lighting on (checked)");
        QElapsedTimer t;
        t.start();
        QImage after;
        while (t.elapsed() < 60000) {
            after = cv->renderImage(320, 180, true, 0, 0);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            if (cv->gameLightmap() && !cv->gameBaking()) break;
            QThread::msleep(20);
        }
        after = cv->renderImage(320, 180, true, 0, 300);
        const auto lm = cv->gameLightmap();
        r.check(lm && lm->error.empty() && lm->width > 0 && lm->polys > 0 && !lm->uv.empty(),
                QString("the preview bake finished: %1 x %2, %3 faces, %4 s%5")
                    .arg(lm ? lm->width : 0).arg(lm ? lm->height : 0).arg(lm ? lm->polys : 0).arg(lm ? lm->seconds : 0, 0, 'f', 2)
                    .arg(lm && !lm->error.empty() ? " (" + QString::fromStdString(lm->error) + ")" : QString()));
        double sum = 0, sum2 = 0, diff = 0;
        size_t n = 0;
        for (int y = 0; y < after.height() && !before.isNull(); ++y)
            for (int x = 0; x < after.width(); ++x) {
                const QRgb a = after.pixel(x, y), b = before.pixel(x, y);
                const double l = (qRed(a) + qGreen(a) + qBlue(a)) / 765.0;
                sum += l, sum2 += l * l, ++n;
                diff += std::abs(qRed(a) - qRed(b)) + std::abs(qGreen(a) - qGreen(b)) + std::abs(qBlue(a) - qBlue(b));
            }
        const double mean = n ? sum / n : 0, var = n ? sum2 / n - mean * mean : 0;
        r.check(n > 0 && diff / std::max<size_t>(n, 1) > 2.0 && mean > 0.02 && mean < 0.98 && var > 1e-4,
                QString("the game look renders a different, non-flat image (mean %1, variance %2, change %3 per pixel)")
                    .arg(mean, 0, 'f', 3).arg(var, 0, 'f', 4).arg(diff / std::max<size_t>(n, 1), 0, 'f', 1));
        const QByteArray shot = qgetenv("IW8R_GAME_LOOK_SHOT");
        if (!shot.isEmpty()) {
            after.save(QString::fromLocal8Bit(shot));
            before.save(QString::fromLocal8Bit(shot).replace(".png", "_editor.png"));
        }
        if (game) game->trigger();
        r.check(!ctx.scene.gameLook, "Ctrl+Alt+L again turns it off");
        if (!texWas && w.action("CameraModeFlat")) w.action("CameraModeFlat")->trigger();
    }

'''
s = s.replace(anchor, block.replace('\n', nl) + anchor)
for inc in ('#include <QThread>', '#include <QElapsedTimer>'):
    if inc not in s:
        i = s.index('#include <')
        s = s[:i] + inc + nl + s[i:]
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
