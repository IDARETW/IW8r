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


GH = 'src/qtapp/game_look_assets.h'
GC = 'src/qtapp/game_look_assets.cpp'
RH = 'src/viewport/render.h'
R = 'src/viewport/render.cpp'
CH = 'src/qtapp/camera_view.h'
CC = 'src/qtapp/camera_view.cpp'

# ---------------- loader
patch(GH, '''// toolPath: the editor's tool paths ("acts", "replay", "oodle"; MainWindow::toolPath). Reads the cached copy, or runs
// ACTS once (a few seconds) and caches it.
EnvBrdfLut loadEnvBrdfLut(const std::function<QString(const QString&)>& toolPath);''', '''// toolPath: the editor's tool paths ("acts", "replay", "oodle"; MainWindow::toolPath). Reads the cached copy, or runs
// ACTS once (a few seconds) and caches it.
EnvBrdfLut loadEnvBrdfLut(const std::function<QString(const QString&)>& toolPath);

// The stock world shader's specular occlusion table (`gtso_lut`, 16 x 16 x 16 BC4 3D in code_post_gfx; research 95).
// The shader samples it (linear, clamped) at (max(sqrt(1 - ao), sqrt(1 - cavity)), a2, saturate(N_geom . R)).
struct GtsoLut {
    int width = 0, height = 0, depth = 0;
    std::vector<uint8_t> r;  // width * height * depth, x fastest (occlusion), then y (a2), then z (N.R)
    QString error;
};
GtsoLut loadGtsoLut(const std::function<QString(const QString&)>& toolPath);

// A BC4 DDS volume (DX10 header BC4_UNORM, or FourCC ATI1 / BC4U), mip 0 decoded to one byte per texel. Fails on
// anything else. Exposed for the tests.
bool decodeBc4VolumeDds(const QByteArray& dds, GtsoLut& out);''')
patch(GH, '''#include <QString>
''', '''#include <QByteArray>
#include <QString>
''')
patch(GC, '''}  // namespace

EnvBrdfLut loadEnvBrdfLut(const std::function<QString(const QString&)>& toolPath) {
    EnvBrdfLut lut;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/game_look";
    const QString cached = dir + "/ggx_env_brdf_lut.dds";
    if (QFileInfo::exists(cached) && decodeLut(cached, lut)) return lut;
    const QString acts = toolPath ? toolPath("acts") : QString();
    const QString replay = toolPath ? toolPath("replay") : QString();
    const QString oodle = toolPath ? toolPath("oodle") : QString();
    const QString zone = QFileInfo(replay).absolutePath() + "/zone/code_post_gfx.ff";
    if (acts.isEmpty() || !QFileInfo::exists(acts) || !QFileInfo::exists(zone)) {
        lut.error = "ACTS or code_post_gfx.ff not found (Tools > Tool Paths)";
        return lut;
    }
    QDir().mkpath(dir);
    const QString scratch = dir + "/w";
    QDir(scratch).removeRecursively();
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(acts, {"--noUpdater", "fastfile", "-r", "mw19replay", "-g", replay, "--oodle", oodle, "-a", "image", "-n", "ggx_env_brdf_lut",
                   "-l", "-o", scratch, zone});
    if (!p.waitForFinished(180000)) {
        p.kill();
        lut.error = "ACTS did not finish exporting the BRDF table";
        QDir(scratch).removeRecursively();
        return lut;
    }
    QString found;
    for (QDirIterator it(scratch, {"ggx_env_brdf_lut*.dds"}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
        found = it.next();
        break;
    }
    if (found.isEmpty()) {
        lut.error = "ACTS wrote no ggx_env_brdf_lut image";
    } else {
        QFile::remove(cached);
        QFile::copy(found, cached);
        decodeLut(cached, lut);
    }
    QDir(scratch).removeRecursively();
    return lut;
}''', '''// Exports image `name` of the always-loaded zone code_post_gfx through ACTS into <AppLocalData>/game_look/<name>.dds
// (once; a few seconds). Returns the cached path, or "" with *err.
QString cachedCodePostImage(const std::function<QString(const QString&)>& toolPath, const QString& name, QString* err) {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/game_look";
    const QString cached = dir + "/" + name + ".dds";
    if (QFileInfo::exists(cached)) return cached;
    const QString acts = toolPath ? toolPath("acts") : QString();
    const QString replay = toolPath ? toolPath("replay") : QString();
    const QString oodle = toolPath ? toolPath("oodle") : QString();
    const QString zone = QFileInfo(replay).absolutePath() + "/zone/code_post_gfx.ff";
    if (acts.isEmpty() || !QFileInfo::exists(acts) || !QFileInfo::exists(zone)) {
        *err = "ACTS or code_post_gfx.ff not found (Tools > Tool Paths)";
        return {};
    }
    QDir().mkpath(dir);
    const QString scratch = dir + "/w_" + name;
    QDir(scratch).removeRecursively();
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(acts, {"--noUpdater", "fastfile", "-r", "mw19replay", "-g", replay, "--oodle", oodle, "-a", "image", "-n", name, "-l", "-o",
                   scratch, zone});
    if (!p.waitForFinished(180000)) {
        p.kill();
        *err = "ACTS did not finish exporting " + name;
        QDir(scratch).removeRecursively();
        return {};
    }
    QString found;
    for (QDirIterator it(scratch, {name + "*.dds"}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
        found = it.next();
        break;
    }
    QString out;
    if (found.isEmpty()) {
        *err = "ACTS wrote no " + name + " image";
    } else {
        QFile::remove(cached);
        if (QFile::copy(found, cached)) out = cached;
        else *err = "could not write " + cached;
    }
    QDir(scratch).removeRecursively();
    return out;
}

}  // namespace

EnvBrdfLut loadEnvBrdfLut(const std::function<QString(const QString&)>& toolPath) {
    EnvBrdfLut lut;
    const QString cached = cachedCodePostImage(toolPath, "ggx_env_brdf_lut", &lut.error);
    if (!cached.isEmpty() && !decodeLut(cached, lut)) QFile::remove(cached);  // asked again next session
    return lut;
}

bool decodeBc4VolumeDds(const QByteArray& dds, GtsoLut& out) {
    const auto u32 = [&](int o) {
        uint32_t v = 0;
        for (int k = 0; k < 4; ++k) v |= uint32_t(uint8_t(dds[o + k])) << (8 * k);
        return v;
    };
    out = GtsoLut{};
    if (dds.size() < 128 || std::memcmp(dds.constData(), "DDS ", 4) != 0) {
        out.error = "not a DDS file";
        return false;
    }
    const uint32_t h = u32(12), w = u32(16), d = std::max<uint32_t>(u32(24), 1);
    int data = 128;
    const char* cc = dds.constData() + 84;
    if (std::memcmp(cc, "DX10", 4) == 0) {
        if (dds.size() < 148 || u32(128) != 80) {  // DXGI_FORMAT_BC4_UNORM
            out.error = "the table is not BC4";
            return false;
        }
        data = 148;
    } else if (std::memcmp(cc, "ATI1", 4) != 0 && std::memcmp(cc, "BC4U", 4) != 0) {
        out.error = "the table is not BC4";
        return false;
    }
    if (w < 4 || h < 4 || w % 4 || h % 4 || w > 256 || h > 256 || d > 256) {
        out.error = "the table has an unexpected size";
        return false;
    }
    const size_t bw = w / 4, bh = h / 4;
    if (size_t(dds.size()) < size_t(data) + bw * bh * d * 8) {
        out.error = "the table is truncated";
        return false;
    }
    out.width = int(w), out.height = int(h), out.depth = int(d);
    out.r.assign(size_t(w) * h * d, 0);
    const uint8_t* p = reinterpret_cast<const uint8_t*>(dds.constData()) + data;
    for (size_t z = 0; z < d; ++z)
        for (size_t by = 0; by < bh; ++by)
            for (size_t bx = 0; bx < bw; ++bx, p += 8) {
                const int r0 = p[0], r1 = p[1];
                int pal[8] = {r0, r1, 0, 0, 0, 0, 0, 255};
                if (r0 > r1) {
                    for (int i = 1; i < 7; ++i) pal[i + 1] = ((7 - i) * r0 + i * r1) / 7;
                } else {
                    for (int i = 1; i < 5; ++i) pal[i + 1] = ((5 - i) * r0 + i * r1) / 5;
                    pal[6] = 0;
                }
                uint64_t bits = 0;
                for (int k = 0; k < 6; ++k) bits |= uint64_t(p[2 + k]) << (8 * k);
                for (int i = 0; i < 16; ++i) {
                    const size_t x = bx * 4 + size_t(i % 4), y = by * 4 + size_t(i / 4);
                    out.r[(z * h + y) * w + x] = uint8_t(pal[(bits >> (3 * i)) & 7]);
                }
            }
    return true;
}

GtsoLut loadGtsoLut(const std::function<QString(const QString&)>& toolPath) {
    GtsoLut lut;
    QString err;
    const QString cached = cachedCodePostImage(toolPath, "gtso_lut", &err);
    if (cached.isEmpty()) {
        lut.error = err;
        return lut;
    }
    QFile f(cached);
    if (!f.open(QIODevice::ReadOnly)) {
        lut.error = "cannot read " + cached;
        return lut;
    }
    if (!decodeBc4VolumeDds(f.readAll(), lut)) {
        f.close();
        QFile::remove(cached);  // asked again next session
    }
    return lut;
}''')
patch(GC, '''#include "texture_decode.h"
''', '''#include "texture_decode.h"

#include <algorithm>
#include <cstring>
''')

# ---------------- renderer
patch(RH, '''    void setEnvBrdfLut(int width, int height, const uint8_t* rg);''', '''    void setEnvBrdfLut(int width, int height, const uint8_t* rg);
    // The stock specular occlusion table (gtso_lut, one byte per texel, x fastest; research 95), t18.
    void setGtsoLut(int width, int height, int depth, const uint8_t* r);
    bool hasGtsoLut() const { return gtsoSrv_ != nullptr; }''')
patch(RH, '''    ID3D11Texture2D* envBrdfTex_ = nullptr;''', '''    ID3D11Texture2D* envBrdfTex_ = nullptr;
    ID3D11Texture3D* gtsoTex_ = nullptr;
    ID3D11ShaderResourceView* gtsoSrv_ = nullptr;''')
patch(R, '''void SceneRenderer::setEnvBrdfLut(int width, int height, const uint8_t* rg) {''', '''void SceneRenderer::setGtsoLut(int width, int height, int depth, const uint8_t* r) {
    safeRelease(gtsoSrv_), safeRelease(gtsoTex_);
    if (!dev_ || width < 2 || height < 2 || depth < 2 || !r) return;
    D3D11_TEXTURE3D_DESC td{};
    td.Width = UINT(width), td.Height = UINT(height), td.Depth = UINT(depth);
    td.MipLevels = 1;
    td.Format = DXGI_FORMAT_R8_UNORM;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{r, UINT(width), UINT(width) * UINT(height)};
    if (FAILED(dev_->CreateTexture3D(&td, &init, &gtsoTex_)) || FAILED(dev_->CreateShaderResourceView(gtsoTex_, nullptr, &gtsoSrv_)))
        safeRelease(gtsoTex_);
}

void SceneRenderer::setEnvBrdfLut(int width, int height, const uint8_t* rg) {''')
patch(R, '''    for (int k = 0; k < 3; ++k) safeRelease(stockLmSrv_[k]), safeRelease(stockLmTex_[k]);
    safeRelease(envBrdfSrv_), safeRelease(envBrdfTex_);
''', '''    for (int k = 0; k < 3; ++k) safeRelease(stockLmSrv_[k]), safeRelease(stockLmTex_[k]);
    safeRelease(envBrdfSrv_), safeRelease(envBrdfTex_);
    safeRelease(gtsoSrv_), safeRelease(gtsoTex_);
''')
print('part 1 ok')

# ---------------- shader: the table where the game uses it
patch(R, '''// The stock world shader's ggx_env_brdf_lut (A, B by N.V across and gloss down; research 95), when the editor has it.
Texture2D envBrdf : register(t10);''', '''// The stock world shader's ggx_env_brdf_lut (A, B by N.V across and gloss down; research 95), when the editor has it.
Texture2D envBrdf : register(t10);
// Its specular occlusion table gtso_lut (research 95, W:796), when the editor has it (gameSun.w = 1).
Texture3D gtsoLut : register(t18);''')
patch(R, '''            const float so = min(aoV, baseMat ? nmCavity : 1.0);''', '''            // Specular occlusion: the game's gtso_lut(max(sqrt(1 - ao), sqrt(1 - cavity)), a2, saturate(Ngeom . R))
            // (research 95 W:796, raw coordinates, linear clamp); without the table, the smaller of the two.
            const float cavS = baseMat ? nmCavity : 1.0;
            const float so = gameSun.w > 0.5
                ? gtsoLut.SampleLevel(lutSamp, float3(max(sqrt(saturate(1.0 - aoV)), sqrt(saturate(1.0 - cavS))), a2, saturate(dot(Nf, R))), 0).r
                : min(aoV, cavS);''')
patch(R, '''                // Specular occlusion: the game's gtso_lut(max(sqrt(1 - ao), sqrt(1 - cavity)), ...) without GTAO;
                // the cavity stands in for it (INFERRED shape).
                S = lerp(0.18 * mixed, P, so);''', '''                S = lerp(0.18 * mixed, P, so);  // so: the specular occlusion above''')
patch(R, '''                   game_.sunToward[0], game_.sunToward[1], game_.sunToward[2], 0,''', '''                   game_.sunToward[0], game_.sunToward[1], game_.sunToward[2], on && gtsoSrv_ ? 1.0f : 0.0f,''')
patch(R, '''    ctx_->PSSetShaderResources(14, 1, &ao);
    if (cbProbes_)''', '''    ctx_->PSSetShaderResources(14, 1, &ao);
    ctx_->PSSetShaderResources(18, 1, &gtsoSrv_);
    if (cbProbes_)''')

# ---------------- camera view: ask once, like the BRDF table
patch(CH, '''    bool envBrdfAsked_ = false;''', '''    bool envBrdfAsked_ = false;
    bool gtsoAsked_ = false;
    std::shared_ptr<GtsoLut> pendingGtso_;''')
patch(CH, '''    bool hasEnvBrdfLut() const { return renderer_.hasEnvBrdfLut(); }''', '''    bool hasEnvBrdfLut() const { return renderer_.hasEnvBrdfLut(); }
    bool hasGtsoLut() const { return renderer_.hasGtsoLut(); }''')
patch(CC, '''        if (pendingEnvBrdf_) {
            renderer_.setEnvBrdfLut(pendingEnvBrdfW_, pendingEnvBrdfH_, pendingEnvBrdf_->data());
            pendingEnvBrdf_.reset();
        }''', '''        if (pendingEnvBrdf_) {
            renderer_.setEnvBrdfLut(pendingEnvBrdfW_, pendingEnvBrdfH_, pendingEnvBrdf_->data());
            pendingEnvBrdf_.reset();
        }
        // Its specular occlusion table (gtso_lut), the same way.
        if (!gtsoAsked_ && toolPathFn_) {
            gtsoAsked_ = true;
            QPointer<CameraView> self(this);
            auto tools = toolPathFn_;
            std::thread([self, tools] {
                auto lut = std::make_shared<GtsoLut>(loadGtsoLut(tools));
                QMetaObject::invokeMethod(qApp, [self, lut] {
                    if (!self) return;
                    if (lut->width > 0) {
                        self->pendingGtso_ = lut;
                        self->update();
                    } else {
                        emit self->ctx_->statusMessage("Game Lighting: specular occlusion table: " + lut->error + " (an approximation is used)");
                    }
                }, Qt::QueuedConnection);
            }).detach();
        }
        if (pendingGtso_) {
            renderer_.setGtsoLut(pendingGtso_->width, pendingGtso_->height, pendingGtso_->depth, pendingGtso_->r.data());
            pendingGtso_.reset();
        }''')
print('ok')
