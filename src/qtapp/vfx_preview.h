#pragma once
// FX preview (roadmap 8.1): plays core vfx_sim's simplified particle simulation of a stock effect (spawn rate, burst,
// life, velocity, size, colour from the ACTS export) as a side view (X across, Z up) that loops, with the time shown.
// A preview, not the game's simulation: no curves, child effects, spawn shapes or gravity (vfx_sim.h lists the limits).
#include <QElapsedTimer>
#include <QWidget>

#include "vfx_sim.h"

class QTimer;

namespace iw8r::qt {

class VfxPreview : public QWidget {
    Q_OBJECT
public:
    explicit VfxPreview(QWidget* parent = nullptr);
    void setEffect(const VfxSimDef& def);  // restarts playback and refits the view
    void setTime(double t);                // stops playback at t seconds (tests)
    int particlesNow() const { return int(last_.size()); }
    const VfxSimDef& effect() const { return def_; }

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;  // click: pause / play

private:
    void fit();
    double loopLength() const;

    VfxSimDef def_;
    std::vector<VfxParticle> last_;
    QTimer* timer_ = nullptr;
    QElapsedTimer clock_;
    double t_ = 0, offset_ = 0;
    bool playing_ = true;
    double scale_ = 1;       // pixels per inch
    double cx_ = 0, cz_ = 0;  // view centre in effect space
};

}  // namespace iw8r::qt
