#include "material_preview_widget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPixmap>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "custom_materials.h"

namespace iw8r::qt {

namespace {

constexpr int kShown = 256;   // picture size on screen and full render size
constexpr int kFast = 128;    // render size while dragging or spinning on a slow machine
constexpr double kSlowMs = 30.0;

double wrapDeg(double d) {
    d = std::fmod(d, 360.0);
    return d < 0 ? d + 360.0 : d;
}

}  // namespace

MaterialPreviewWidget::MaterialPreviewWidget(QWidget* parent) : QWidget(parent) {
    renderTimer_.setSingleShot(true);
    connect(&renderTimer_, &QTimer::timeout, this, [this] { renderNow(); });

    spinTimer_.setInterval(40);
    connect(&spinTimer_, &QTimer::timeout, this, [this] {
        yawDeg_ = wrapDeg(yawDeg_ + 3);
        requestRender();
    });

    auto* root = new QVBoxLayout(this);
    picture_ = new QLabel(this);
    picture_->setFixedSize(kShown, kShown);
    picture_->setAlignment(Qt::AlignCenter);
    picture_->setCursor(Qt::OpenHandCursor);
    picture_->installEventFilter(this);
    root->addWidget(picture_, 0, Qt::AlignHCenter);

    shape_ = new QComboBox(this);
    shape_->addItem("Sphere");
    shape_->addItem("Plane");
    shape_->addItem("Cube");

    background_ = new QComboBox(this);
    background_->addItem("Checker");
    background_->addItem("Dark grey");
    background_->addItem("Sky");

    spin_ = new QCheckBox("Spin", this);

    sunYaw_ = new QSlider(Qt::Horizontal, this);
    sunYaw_->setRange(0, 360);
    sunYaw_->setValue(45);

    sunPitch_ = new QSlider(Qt::Horizontal, this);
    sunPitch_->setRange(5, 90);
    sunPitch_->setValue(50);

    exposure_ = new QSlider(Qt::Horizontal, this);  // in hundredths: 0.25 .. 4
    exposure_->setRange(25, 400);
    exposure_->setValue(100);

    auto* form = new QFormLayout;
    form->addRow("Shape", shape_);
    form->addRow("Background", background_);
    form->addRow("", spin_);
    form->addRow("Sun yaw", sunYaw_);
    form->addRow("Sun pitch", sunPitch_);
    form->addRow("Exposure", exposure_);
    root->addLayout(form);

    connect(shape_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { requestRender(); });
    connect(background_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { requestRender(); });
    connect(spin_, &QCheckBox::toggled, this, [this](bool on) {
        if (on)
            spinTimer_.start();
        else
            spinTimer_.stop();
        requestRender();
    });
    connect(sunYaw_, &QSlider::valueChanged, this, [this](int) { requestRender(); });
    connect(sunPitch_, &QSlider::valueChanged, this, [this](int) { requestRender(); });
    connect(exposure_, &QSlider::valueChanged, this, [this](int) { requestRender(); });

    requestRender();
}

void MaterialPreviewWidget::setMaterial(const iw8r::MaterialPreviewInput& input) {
    input_ = input;
    requestRender();
}

void MaterialPreviewWidget::setShape(int index) {
    shape_->setCurrentIndex(std::clamp(index, 0, 2));
    requestRender();
}

void MaterialPreviewWidget::requestRender() {
    if (!renderTimer_.isActive()) renderTimer_.start(0);
}

void MaterialPreviewWidget::renderNow() {
    iw8r::MaterialPreviewInput in = input_;
    in.shape = static_cast<iw8r::PreviewShape>(std::clamp(shape_->currentIndex(), 0, 2));
    in.background = background_->currentIndex();
    in.yawDeg = yawDeg_;
    in.pitchDeg = pitchDeg_;
    in.sunYawDeg = sunYaw_->value();
    in.sunPitchDeg = sunPitch_->value();
    in.exposure = exposure_->value() / 100.0;

    const bool fast = dragging_ || spin_->isChecked();
    const int size = (fast && fullMs_ > kSlowMs) ? kFast : kShown;

    QElapsedTimer timer;
    timer.start();
    const iw8r::PreviewImage pi = iw8r::renderMaterialPreview(in, size);
    const double ms = static_cast<double>(timer.nsecsElapsed()) / 1e6;
    if (size == kShown) fullMs_ = ms;

    if (pi.rgba.size() != static_cast<size_t>(size) * size * 4) return;
    last_ = QImage(pi.rgba.data(), size, size, size * 4, QImage::Format_RGBA8888).copy();
    picture_->setPixmap(QPixmap::fromImage(last_.scaled(kShown, kShown, Qt::IgnoreAspectRatio,
                                                        size == kShown ? Qt::SmoothTransformation
                                                                       : Qt::FastTransformation)));
}

bool MaterialPreviewWidget::eventFilter(QObject* watched, QEvent* event) {
    if (watched != picture_) return QWidget::eventFilter(watched, event);

    if (event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            dragging_ = true;
            dragFrom_ = me->position().toPoint();
            picture_->setCursor(Qt::ClosedHandCursor);
            return true;
        }
    } else if (event->type() == QEvent::MouseMove && dragging_) {
        auto* me = static_cast<QMouseEvent*>(event);
        const QPoint p = me->position().toPoint();
        const QPoint d = p - dragFrom_;
        dragFrom_ = p;
        yawDeg_ = wrapDeg(yawDeg_ + d.x() * 0.5);
        pitchDeg_ = std::clamp(pitchDeg_ + d.y() * 0.5, -89.0, 89.0);
        requestRender();
        return true;
    } else if (event->type() == QEvent::MouseButtonRelease && dragging_) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            dragging_ = false;
            picture_->setCursor(Qt::OpenHandCursor);
            requestRender();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

iw8r::PreviewImage loadPreviewImage(const QString& path, int maxSide) {
    iw8r::PreviewImage out;
    if (path.isEmpty()) return out;
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    std::string err;
    if (!iw8r::decodeImageFile(path.toStdString(), w, h, rgba, &err) || w <= 0 || h <= 0) return out;

    QImage img(rgba.data(), w, h, w * 4, QImage::Format_RGBA8888);
    maxSide = std::max(1, maxSide);
    if (std::max(w, h) > maxSide) {
        const double s = static_cast<double>(maxSide) / std::max(w, h);
        img = img.scaled(std::max(1, static_cast<int>(std::lround(w * s))),
                         std::max(1, static_cast<int>(std::lround(h * s))), Qt::IgnoreAspectRatio,
                         Qt::SmoothTransformation);
    }
    img = img.convertToFormat(QImage::Format_RGBA8888);
    out.width = img.width();
    out.height = img.height();
    out.rgba.resize(static_cast<size_t>(out.width) * out.height * 4);
    for (int y = 0; y < out.height; ++y)
        std::memcpy(out.rgba.data() + static_cast<size_t>(y) * out.width * 4, img.constScanLine(y),
                    static_cast<size_t>(out.width) * 4);
    return out;
}

}  // namespace iw8r::qt
