// Drawing a map's HUD fonts into pictures (hud_font_render.h).
#include "hud_font_render.h"

#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace iw8r::qt {
namespace {

// Text in `font` and `colour` on transparent, `width` x `height` pixels, the baseline at the font's ascent (+1 margin).
CompileOptions::HudRgbaImage drawText(const QString& text, const QFont& font, const QColor& colour, int width, int height, double ascent) {
    QImage img(std::max(1, width), std::max(1, height), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setFont(font);
        p.setPen(colour);
        p.drawText(QPointF(1.0, 1.0 + ascent), text);
    }
    const QImage straight = img.convertToFormat(QImage::Format_RGBA8888);  // not premultiplied: the DDS takes it so
    CompileOptions::HudRgbaImage out;
    out.width = straight.width(), out.height = straight.height();
    out.rgba.resize(size_t(out.width) * size_t(out.height) * 4);
    for (int y = 0; y < out.height; ++y)
        std::memcpy(&out.rgba[size_t(y) * size_t(out.width) * 4], straight.constScanLine(y), size_t(out.width) * 4);
    return out;
}

}  // namespace

HudFontRender renderHudFonts(const std::string& hudFolder) {
    HudFontRender r;
    if (hudFolder.empty()) return r;
    const std::vector<HudFontEntry> entries = readHudFonts(hudFolder, &r.warnings);
    std::map<std::string, QString> families;  // font file -> its family ("" = could not be read)
    for (const HudFontEntry& e : entries) {
        auto fam = families.find(e.fontFile);
        if (fam == families.end()) {
            const int id = QFontDatabase::addApplicationFont(QString::fromStdString(e.fontFile));
            const QStringList names = id >= 0 ? QFontDatabase::applicationFontFamilies(id) : QStringList();
            fam = families.emplace(e.fontFile, names.isEmpty() ? QString() : names.front()).first;
            if (names.isEmpty()) r.warnings.push_back("fonts.txt line " + std::to_string(e.line) + ": the font file " + e.fontFile + " could not be read");
        }
        if (fam->second.isEmpty()) continue;
        QFont font(fam->second);
        font.setPixelSize(e.pixelHeight);
        font.setStyleStrategy(QFont::PreferAntialias);
        font.setHintingPreference(QFont::PreferNoHinting);
        const QFontMetricsF fm(font);
        const int height = int(std::ceil(fm.ascent() + fm.descent())) + 2;
        const QColor colour(e.rgb[0], e.rgb[1], e.rgb[2]);
        if (!e.glyphs) {
            const QString text = QString::fromUtf8(e.text.c_str());
            const double w = std::max(fm.horizontalAdvance(text), fm.boundingRect(text).right());
            if (r.images.size() >= kMaxHudFontImages) {
                r.warnings.push_back("fonts.txt line " + std::to_string(e.line) + ": past " + std::to_string(kMaxHudFontImages) + " font pictures, left out");
                continue;
            }
            CompileOptions::HudRgbaImage img = drawText(text, font, colour, int(std::ceil(w)) + 2, height, fm.ascent());
            img.name = "hud_" + e.name;
            r.labels.push_back({e.name, img.width, img.height});
            r.images.push_back(std::move(img));
            continue;
        }
        HudGlyphSet set;
        set.name = e.name;
        set.pixelHeight = height;
        set.spaceAdvance = fm.horizontalAdvance(QChar(' '));
        for (const char ch : e.text) {
            if (r.images.size() >= kMaxHudFontImages) {
                r.warnings.push_back("fonts.txt line " + std::to_string(e.line) + ": past " + std::to_string(kMaxHudFontImages) +
                                     " font pictures, the rest of the set is left out");
                break;
            }
            const QChar qc(ch);
            const double advance = fm.horizontalAdvance(qc);
            const double w = std::max(advance, fm.boundingRect(qc).right());
            CompileOptions::HudRgbaImage img = drawText(QString(qc), font, colour, int(std::ceil(w)) + 2, height, fm.ascent());
            img.name = hudGlyphImageName(e.name, static_cast<unsigned char>(ch));
            HudGlyphSet::Glyph g;
            g.c = static_cast<unsigned char>(ch);
            g.width = img.width, g.height = img.height, g.advance = advance;
            set.glyphs.push_back(g);
            r.images.push_back(std::move(img));
        }
        if (!set.glyphs.empty()) r.sets.push_back(std::move(set));
    }
    r.script = hudFontScriptGsc(r.sets, r.labels);
    return r;
}

}  // namespace iw8r::qt
