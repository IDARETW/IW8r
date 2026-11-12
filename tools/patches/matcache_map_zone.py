import io

def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

patch('src/qtapp/material_cache.h', [
 ('''    void request(const QStringList& materials);                  // async; de-duplicated; batched per zone
''', '''    void request(const QStringList& materials);                  // async; de-duplicated; batched per zone
    // A stock map shown as the reference: materials the catalog does not list are looked up in this zone (and its
    // techsets zone). The per-map terrain blends (tw4/..., twc4/...) are named per map, and the catalog lists only
    // those of the traced zones. Materials that failed as "not listed" are tried again.
    void addMapZone(const QString& zone);
'''),
 ('''    void loadIndex();
''', '''    QStringList mapZones_;  // addMapZone, newest first
    void loadIndex();
'''),
])
patch('src/qtapp/material_cache.cpp', [
 ('''        if (e.failed && (e.error.contains("not in zone") || e.error.contains("body not found"))) continue;
''', '''        if (e.failed && (e.error.contains("not in zone") || e.error.contains("body not found"))) continue;
        // Not listed in the catalog: a stock map reference may name its zone (addMapZone), so try again.
        if (e.failed && e.error.contains("not listed")) continue;
'''),
 ('''bool MaterialCache::busy() const { return running_ || !pending_.isEmpty(); }
''', '''void MaterialCache::addMapZone(const QString& zone) {
    if (zone.isEmpty() || mapZones_.contains(zone)) return;
    mapZones_.prepend(zone);
    for (auto it = index_.begin(); it != index_.end();)
        it = it->second.failed && it->second.error.contains("not listed") ? index_.erase(it) : std::next(it);
}

bool MaterialCache::busy() const { return running_ || !pending_.isEmpty(); }
'''),
 ('''        const QStringList zones = catalog_.count(m) ? catalog_.at(m) : QStringList();
        if (zones.isEmpty()) w.error = "not listed in iw8_materials.tsv";
''', '''        QStringList zones = catalog_.count(m) ? catalog_.at(m) : QStringList();
        if (zones.isEmpty()) zones = mapZones_;  // a stock map's own materials (addMapZone)
        if (zones.isEmpty()) w.error = "not listed in iw8_materials.tsv";
'''),
])
patch('src/qtapp/main_window.cpp', [
 ('''    ctx_.setStatus("Loading " + zone + " world geometry...");
''', '''    ctx_.setStatus("Loading " + zone + " world geometry...");
    matCache_->addMapZone(zone);  // its own terrain blends are not in the material catalog
'''),
])
print('ok')
