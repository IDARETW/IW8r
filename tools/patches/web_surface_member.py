import io

# The Surface web form reaches the Surface panel through a member of the window: a dock that floats or was closed by a
# restored layout is not always a child of the window, so findChild did not find it in a running editor.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, n=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('src/qtapp/main_window.cpp', 'dock_->addDockWidgetTabToArea(makeDock("Surface", new SurfacePanel(&ctx_), true), entArea);',
      'surface_ = new SurfacePanel(&ctx_);\n    dock_->addDockWidgetTabToArea(makeDock("Surface", surface_, true), entArea);')
patch('src/qtapp/main_window_web.cpp', 'const SurfacePanel* sp = findChild<SurfacePanel*>();', 'const SurfacePanel* sp = surface_;')
patch('src/qtapp/main_window_web.cpp', 'SurfacePanel* sp = findChild<SurfacePanel*>();', 'SurfacePanel* sp = surface_;')
print('ok')

# The member itself, and the wording of the effect text for a browser (its window does not refresh by itself).
s = io.open(ROOT + 'src/qtapp/main_window.h', encoding='utf-8', newline='').read()
if 'surface_ = nullptr' not in s:
    patch('src/qtapp/main_window.h', '    LayersPanel* layers_ = nullptr;',
          '    LayersPanel* layers_ = nullptr;\n    class SurfacePanel* surface_ = nullptr;  // the Surface dock; the web form "surface" works through it')
s = io.open(ROOT + 'src/qtapp/main_window.cpp', encoding='utf-8', newline='').read()
if 'open this again in a moment.</p>")' not in s:
    patch('src/qtapp/main_window.cpp', '''                    ? QString("<p>Exporting this effect with ACTS now; this window refreshes when it is done.</p>")''',
          '''                    ? (show ? QString("<p>Exporting this effect with ACTS now; this window refreshes when it is done.</p>")
                            : QString("<p>Exporting this effect with ACTS now; open this again in a moment.</p>"))''')
print('ok 2')
