#include "vfx_preview.h"

#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace iw8r::qt {

VfxPreview::VfxPreview(QWidget* parent) : QWidget(parent) {
    setObjectName("vfxPreview");
    setMinimumHeight(220);
    timer_ = new QTimer(this);
    timer_->setInterval(33);
    connect(timer_, &QTimer::timeout, this, [this] {
        if (!playing_) return;
        const double len = loopLength();
        t_ = std::fmod(offset_ + clock_.elapsed() / 1000.0, len);
        last_ = sampleVfx(def_, t_);
        update();
    });
}

double VfxPreview::loopLength() const {
    // One-shots replay after their duration plus a short pause; looping effects show a 4 second window, repeated.
    return def_.duration > 0 ? def_.duration + 0.5 : 4.0;
}

void VfxPreview::setEffect(const VfxSimDef& def) {
    def_ = def;
    t_ = offset_ = 0;
    playing_ = true;
    fit();
    last_ = sampleVfx(def_, 0);
    clock_.restart();
    timer_->start();
    update();
}

void VfxPreview::setTime(double t) {
    playing_ = false;
    t_ = t;
    last_ = sampleVfx(def_, t_);
    update();
}

void VfxPreview::fit() {
    // Bounds of a few samples across the run: the view is centred on them with a margin.
    double lo[2] = {1e30, 1e30}, hi[2] = {-1e30, -1e30};
    const double len = loopLength();
    for (int k = 1; k <= 8; ++k)
        for (const VfxParticle& p : sampleVfx(def_, len * k / 9.0)) {
            const double r = p.size / 2;
            lo[0] = std::min(lo[0], p.pos[0] - r), hi[0] = std::max(hi[0], p.pos[0] + r);
            lo[1] = std::min(lo[1], p.pos[2] - r), hi[1] = std::max(hi[1], p.pos[2] + r);
        }
    if (lo[0] > hi[0]) lo[0] = lo[1] = -32, hi[0] = hi[1] = 32;
    cx_ = (lo[0] + hi[0]) / 2, cz_ = (lo[1] + hi[1]) / 2;
    const double span = std::max({hi[0] - lo[0], hi[1] - lo[1], 16.0});
    scale_ = 0.8 * std::min(std::max(width(), 200), std::max(height(), 200)) / span;
}

void VfxPreview::paintEvent(QPaintEvent*) {
    QPainter g(this);
    g.fillRect(rect(), QColor(18, 20, 24));
    g.setRenderHint(QPainter::Antialiasing);
    const QPointF c(width() / 2.0, height() / 2.0);
    auto at = [&](double x, double z) { return c + QPointF((x - cx_) * scale_, -(z - cz_) * scale_); };
    // Ground line at z = 0 and the effect origin.
    g.setPen(QPen(QColor(255, 255, 255, 40), 1));
    g.drawLine(QPointF(0, at(0, 0).y()), QPointF(width(), at(0, 0).y()));
    g.setPen(QPen(QColor(255, 210, 60), 1));
    const QPointF o = at(0, 0);
    g.drawLine(o - QPointF(6, 0), o + QPointF(6, 0));
    g.drawLine(o - QPointF(0, 6), o + QPointF(0, 6));
    // Particles: additive-looking soft discs, colour and alpha from the sim.
    g.setPen(Qt::NoPen);
    g.setCompositionMode(QPainter::CompositionMode_Plus);
    for (const VfxParticle& p : last_) {
        const double r = std::max(1.5, p.size * scale_ / 2);
        QColor col = QColor::fromRgbF(std::clamp(p.rgba[0], 0.f, 1.f), std::clamp(p.rgba[1], 0.f, 1.f),
                                      std::clamp(p.rgba[2], 0.f, 1.f), std::clamp(p.rgba[3], 0.f, 1.f) * 0.6f);
        QRadialGradient grad(at(p.pos[0], p.pos[2]), r);
        grad.setColorAt(0, col);
        col.setAlphaF(0);
        grad.setColorAt(1, col);
        g.setBrush(grad);
        g.drawEllipse(at(p.pos[0], p.pos[2]), r, r);
    }
    g.setCompositionMode(QPainter::CompositionMode_SourceOver);
    g.setPen(QColor(200, 200, 200));
    g.drawText(rect().adjusted(8, 4, -8, -4), Qt::AlignLeft | Qt::AlignBottom,
               QString("t = %1 s, %2 particle(s), %3 emitter(s)%4. Side view, simplified (click: %5)")
                   .arg(t_, 0, 'f', 2).arg(last_.size()).arg(def_.emitters.size())
                   .arg(def_.duration < 0 ? QString(", looping") : QString(", one-shot %1 s").arg(def_.duration, 0, 'f', 1))
                   .arg(playing_ ? "pause" : "play"));
}

void VfxPreview::mousePressEvent(QMouseEvent*) {
    playing_ = !playing_;
    if (playing_) {
        offset_ = t_;
        clock_.restart();
    }
    update();
}

}  // namespace iw8r::qt
