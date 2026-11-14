import io

# Viewport bench: IW8R_UMBRA=cull|cells|both turns View > Umbra Visibility on for the game_still shots and reports the
# query's counts (plan M2.2 verification).


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('E:/IW8-Radiant/src/qtapp/main.cpp', '''                      .arg(lm && !lm->error.empty() ? QString::fromStdString(lm->error) : QString());
        settle();
''', '''                      .arg(lm && !lm->error.empty() ? QString::fromStdString(lm->error) : QString());
        settle();
        {
            // IW8R_UMBRA=cull | cells | both: View > Umbra Visibility for the shots (always set, so a bench without it
            // is not left with the last run's choice).
            const QString um = qEnvironmentVariable("IW8R_UMBRA");
            w.setUmbraView(um == "cull" || um == "both", um == "cells" || um == "both");
            if (!um.isEmpty()) {
                QElapsedTimer ut;
                ut.start();
                while (ut.elapsed() < 120000) {
                    frame(nullptr);
                    QApplication::processEvents(QEventLoop::AllEvents, 20);
                    if (!w.umbraBusy() && ut.elapsed() > 1500) break;
                }
                settle();
                const iw8r::UmbraViewResult& ur = w.umbraLastResult();
                report << QString("umbra %1 status '%2' culls %3 surfaces %4 models %5 lights %6 probes %7 cells %8 query_ms %9 props_hidden %10 lines %11")
                              .arg(um).arg(iw8r::umbraStatusText(ur.status)).arg(ur.culls ? 1 : 0).arg(ur.visibleByClass[iw8r::kUmbraSurface])
                              .arg(ur.visibleByClass[iw8r::kUmbraStaticModel]).arg(ur.visibleByClass[iw8r::kUmbraPrimaryLight])
                              .arg(ur.visibleByClass[iw8r::kUmbraReflectionProbe]).arg(ur.cells).arg(ur.seconds * 1000, 0, 'f', 1)
                              .arg(cam->tomeCulledProps()).arg(ctx.scene.tomeLines.size());
            }
        }
''')
print('ok')
