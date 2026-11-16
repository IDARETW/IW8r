"""VfxPreview: per-emitter extents of the fit in the debug lines (bench)."""
import pathlib, sys
root = pathlib.Path(__file__).resolve().parents[2]
h = root / "src/qtapp/vfx_preview.h"
c = root / "src/qtapp/vfx_preview.cpp"
ht = h.read_text(encoding="utf-8")
old = "    QStringList debugLines() const { return debug_; }"
if old not in ht: sys.exit("h")
ht = ht.replace(old, "    QStringList debugLines() const { return debug_ + fitInfo_; }")
ht = ht.replace("    QStringList debug_;\n", "    QStringList debug_, fitInfo_;\n")
h.write_text(ht, encoding="utf-8")
ct = c.read_text(encoding="utf-8")
old = """    double lo[2] = {1e30, 1e30}, hi[2] = {-1e30, -1e30};
    const double t1 = loopLength();
    for (int k = 0; k <= 32; ++k)
        for (const VfxParticle& p : sampleVfx(def_, t1 * k / 32.0)) {
            const VfxEmitterSim& em = def_.emitters[size_t(p.emitter)];
            if (!drawnElement(em)) continue;
            double r = 0.5 * std::max(p.size2[0], p.size2[1]);
            if (em.element == 10 && em.tailScaleWithVelocity) r = std::max(r, std::sqrt(dot3(p.vel, p.vel)) * 0.033 * 0.5);
            const double R = dot3(p.pos, right_), U = dot3(p.pos, up_);
            lo[0] = std::min(lo[0], R - r), hi[0] = std::max(hi[0], R + r);
            lo[1] = std::min(lo[1], U - r), hi[1] = std::max(hi[1], U + r);
        }
"""
new = """    double lo[2] = {1e30, 1e30}, hi[2] = {-1e30, -1e30};
    std::vector<std::array<double, 5>> per(def_.emitters.size(), {1e30, -1e30, 1e30, -1e30, 0});
    const double t1 = loopLength();
    for (int k = 0; k <= 32; ++k)
        for (const VfxParticle& p : sampleVfx(def_, t1 * k / 32.0)) {
            const VfxEmitterSim& em = def_.emitters[size_t(p.emitter)];
            if (!drawnElement(em)) continue;
            double r = 0.5 * std::max(p.size2[0], p.size2[1]);
            if (em.element == 10 && em.tailScaleWithVelocity) r = std::max(r, std::sqrt(dot3(p.vel, p.vel)) * 0.033 * 0.5);
            const double R = dot3(p.pos, right_), U = dot3(p.pos, up_);
            lo[0] = std::min(lo[0], R - r), hi[0] = std::max(hi[0], R + r);
            lo[1] = std::min(lo[1], U - r), hi[1] = std::max(hi[1], U + r);
            auto& e = per[size_t(p.emitter)];
            e[0] = std::min(e[0], R - r), e[1] = std::max(e[1], R + r), e[2] = std::min(e[2], U - r), e[3] = std::max(e[3], U + r), e[4] = std::max(e[4], r);
        }
    fitInfo_.clear();
    for (size_t i = 0; i < per.size(); ++i)
        if (per[i][0] < per[i][1])
            fitInfo_ << QString("  fit e%1 (element %2): R %3 .. %4, U %5 .. %6, largest half size %7")
                            .arg(i).arg(def_.emitters[i].element).arg(per[i][0], 0, 'f', 1).arg(per[i][1], 0, 'f', 1)
                            .arg(per[i][2], 0, 'f', 1).arg(per[i][3], 0, 'f', 1).arg(per[i][4], 0, 'f', 1);
"""
if old not in ct: sys.exit("c fit")
ct = ct.replace(old, new)
ct = ct.replace("#include <algorithm>\n#include <cmath>\n#include <functional>", "#include <algorithm>\n#include <array>\n#include <cmath>\n#include <functional>", 1)
c.write_text(ct, encoding="utf-8")
print("ok")
