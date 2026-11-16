#pragma once
// FX preview (roadmap 8.1; owner 2026-11-15: "the preview should be square with the vfx fit in the middle centered",
// "a lot of vfx shows as faint orbs"). Plays core vfx_sim's particle simulation of a stock effect (vfx_sim.h lists
// what it leaves out) in a square, seen from the side and a little above (orthographic), the effect's whole run fitted
// and centred in it, over a ground grid and the effect's origin.
// Each particle is drawn as the camera's View > Effect Preview draws it without Game Lighting (camera_view.cpp
// appendEffectParticles, render.cpp ps_part / ps_dist): its material's sprite image (vfx_textures.h: the colour image
// of a lit set with its opacity, the emissive image of an unlit one), the atlas cell of its frame, turned by its angle
// and mirrored, as a camera-facing billboard, an oriented sprite or a tail along its velocity; x the particle colour
// (linear, shown as display values); lit sets blend over what is behind (premultiplied), unlit sets and flares add,
// all far to near; distortion sets move the picture behind by their image; flare pieces sit on the line from a source
// in the upper left through the centre. A software rasterizer into an image (bilinear, mip levels), no D3D.
// A particle whose image is not read yet is not drawn (the preview redraws when it arrives); the line under the
// square says which materials are still loading or could not be found.
#include <QElapsedTimer>
#include <QImage>
#include <QWidget>

#include <vector>

#include "vfx_sim.h"

class QTimer;

namespace iw8r::qt {

class MaterialCache;

class VfxPreview : public QWidget {
    Q_OBJECT
public:
    explicit VfxPreview(QWidget* parent = nullptr);
    // Where the sprites' materials come from (one for the process; MainWindow sets it). Without it nothing is drawn
    // but the note that the materials are unknown.
    static void setMaterialCache(MaterialCache* cache);
    void setEffect(const VfxSimDef& def);  // restarts playback and refits the view
    void setTime(double t);                // stops playback at t seconds of the effect (tests)
    int particlesNow() const { return int(last_.size()); }
    const VfxSimDef& effect() const { return def_; }
    // Tests and the bench (vfx_preview_bench.h).
    QRect viewRect() const;                        // the square the effect is drawn in
    int texturedParticles() const { return textured_; }  // sprites drawn with their image in the last frame
    int materialsPending() const { return pending_; }    // the effect's materials still being exported / read
    int materialsMissing() const { return int(missingNames_.size()); }  // the ones that could not be found
    QStringList missingMaterials() const { return missingNames_; }
    double lastRenderMs() const { return renderMs_; }
    QImage frame();                                // the square as last drawn (drawn now when out of date)
    double busiestTime() const;                    // a time with the most particles (pictures of one-shots)
    QStringList debugLines() const { return debug_ + fitInfo_; }  // the view and the first sprites of the last frame (bench)
    void setDiscStyle(bool on);                    // A/B: the soft discs of before (also IW8R_VFX_PREVIEW_DISCS=1)
    bool discStyle() const { return discs_; }
    bool hasHeightForWidth() const override { return !discs_; }
    int heightForWidth(int w) const override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;  // click: pause / play
    void resizeEvent(QResizeEvent*) override;
    void showEvent(QShowEvent*) override;  // the simulation runs only while the preview is on screen
    void hideEvent(QHideEvent*) override;

private:
    void fit();
    void fitDiscs();
    void followWidth();             // square: the height follows the width
    double loopLength() const;      // one-shot: its run and a pause; looping: the window the fit looks at
    double warmTime() const;        // looping: when the emitters have filled up (playback starts there)
    int side() const;               // the square's side for the current width
    int stripHeight() const;        // the text under the square
    void render();                  // the square into frame_
    void renderBackground();        // gradient, ground grid, origin into back_ (when the view changed)
    void paintDiscs(QPainter& g);   // the look before (setDiscStyle)
    QString statusText() const;
    QString noteText() const;

    VfxSimDef def_;
    std::vector<VfxParticle> last_;
    QTimer* timer_ = nullptr;
    QElapsedTimer clock_;
    double t_ = 0, offset_ = 0;
    bool playing_ = true;
    bool discs_ = false;
    // The view: orthographic, looking along fwd_ (from the side, a little above); screen x = right_, y = -up_.
    double fwd_[3] = {0, 1, 0}, right_[3] = {1, 0, 0}, up_[3] = {0, 0, 1};
    double span_ = 64;        // inches across the square
    double cr_ = 0, cu_ = 0;  // view centre (along right_ and up_), inches
    double groundStep_ = 16;  // grid spacing, inches
    int fitSide_ = 0;         // the side the fit and the background were made for
    double cx_ = 0, cz_ = 0, discSpan_ = 64;  // disc style: view centre (x, z) and extent, inches
    // The frame: a float picture (display values, RGB) with the background under it.
    std::vector<float> back_, buf_, scene_;
    QImage frame_;
    bool dirty_ = true;
    int textured_ = 0, pending_ = 0;
    int undrawn_ = 0;         // emitters of elements the preview does not draw (models, runners, decals, lights)
    bool backdrop_ = false;   // the background has the checker a distortion set moves
    QStringList missingNames_;
    QStringList debug_, fitInfo_;
    double renderMs_ = 0;
};

}  // namespace iw8r::qt
