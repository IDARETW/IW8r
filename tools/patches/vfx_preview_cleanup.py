"""VfxPreview: span_ instead of scale_, the disc style's fit kept, requested materials looked up again now and then."""
import pathlib
import sys

root = pathlib.Path(__file__).resolve().parents[2]
h = root / "src/qtapp/vfx_preview.h"
c = root / "src/qtapp/vfx_preview.cpp"


def sub(text, old, new, count=1):
    if text.count(old) < 1:
        sys.exit("not found: " + old[:80])
    return text.replace(old, new) if count == 0 else text.replace(old, new, count)


ht = h.read_text(encoding="utf-8")
ht = sub(ht, "    double scale_ = 1;        // pixels per inch at the square's side when the fit ran\n",
         "    double span_ = 64;        // inches across the square\n")
ht = sub(ht, "    double cx_ = 0, cz_ = 0;  // disc style: view centre (x, z)\n",
         "    double cx_ = 0, cz_ = 0, discSpan_ = 64;  // disc style: view centre (x, z) and extent, inches\n")
h.write_text(ht, encoding="utf-8")

ct = c.read_text(encoding="utf-8")
ct = sub(ct, "        quint64 used = 0;\n        size_t bytes = 0;\n    };\n    static SpriteStore& get() {",
         "        quint64 used = 0;\n        size_t bytes = 0;\n        qint64 checked = 0;  // Requested: when the cache was last looked at (ms)\n    };\n    static SpriteStore& get() {")
ct = sub(ct, "    explicit SpriteStore(QObject* parent) : QObject(parent) { pool_.setMaxThreadCount(2); }",
         "    explicit SpriteStore(QObject* parent) : QObject(parent) {\n        pool_.setMaxThreadCount(2);\n        since_.start();\n    }")
ct = sub(ct, """        Slot& s = slots_[name];
        if (s.state != Unknown) return s;""",
         """        Slot& s = slots_[name];
        // A requested material is looked up again every two seconds as well (its answer may come without a signal).
        if (s.state == Requested && since_.elapsed() - s.checked > 2000 && cache_ && cache_->lookup(QString::fromStdString(name))) s.state = Unknown;
        if (s.state != Unknown) return s;""")
ct = sub(ct, """        if (!e) {
            s.state = Requested;
            ask->push_back(qn);""",
         """        if (!e) {
            s.state = Requested;
            s.checked = since_.elapsed();
            ask->push_back(qn);""")
ct = sub(ct, "    quint64 clock_ = 0;\n    QThreadPool pool_;",
         "    quint64 clock_ = 0;\n    QElapsedTimer since_;\n    QThreadPool pool_;")
ct = sub(ct, "    const double t0 = def_.duration >= 0 ? 0.0 : 0.0, t1 = loopLength();\n    for (int k = 0; k <= 32; ++k)\n        for (const VfxParticle& p : sampleVfx(def_, t0 + (t1 - t0) * k / 32.0)) {",
         "    const double t1 = loopLength();\n    for (int k = 0; k <= 32; ++k)\n        for (const VfxParticle& p : sampleVfx(def_, t1 * k / 32.0)) {")
ct = sub(ct, "    scale_ = std::max({hi[0] - lo[0], hi[1] - lo[1], 16.0}) * 1.12;  // inches across the square\n",
         "    span_ = std::max({hi[0] - lo[0], hi[1] - lo[1], 16.0}) * 1.12;  // inches across the square\n")
ct = sub(ct, "std::max(1.0, scale_ / 6.0)", "std::max(1.0, span_ / 6.0)")
ct = sub(ct, "    const double k = s / scale_;\n    const auto at = [&](double x, double y, double z) {",
         "    const double k = s / span_;\n    const auto at = [&](double x, double y, double z) {")
ct = sub(ct, "std::ceil(scale_ / groundStep_)", "std::ceil(span_ / groundStep_)")
ct = sub(ct, "    const double k = s / scale_;  // pixels per inch", "    const double k = s / span_;  // pixels per inch")
ct = sub(ct, "    cx_ = (lo[0] + hi[0]) / 2, cz_ = (lo[1] + hi[1]) / 2;\n}",
         "    cx_ = (lo[0] + hi[0]) / 2, cz_ = (lo[1] + hi[1]) / 2;\n    discSpan_ = std::max({hi[0] - lo[0], hi[1] - lo[1], 16.0});\n}")
old_disc = ct[ct.index("    g.setRenderHint(QPainter::Antialiasing);\n    const double span = 1.0;"):ct.index("    const QPointF c(width() / 2.0, height() / 2.0);\n    auto at = [&](double x, double z)")]
ct = ct.replace(old_disc, "    g.setRenderHint(QPainter::Antialiasing);\n    const double scale = 0.8 * std::min(std::max(width(), 200), std::max(height(), 200)) / discSpan_;\n")
if "scale_" in ct:
    sys.exit("scale_ left in vfx_preview.cpp")
c.write_text(ct, encoding="utf-8")
print("ok")
