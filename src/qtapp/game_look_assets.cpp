#include "game_look_assets.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include "texture_decode.h"

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

}  // namespace

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
}

}  // namespace iw8r::qt
