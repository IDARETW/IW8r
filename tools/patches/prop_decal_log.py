import io
p = 'src/qtapp/main_window.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = '''    for (const StockDecalBatch& b : projectDecalVolumes(receivers, d.set)) {
        app::ReferenceTexBatch rb;
        if (stockDecalBatch(d, b, order, rb)) out.push_back(std::move(rb)), ++order;
    }'''
b = '''    const std::vector<StockDecalBatch> projected = projectDecalVolumes(receivers, d.set);
    if (const QString logPath = qEnvironmentVariable("IW8R_DECAL_LOG"); !logPath.isEmpty()) {
        // Diagnosis: receivers and the largest projected batches.
        QFile lf(logPath);
        if (lf.open(QIODevice::WriteOnly | QIODevice::Text)) {
            size_t rt = 0;
            for (const auto& m : meshes) rt += m.idx.size() / 3;
            lf.write(QString("receiver meshes %1 triangles %2\\n").arg(meshes.size()).arg(rt).toUtf8());
            std::vector<std::pair<size_t, size_t>> sizes;
            for (size_t i = 0; i < projected.size(); ++i) sizes.push_back({projected[i].idx.size() / 3, i});
            std::sort(sizes.rbegin(), sizes.rend());
            for (size_t k = 0; k < sizes.size() && k < 12; ++k) {
                const StockDecalBatch& pb = projected[sizes[k].second];
                const StockDecalVolume& v = d.set.volumes[pb.volume];
                lf.write(QString("%1 tris vol %2 %3 c %4 %5 %6 h %7 %8 %9 cand %10\\n").arg(sizes[k].first).arg(pb.volume)
                             .arg(QString::fromStdString(v.material)).arg(v.center.x).arg(v.center.y).arg(v.center.z)
                             .arg(v.halfSize.x).arg(v.halfSize.y).arg(v.halfSize.z).arg(pb.candidateTriangles).toUtf8());
            }
        }
    }
    for (const StockDecalBatch& b : projected) {
        app::ReferenceTexBatch rb;
        if (stockDecalBatch(d, b, order, rb)) out.push_back(std::move(rb)), ++order;
    }'''
assert s.count(a.replace('\n', nl)) == 1
s = s.replace(a.replace('\n', nl), b.replace('\n', nl))
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
