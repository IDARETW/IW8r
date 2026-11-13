import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


M = 'src/qtapp/main.cpp'
patch(M, '''    }).detach();
    using iw8r::Vec3;
    QDir().mkpath(outDir);''', '''    }).detach();
    // Every log line (status messages say which load step runs) goes into memory.log too, stamped with the time it was
    // logged, so a memory spike can be matched to the step that caused it: "<seconds> S <text>".
    {
        const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
        const std::string path = QDir(outDir).filePath("memory.log").toStdString();
        QObject::connect(&iw8r::qt::EditorLog::instance(), &iw8r::qt::EditorLog::added, &w, [t0, path](const iw8r::qt::LogEntry& e) {
            if (FILE* f = std::fopen(path.c_str(), "a")) {
                std::fprintf(f, "%.1f S %s\\n", (e.time.toMSecsSinceEpoch() - t0) / 1000.0, e.text.left(200).toUtf8().constData());
                std::fclose(f);
            }
        });
    }
    using iw8r::Vec3;
    QDir().mkpath(outDir);''')
print('ok')
