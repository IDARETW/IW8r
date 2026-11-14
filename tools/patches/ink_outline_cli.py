import io

# IW8Radiant.exe <map> --ink-outlines <width>: Tools > Toon Ink Outlines without the GUI, then save the map (plan M6).


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


M = 'E:/IW8-Radiant/src/qtapp/main.cpp'
patch(M, '''    QString compileFile, openFile, keysPath,''', '''    double inkWidth = 0;  // --ink-outlines <width>
    QString compileFile, openFile, keysPath,''')
patch(M, '''        else if (a == "--safe-mode") continue;
''', '''        else if (a == "--ink-outlines" && i + 1 < argc) inkWidth = std::atof(argv[++i]);
        else if (a == "--safe-mode") continue;
''')
patch(M, '''        if (!openFile.isEmpty()) w.openPath(openFile);
''', '''        if (!openFile.isEmpty()) w.openPath(openFile);
        if (inkWidth > 0) {
            // Tools > Toon Ink Outlines on the opened map, saved; exit 0 when strips were written.
            QTimer::singleShot(300, &w, [&] {
                if (w.context().doc.path().empty()) {
                    app.exit(3);
                    return;
                }
                const size_t n = w.generateInkOutlines(inkWidth);
                std::string err;
                const bool ok = n > 0 && w.context().doc.save(&err);
                std::printf("ink outlines: %zu strip(s)%s%s\\n", n, ok ? ", saved" : ", not saved ", err.c_str());
                std::fflush(stdout);
                app.exit(ok ? 0 : 4);
            });
        }
''')
print('ok')
