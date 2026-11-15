import io

# The camera's sky: the DDS read and converted to six faces on a worker thread (skyCubemapFromDds held the GUI thread
# for up to a second when a stock sky arrived; stall watch 2026-11-15); skyDecoded() uploads the faces.
# texturesPending() and renderImage() wait for a decode under way. StockSkyCache::cachedDds (asked every frame)
# remembers the file it found and checks a missing one at most twice a second.

R = 'E:/IW8-Radiant/src/qtapp/'


def rw(p, pairs):
    s = io.open(R + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl)
        b = b.replace('\n', nl)
        if b in s and a not in s:
            continue
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(R + p, 'w', encoding='utf-8', newline='').write(s)


rw('camera_view.cpp', [
('''        if (skyKey != skyPath_) {
            skyPath_ = skyKey;
            skyTex_.clear();
            std::string bytes, cube, err;
            SkyConvertOptions so;
            so.faceSize = skyHdr ? 512 : 256;
            so.rgbm = skyHdr;
            // The map's sky rotation (worldspawn skyrotation; stock maps: from their entity string).
            for (int k = 0; k < 3; ++k) so.rotation[k] = skyRot[k];
            so.brightness = skyBrightness;
            SkyConvertInfo skyInfo;
            if (!sky.isEmpty() && readFile(sky.toStdString(), bytes) && skyCubemapFromDds(bytes, cube, so, &skyInfo, &err)) {
                skyExposure_ = skyInfo.hdr ? skyInfo.exposure : 0.0f;
                skyRgbmRange_ = skyInfo.rgbm ? so.rgbmRange : 1.0f;
                skyMeanLum_ = skyInfo.meanLuminance, skyHighLum_ = skyInfo.highLuminance;
                // Legacy DDS: 128-byte header, six faces of B8G8R8A8, one mip each.
                const int fs = int(std::lround(std::sqrt((cube.size() - 128) / 24.0)));
                if (fs > 0 && cube.size() == 128 + size_t(6) * size_t(fs) * size_t(fs) * 4)
                    for (int f = 0; f < 6; ++f) {
                        std::vector<uint8_t> rgba(size_t(fs) * size_t(fs) * 4);
                        const uint8_t* src = reinterpret_cast<const uint8_t*>(cube.data()) + 128 + size_t(f) * rgba.size();
                        for (size_t k = 0; k < rgba.size(); k += 4) {
                            rgba[k] = src[k + 2];
                            rgba[k + 1] = src[k + 1];
                            rgba[k + 2] = src[k];
                            rgba[k + 3] = skyInfo.rgbm ? src[k + 3] : 255;  // RGBM: the multiplier
                        }
                        skyTex_.push_back(renderer_.createTexture(rgba.data(), fs, fs));
                        if (skyInfo.rgbm) renderer_.setTextureRawAlpha(skyTex_.back(), true);
                        // Diagnosis: IW8R_DUMP_SKY=<folder> writes the six preview faces as PNG.
                        if (const QString dump = qEnvironmentVariable("IW8R_DUMP_SKY"); !dump.isEmpty())
                            QImage(rgba.data(), fs, fs, QImage::Format_RGBA8888).save(QDir(dump).filePath(QString("sky_face%1.png").arg(f)));
                    }
            }
            geomDirty_ = true;
        }''', '''        if (skyKey != skyPath_) {
            skyPath_ = skyKey;
            skyTex_.clear();
            SkyConvertOptions so;
            so.faceSize = skyHdr ? 512 : 256;
            so.rgbm = skyHdr;
            // The map's sky rotation (worldspawn skyrotation; stock maps: from their entity string).
            for (int k = 0; k < 3; ++k) so.rotation[k] = skyRot[k];
            so.brightness = skyBrightness;
            // Read and converted to six faces on a worker thread (up to a second on the GUI thread for a stock sky);
            // skyDecoded() uploads them. texturesPending() and captures wait for it.
            skyDecoding_ = !sky.isEmpty();
            if (skyDecoding_) {
                QPointer<CameraView> self(this);
                std::thread(guardedWorker(__FILE__, __LINE__, [self, skyKey, file = sky, so] {
                    auto r = std::make_shared<SkyFaces>();
                    r->options = so;
                    std::string bytes, cube, err;
                    if (readFile(file.toStdString(), bytes) && skyCubemapFromDds(bytes, cube, r->options, &r->info, &err)) {
                        // Legacy DDS: 128-byte header, six faces of B8G8R8A8, one mip each.
                        const int fs = int(std::lround(std::sqrt((cube.size() - 128) / 24.0)));
                        if (fs > 0 && cube.size() == 128 + size_t(6) * size_t(fs) * size_t(fs) * 4) {
                            r->size = fs;
                            for (int f = 0; f < 6; ++f) {
                                std::vector<uint8_t> rgba(size_t(fs) * size_t(fs) * 4);
                                const uint8_t* src = reinterpret_cast<const uint8_t*>(cube.data()) + 128 + size_t(f) * rgba.size();
                                for (size_t k = 0; k < rgba.size(); k += 4) {
                                    rgba[k] = src[k + 2];
                                    rgba[k + 1] = src[k + 1];
                                    rgba[k + 2] = src[k];
                                    rgba[k + 3] = r->info.rgbm ? src[k + 3] : 255;  // RGBM: the multiplier
                                }
                                r->faces.push_back(std::move(rgba));
                            }
                        }
                        r->ok = true;
                    }
                    QMetaObject::invokeMethod(qApp, [self, skyKey, r] {
                        if (self) self->skyDecoded(skyKey, *r);
                    }, Qt::QueuedConnection);
                })).detach();
            }
            geomDirty_ = true;
        }'''),
('''QImage CameraView::renderImage(int w, int h, bool clean, double aspect, int settleMs, bool keepGeometry) {
    if (!rendererOk_ || w < 1 || h < 1 || w > 16384 || h > 16384) return {};
    captureW_ = w, captureH_ = h, captureAspect_ = aspect, cleanCapture_ = clean;
    const bool remake = clean || !keepGeometry;
    if (remake) geomDirty_ = true;
    QElapsedTimer t;
    t.start();
    render();
''', '''void CameraView::skyDecoded(const QString& key, const SkyFaces& r) {
    if (key != skyPath_) return;  // the sky changed again meanwhile: a newer decode is under way
    skyDecoding_ = false;
    if (r.ok) {
        skyExposure_ = r.info.hdr ? r.info.exposure : 0.0f;
        skyRgbmRange_ = r.info.rgbm ? r.options.rgbmRange : 1.0f;
        skyMeanLum_ = r.info.meanLuminance, skyHighLum_ = r.info.highLuminance;
        for (size_t f = 0; f < r.faces.size(); ++f) {
            skyTex_.push_back(renderer_.createTexture(r.faces[f].data(), r.size, r.size));
            if (r.info.rgbm) renderer_.setTextureRawAlpha(skyTex_.back(), true);
            // Diagnosis: IW8R_DUMP_SKY=<folder> writes the six preview faces as PNG.
            if (const QString dump = qEnvironmentVariable("IW8R_DUMP_SKY"); !dump.isEmpty())
                QImage(r.faces[f].data(), r.size, r.size, QImage::Format_RGBA8888).save(QDir(dump).filePath(QString("sky_face%1.png").arg(f)));
        }
    }
    geomDirty_ = true;
    update();
}

QImage CameraView::renderImage(int w, int h, bool clean, double aspect, int settleMs, bool keepGeometry) {
    if (!rendererOk_ || w < 1 || h < 1 || w > 16384 || h > 16384) return {};
    captureW_ = w, captureH_ = h, captureAspect_ = aspect, cleanCapture_ = clean;
    const bool remake = clean || !keepGeometry;
    if (remake) geomDirty_ = true;
    QElapsedTimer t;
    t.start();
    render();
    // A sky being decoded on its worker thread is waited for: a capture shows the sky it was taken with.
    while (skyDecoding_ && t.elapsed() < 10000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
        render();
    }
'''),
])
rw('camera_view.h', [
('''    bool texturesPending() const { return moreToLoad_ || refMoreToLoad_; }''',
 '''    bool texturesPending() const { return moreToLoad_ || refMoreToLoad_ || skyDecoding_; }'''),
('''    float skyExposure_ = 0, skyMeanLum_ = 0, skyHighLum_ = 0;''', '''    // The sky faces decoded on a worker thread (render: the sky key changed), uploaded by skyDecoded().
    struct SkyFaces {
        bool ok = false;
        int size = 0;
        std::vector<std::vector<uint8_t>> faces;  // six RGBA faces
        SkyConvertInfo info;
        SkyConvertOptions options;
    };
    void skyDecoded(const QString& key, const SkyFaces& r);
    bool skyDecoding_ = false;
    float skyExposure_ = 0, skyMeanLum_ = 0, skyHighLum_ = 0;'''),
])

# StockSkyCache::cachedDds is asked every frame: the file it found is remembered; a missing one is checked again at
# most twice a second.
rw('stock_sky.cpp', [
('''QString StockSkyCache::cachedDds(const QString& sky) const {
    const QString name = keyOf(sky);
    if (name.isEmpty()) return {};
    const QString file = fileOf(name);
    return QFileInfo::exists(file) ? file : QString();
}''', '''QString StockSkyCache::cachedDds(const QString& sky) const {
    const QString name = keyOf(sky);
    if (name.isEmpty()) return {};
    // Asked every frame: a file found is remembered, a missing one checked again at most twice a second.
    if (const auto it = found_.find(name); it != found_.end()) return *it;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (const auto it = missedAt_.find(name); it != missedAt_.end() && now - *it < 500) return {};
    const QString file = fileOf(name);
    if (QFileInfo::exists(file)) {
        found_.insert(name, file);
        missedAt_.remove(name);
        return file;
    }
    missedAt_.insert(name, now);
    return {};
}'''),
])
print('ok')
