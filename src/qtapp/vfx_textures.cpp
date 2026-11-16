#include "vfx_textures.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace iw8r::qt {

VfxSpriteMaterial vfxSpriteMaterial(const QString& material, const MaterialCache::Entry& e, const QString& cacheRoot) {
    // The rules of the camera's particle materials (main_window.cpp, setParticleMaterials): distortion and flare sets
    // by their technique set; the 13th technique token ends in 23 on unlit sets (they add their emissive image) and in
    // b on lit ones; constant 94 = (columns, rows, ...) of the atlas; constant 36 = the emissive scale.
    VfxSpriteMaterial pm;
    pm.ready = true;
    if (e.failed) {
        pm.failed = true;
        return pm;
    }
    const QString ts = e.techset.section('/', -1);
    pm.distortion = ts.startsWith("dist_") || ts.contains("distortion");
    if (pm.distortion) {
        pm.distortionFile = e.distortionDds.toStdString();
        if (const auto c = e.consts.find(127); c != e.consts.end())
            pm.distScale[0] = std::max(0.0f, c->second[0]), pm.distScale[1] = std::max(0.0f, c->second[1]);
        if (pm.distortionFile.empty() || pm.distScale[0] + pm.distScale[1] <= 0) pm.skip = true;
    }
    if (material.startsWith("ecf/")) pm.flare = true, pm.lit = false;
    const QStringList tok = ts.split('_');
    if (tok.size() > 12) pm.lit = !tok[12].endsWith("23");
    if (e.colorDds.isEmpty() && !e.emissiveDds.isEmpty()) pm.lit = false;
    pm.colorFile = e.colorDds.toStdString();
    pm.alphaFile = e.maskDds.toStdString();
    pm.emissiveFile = e.emissiveDds.toStdString();
    pm.densityFile = e.transmissionDds.toStdString();
    if (const auto c = e.consts.find(94); c != e.consts.end() && c->second[0] >= 1 && c->second[1] >= 1)
        pm.cols = int(std::lround(c->second[0])), pm.rows = int(std::lround(c->second[1]));
    if (const auto c = e.consts.find(36); c != e.consts.end())
        for (int k = 0; k < 3; ++k) pm.emissive[k] = std::max(0.0f, c->second[size_t(k)]);
    if (pm.flare)
        for (const QString& t : e.textures)
            if (t.startsWith("145=")) {
                pm.screenMask = true;
                QString img = t.mid(4).section('~', 0, 0);
                if (img.startsWith(',')) img.remove(0, 1);
                const QString dir = !e.emissiveDds.isEmpty() ? QFileInfo(e.emissiveDds).absolutePath() : cacheRoot + "/images";
                const QStringList hits = QDir(dir).entryList({img + "_*.dds", img + ".dds"}, QDir::Files);
                if (!hits.isEmpty()) pm.maskFile = QDir(dir).filePath(hits.first()).toStdString();
            }
    return pm;
}

void composeVfxSprite(const VfxSpriteMaterial& pm, app::Rgba8Image& img, const app::Rgba8Image* m, const app::Rgba8Image* dn, bool* softened) {
    const bool maskOk = pm.lit && m && m->width > 0 && m->height > 0 && m->pixels.size() == size_t(m->width) * size_t(m->height) * 4;
    const bool densityOk = pm.lit && !maskOk && dn && dn->width > 0 && dn->height > 0 && dn->pixels.size() == size_t(dn->width) * size_t(dn->height) * 4;
    if (maskOk) {
        int ch = 0;  // red, unless the alpha varies
        for (size_t k = 3; k < m->pixels.size(); k += 4)
            if (m->pixels[k] < 250) {
                ch = 3;
                break;
            }
        for (int y = 0; y < img.height; ++y)
            for (int x = 0; x < img.width; ++x) {
                const int mx = int(int64_t(x) * m->width / img.width), my = int(int64_t(y) * m->height / img.height);
                img.pixels[(size_t(y) * size_t(img.width) + size_t(x)) * 4 + 3] = m->pixels[(size_t(my) * size_t(m->width) + size_t(mx)) * 4 + size_t(ch)];
            }
    } else if (densityOk) {
        for (int y = 0; y < img.height; ++y)
            for (int x = 0; x < img.width; ++x) {
                const int mx = int(int64_t(x) * dn->width / img.width), my = int(int64_t(y) * dn->height / img.height);
                uint8_t& a = img.pixels[(size_t(y) * size_t(img.width) + size_t(x)) * 4 + 3];
                // Slot 68 is a transmittance (255 at the cells' edges, near 0 in the smoke): alpha x (1 - T).
                a = uint8_t((int(a) * (255 - int(dn->pixels[(size_t(my) * size_t(dn->width) + size_t(mx)) * 4])) + 127) / 255);
            }
    } else if (!pm.lit) {
        for (size_t k = 3; k < img.pixels.size(); k += 4) img.pixels[k] = 255;
    }
    // Never a hard square: a sprite whose image has no edge of its own gets a soft round edge in each atlas cell.
    const int cols = std::max(1, pm.cols), rows = std::max(1, pm.rows);
    const double cw = double(img.width) / cols, chh = double(img.height) / rows;
    bool opaque = true;
    for (size_t k = 3; k < img.pixels.size() && opaque; k += 4) opaque = img.pixels[k] >= 250;
    double border = 0;
    size_t borderN = 0;
    for (int y = 0; y < img.height; ++y) {
        const double ly = std::fmod(y + 0.5, chh);
        const bool rowEdge = ly < 1.5 || ly > chh - 1.5;
        for (int x = 0; x < img.width; ++x) {
            const double lx = std::fmod(x + 0.5, cw);
            if (!rowEdge && lx >= 1.5 && lx <= cw - 1.5) continue;
            const uint8_t* p = &img.pixels[(size_t(y) * size_t(img.width) + size_t(x)) * 4];
            border += (p[0] + p[1] + p[2]) / 3.0;
            ++borderN;
        }
    }
    const bool brightEdge = borderN && border / double(borderN) > 24.0;
    const bool soften = pm.lit ? (opaque && !maskOk && !densityOk) : (brightEdge && !pm.screenMask);
    if (softened) *softened = soften;
    if (!soften) return;
    for (int y = 0; y < img.height; ++y)
        for (int x = 0; x < img.width; ++x) {
            const double u = std::fmod(double(x) + 0.5, double(img.width) / cols) * cols / img.width - 0.5;
            const double v = std::fmod(double(y) + 0.5, double(img.height) / rows) * rows / img.height - 0.5;
            const double w = std::clamp((1.0 - 2.0 * std::sqrt(u * u + v * v)) / 0.4, 0.0, 1.0);
            uint8_t* p = &img.pixels[(size_t(y) * size_t(img.width) + size_t(x)) * 4];
            if (pm.lit) p[3] = uint8_t(std::lround(p[3] * w));
            else
                for (int c = 0; c < 3; ++c) p[c] = uint8_t(std::lround(p[c] * w));
        }
}

void prepareVfxDistortion(app::Rgba8Image& img, bool srgb) {
    if (srgb) {
        uint8_t lin[256];
        for (int i = 0; i < 256; ++i) {
            const double c = i / 255.0;
            lin[i] = uint8_t(std::lround(255.0 * (c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4))));
        }
        for (size_t k = 0; k < img.pixels.size(); ++k)
            if (k % 4 != 3) img.pixels[k] = lin[img.pixels[k]];
    }
    // The strength mask: the world's distortion images (BC1) keep it in blue; the effect images (BC3) keep it in
    // alpha, their blue empty or flat (vfx_dist_heat_3_c: blue 123-131 of 255 in every block, alpha 0-255). The
    // camera's copy moves alpha into blue only when blue is empty, so a flat blue (0.2 once read linear) left such a
    // heat haze without any strength there.
    uint8_t blueMin = 255, blueMax = 0, alphaMin = 255, alphaMax = 0;
    for (size_t k = 0; k + 3 < img.pixels.size(); k += 4) {
        blueMin = std::min(blueMin, img.pixels[k + 2]), blueMax = std::max(blueMax, img.pixels[k + 2]);
        alphaMin = std::min(alphaMin, img.pixels[k + 3]), alphaMax = std::max(alphaMax, img.pixels[k + 3]);
    }
    if ((blueMax < 8 && alphaMax > alphaMin) || (blueMax - blueMin < 16 && alphaMax - alphaMin > 32))
        for (size_t k = 0; k + 3 < img.pixels.size(); k += 4) img.pixels[k + 2] = img.pixels[k + 3];
}

namespace {

// Half the size (a 2 x 2 box; an odd last row / column is folded into the one before).
app::Rgba8Image halve(const app::Rgba8Image& in) {
    app::Rgba8Image out;
    out.width = std::max(1, in.width / 2), out.height = std::max(1, in.height / 2);
    out.pixels.resize(size_t(out.width) * size_t(out.height) * 4);
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x) {
            const int x0 = std::min(in.width - 1, 2 * x), x1 = std::min(in.width - 1, 2 * x + 1);
            const int y0 = std::min(in.height - 1, 2 * y), y1 = std::min(in.height - 1, 2 * y + 1);
            const uint8_t* a = &in.pixels[(size_t(y0) * size_t(in.width) + size_t(x0)) * 4];
            const uint8_t* b = &in.pixels[(size_t(y0) * size_t(in.width) + size_t(x1)) * 4];
            const uint8_t* c = &in.pixels[(size_t(y1) * size_t(in.width) + size_t(x0)) * 4];
            const uint8_t* d = &in.pixels[(size_t(y1) * size_t(in.width) + size_t(x1)) * 4];
            uint8_t* o = &out.pixels[(size_t(y) * size_t(out.width) + size_t(x)) * 4];
            for (int k = 0; k < 4; ++k) o[k] = uint8_t((a[k] + b[k] + c[k] + d[k] + 2) / 4);
        }
    return out;
}

}  // namespace

std::shared_ptr<VfxSprite> loadVfxSprite(const VfxSpriteMaterial& pm, std::string* why) {
    const auto fail = [&](const std::string& w) {
        if (why) *why = w;
        return std::shared_ptr<VfxSprite>();
    };
    if (!pm.ready || pm.failed) return fail("not in the material cache");
    if (pm.skip) return fail("a distortion set without its image or strength");
    // An atlas keeps about 128 texels per cell (the camera reads 512 at most; the panel draws a sprite larger).
    const int side = std::clamp(128 * std::max(pm.cols, pm.rows), 512, 1024);
    auto sprite = std::make_shared<VfxSprite>();
    sprite->mat = pm;
    app::Rgba8Image img;
    if (pm.distortion) {
        if (!app::loadDdsPreview(pm.distortionFile, 512, img) || img.width <= 0 || img.height <= 0)
            return fail("cannot read its distortion image " + pm.distortionFile);
        prepareVfxDistortion(img, app::ddsIsSrgb(pm.distortionFile));
    } else {
        const std::string& main = pm.lit ? pm.colorFile : (pm.emissiveFile.empty() ? pm.colorFile : pm.emissiveFile);
        if (main.empty()) return fail("no colour or emissive image");
        if (!app::loadDdsPreview(main, side, img) || img.width <= 0 || img.height <= 0) return fail("cannot read " + main);
        app::Rgba8Image mask, density;
        const bool withMask = pm.lit && !pm.alphaFile.empty() && app::loadDdsPreview(pm.alphaFile, side, mask);
        const bool withDensity = pm.lit && !withMask && !pm.densityFile.empty() && app::loadDdsPreview(pm.densityFile, side, density);
        composeVfxSprite(pm, img, withMask ? &mask : nullptr, withDensity ? &density : nullptr, &sprite->softened);
    }
    sprite->levels.push_back(std::move(img));
    const int cells = std::max(pm.cols, pm.rows);
    while (sprite->levels.back().width / std::max(1, pm.cols) > 2 && sprite->levels.back().height / std::max(1, pm.rows) > 2 &&
           sprite->levels.size() < 10 && cells > 0)
        sprite->levels.push_back(halve(sprite->levels.back()));
    return sprite;
}

}  // namespace iw8r::qt
