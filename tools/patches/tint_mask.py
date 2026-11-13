import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b, *c in pairs:
        n = c[0] if c else 1
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == n, (p, s.count(a), a[:100])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/qtapp/material_cache.h', [
 ('''        int tint = -1;
        bool tintMasked = false;
        bool failed = false;''', '''        int tint = -1;
        bool tintMasked = false;
        QString tintMaskDds;  // slot 59 (red = where the tint applies); empty when none or not exported
        bool failed = false;'''),
 ('''        int tint = -1;          // constant 9 (RRGGBB), -1 = neutral / none
        bool tintMasked = false;''', '''        int tint = -1;          // psData row 2 bytes 0-2 (else constant 9), RRGGBB; -1 = neutral / none
        bool tintMasked = false;
        QString tintMask;       // slot 59 image name, if any'''),
])
patch('src/qtapp/material_cache.cpp', [
 # tint from psData row 2 (research 95), the flag bit 6 for the mask
 ('''    // The colour tint: constant 9's literal, R G B bytes then 0 (tools/build_material_tints.py); white = neutral.
    work.tint = -1;
    work.tintMasked = false;''', '''    // The colour tint: psData row 2 .x bytes 0..2 (research 95; models use the same rows), the mask with row 1 .x bit 6;
    // else constant 9's literal (tools/build_material_tints.py). White = neutral.
    work.tint = -1;
    work.tintMasked = false;
    work.tintMask.clear();
    if (work.psData.size() >= 48) {
        const auto* p = reinterpret_cast<const uint8_t*>(work.psData.constData());
        if (!(p[32] == 255 && p[33] == 255 && p[34] == 255)) work.tint = (p[32] << 16) | (p[33] << 8) | p[34];
        work.tintMasked = (p[16] & 64) != 0;
    }'''),
 ('''        if (b[3] == 0 && !(b[0] == 255 && b[1] == 255 && b[2] == 255)) work.tint = (b[0] << 16) | (b[1] << 8) | b[2];
        break;''', '''        if (work.tint < 0 && b[3] == 0 && !(b[0] == 255 && b[1] == 255 && b[2] == 255)) work.tint = (b[0] << 16) | (b[1] << 8) | b[2];
        break;'''),
 ('''        if (slot == 59) work.tintMasked = true;                           // tint mask (research 95)''',
  '''        if (slot == 59 && work.tintMask.isEmpty()) work.tintMask = name;  // tint mask (research 95)'''),
 ('''            QStringList names{w.color, w.normal, w.mask, w.emissive};''', '''            QStringList names{w.color, w.normal, w.mask, w.emissive};
            if (w.tintMasked && w.tint >= 0) names << w.tintMask;  // only a tinted material needs its mask'''),
 ('''            e.tint = w.tint;
            e.tintMasked = w.tintMasked;''', '''            e.tint = w.tint;
            e.tintMasked = w.tintMasked;
            if (!w.tintMask.isEmpty() && images_.count(w.tintMask)) {
                const Image& im = images_.at(w.tintMask);
                if (im.done) e.tintMaskDds = im.file;  // optional, as the emissive map
            }'''),
 ('''        e.tintMasked = o.value("tintMasked").toBool();''', '''        e.tintMasked = o.value("tintMasked").toBool();
        if (const QString tm = o.value("tintMask").toString(); !tm.isEmpty()) e.tintMaskDds = QDir(root_).filePath(tm);'''),
 ('''        if (!e.failed && o.value("tintRules").toInt() < 1) continue;''', '''        if (!e.failed && o.value("tintRules").toInt() < 2) continue;  // 2: tint from psData row 2, the slot-59 mask'''),
 ('''        if (!e.failed) o.insert("tintRules", 1);   // 1: every body's psData, tint (constant 9) and tint mask flag''',
  '''        if (!e.failed) o.insert("tintRules", 2);   // 2: every body's psData, tint (psData row 2) and its mask image
        if (!e.tintMaskDds.isEmpty()) o.insert("tintMask", root.relativeFilePath(e.tintMaskDds));'''),
])
patch('src/qtapp/camera_view.h', [
 ('''        int tint = -1;
        bool tintMasked = false;
        StockLayeredMaterial constants;''', '''        int tint = -1;
        bool tintMasked = false;
        std::string tintMaskFile;  // slot 59 DDS: the tint applies by its red channel
        StockLayeredMaterial constants;'''),
])
patch('src/qtapp/camera_view.cpp', [
 ('''            if (const auto f = factsLookup_(material); f && f->tint >= 0 && !f->tintMasked) {
                const std::array<uint8_t, 3> t{uint8_t(f->tint >> 16), uint8_t(f->tint >> 8), uint8_t(f->tint)};
                applyMaterialTint(img.pixels.data(), size_t(img.width) * size_t(img.height), t);
            }''', '''            if (const auto f = factsLookup_(material); f && f->tint >= 0) {
                const double t[3] = {double(f->tint >> 16 & 255) / 255.0, double(f->tint >> 8 & 255) / 255.0, double(f->tint & 255) / 255.0};
                app::Rgba8Image mask;
                const bool masked = f->tintMasked && !f->tintMaskFile.empty() &&
                                    app::loadDdsPreview(f->tintMaskFile, std::max(img.width, img.height), mask) && mask.width > 0 && mask.height > 0;
                if (!f->tintMasked || masked) {
                    // albedo x lerp(1, tint, mask.r) (research 95); a masked tint without its mask image is left out.
                    for (int y = 0; y < img.height; ++y)
                        for (int x = 0; x < img.width; ++x) {
                            double m = 1.0;
                            if (masked) {
                                const int mx = std::min(mask.width - 1, x * mask.width / std::max(1, img.width));
                                const int my = std::min(mask.height - 1, y * mask.height / std::max(1, img.height));
                                m = mask.pixels[(size_t(my) * size_t(mask.width) + size_t(mx)) * 4] / 255.0;
                            }
                            uint8_t* q = &img.pixels[(size_t(y) * size_t(img.width) + size_t(x)) * 4];
                            for (int c = 0; c < 3; ++c) q[c] = uint8_t(std::clamp(q[c] * (1.0 + (t[c] - 1.0) * m), 0.0, 255.0));
                        }
                }
            }'''),
])
patch('src/qtapp/main_window.cpp', [
 ('''        f.tint = e->tint;
        f.tintMasked = e->tintMasked;''', '''        f.tint = e->tint;
        f.tintMasked = e->tintMasked;
        f.tintMaskFile = e->tintMaskDds.toStdString();'''),
])
print('ok')
