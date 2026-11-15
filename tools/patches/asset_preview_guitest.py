import io
ROOT = 'E:/IW8-Radiant/'
p = ROOT + 'src/qtapp/qt_gui_test.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = '    // Prefab bundles (docs/PREFAB_BUNDLES.md): dock, placing, parameters, save, explode / edit, assets, build.'
assert s.count(a) == 1
block = r'''    r.section("asset preview window (navigable textured 3D view of a model or prefab)");
    {
        QDir d(QCoreApplication::applicationDirPath());
        for (int up = 0; up < 4 && !QFileInfo::exists(d.filePath("data/prefabs")); ++up) d.cdUp();
        const QString prefab = d.filePath("data/prefabs/iw8r/cover/cover_block_concrete_line.map");
        if (!QFileInfo::exists(prefab)) {
            r.check(false, "the shipped prefab for the preview check exists: " + prefab);
        } else {
            iw8r::qt::AssetPreviewWindow* pw = w.openAssetPreview(prefab, true);
            r.check(pw && pw->isVisible() && pw->view() && pw->view()->hasSwapChain(), "Preview in 3D opens a window with its own 3D view");
            if (pw) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
                const QImage shot = pw->view()->renderImage(320, 200, false, 0, 300);
                double sum = 0, sum2 = 0;
                size_t n = 0;
                for (int y = 0; y < shot.height(); y += 2)
                    for (int x = 0; x < shot.width(); x += 2) {
                        const double l = qGray(shot.pixel(x, y)) / 255.0;
                        sum += l, sum2 += l * l, ++n;
                    }
                const double var = n ? sum2 / n - (sum / n) * (sum / n) : 0;
                r.check(!shot.isNull() && var > 1e-3, QString("the prefab is drawn (picture variance %1)").arg(var, 0, 'f', 4));
                shot.save(QDir(outDir).filePath("asset_preview.png"));
                r.check(pw->materialCount() > 0 && pw->infoText().contains("triangles"),
                        QString("the side panel lists the materials (%1) and the size").arg(pw->materialCount()));
                // Orbit: a left drag turns the camera about the asset; the wheel moves it closer; R goes back.
                iw8r::qt::CameraView* pv = pw->view();
                pv->resize(400, 300);
                const double yaw0 = pw->context().cam.yaw, dist0 = pv->orbitDistance();
                const iw8r::Vec3 target = pv->orbitTarget();
                QTest::mousePress(pv, Qt::LeftButton, Qt::NoModifier, QPoint(200, 150));
                QMouseEvent move(QEvent::MouseMove, QPointF(260, 150), pv->mapToGlobal(QPointF(260, 150)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(pv, &move);
                QTest::mouseRelease(pv, Qt::LeftButton, Qt::NoModifier, QPoint(260, 150));
                const double yaw1 = pw->context().cam.yaw;
                const double away = iw8r::length(pw->context().cam.pos - target);
                QWheelEvent wheel(QPointF(200, 150), pv->mapToGlobal(QPointF(200, 150)), QPoint(), QPoint(0, 240), Qt::NoButton, Qt::NoModifier,
                                  Qt::NoScrollPhase, false);
                QCoreApplication::sendEvent(pv, &wheel);
                const double dist1 = pv->orbitDistance();
                QTest::keyClick(pv, Qt::Key_R);
                r.check(std::fabs(yaw1 - yaw0) > 10 && std::fabs(away - dist0) < 1.0 && dist1 < dist0 * 0.9 && std::fabs(pw->context().cam.yaw - yaw0) < 1e-6,
                        QString("orbit: a drag turns about the asset (yaw %1 -> %2, at distance %3), the wheel zooms (%4 -> %5), R resets")
                            .arg(yaw0, 0, 'f', 1).arg(yaw1, 0, 'f', 1).arg(away, 0, 'f', 1).arg(dist0, 0, 'f', 1).arg(dist1, 0, 'f', 1));
                // The view modes, and the main view's camera is not moved by any of it.
                const iw8r::Vec3 mainPos = ctx.cam.pos;
                if (auto* mode = pw->findChild<QComboBox*>("assetPreviewMode")) {
                    mode->setCurrentIndex(1);
                    const QImage game = pv->renderImage(160, 100, false, 0, 100);
                    mode->setCurrentIndex(3);
                    const QImage wire = pv->renderImage(160, 100, false, 0, 100);
                    mode->setCurrentIndex(0);
                    r.check(!game.isNull() && !wire.isNull() && game != wire, "the view modes (Game Lighting, Wireframe) draw");
                }
                r.check(ctx.cam.pos == mainPos, "the preview leaves the main camera where it is");
                pw->hide();
            }
        }
    }

'''.replace('\n', nl)
s = s.replace(a, block + a)
inc = '#include "custom_shader.h"'
assert s.count(inc) == 1
s = s.replace(inc, inc + nl + '#include "asset_preview_window.h"', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
