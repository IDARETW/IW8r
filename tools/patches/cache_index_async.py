import io
import re

# The material and model caches read their index files (13 MB and 4 MB of JSON, with a file check per cached image /
# GLB) on a worker thread started in the constructor; the first use waits for them (index() / seeds()). The stall
# watch measured about 2.2 s + 1 s of the start-up freeze in MaterialCache::loadIndex / ModelCache::loadIndex.

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def func_span(s, header):
    """Start and end (after the closing brace line) of the function starting with `header`."""
    i = s.index(header)
    depth = 0
    j = s.index('{', i)
    k = j
    while True:
        c = s[k]
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                break
        k += 1
    return i, k + 1


def sub_in(text, a, b, count=1):
    assert text.count(a) == count, (text.count(a), a[:80])
    return text.replace(a, b)


# ---------------------------------------------------------------- MaterialCache
P, PH = 'src/qtapp/material_cache.cpp', 'src/qtapp/material_cache.h'
s = read(P)
nl = '\r\n' if '\r\n' in s else '\n'
if 'MaterialCache::readIndex(' not in s:
    s = s.replace('\r\n', '\n')
    # loadIndex -> static readIndex(root)
    i, j = func_span(s, 'void MaterialCache::loadIndex() {')
    body = s[i:j]
    body = sub_in(body, 'void MaterialCache::loadIndex() {\n    QFile f(root_ + "/index.json");\n    if (!f.open(QIODevice::ReadOnly)) return;',
                  'std::map<QString, MaterialCache::Entry> MaterialCache::readIndex(const QString& root) {\n'
                  '    std::map<QString, Entry> out;\n    QFile f(root + "/index.json");\n    if (!f.open(QIODevice::ReadOnly)) return out;')
    body = body.replace('QDir(root_)', 'QDir(root)')
    body = sub_in(body, '        index_[it.key()] = e;\n    }\n}', '        out[it.key()] = e;\n    }\n    return out;\n}')
    assert 'root_' not in body and 'index_' not in body, body
    s = s[:i] + body + s[j:]
    # loadSeeds -> static readSeeds(data, index)
    i, j = func_span(s, 'void MaterialCache::loadSeeds() {')
    body = s[i:j]
    body = sub_in(body, 'void MaterialCache::loadSeeds() {\n    const QString data = repoDataDir();\n    if (data.isEmpty()) return;',
                  'std::map<QString, MaterialCache::Entry> MaterialCache::readSeeds(const QString& data, const std::map<QString, Entry>& index) {\n'
                  '    std::map<QString, Entry> out;\n    if (data.isEmpty()) return out;')
    body = sub_in(body, 'index_.count(name)', 'index.count(name)')
    body = sub_in(body, 'seeds_[name] = e;', 'out[name] = e;')
    assert body.rstrip().endswith('}')
    body = body[:body.rstrip().rfind('}')] + '    return out;\n}'
    assert 'index_' not in body and 'seeds_' not in body, body
    s = s[:i] + body + s[j:]
    # Constructor: start the worker.
    s = sub_in(s, '''    QDir(runRoot()).removeRecursively();  // scratch left behind by a crash
    loadIndex();
    loadSeeds();
}''', '''    QDir(runRoot()).removeRecursively();  // scratch left behind by a crash
    // The index and the seeds are read on a worker thread; index() / seeds() wait for them on first use.
    indexLoad_ = std::async(std::launch::async, [root = root_, data = repoDataDir()] {
        IndexFiles f;
        f.index = readIndex(root);
        f.seeds = readSeeds(data, f.index);
        return f;
    });
}

std::map<QString, MaterialCache::Entry>& MaterialCache::index() const {
    if (indexLoad_.valid()) {
        IndexFiles f = indexLoad_.get();  // waits when the worker is not done yet
        indexData_ = std::move(f.index);
        seedsData_ = std::move(f.seeds);
    }
    return indexData_;
}

std::map<QString, MaterialCache::Entry>& MaterialCache::seeds() const {
    index();
    return seedsData_;
}''')
    # Every other use goes through the accessors.
    s = re.sub(r'\bindex_\b', 'index()', s)
    s = re.sub(r'\bseeds_\b', 'seeds()', s)
    s = s.replace('\n', nl)
    if '#include <future>' not in s:
        s = s.replace('#include "material_cache.h"' + nl, '#include "material_cache.h"' + nl + nl + '#include <future>' + nl, 1)
    write(P, s)

h = read(PH)
nl = '\r\n' if '\r\n' in h else '\n'
if 'indexLoad_' not in h:
    h = h.replace('\r\n', '\n')
    h = sub_in(h, '''    std::map<QString, Entry> index_;   // from root_/index.json
    std::map<QString, Entry> seeds_;   // from data/stock_assets (never written back)''',
               '''    // From root_/index.json and data/stock_assets (the seeds are never written back), read on a worker thread from
    // the constructor (13 MB of JSON and a file check per cached image held the start-up for about 2 s): index() and
    // seeds() wait for them on first use.
    struct IndexFiles {
        std::map<QString, Entry> index, seeds;
    };
    static std::map<QString, Entry> readIndex(const QString& root);
    static std::map<QString, Entry> readSeeds(const QString& dataDir, const std::map<QString, Entry>& index);
    std::map<QString, Entry>& index() const;
    std::map<QString, Entry>& seeds() const;
    mutable std::future<IndexFiles> indexLoad_;
    mutable std::map<QString, Entry> indexData_;
    mutable std::map<QString, Entry> seedsData_;''')
    h = sub_in(h, '    void loadIndex();\n    void loadSeeds();\n', '')
    if '#include <future>' not in h:
        h = h.replace('#include <map>\n', '#include <future>\n#include <map>\n', 1)
        assert '#include <future>' in h
    write(PH, h.replace('\n', nl))

# ---------------------------------------------------------------- ModelCache
P, PH = 'src/qtapp/model_cache.cpp', 'src/qtapp/model_cache.h'
s = read(P)
nl = '\r\n' if '\r\n' in s else '\n'
if 'ModelCache::readIndex(' not in s:
    s = s.replace('\r\n', '\n')
    i, j = func_span(s, 'void ModelCache::loadIndex() {')
    body = s[i:j]
    body = sub_in(body, 'void ModelCache::loadIndex() {\n    QFile f(root_ + "/index.json");\n    if (!f.open(QIODevice::ReadOnly)) return;',
                  'std::map<QString, ModelCache::Entry> ModelCache::readIndex(const QString& root) {\n'
                  '    std::map<QString, Entry> out;\n    QFile f(root + "/index.json");\n    if (!f.open(QIODevice::ReadOnly)) return out;')
    body = body.replace('QDir(root_)', 'QDir(root)')
    body = sub_in(body, '        index_[e.name] = e;\n    }\n}', '        out[e.name] = e;\n    }\n    return out;\n}')
    assert 'root_' not in body and 'index_' not in body, body
    s = s[:i] + body + s[j:]
    s = sub_in(s, '''    QDir(root_ + "/w").removeRecursively();  // scratch left behind by a crash
    loadIndex();
}''', '''    QDir(root_ + "/w").removeRecursively();  // scratch left behind by a crash
    // The index is read on a worker thread; index() waits for it on first use.
    indexLoad_ = std::async(std::launch::async, [root = root_] { return readIndex(root); });
}

std::map<QString, ModelCache::Entry>& ModelCache::index() const {
    if (indexLoad_.valid()) indexData_ = indexLoad_.get();  // waits when the worker is not done yet
    return indexData_;
}''')
    s = re.sub(r'\bindex_\b', 'index()', s)
    s = s.replace('\n', nl)
    if '#include <future>' not in s:
        s = s.replace('#include "model_cache.h"' + nl, '#include "model_cache.h"' + nl + nl + '#include <future>' + nl, 1)
    write(P, s)

h = read(PH)
nl = '\r\n' if '\r\n' in h else '\n'
if 'indexLoad_' not in h:
    h = h.replace('\r\n', '\n')
    h = sub_in(h, '    std::map<QString, Entry> index_;          // from root_/index.json\n',
               '''    // From root_/index.json, read on a worker thread from the constructor (4 MB of JSON and a file check per GLB held
    // the start-up for about 1 s): index() waits for it on first use.
    static std::map<QString, Entry> readIndex(const QString& root);
    std::map<QString, Entry>& index() const;
    mutable std::future<std::map<QString, Entry>> indexLoad_;
    mutable std::map<QString, Entry> indexData_;
''')
    h = sub_in(h, '    void loadIndex();\n', '')
    if '#include <future>' not in h:
        h = h.replace('#include <map>\n', '#include <future>\n#include <map>\n', 1)
        assert '#include <future>' in h
    write(PH, h.replace('\n', nl))
print('ok')
