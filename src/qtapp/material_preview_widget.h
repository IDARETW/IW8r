// IW8 Radiant Suite - live preview panel for the Material Editor (software render of a custom material).
#pragma once

#include <QElapsedTimer>
#include <QEvent>
#include <QImage>
#include <QPoint>
#include <QString>
#include <QTimer>
#include <QWidget>

#include "material_preview.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;

namespace iw8r::qt {

class MaterialPreviewWidget : public QWidget {
public:
    explicit MaterialPreviewWidget(QWidget* parent = nullptr);

    // Keeps the widget's own shape, rotation, sun, background and exposure.
    void setMaterial(const iw8r::MaterialPreviewInput& input);
    QImage image() const { return last_; }   // the last rendered picture
    void setShape(int index);                // 0 sphere, 1 plane, 2 cube

    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void requestRender();  // coalesced with a 0 ms single-shot timer
    void renderNow();

    iw8r::MaterialPreviewInput input_;
    QLabel* picture_ = nullptr;
    QComboBox* shape_ = nullptr;
    QComboBox* background_ = nullptr;
    QCheckBox* spin_ = nullptr;
    QSlider* sunYaw_ = nullptr;
    QSlider* sunPitch_ = nullptr;
    QSlider* exposure_ = nullptr;
    QTimer spinTimer_;    // 3 degrees every 40 ms while Spin is checked
    QTimer renderTimer_;
    QImage last_;
    double yawDeg_ = 30, pitchDeg_ = 20;
    bool dragging_ = false;
    QPoint dragFrom_;
    double fullMs_ = 0;   // duration of the last 256 pixel render
};

// Loads an image file (PNG / TGA / JPG / BMP / uncompressed DDS) as a preview image, downscaled so its longer side is at
// most maxSide. An empty image when the path is empty or unreadable.
iw8r::PreviewImage loadPreviewImage(const QString& path, int maxSide = 256);

}  // namespace iw8r::qt
