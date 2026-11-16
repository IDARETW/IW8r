"""VfxPreview: the fit by weighted quantiles (outliers left out), sprites at least a pixel across, a checker backdrop
behind distortion sets, the emitters it cannot draw named in the note; distortion strength in alpha when blue is flat."""
import pathlib
import sys

root = pathlib.Path(__file__).resolve().parents[2]
h = root / "src/qtapp/vfx_preview.h"
c = root / "src/qtapp/vfx_preview.cpp"
t = root / "src/qtapp/vfx_textures.cpp"


def sub(text, old, new):
    if old not in text:
        sys.exit("not found: " + old[:90])
    return text.replace(old, new, 1)


ht = h.read_text(encoding="utf-8")
ht = sub(ht, "    int textured_ = 0, pending_ = 0;\n",
         "    int textured_ = 0, pending_ = 0;\n"
         "    int undrawn_ = 0;         // emitters of elements the preview does not draw (models, runners, decals, lights)\n"
         "    bool backdrop_ = false;   // the background has the checker a distortion set moves\n")
h.write_text(ht, encoding="utf-8")

ct = c.read_text(encoding="utf-8")
# (a) the fit: weighted quantiles of the particles' extents.
ct = sub(ct, """    double lo[2] = {1e30, 1e30}, hi[2] = {-1e30, -1e30};
    std::vector<std::array<double, 5>> per(def_.emitters.size(), {1e30, -1e30, 1e30, -1e30, 0});""",
         """    // Weighted quantiles (3 % and 97 %), each particle by its opacity x size: a few stray particles far out (sparks
    // flying off, smoke drifting for minutes) do not shrink the effect to a dot.
    struct Edge {
        double v, w;
    };
    std::vector<Edge> edges[4];  // low R, high R, low U, high U
    std::vector<std::array<double, 5>> per(def_.emitters.size(), {1e30, -1e30, 1e30, -1e30, 0});""")
ct = sub(ct, """            const double R = dot3(p.pos, right_), U = dot3(p.pos, up_);
            lo[0] = std::min(lo[0], R - r), hi[0] = std::max(hi[0], R + r);
            lo[1] = std::min(lo[1], U - r), hi[1] = std::max(hi[1], U + r);
            auto& e = per[size_t(p.emitter)];""",
         """            const double R = dot3(p.pos, right_), U = dot3(p.pos, up_);
            const double w = std::clamp(double(p.rgba[3]), 0.05, 1.0) * std::max(0.5, r);
            edges[0].push_back({R - r, w}), edges[1].push_back({R + r, w}), edges[2].push_back({U - r, w}), edges[3].push_back({U + r, w});
            auto& e = per[size_t(p.emitter)];""")
ct = sub(ct, """    if (lo[0] > hi[0]) lo[0] = lo[1] = -32, hi[0] = hi[1] = 32;
    cr_ = (lo[0] + hi[0]) / 2, cu_ = (lo[1] + hi[1]) / 2;
    span_ = std::max({hi[0] - lo[0], hi[1] - lo[1], 16.0}) * 1.12;  // inches across the square
    // The grid: a power of two of inches, about six cells across.
    groundStep_ = std::pow(2.0, std::round(std::log2(std::max(1.0, span_ / 6.0))));""",
         """    const auto quantile = [](std::vector<Edge>& e, double q) {
        std::sort(e.begin(), e.end(), [](const Edge& a, const Edge& b) { return a.v < b.v; });
        double total = 0;
        for (const Edge& x : e) total += x.w;
        double acc = 0;
        for (const Edge& x : e)
            if ((acc += x.w) >= q * total) return x.v;
        return e.back().v;
    };
    double lo[2] = {-32, -32}, hi[2] = {32, 32};
    if (!edges[0].empty()) lo[0] = quantile(edges[0], 0.03), hi[0] = quantile(edges[1], 0.97), lo[1] = quantile(edges[2], 0.03), hi[1] = quantile(edges[3], 0.97);
    cr_ = (lo[0] + hi[0]) / 2, cu_ = (lo[1] + hi[1]) / 2;
    span_ = std::max({hi[0] - lo[0], hi[1] - lo[1], 2.0}) * 1.15;  // inches across the square
    // The grid: a power of two of inches, about six cells across.
    groundStep_ = std::pow(2.0, std::round(std::log2(std::max(0.125, span_ / 6.0))));
    fitInfo_ << QString("  fit: R %1 .. %2, U %3 .. %4").arg(lo[0], 0, 'f', 1).arg(hi[0], 0, 'f', 1).arg(lo[1], 0, 'f', 1).arg(hi[1], 0, 'f', 1);""")
# (c) the backdrop.
ct = sub(ct, """    g.fillRect(bg.rect(), grad);
    g.setRenderHint(QPainter::Antialiasing);""",
         """    g.fillRect(bg.rect(), grad);
    if (backdrop_) {
        // Behind a distortion set: a fine checker, so the picture it moves shows (the camera moves the scene).
        constexpr int kCell = 10;
        for (int y = 0; y < s; y += kCell)
            for (int x = (y / kCell) % 2 * kCell; x < s; x += 2 * kCell) g.fillRect(x, y, kCell, kCell, QColor(255, 255, 255, 16));
    }
    g.setRenderHint(QPainter::Antialiasing);""")
# (e) a fainter grid.
ct = sub(ct, "        const int alpha = i == 0 ? 60 : 26;", "        const int alpha = i == 0 ? 50 : 20;")
# (c, d) in render: the backdrop follows the effect's materials; the emitters not drawn are counted.
ct = sub(ct, """    if (fitSide_ != s || back_.size() != size_t(s) * s * 3) renderBackground();
    buf_ = back_;
    textured_ = pending_ = 0;
    missingNames_.clear();""",
         """    textured_ = pending_ = undrawn_ = 0;
    missingNames_.clear();""")
ct = sub(ct, """        for (const VfxEmitterSim& em : def_.emitters) {
            if (!drawnElement(em) && !(em.element == 3 && em.flare.present)) continue;""",
         """        for (const VfxEmitterSim& em : def_.emitters) {
            if (!drawnElement(em) && !(em.element == 3 && em.flare.present)) {
                ++undrawn_;
                continue;
            }""")
ct = sub(ct, """            for (const std::string& m : ms) matSlots[e].push_back(m.empty() || !by.count(m) ? nullptr : by[m]);
        }
    }""",
         """            for (const std::string& m : ms) matSlots[e].push_back(m.empty() || !by.count(m) ? nullptr : by[m]);
        }
        bool distortion = false;
        for (const SpriteStore::Slot* sl : got) distortion = distortion || (sl->state == SpriteStore::Ready && sl->sprite && sl->sprite->mat.distortion);
        if (distortion != backdrop_) backdrop_ = distortion, fitSide_ = 0;
    }
    if (fitSide_ != s || back_.size() != size_t(s) * s * 3) renderBackground();
    buf_ = back_;""")
# (b) sprites at least about a pixel across (a far spark of a wide effect would fall between the pixel centres).
ct = sub(ct, """        toScreen(ax0, q.a0x, q.a0y);
        toScreen(ax1, q.a1x, q.a1y);
        q.depth = float(dot3(p.pos, fwd_));""",
         """        toScreen(ax0, q.a0x, q.a0y);
        toScreen(ax1, q.a1x, q.a1y);
        const auto atLeast = [](float& x, float& y, float m, float dx, float dy) {
            const float l = std::hypot(x, y);
            if (l >= m) return;
            if (l < 1e-6f) x = dx * m, y = dy * m;
            else x *= m / l, y *= m / l;
        };
        atLeast(q.a0x, q.a0y, 0.75f, 1, 0);
        atLeast(q.a1x, q.a1y, 0.75f, 0, -1);
        q.depth = float(dot3(p.pos, fwd_));""")
# (d) the note.
ct = sub(ct, """    if (parts.isEmpty()) return QString("%1 sprite(s) drawn with their images").arg(textured_);
    return parts.join(" ");""",
         """    if (parts.isEmpty()) parts << QString("%1 sprite(s) drawn with their images.").arg(textured_);
    if (undrawn_ > 0)
        parts << QString("%1 emitter(s) of models, child effects, decals or lights not shown.").arg(undrawn_);
    return parts.join(" ");""")
c.write_text(ct, encoding="utf-8")

tt = t.read_text(encoding="utf-8")
tt = sub(tt, """    uint8_t blueMax = 0, alphaMin = 255, alphaMax = 0;
    for (size_t k = 0; k + 3 < img.pixels.size(); k += 4) {
        blueMax = std::max(blueMax, img.pixels[k + 2]);
        alphaMin = std::min(alphaMin, img.pixels[k + 3]), alphaMax = std::max(alphaMax, img.pixels[k + 3]);
    }
    if (blueMax < 8 && alphaMax > alphaMin)""",
         """    // The strength mask: the world's distortion images (BC1) keep it in blue; the effect images (BC3) keep it in
    // alpha, their blue empty or flat (vfx_dist_heat_3_c: blue 123-131 of 255 in every block, alpha 0-255). The
    // camera's copy moves alpha into blue only when blue is empty, so a flat blue (0.2 once read linear) left such a
    // heat haze without any strength there.
    uint8_t blueMin = 255, blueMax = 0, alphaMin = 255, alphaMax = 0;
    for (size_t k = 0; k + 3 < img.pixels.size(); k += 4) {
        blueMin = std::min(blueMin, img.pixels[k + 2]), blueMax = std::max(blueMax, img.pixels[k + 2]);
        alphaMin = std::min(alphaMin, img.pixels[k + 3]), alphaMax = std::max(alphaMax, img.pixels[k + 3]);
    }
    if ((blueMax < 8 && alphaMax > alphaMin) || (blueMax - blueMin < 16 && alphaMax - alphaMin > 32))""")
t.write_text(tt, encoding="utf-8")
print("ok")
