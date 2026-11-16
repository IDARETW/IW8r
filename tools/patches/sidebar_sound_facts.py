import io

# Side bar revamp: the Sounds panel's facts. SoundPreview reads a prepared zone's index without ACTS (loadIndex, on the
# panel's first showing) and gives every alias's variants and length (aliasFacts); a sound that starts playing sends
# its waveform (waveReady). The main window puts them on the rows and in the strip, the effects' loop / one-shot length
# on theirs (kept in the settings), and the Images strip gets the DDS format and mip count.

ROOT = 'E:/IW8-Radiant/src/'


def rw(p, pairs):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    s = s.replace('\r\n', '\n')
    for a, b in pairs:
        if b in s and a not in s:
            continue
        assert s.count(a) == 1, (p, s.count(a), a[:90])
        s = s.replace(a, b)
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))


rw('qtapp/sound_preview.h', [
(r'''#include <QStringList>
#include <cstdint>''', r'''#include <QStringList>
#include <QVector>
#include <cstdint>'''),
(r'''    // Tests: decode only, nothing is played.
    void setMuted(bool m) { muted_ = m; }

signals:
    void statusChanged(const QString& text, bool playing);''', r'''    // Tests: decode only, nothing is played.
    void setMuted(bool m) { muted_ = m; }
    // The index of a zone prepared before (its alias table and banks in the cache), read without ACTS on a worker
    // thread; indexReady when it is in memory. Nothing for a zone never prepared (its first Play prepares it).
    void loadIndex(const QString& zone = "common_mp");
    // Every alias of a read zone: how many variants have samples, the shortest and longest, whether one loops.
    struct AliasFacts {
        int variants = 0;
        double shortest = 0, longest = 0;
        bool loop = false;
    };
    QHash<QString, AliasFacts> aliasFacts(const QString& zone = "common_mp") const;

signals:
    void statusChanged(const QString& text, bool playing);
    void indexReady(const QString& zone);
    // A sound started: its waveform (the peak of each of 480 slices, 0-1), its length, whether it loops.
    void waveReady(const QVector<float>& peaks, double seconds, bool loop);'''),
(r'''    void playResolved(const QString& alias);''', r'''    void playResolved(const QString& alias);
    void loadPrepared(const QString& zone, bool quiet);  // the cached index read (quiet: no status lines)'''),
(r'''    bool playing_ = false, preparing_ = false, muted_ = false;''', r'''    bool playing_ = false, preparing_ = false, muted_ = false, indexing_ = false;'''),
])

rw('qtapp/sound_preview.cpp', [
(r'''    if (!z->ready) {
        if (!preparing_) prepare(*z);
        return;
    }
    playResolved(alias);
}''', r'''    if (!z->ready) {
        if (indexing_) return setStatus("Reading the sound banks of " + zoneName + "...");  // it plays once read
        if (!preparing_) prepare(*z);
        return;
    }
    playResolved(alias);
}

void SoundPreview::loadIndex(const QString& zoneName) {
    Zone* z = zone(zoneName);
    if (z->ready) {
        emit indexReady(zoneName);
        return;
    }
    if (z->failed || preparing_ || indexing_) return;
    const QString dir = cacheDir();
    if (!QFileInfo::exists(dir + "/" + zoneName + ".aliases.tsv") || !QFileInfo::exists(dir + "/" + zoneName + ".all.sabl")) return;
    loadPrepared(zoneName, true);
}

QHash<QString, SoundPreview::AliasFacts> SoundPreview::aliasFacts(const QString& zoneName) const {
    QHash<QString, AliasFacts> out;
    const auto it = zones_.constFind(zoneName);
    if (it == zones_.constEnd() || !it.value()->ready) return out;
    const Zone& z = *it.value();
    for (auto a = z.aliases.constBegin(); a != z.aliases.constEnd(); ++a) {
        AliasFacts f;
        for (const uint32_t k : a.value())
            for (const Zone::Bank& bank : z.banks)
                if (const auto e = bank.byKey.constFind(k); e != bank.byKey.constEnd()) {
                    const SabEntry& s = bank.entries[size_t(*e)];
                    const double sec = s.rate ? double(s.frames) / s.rate : 0.0;
                    f.shortest = f.variants ? std::min(f.shortest, sec) : sec;
                    f.longest = std::max(f.longest, sec);
                    f.loop |= s.looping != 0;
                    ++f.variants;
                    break;
                }
        if (f.variants) out.insert(a.key(), f);
    }
    return out;
}

void SoundPreview::loadPrepared(const QString& zoneName, bool quiet) {
    const QString dir = cacheDir();
    const QString zoneDir = QFileInfo(toolPath_("replay")).absolutePath() + "/zone";
    const QString aliasFile = dir + "/" + zoneName + ".aliases.tsv", loaded = dir + "/" + zoneName + ".all.sabl";
    if (quiet) indexing_ = true;
    else setStatus("Reading the sound banks of " + zoneName + "...");
    QPointer<SoundPreview> self(this);
    std::thread(guardedWorker(__FILE__, __LINE__, [self, zoneName, zoneDir, aliasFile, loaded, quiet] {
        auto z = std::make_shared<Zone>();
        z->name = zoneName;
        QFile f(aliasFile);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&f);
            while (!in.atEnd()) {
                const QStringList c = in.readLine().split('\t');
                if (c.size() < 2) continue;
                QVector<uint32_t> keys;
                for (const QString& k : c[1].split(',', Qt::SkipEmptyParts)) keys << uint32_t(k.toULongLong(nullptr, 16));
                z->aliases.insert(c[0], keys);
            }
        }
        for (const QString& b : {zoneDir + "/patch_" + zoneName + ".sabs", zoneDir + "/" + zoneName + ".sabs", loaded}) {
            if (!QFileInfo::exists(b)) continue;
            Zone::Bank bank;
            bank.file = b;
            bank.entries = readSabIndex(b);
            for (int i = 0; i < int(bank.entries.size()); ++i) bank.byKey.insert(bank.entries[size_t(i)].key, i);
            if (!bank.entries.empty()) z->banks.push_back(std::move(bank));
        }
        QMetaObject::invokeMethod(qApp, [self, z, quiet] {
            if (!self) return;
            if (quiet) self->indexing_ = false;
            if (z->aliases.isEmpty() || z->banks.empty()) {
                if (quiet) return;  // a Play prepares the zone again, with its messages
                self->preparing_ = false;
                Zone* zz = self->zone(z->name);
                zz->failed = true;
                zz->error = "the alias index or the sound banks could not be read";
                return self->setStatus("No sound preview for " + z->name + ": " + zz->error);
            }
            z->ready = true;
            self->zones_[z->name] = z;
            self->preparing_ = false;
            if (!quiet) self->setStatus(QString("Sound previews ready: %1 aliases in %2 bank(s)").arg(z->aliases.size()).arg(z->banks.size()));
            emit self->indexReady(z->name);
            if (!self->wantAlias_.isEmpty() && self->wantZone_ == z->name) self->playResolved(self->wantAlias_);
        }, Qt::QueuedConnection);
    })).detach();
}'''),
(r'''    // The last step, on a worker thread: the alias index and the banks' indexes read into memory.
    const auto load = [self, zoneName, zoneDir, aliasFile, loaded, fail] {
        if (!self) return;
        self->setStatus("Reading the sound banks of " + zoneName + "...");
        std::thread(guardedWorker(__FILE__, __LINE__, [self, zoneName, zoneDir, aliasFile, loaded, fail] {
            auto z = std::make_shared<Zone>();
            z->name = zoneName;
            QFile f(aliasFile);
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream in(&f);
                while (!in.atEnd()) {
                    const QStringList c = in.readLine().split('\t');
                    if (c.size() < 2) continue;
                    QVector<uint32_t> keys;
                    for (const QString& k : c[1].split(',', Qt::SkipEmptyParts)) keys << uint32_t(k.toULongLong(nullptr, 16));
                    z->aliases.insert(c[0], keys);
                }
            }
            for (const QString& b : {zoneDir + "/patch_" + zoneName + ".sabs", zoneDir + "/" + zoneName + ".sabs", loaded}) {
                if (!QFileInfo::exists(b)) continue;
                Zone::Bank bank;
                bank.file = b;
                bank.entries = readSabIndex(b);
                for (int i = 0; i < int(bank.entries.size()); ++i) bank.byKey.insert(bank.entries[size_t(i)].key, i);
                if (!bank.entries.empty()) z->banks.push_back(std::move(bank));
            }
            QMetaObject::invokeMethod(qApp, [self, z, fail] {
                if (!self) return;
                if (z->aliases.isEmpty() || z->banks.empty()) return fail("the alias index or the sound banks could not be read");
                z->ready = true;
                self->zones_[z->name] = z;
                self->preparing_ = false;
                self->setStatus(QString("Sound previews ready: %1 aliases in %2 bank(s)").arg(z->aliases.size()).arg(z->banks.size()));
                if (!self->wantAlias_.isEmpty() && self->wantZone_ == z->name) self->playResolved(self->wantAlias_);
            }, Qt::QueuedConnection);
        })).detach();
    };''', r'''    // The last step, on a worker thread: the alias index and the banks' indexes read into memory (loadPrepared).
    const auto load = [self, zoneName] {
        if (self) self->loadPrepared(zoneName, false);
    };'''),
(r'''    playing_ = true;
    if (loop) endTimer_->stop();''', r'''    playing_ = true;
    // The waveform for the Sounds panel: the loudest sample of each of 480 slices.
    {
        QVector<float> peaks(480, 0.f);
        const int64_t frames = last_.frames();
        const int ch = std::max(1, last_.channels);
        for (int64_t f = 0; f < frames; ++f) {
            float m = 0;
            for (int c = 0; c < ch; ++c) m = std::max(m, std::abs(float(last_.samples[size_t(f * ch + c)])) / 32768.f);
            float& p = peaks[int(f * peaks.size() / frames)];
            p = std::max(p, m);
        }
        emit waveReady(peaks, last_.rate ? double(frames) / last_.rate : 0.0, loop);
    }
    if (loop) endTimer_->stop();'''),
])

rw('qtapp/main_window.cpp', [
(r'''            if (!f.isEmpty() && readFile(f.toStdString(), text) && buildVfxSim(text, def) && !def.emitters.empty()) {
                effects_->setEffectPreview(&def,''', r'''            if (!f.isEmpty() && readFile(f.toStdString(), text) && buildVfxSim(text, def) && !def.emitters.empty()) {
                // Its row says loop / one-shot length from now on (kept: the next session knows it too).
                effects_->setRowFact(name, def.duration < 0 ? QString("loop") : QString("%1 s").arg(def.duration, 0, 'f', 1), true);
                effects_->setEffectPreview(&def,'''),
(r'''        connect(sp, &SoundPreview::statusChanged, sounds_, [this, sp](const QString& text, bool playing) {
            sounds_->setSoundStatus(text, playing || sp->preparing());
        });''', r'''        connect(sp, &SoundPreview::statusChanged, sounds_, [this, sp](const QString& text, bool playing) {
            sounds_->setSoundStatus(text, playing || sp->preparing());
        });
        connect(sp, &SoundPreview::waveReady, sounds_,
                [this](const QVector<float>& peaks, double seconds, bool loop) { sounds_->setSoundWave(peaks, seconds, loop); });
        // Every alias's length and variants on its row, once the zone's index is in memory: read from the cache when
        // the panel is first shown (a zone never prepared gets them after its first Play prepares it).
        connect(sounds_, &AssetBrowser::firstShown, sp, [sp] { sp->loadIndex(); });
        connect(sp, &SoundPreview::indexReady, sounds_, [this, sp](const QString& zone) {
            const auto secs = [](double s) { return QString::number(s, 'f', s < 10 ? 1 : 0); };
            QHash<QString, QString> facts;
            const QHash<QString, SoundPreview::AliasFacts> all = sp->aliasFacts(zone);
            for (auto it = all.begin(); it != all.end(); ++it) {
                const SoundPreview::AliasFacts& f = it.value();
                QStringList parts;
                if (f.longest >= 0.05)
                    parts << (f.longest - f.shortest < 0.05 ? secs(f.longest) : secs(f.shortest) + QChar(0x2013) + secs(f.longest)) + " s";
                if (f.variants > 1) parts << QString(QChar(0x00D7)) + QString::number(f.variants);
                if (!parts.isEmpty()) facts.insert(it.key(), parts.join(factSep()));
            }
            sounds_->setRowFacts(facts);
        });'''),
(r'''    const app::Rgba8Image small = app::downscale(img, 256);
    const QImage q = QImage(small.pixels.data(), small.width, small.height, small.width * 4, QImage::Format_RGBA8888).copy();
    images_->setPreview(q, QString("%1\n%2 x %3").arg(name).arg(img.width).arg(img.height));''', r'''    const app::Rgba8Image small = app::downscale(img, 256);
    const QImage q = QImage(small.pixels.data(), small.width, small.height, small.width * 4, QImage::Format_RGBA8888).copy();
    // The strip shows the lines after the name: the size, the format and the mip count of the file.
    QStringList lines{name, QString("%1 x %2").arg(img.width).arg(img.height)};
    lines += ddsFacts(bytes);
    images_->setPreview(q, lines.join('\n'));'''),
(r'''void MainWindow::previewImage(const QString& name) {''', r'''// The format and mip count in a DDS file's header ("BC7 sRGB", "12 mips"), for the Images panel's strip.
static QStringList ddsFacts(const std::string& b) {
    QStringList out;
    if (b.size() < 128 || b.compare(0, 4, "DDS ") != 0) return out;
    const auto u32 = [&b](size_t at) {
        uint32_t v = 0;
        std::memcpy(&v, b.data() + at, 4);
        return v;
    };
    const uint32_t mips = u32(28);
    const std::string cc = b.substr(84, 4);
    QString fmt;
    if (cc == "DX10" && b.size() >= 148) {
        static const QHash<uint32_t, const char*> names = {
            {2, "RGBA32F"}, {10, "RGBA16F"}, {24, "RGB10A2"}, {26, "R11G11B10F"}, {28, "RGBA8"}, {29, "RGBA8 sRGB"},
            {49, "RG8"}, {61, "R8"}, {71, "BC1"}, {72, "BC1 sRGB"}, {74, "BC2"}, {75, "BC2 sRGB"}, {77, "BC3"},
            {78, "BC3 sRGB"}, {80, "BC4"}, {81, "BC4 signed"}, {83, "BC5"}, {84, "BC5 signed"}, {95, "BC6H"},
            {96, "BC6H signed"}, {98, "BC7"}, {99, "BC7 sRGB"}};
        const uint32_t dxgi = u32(128);
        fmt = names.contains(dxgi) ? QString(names.value(dxgi)) : QString("DXGI format %1").arg(dxgi);
    } else if (u32(84)) {
        fmt = QString::fromLatin1(cc.data(), 4).trimmed();
    }
    if (!fmt.isEmpty()) out << fmt;
    if (mips > 1) out << QString("%1 mips").arg(mips);
    return out;
}

void MainWindow::previewImage(const QString& name) {'''),
])
print('ok')
