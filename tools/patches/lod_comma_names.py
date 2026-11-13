import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


C = 'src/qtapp/model_cache.cpp'
patch(C, '''        if (!e || e->failed || e->zone.isEmpty() || !d || lod >= int(d->lods.size()) || d->lods[size_t(lod)].surfs.empty()) {
            lodFailed_.insert(key);
            continue;
        }''', '''        // A surfs name with a comma is a reference to another zone's asset (",name", as the LOD0 failures say); the
        // ACTS server refuses such a name and with it the whole batch ("Invalid asset name in request": on Verdansk
        // every LOD batch failed, 2026-11-13). Those LODs stay with the level before them.
        if (!e || e->failed || e->zone.isEmpty() || !d || lod >= int(d->lods.size()) || d->lods[size_t(lod)].surfs.empty() ||
            d->lods[size_t(lod)].surfs.find(',') != std::string::npos) {
            lodFailed_.insert(key);
            continue;
        }''')
print('ok')
