#include "material_clone.h"

#include "custom_materials.h"
#include "iw8_contents.h"
#include "material_blend.h"
#include "material_tints.h"
#include "normal_pack.h"
#include "texture_decode.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace iw8r::qt {

namespace {

// Reads and decodes a stock DDS. False with *msg set on failure.
bool loadDds(const QString& path, app::Rgba8Image& img, QString* msg) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *msg = "cannot read '" + path + "'";
        return false;
    }
    const QByteArray data = f.readAll();
    std::string why;
    if (!app::decodeDds(std::string(data.constData(), size_t(data.size())), img, &why) || img.width <= 0 || img.height <= 0 ||
        img.pixels.size() < size_t(img.width) * size_t(img.height) * 4) {
        *msg = "cannot decode '" + path + "'" + (why.empty() ? QString() : QString(": %1").arg(QString::fromStdString(why)));
        return false;
    }
    return true;
}

}  // namespace

bool cloneStockMaterial(const MaterialCache::Entry& entry, const QString& destDir, const QString& newName,
                        MaterialCloneResult* out, QString* err) {
    const auto fail = [&](const QString& message) {
        if (err) *err = message;
        return false;
    };
    if (!out) return fail("no result object");
    if (!isValidCustomMaterialName(newName.toStdString()))
        return fail(QString("'%1' is not a valid material name (letters, digits, _ - . and / between names; not under mc/)")
                        .arg(newName));
    if (entry.colorDds.isEmpty()) return fail(QString("stock material '%1' has no colour image").arg(entry.material));

    QString stem = newName;
    stem.replace('/', "__");
    const QDir dir(destDir);
    const QString defPath = dir.filePath(stem + ".iw8mat.json");
    if (QFileInfo::exists(defPath)) return fail("definition already exists: " + defPath);
    if (!QDir().mkpath(destDir)) return fail("cannot create " + destDir);

    // Colour image, full size. The raw pixels are kept for the specular map (the build samples the untinted colour).
    app::Rgba8Image color;
    QString msg;
    if (!loadDds(entry.colorDds, color, &msg)) return fail(msg);
    const int w = color.width, h = color.height;

    // Blend and cut-out as the build picks them (compile_panel.cpp): blended wins over cut-out.
    const bool blended = materialBlendFromTechset(entry.techset.toStdString()) == MaterialBlend::Blend;
    const bool cutout = !entry.maskDds.isEmpty() && QFileInfo(entry.maskDds).isFile();

    // Opacity / cut-out mask, scaled to the colour size when the sizes differ.
    MaterialCloneResult res;
    QImage maskImg;
    if (cutout) {
        app::Rgba8Image mi;
        if (loadDds(entry.maskDds, mi, &msg)) {
            QImage m = QImage(mi.pixels.data(), mi.width, mi.height, mi.width * 4, QImage::Format_RGBA8888).copy();
            if (m.size() != QSize(w, h))
                m = m.scaled(QSize(w, h), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            maskImg = m.convertToFormat(QImage::Format_RGBA8888);
        } else {
            res.warnings << "mask not read, colour image left opaque: " + msg;
        }
    }
    // Research 66: the opacity is red, or the alpha of a colour image when that alpha varies.
    int maskChannel = 0;
    if (!maskImg.isNull()) {
        bool varies = false;
        for (int y = 0; y < maskImg.height() && !varies; ++y) {
            const uchar* mrow = maskImg.constScanLine(y);
            for (int x = 0; x < maskImg.width(); ++x)
                if (mrow[x * 4 + 3] < 250) {
                    varies = true;
                    break;
                }
        }
        if (varies) maskChannel = 3;
    }

    QImage q = QImage(color.pixels.data(), w, h, w * 4, QImage::Format_RGBA8888).copy();
    for (int y = 0; y < h; ++y) {
        uchar* row = q.scanLine(y);
        const uchar* mrow = maskImg.isNull() ? nullptr : maskImg.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            const uchar v = mrow ? mrow[x * 4 + maskChannel] : 255;
            row[x * 4 + 3] = blended ? v : (v >= 128 ? 255 : 0);
        }
    }

    // Stock tint (material constant 9), baked into the colour image as the build does.
    const auto tint = MaterialTints::shared(QCoreApplication::applicationDirPath().toStdString()).find(entry.material.toStdString());
    if (tint)
        for (int y = 0; y < h; ++y) applyMaterialTint(q.scanLine(y), size_t(w), *tint);

    const QString colourPng = dir.filePath(stem + "_c.png");
    if (!q.save(colourPng, "PNG")) return fail("cannot write '" + colourPng + "'");

    QJsonObject def;
    def["name"] = newName;
    def["color"] = QFileInfo(colourPng).fileName();
    if (blended) {
        def["blend"] = true;
        def["twoSided"] = true;
    } else if (cutout) {
        def["alphaTest"] = true;
        def["twoSided"] = true;
    }

    // Normal + specular, from the stock packed normal (compile_panel.cpp, at full size).
    if (!entry.normalDds.isEmpty()) {
        if (!QFileInfo(entry.normalDds).isFile()) {
            res.warnings << "normal map file missing, not written";
        } else {
            app::Rgba8Image ni;
            if (!loadDds(entry.normalDds, ni, &msg)) {
                res.warnings << "normal map not read, not written: " + msg;
            } else {
                const size_t nw = size_t(ni.width), nh = size_t(ni.height), texels = nw * nh;
                std::vector<uint8_t> sl(texels * 4), sp(texels * 4), colour(texels * 4);
                replayNormalToSlopeMap(ni.pixels.data(), texels, sl.data());
                // The colour image at the normal map's size (nearest texel), for its specular alpha.
                for (size_t y = 0; y < nh; ++y)
                    for (size_t x = 0; x < nw; ++x) {
                        const size_t src = (y * size_t(h) / nh * size_t(w) + x * size_t(w) / nw) * 4;
                        std::copy_n(&color.pixels[src], 4, &colour[(y * nw + x) * 4]);
                    }
                replaySpecularMap(colour.data(), ni.pixels.data(), texels, sp.data());

                QImage slope(ni.width, ni.height, QImage::Format_RGBA8888), spec(ni.width, ni.height, QImage::Format_RGBA8888);
                for (int y = 0; y < ni.height; ++y) {
                    std::copy_n(&sl[size_t(y) * nw * 4], nw * 4, slope.scanLine(y));
                    std::copy_n(&sp[size_t(y) * nw * 4], nw * 4, spec.scanLine(y));
                }
                const QString normalPng = dir.filePath(stem + "_n.png"), specPng = dir.filePath(stem + "_s.png");
                if (!slope.save(normalPng, "PNG")) return fail("cannot write '" + normalPng + "'");
                if (!spec.save(specPng, "PNG")) return fail("cannot write '" + specPng + "'");
                def["normal"] = QFileInfo(normalPng).fileName();
                def["specular"] = QFileInfo(specPng).fileName();
            }
        }
    }

    // Surface: the stock surface flags when they name one, else the name keywords (compile_panel.cpp).
    int st = iw3SurfaceTypeForStockSurfaceFlags(entry.surfaceFlags);
    if (st <= 0) st = iw3SurfaceTypeForMaterial(entry.material.toStdString());
    if (st > 0) def["surface"] = QString::fromLatin1(iw3SurfaceTypeName(st));

    QFile f(defPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(QJsonDocument(def).toJson()) < 0)
        return fail("cannot write '" + defPath + "'");

    res.definitionFile = QFileInfo(defPath).absoluteFilePath();
    res.name = newName;
    *out = res;
    return true;
}

QString suggestCloneName(const QString& stockName, const QStringList& taken) {
    const QString last = stockName.mid(stockName.lastIndexOf('/') + 1).toLower();
    QString base;
    for (const QChar c : last) {
        const bool ok = (c >= QChar('a') && c <= QChar('z')) || (c >= QChar('0') && c <= QChar('9')) || c == QChar('_');
        base += ok ? c : QChar('_');
    }
    if (base.isEmpty()) base = "material";

    QStringList takenLower;
    takenLower.reserve(taken.size());
    for (const QString& t : taken) takenLower << t.toLower();

    QString candidate = "my/" + base;
    for (int n = 2; takenLower.contains(candidate.toLower()); ++n) candidate = QString("my/%1_%2").arg(base).arg(n);
    return candidate;
}

}  // namespace iw8r::qt
