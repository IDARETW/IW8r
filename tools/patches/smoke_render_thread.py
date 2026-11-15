import io

# Smoke test: the viewport render thread draws a frame this thread prepared while this thread is busy, and moves the
# fly camera (fake right button + W) while this thread does not answer for 1.5 s; the GUI thread then takes the camera
# over. Part of the smoke result (fails the smoke when the render thread is on and did neither).

P = 'E:/IW8-Radiant/src/qtapp/main.cpp'
s = io.open(P, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
s = s.replace('\r\n', '\n')


def sub(a, b):
    global s
    assert s.count(a) == 1, (s.count(a), a[:80])
    s = s.replace(a, b)


sub('''                const bool ok = w.cameraView()->hasSwapChain() && pixels > 1000;
''', '''                // The viewport render thread (camera_view_render_thread.cpp).
                QString renderThreadLine;
                bool renderThreadOk = true;
                if (iw8r::qt::CameraView* cam = w.cameraView(); cam->hasSwapChain() && iw8r::qt::CameraView::renderThreadOn()) {
                    const int f0 = cam->renderThreadFrames();
                    cam->repaint();  // prepared here, drawn by the render thread while this thread sleeps
                    Sleep(400);
                    const int f1 = cam->renderThreadFrames();
                    const iw8r::Vec3 before = w.context().cam.pos;
                    w.context().looking = true;
                    cam->setFakeFlyInputForTest(true);
                    cam->repaint();
                    Sleep(1500);  // this thread does not answer: the render thread flies the camera (W held)
                    const int f2 = cam->renderThreadFrames();
                    cam->setFakeFlyInputForTest(false);
                    w.context().looking = false;
                    cam->repaint();  // takes the render thread's camera over
                    QApplication::processEvents();
                    const double moved = iw8r::length(w.context().cam.pos - before);
                    w.context().cam.pos = before;
                    renderThreadLine = QString(" render_thread_frames=%1 takeover_frames=%2 takeover_moved=%3").arg(f1 - f0).arg(f2 - f1).arg(moved, 0, 'f', 0);
                    renderThreadOk = f1 > f0 && f2 - f1 >= 10 && moved > 50;
                }
                const bool ok = w.cameraView()->hasSwapChain() && pixels > 1000 && renderThreadOk;
''')
sub('''                    f.write(QString("%1 swapchain=%2 drawn_pixels=%3 entities=%4 textures=%6 normal_maps=%7 title=%5\\n")''',
    '''                    f.write(QString("%1 swapchain=%2 drawn_pixels=%3 entities=%4 textures=%6 normal_maps=%7%8 title=%5\\n")''')
sub('''                                .arg(w.cameraView()->loadedNormalMaps())
                                .toUtf8());''', '''                                .arg(w.cameraView()->loadedNormalMaps())
                                .arg(renderThreadLine)
                                .toUtf8());''')
io.open(P, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))
print('ok')
