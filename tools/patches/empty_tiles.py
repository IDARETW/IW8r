import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/qtapp/world_cache.cpp', '''            if (it.next().contains("gfx_map_trzone")) ok = true;''',
      '''            if (const QString f = it.next(); f.contains("gfx_map_trzone") && QFileInfo(f).size() > 0) ok = true;  // 0 bytes: a broken export''')
patch('src/qtapp/main_window.cpp', '''        if (!QFileInfo(tileDir).isDir()) {''', '''        // A tile whose export is an empty file (two Verdansk tiles were, 2026-11-13) is exported again.
        if (QFileInfo(tileDir).isDir() && !streamRequested_.count(name)) {
            bool empty = false;
            for (QDirIterator it(tileDir + "/assets/gfx_map_trzone", {"*.json"}, QDir::Files); it.hasNext();)
                empty |= QFileInfo(it.next()).size() == 0;
            if (empty) QDir(tileDir).removeRecursively();
        }
        if (!QFileInfo(tileDir).isDir()) {''')
print('ok')
