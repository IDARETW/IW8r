#include "game_look_assets.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include "texture_decode.h"

#include <algorithm>
#include <cstring>

namespace iw8r::qt {

namespace {

bool decodeLut(const QString& dds, EnvBrdfLut& out) {
    QFile f(dds);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray bytes = f.readAll();
    app::Rgba8Image img;
    std::string err;
    if (!app::decodeDds(std::string(bytes.constData(), size_t(bytes.size())), img, &err) || img.width < 2 || img.height < 2) {
        out.error = "the BRDF table is not readable: " + QString::fromStdString(err);
        return false;
    }
    out.width = img.width, out.height = img.height;
    out.rg.resize(size_t(img.width) * img.height * 2);
    for (size_t i = 0; i < size_t(img.width) * img.height; ++i) {
        out.rg[i * 2] = img.pixels[i * 4];
        out.rg[i * 2 + 1] = img.pixels[i * 4 + 1];
    }
    out.error.clear();
    return true;
}

// Exports image `name` of the always-loaded zone code_post_gfx through ACTS into <AppLocalData>/game_look/<name>.dds
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
}

}  // namespace iw8r::qt
