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


CH = 'src/qtapp/camera_view.h'
CC = 'src/qtapp/camera_view.cpp'
C = 'src/qtapp/main_window.cpp'
M = 'src/qtapp/main.cpp'
GH = 'src/qtapp/game_look_assets.h'
GC = 'src/qtapp/game_look_assets.cpp'

# ---------------- camera
patch(CH, '''    // The stock map's own sun (its ComWorld sun light: intensity, linear colour, direction toward the sun).''',
      '''    // The stock map's local lights (core stock_lights.h, research 106) and its IES strip (R8 texels; empty = white), sent
    // to the renderer on the next Game Lighting frame. null clears them.
    void setStockLocalLights(std::shared_ptr<const std::vector<StockGameLight>> lights, std::shared_ptr<const std::vector<uint8_t>> ies) {
        pendingLights_ = lights ? std::move(lights) : std::make_shared<const std::vector<StockGameLight>>();
        pendingIes_ = std::move(ies);
        update();
    }
    size_t stockLocalLightCount() const { return renderer_.stockLocalLightCount(); }
    size_t stockLocalLightsDrawn() const { return renderer_.stockLocalLightsDrawn(); }
    // The stock map's own sun (its ComWorld sun light: intensity, linear colour, direction toward the sun).''')
patch(CH, '''    std::shared_ptr<GtsoLut> pendingGtso_;''', '''    std::shared_ptr<GtsoLut> pendingGtso_;
    std::shared_ptr<const std::vector<StockGameLight>> pendingLights_;
    std::shared_ptr<const std::vector<uint8_t>> pendingIes_;''')
patch(CC, '''        if (pendingGtso_) {
            renderer_.setGtsoLut(pendingGtso_->width, pendingGtso_->height, pendingGtso_->depth, pendingGtso_->r.data());
            pendingGtso_.reset();
        }''', '''        if (pendingGtso_) {
            renderer_.setGtsoLut(pendingGtso_->width, pendingGtso_->height, pendingGtso_->depth, pendingGtso_->r.data());
            pendingGtso_.reset();
        }
        if (pendingLights_) {
            renderer_.setStockLocalLights(*pendingLights_);
            renderer_.setIesStrip(pendingIes_ ? int(pendingIes_->size()) : 0, pendingIes_ && !pendingIes_->empty() ? pendingIes_->data() : nullptr);
            pendingLights_.reset();
            pendingIes_.reset();
        }''')

# ---------------- the IES strip decode
patch(GH, '''// A BC4 DDS volume (DX10 header BC4_UNORM, or FourCC ATI1 / BC4U), mip 0 decoded to one byte per texel. Fails on
// anything else. Exposed for the tests.
bool decodeBc4VolumeDds(const QByteArray& dds, GtsoLut& out);''', '''// A BC4 DDS volume (DX10 header BC4_UNORM, or FourCC ATI1 / BC4U), mip 0 decoded to one byte per texel. Fails on
// anything else. Exposed for the tests.
bool decodeBc4VolumeDds(const QByteArray& dds, GtsoLut& out);

// A stock map's IES strip (`*ieslookup`: DX10 header R8_UNORM, width x 1): level 0, one byte per texel. Empty on
// anything else.
std::vector<uint8_t> decodeR8StripDds(const QByteArray& dds);''')
patch(GC, '''GtsoLut loadGtsoLut(const std::function<QString(const QString&)>& toolPath) {''', '''std::vector<uint8_t> decodeR8StripDds(const QByteArray& dds) {
    const auto u32 = [&](int o) {
        uint32_t v = 0;
        for (int k = 0; k < 4; ++k) v |= uint32_t(uint8_t(dds[o + k])) << (8 * k);
        return v;
    };
    if (dds.size() < 148 || std::memcmp(dds.constData(), "DDS ", 4) != 0 || std::memcmp(dds.constData() + 84, "DX10", 4) != 0)
        return {};
    const uint32_t h = u32(12), w = u32(16);
    if (u32(128) != 61 || h != 1 || w < 2 || w > 65536 || size_t(dds.size()) < 148 + size_t(w)) return {};  // DXGI R8_UNORM
    const uint8_t* p = reinterpret_cast<const uint8_t*>(dds.constData()) + 148;
    return std::vector<uint8_t>(p, p + w);
}

GtsoLut loadGtsoLut(const std::function<QString(const QString&)>& toolPath) {''')

# ---------------- main window: read, export once, pack, send
patch(C, '''        double intensity = 0;
        Vec3 color{1, 1, 1}, toward{0, 0, 1};
        QFile f(json);''', '''        // The map's local lights (research 106): spots and omnis of the same ComWorld, their lightdefs and the IES strip
        // (both exported once by ACTS into <zone>/lights; "done" marks a run, whatever it found).
        auto localLights = std::make_shared<std::vector<StockGameLight>>();
        auto iesStrip = std::make_shared<std::vector<uint8_t>>();
        if (!json.isEmpty()) {
            QFile lf(json);
            std::vector<StockPrimaryLight> prim;
            if (lf.open(QIODevice::ReadOnly) && parseStockPrimaryLights(lf.readAll().toStdString(), prim)) {
                const bool any = std::any_of(prim.begin(), prim.end(), [](const StockPrimaryLight& l) { return l.type == 2 || l.type == 3; });
                const QString lightDir = QFileInfo(dir).absolutePath() + "/lights";
                if (any && !QFileInfo::exists(lightDir + "/done") && QFileInfo::exists(acts) && QFileInfo::exists(ff)) {
                    QDir().mkpath(lightDir);
                    QProcess p;
                    p.start(acts, {"--noUpdater", "fastfile", "-r", "mw19replay", "-g", replay, "--oodle", oodle, "-a", "lightdef", "-l", "-o",
                                   lightDir + "/defs", ff});
                    p.waitForFinished(120000);
                    QProcess q;
                    q.start(acts, {"--noUpdater", "fastfile", "-r", "mw19replay", "-g", replay, "--oodle", oodle, "-a", "image", "-n",
                                   "*ieslookup", "-l", "-o", lightDir + "/ies", ff});
                    q.waitForFinished(120000);
                    QFile done(lightDir + "/done");
                    if (done.open(QIODevice::WriteOnly)) done.write("lightdef and *ieslookup exported\\n");
                }
                std::map<std::string, StockLightDef> defs;
                for (QDirIterator it(lightDir + "/defs", {"*.json"}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
                    QFile df(it.next());
                    StockLightDef d;
                    if (df.open(QIODevice::ReadOnly) && parseStockLightDef(df.readAll().toStdString(), d)) defs[d.name] = d;
                }
                for (QDirIterator it(lightDir + "/ies", {"*.dds"}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
                    QFile sf(it.next());
                    if (sf.open(QIODevice::ReadOnly)) *iesStrip = decodeR8StripDds(sf.readAll());
                    if (!iesStrip->empty()) break;
                }
                for (const StockPrimaryLight& l : prim) {
                    const auto d = defs.find(l.defName);
                    StockGameLight g;
                    // Without the strip every light samples white: the profile offsets would read nothing there.
                    if (packStockLight(l, d != defs.end() && !iesStrip->empty() ? &d->second : nullptr, g)) localLights->push_back(g);
                }
            }
        }
        double intensity = 0;
        Vec3 color{1, 1, 1}, toward{0, 0, 1};
        QFile f(json);''')
patch(C, '''        QMetaObject::invokeMethod(qApp, [self, zone, intensity, color, toward, visionText, skyIntensity, grade] {
            if (!self || self->referenceZone_ != zone) return;''', '''        QMetaObject::invokeMethod(qApp, [self, zone, intensity, color, toward, visionText, skyIntensity, grade, localLights, iesStrip] {
            if (!self || self->referenceZone_ != zone) return;
            self->camera_->setStockLocalLights(localLights, iesStrip);''')
patch(C, '''        camera_->setStockProbes(nullptr);
        refPropVolLight_.reset();''', '''        camera_->setStockProbes(nullptr);
        camera_->setStockLocalLights(nullptr, nullptr);
        refPropVolLight_.reset();''')
patch(C, '''#include "stock_lightgrid.h"''', '''#include "stock_lightgrid.h"
#include "stock_lights.h"''')

# ---------------- bench line
patch(M, '''                report << QString("game_ao drawn %1 hdr %2").arg(cam->gameAoDrawn() ? 1 : 0).arg(cam->gameHdrDrawn() ? 1 : 0);''',
      '''                report << QString("game_ao drawn %1 hdr %2").arg(cam->gameAoDrawn() ? 1 : 0).arg(cam->gameHdrDrawn() ? 1 : 0);
                report << QString("stock_lights %1 in the map, %2 drawn").arg(cam->stockLocalLightCount()).arg(cam->stockLocalLightsDrawn());''')
print('ok')
patch(C, '''            self->camera_->setStockProbes(nullptr);
''', '''            self->camera_->setStockProbes(nullptr);
            self->camera_->setStockLocalLights(nullptr, nullptr);
''')
print('ok 2')
