import io

def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

patch('src/qtapp/main.cpp', [
 ('#include "qt_gui_test.h"\n', '#include "feature_shots.h"\n#include "qt_gui_test.h"\n'),
 ('''    bool chainTest = false;  // --model-chain-test
''', '''    bool chainTest = false;  // --model-chain-test
    bool featureShots = false;  // --feature-shots (feature_shots.h)
'''),
 ('''        else if (a == "--model-chain-test") chainTest = true;
''', '''        else if (a == "--model-chain-test") chainTest = true;
        else if (a == "--feature-shots") featureShots = true;
'''),
 # real window system like the smoke test: off-screen position, volatile settings, no first-run dialogs
 ('''    const bool smoke = !smokeFile.isEmpty() || !compileFile.isEmpty() || !propsZone.isEmpty();
''', '''    const bool smoke = !smokeFile.isEmpty() || !compileFile.isEmpty() || !propsZone.isEmpty() || featureShots;
'''),
 ('''        } else if (!propsZone.isEmpty()) {
''', '''        } else if (featureShots) {
            QTimer::singleShot(300, &w, [&] {
                app.exit(iw8r::qt::runFeatureShots(w, outDir.isEmpty() ? QCoreApplication::applicationDirPath() + "/feature_shots" : outDir));
            });
        } else if (!propsZone.isEmpty()) {
'''),
])
print('ok')
