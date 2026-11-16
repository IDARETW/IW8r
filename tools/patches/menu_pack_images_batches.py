import io

# The images fill exports in batches into a scratch folder that is deleted after each batch (50k images would be about
# 67 GB of DDS in the image cache; the disks have 8-23 GB free, 2026-11-16).

def slice_replace(p, start, end, new):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    s = s.replace('\r\n', '\n')
    if new in s:
        return
    assert s.count(start) == 1 and s.count(end) == 1, (p, s.count(start), s.count(end))
    a = s.index(start)
    b = s.index(end)
    s = s[:a] + new + s[b:]
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))


NEW = r'''        if (kind == "images") {
            MenuPackWriter w(kind, 75);  // 50k images: 64 px thumbnails, 160 px previews at 75 keep the pack small
            auto store = std::make_shared<ThumbStore>(thumbDir + "images.pack");
            QStringList names;
            for (const auto& [n, z] : catalogRows("iw8_images.tsv", false)) names << n;
            if (limit > 0 && names.size() > limit) names = names.mid(0, limit);
            // One image from its DDS: the thumbnail, the preview and the facts.
            const auto fromDds = [](const QString& dds, QImage* thumb, QImage* preview, QString* facts) {
                if (thumb->isNull()) *thumb = ddsPicture(dds, 64);
                *preview = ddsPicture(dds, 160);
                QFile f(dds);
                if (!f.open(QIODevice::ReadOnly)) return;
                const QByteArray head = f.read(160);
                int fw = 0, fh = 0;
                app::Rgba8Image tiny;
                app::loadDdsPreview(dds.toStdString(), 1, tiny, &fw, &fh);
                QStringList parts{QString("%1 x %2").arg(fw).arg(fh)};
                parts += ddsFactsOf(head.toStdString());
                *facts = parts.join(factSep());
            };
            QStringList missing;
            int done = 0;
            for (const QString& n : names) {
                const QString dds = imageCache_ ? imageCache_->cachedDds(n) : QString();
                QImage thumb = store->getAny(n), preview;
                QString facts;
                if (!dds.isEmpty()) fromDds(dds, &thumb, &preview, &facts);
                else if (thumb.isNull()) missing << n;
                w.add(n, fitted(thumb, 64), {}, facts, fitted(preview, 160));
                if (++done % 10000 == 0) std::printf("  %d of %lld images, %.0f s\n", done, qlonglong(names.size()), clock.elapsed() / 1000.0), std::fflush(stdout);
            }
            if (fill && !missing.isEmpty()) {
                // Batches exported into a scratch folder, packed and deleted: the image cache does not grow.
                const QString scratch = QDir::tempPath() + "/iw8r_menu_images";
                constexpr int kBatch = 300;
                int got = 0;
                for (int at = 0; at < missing.size(); at += kBatch) {
                    QDir(scratch).removeRecursively();
                    QDir().mkpath(scratch);
                    const QStringList batch = missing.mid(at, kBatch);
                    {
                        StockSkyCache cache([this](const QString& k) { return toolPath(k); }, scratch, nullptr, true);
                        for (const QString& n : batch) cache.request(n);
                        waitFor([&cache] { return !cache.busy(); }, 2 * 60 * 60 * 1000);
                        for (const QString& n : batch) {
                            const QString dds = cache.cachedDds(n);
                            if (dds.isEmpty()) continue;
                            QImage thumb, preview;
                            QString facts;
                            fromDds(dds, &thumb, &preview, &facts);
                            w.add(n, fitted(thumb, 64), {}, facts, fitted(preview, 160));
                            ++got;
                        }
                    }
                    QDir(scratch).removeRecursively();
                    const double s = clock.elapsed() / 1000.0;
                    const int doneN = std::min<int>(at + kBatch, int(missing.size()));
                    std::printf("  exported %d of %lld missing images (%d with a picture), %.0f s, about %.0f min left\n", doneN,
                                qlonglong(missing.size()), got, s, s / std::max(1, doneN) * (missing.size() - doneN) / 60.0);
                    std::fflush(stdout);
                    // Saved every few batches: a stopped run keeps what it made (the next run skips nothing it has, but
                    // the pictures are in the pack).
                    if ((at / kBatch) % 10 == 9) w.save(QDir(outDir).filePath(kind + ".iw8mp"));
                }
            }
            MenuPackWriter::Stats st;
            QString err;
            if (!w.save(QDir(outDir).filePath(kind + ".iw8mp"), &err, &st)) ++failures, std::printf("  %s\n", qPrintable(err));
            report(kind, st, clock.elapsed());
            continue;
        }
'''
slice_replace('E:/IW8-Radiant/src/qtapp/main_window_menu_pack.cpp', '        if (kind == "images") {\n', '        if (kind == "materials") {\n', NEW)
print('ok')
