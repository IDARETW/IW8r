import io
def patch(p, pairs):
    s=io.open(p,encoding='utf-8',newline='').read(); nl='\r\n' if '\r\n' in s else '\n'
    for a,b in pairs:
        a=a.replace('\n',nl); b=b.replace('\n',nl); assert s.count(a)==1,(p,a[:80]); s=s.replace(a,b)
    io.open(p,'w',encoding='utf-8',newline='').write(s)
patch('src/qtapp/material_cache.cpp', [
 ('''    QDir(root_ + "/w").removeRecursively();  // scratch left behind by a crash''',
  '''    QDir(runRoot()).removeRecursively();  // scratch left behind by a crash'''),
 ('''    const QString dir = QDir::toNativeSeparators(root_ + "/w/" + QString::number(++runSeq_));''',
  '''    const QString dir = QDir::toNativeSeparators(runRoot() + "/" + QString::number(++runSeq_));'''),
 ('''    else if (!running_) QDir(root_ + "/w").removeRecursively();''', '''    else if (!running_) QDir(runRoot()).removeRecursively();'''),
 ('''std::optional<MaterialCache::Entry> MaterialCache::lookup(const QString& material) const {''',
  '''QString MaterialCache::runRoot() const {
    // ACTS writes <run>/mw19replay/<zone>/assets/image/<packed image name>.<n>.dds: packed names reach 130 characters,
    // so a run folder under a deep cache root went past Windows' 260 (ACTS: "Cannot write Replay asset"; mp_rust's
    // corrugated metal stayed the missing-material checker). Runs go to a short folder of the system temp instead.
    return QDir::tempPath() + "/iw8m" + QString::number(quintptr(this) & 0xFFFFF, 16);
}

std::optional<MaterialCache::Entry> MaterialCache::lookup(const QString& material) const {'''),
])
patch('src/qtapp/material_cache.h', [
 ('''    QString cacheRoot() const;''', '''    QString cacheRoot() const;
    QString runRoot() const;  // ACTS run folders (short: long packed image names)'''),
])
print('ok')
