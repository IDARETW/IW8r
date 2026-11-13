import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:90])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/qtapp/main_window.cpp', [
 ('''        std::string visionText;
        if (QFile vf(vision); !vision.isEmpty() && vf.open(QIODevice::ReadOnly)) visionText = vf.readAll().toStdString();''',
  '''        std::string visionText;
        if (QFile vf(vision); !vision.isEmpty() && vf.open(QIODevice::ReadOnly)) visionText = vf.readAll().toStdString();
        // The sky material's first pixel constant scales its image (tw/sky_<map>, in techsets_<map>.ff: 39.1 on
        // mp_m_speedball): the stock sky image is stored that many times darker than the game draws it.
        double skyIntensity = 0;
        {
            const QString skyDir = QFileInfo(dir).absolutePath() + "/sky_material";
            const auto findMat = [&] {
                for (QDirIterator it(skyDir, {"*.json"}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
                    const QString f = it.next();
                    if (f.contains("/assets/material/")) return f;
                }
                return QString();
            };
            QString mj = findMat();
            const QString techsets = QFileInfo(ff).absolutePath() + "/techsets_" + zone + ".ff";
            if (mj.isEmpty() && QFileInfo::exists(acts))
                for (const QString& z : {techsets, ff}) {
                    if (!QFileInfo::exists(z)) continue;
                    QProcess p;
                    p.start(acts, {"--noUpdater", "fastfile", "-r", "mw19replay", "-g", replay, "--oodle", oodle, "-a", "material", "-n",
                                   "tw/sky_" + zone, "-l", "-o", skyDir, z});
                    p.waitForFinished(120000);
                    mj = findMat();
                    if (!mj.isEmpty()) break;
                }
            QFile mf(mj);
            if (!mj.isEmpty() && mf.open(QIODevice::ReadOnly)) {
                const QJsonArray vals = QJsonDocument::fromJson(mf.readAll())
                                            .object()["asset"].toObject()["fields"].toObject()["constantBufferTable"].toObject()["values"].toArray();
                const QString hex = vals.isEmpty() ? QString() : vals[0].toObject()["psData"].toObject()["bytes"].toString();
                const QByteArray b = QByteArray::fromHex(hex.left(8).toLatin1());
                if (b.size() == 4) {
                    float v = 0;
                    std::memcpy(&v, b.constData(), 4);
                    if (std::isfinite(v) && v > 0 && v < 1e5) skyIntensity = v;
                }
            }
        }'''),
 ('''        QMetaObject::invokeMethod(qApp, [self, zone, intensity, color, toward, visionText] {
            if (!self || self->referenceZone_ != zone) return;
            if (!visionText.empty()) self->camera_->setGameStockVision(visionText);''',
  '''        QMetaObject::invokeMethod(qApp, [self, zone, intensity, color, toward, visionText, skyIntensity] {
            if (!self || self->referenceZone_ != zone) return;
            if (!visionText.empty()) self->camera_->setGameStockVision(visionText);
            if (skyIntensity > 0) self->camera_->setGameStockSkyIntensity(skyIntensity);'''),
])
patch('src/qtapp/camera_view.h', [
 ('''    void setGameStockVision(const std::string& visionText) {''',
  '''    // The stock sky material's intensity (its first pixel constant): the sky image times this is the drawn sky.
    void setGameStockSkyIntensity(double intensity) {
        stockSkyIntensity_ = intensity;
        update();
    }
    void setGameStockVision(const std::string& visionText) {'''),
 ('''    GameFog stockFog_;''', '''    GameFog stockFog_;
    double stockSkyIntensity_ = 0;'''),
 ('''stockSunKnown_ = false, stockFog_ = GameFog{};''', '''stockSunKnown_ = false, stockFog_ = GameFog{}, stockSkyIntensity_ = 0;'''),
])
patch('src/qtapp/camera_view.cpp', [
 ('''        if (stock && skyExposure_ > 0) g.skyScale = 1.0f / skyExposure_;''',
  '''        if (stock && skyExposure_ > 0) g.skyScale = float((stockSkyIntensity_ > 0 ? stockSkyIntensity_ : 1.0) / skyExposure_);'''),
])
s = io.open('src/qtapp/main_window.cpp', encoding='utf-8', newline='').read()
for inc in ('#include <cstring>', '#include <cmath>'):
    if inc not in s:
        i = s.index('#include <')
        s = s[:i] + inc + '\n' + s[i:]
io.open('src/qtapp/main_window.cpp', 'w', encoding='utf-8', newline='').write(s)
print('ok')
