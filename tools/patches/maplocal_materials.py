import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:100])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/qtapp/material_cache.h', [
 ('''    void addMapZone(const QString& zone);''', '''    void addMapZone(const QString& zone);
    // Layered world materials are numbered per map (tw4/29n_4n_4n, twc4/12n_13n_35n): the same name is a different
    // material on every map, so they are resolved in the stock map's own zones only and cached per map
    // ("<map zone>|<name>"). Owner report 2026-11-12: mp_m_speedball's ground took mp_bm_tut's and backlot2's
    // same-named blends (a metal base showing through, grass missing).
    static bool mapLocalName(const QString& material);
    QString indexKey(const QString& material) const;'''),
 ('''    struct Work {  // one requested material in the current batch
        QString material;''', '''    struct Work {  // one requested material in the current batch
        QString material;
        QString key;  // index key (indexKey: per map for map-local names)'''),
])

patch('src/qtapp/material_cache.cpp', [
 ('''std::optional<MaterialCache::Entry> MaterialCache::lookup(const QString& material) const {
    if (auto it = index_.find(material); it != index_.end()) return it->second;
    if (auto it = seeds_.find(material); it != seeds_.end()) return it->second;
    return std::nullopt;
}''', '''bool MaterialCache::mapLocalName(const QString& material) {
    static const QRegularExpression re(QStringLiteral("^twc?4/\\\\d+d?n(_\\\\d+d?n)*$"), QRegularExpression::CaseInsensitiveOption);
    return re.match(material.trimmed()).hasMatch();
}

QString MaterialCache::indexKey(const QString& material) const {
    return mapLocalName(material) && !mapZones_.isEmpty() ? mapZones_.first() + "|" + material.trimmed() : material.trimmed();
}

std::optional<MaterialCache::Entry> MaterialCache::lookup(const QString& material) const {
    const bool local = mapLocalName(material);
    if (local && mapZones_.isEmpty()) return std::nullopt;  // which map's blend: unknown without a stock map
    if (auto it = index_.find(indexKey(material)); it != index_.end()) return it->second;
    if (auto it = local ? seeds_.end() : seeds_.find(material); it != seeds_.end()) return it->second;
    return std::nullopt;
}'''),
 ('''        if (m.isEmpty() || lookup(m) || queued_.count(m)) continue;
        queued_.insert(m);''', '''        if (m.isEmpty() || lookup(m) || queued_.count(indexKey(m))) continue;
        if (mapLocalName(m) && mapZones_.isEmpty()) continue;  // no stock map: nothing to resolve it in
        queued_.insert(indexKey(m));'''),
 ('''        e.material = it.key();
        e.zone = o.value("zone").toString();''', '''        e.material = it.key().section('|', -1);  // "<map zone>|<name>" for map-local names
        e.zone = o.value("zone").toString();'''),
 ('''        if (e.colorDds.isEmpty() && e.error.contains("not exported") && o.value("maskRules").toInt() < 2) continue;
        index_[e.material] = e;''', '''        if (e.colorDds.isEmpty() && e.error.contains("not exported") && o.value("maskRules").toInt() < 2) continue;
        // A map-local name cached before it was keyed per map: whichever map the catalog named first. Dropped.
        if (mapLocalName(e.material) && !it.key().contains('|')) continue;
        index_[it.key()] = e;'''),
 ('''            for (const QString& m : mats) queued_.erase(m);
            for (const QString& m : pending_) queued_.erase(m);''', '''            for (const QString& m : mats) queued_.erase(indexKey(m));
            for (const QString& m : pending_) queued_.erase(indexKey(m));'''),
 ('''        w.material = m;
        QStringList zones = catalog_.count(m) ? catalog_.at(m) : QStringList();
        if (zones.isEmpty()) zones = mapZones_;  // a stock map's own materials (addMapZone)''',
  '''        w.material = m;
        w.key = indexKey(m);
        // A map-local name only in the stock map's own zones; others from the catalog, else the map's zones.
        QStringList zones = mapLocalName(m) ? QStringList() : catalog_.count(m) ? catalog_.at(m) : QStringList();
        if (zones.isEmpty()) zones = mapLocalName(m) && !mapZones_.isEmpty() ? QStringList{mapZones_.first()} : mapZones_;'''),
 ('''        index_[w.material] = e;
        done << w.material;
    }''', '''        index_[w.key.isEmpty() ? w.material : w.key] = e;
        done << w.material;
        queued_.erase(w.key);
    }'''),
])
s = io.open('src/qtapp/material_cache.cpp', encoding='utf-8', newline='').read()
if '#include <QRegularExpression>' not in s:
    i = s.index('#include <Q')
    s = s[:i] + '#include <QRegularExpression>\n' + s[i:]
    io.open('src/qtapp/material_cache.cpp', 'w', encoding='utf-8', newline='').write(s)
print('ok')
