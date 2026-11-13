import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if b and s.count(b) >= 1:
        return
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


WH = 'src/qtapp/world_cache.h'
H = 'src/qtapp/main_window.h'
C = 'src/qtapp/main_window.cpp'

patch(WH, '''    int streamedPending() const { return int(streamQueue_.size()) + (streamRunning_ ? 1 : 0); }''',
      '''    int streamedPending() const { return int(streamQueue_.size()) + (streamRunning_ ? 1 : 0); }
    // An exported tile's lightmap / light grid job, when it lacks either verdict (nothing otherwise).
    void requestTileLightmap(const QString& zone, const QString& tile) { queueTileLightmap(zone, tile); }''')
patch(H, '''    std::set<int> refPropTileLightAsked_;''', '''    std::set<int> refPropTileLightAsked_;
    std::set<QString> streamExtrasAsked_;  // exported tiles whose lightmap / light grid job was asked for this session''')
patch(C, '''        QFile(tileDir + "/used").open(QIODevice::WriteOnly);  // its last use (the disk budget drops the oldest)''',
      '''        QFile(tileDir + "/used").open(QIODevice::WriteOnly);  // its last use (the disk budget drops the oldest)
        // A tile exported before its light grid existed: its job (one decompression, in the background).
        if (streamExtrasAsked_.insert(name).second) worldCache_->requestTileLightmap(referenceZone_, name);''')
print('ok')
