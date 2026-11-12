#include "image_adjust_dialog.h"

#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "custom_materials.h"
#include "image_adjust.h"

namespace iw8r::qt {

namespace {

// The live preview works on a copy no larger than this on its long side.
constexpr int kPreviewMax = 512;
// Preview pixmap box on screen.
constexpr int kPreviewBoxW = 480;
constexpr int kPreviewBoxH = 420;

struct Source {
    int w = 0;
    int h = 0;
    std::vector<uint8_t> rgba;  // w * h * 4, top row first
};

// Reads the file as RGBA8. Qt first, then stb_image (TGA and others Qt may lack a plugin for).
bool loadRgba(const QString& path, Source& s, QString& err) {
    QImage img(path);
    if (!img.isNull()) {
        const QImage rgba = img.convertToFormat(QImage::Format_RGBA8888);
        s.w = rgba.width();
        s.h = rgba.height();
        const size_t rowBytes = static_cast<size_t>(s.w) * 4;
        s.rgba.resize(rowBytes * static_cast<size_t>(s.h));
        for (int y = 0; y < s.h; ++y) {
            std::memcpy(&s.rgba[rowBytes * static_cast<size_t>(y)], rgba.constScanLine(y), rowBytes);
        }
        return true;
    }
    std::string e;
    if (decodeImageFile(path.toStdString(), s.w, s.h, s.rgba, &e)) return true;
    err = QString::fromStdString(e);
    return false;
}

// Settings read from the widgets.
struct Controls {
    ImageAdjust adj;
    bool normal = false;
    double strength = 1.0;
    int outW = 1;
    int outH = 1;
};

// Adjust, then resize to tw x th (when different), then normal map (when on). Empty on failure.
std::vector<uint8_t> applyPipeline(const std::vector<uint8_t>& base, int w, int h, const Controls& c, int tw,
                                   int th) {
    std::vector<uint8_t> buf = base;
    adjustImage(buf, w, h, c.adj);
    int cw = w;
    int ch = h;
    if (tw != w || th != h) {
        std::vector<uint8_t> out;
        if (!resizeImage(buf, w, h, tw, th, out)) return {};
        buf.swap(out);
        cw = tw;
        ch = th;
    }
    if (c.normal) {
        std::vector<uint8_t> out;
        if (!normalMapFromHeight(buf, cw, ch, c.strength, false, out)) return {};
        buf.swap(out);
    }
    return buf;
}

// The one path for Save As and for runImageAdjustHeadless: adjust, resize, normal map, PNG write to out.
bool writeAdjusted(const Source& src, const Controls& ctl, const QString& out, QString& err) {
    std::vector<uint8_t> full = applyPipeline(src.rgba, src.w, src.h, ctl, ctl.outW, ctl.outH);
    if (full.empty()) {
        err = "Could not process the image.";
        return false;
    }
    const QImage img(full.data(), ctl.outW, ctl.outH, ctl.outW * 4, QImage::Format_RGBA8888);
    if (!img.copy().save(out, "PNG")) {
        err = "Could not write:\n" + out;
        return false;
    }
    return true;
}

// Adds a "name | slider | value" row. key names the slider (the label and readout get key + "Label" / key + "Value").
// fmt turns the slider integer into the readout text.
QSlider* sliderRow(QGridLayout* grid, int row, const QString& key, const QString& name, int lo, int hi, int value,
                   const std::function<QString(int)>& fmt) {
    QWidget* parent = grid->parentWidget();
    auto* label = new QLabel(name, parent);
    auto* slider = new QSlider(Qt::Horizontal, parent);
    slider->setRange(lo, hi);
    slider->setValue(value);
    auto* readout = new QLabel(fmt(value), parent);
    readout->setMinimumWidth(56);
    label->setObjectName(key + "Label");
    slider->setObjectName(key);
    readout->setObjectName(key + "Value");
    grid->addWidget(label, row, 0);
    grid->addWidget(slider, row, 1);
    grid->addWidget(readout, row, 2);
    QObject::connect(slider, &QSlider::valueChanged, readout, [readout, fmt](int v) { readout->setText(fmt(v)); });
    return slider;
}

// The widgets the dialog's lambdas use.
struct Parts {
    QDialog* dlg = nullptr;
    QLabel* preview = nullptr;
    QSlider* bright = nullptr;
    QSlider* contrast = nullptr;
    QSlider* sat = nullptr;
    QSlider* hue = nullptr;
    QSlider* gamma = nullptr;
    QSlider* strength = nullptr;
    QPushButton* tintBtn = nullptr;
    QPushButton* tintReset = nullptr;
    QCheckBox* invert = nullptr;
    QCheckBox* normal = nullptr;
    QSpinBox* width = nullptr;
    QSpinBox* height = nullptr;
    QCheckBox* keepAspect = nullptr;
    QLabel* status = nullptr;
};

// Everything the dialog needs. Shared with the dialog's connections, so a dialog built for a test can
// outlive the function that built it. The connections go away with the dialog, which releases this.
struct State {
    QString path;
    Source src;
    int pw = 0;
    int ph = 0;
    std::vector<uint8_t> preview;  // downscaled copy for the live preview
    Controls ctl;
    QColor tint = QColor(Qt::white);
    bool saved = false;
    QString savedTo;
    bool syncing = false;
    Parts ui;
};

// Loads the source and makes the preview copy. False with err set when the file cannot be read.
bool prepare(State& s, const QString& path, QString& err) {
    s.path = path;
    if (!loadRgba(path, s.src, err)) return false;
    s.pw = s.src.w;
    s.ph = s.src.h;
    s.preview = s.src.rgba;
    const int longSide = std::max(s.src.w, s.src.h);
    if (longSide > kPreviewMax) {
        const double scale = static_cast<double>(kPreviewMax) / longSide;
        const int nw = std::max(1, static_cast<int>(std::lround(s.src.w * scale)));
        const int nh = std::max(1, static_cast<int>(std::lround(s.src.h * scale)));
        std::vector<uint8_t> small;
        if (resizeImage(s.src.rgba, s.src.w, s.src.h, nw, nh, small)) {
            s.preview.swap(small);
            s.pw = nw;
            s.ph = nh;
        }
    }
    return true;
}

void setTintButton(State& s) {
    s.ui.tintBtn->setText(s.tint.name());
    s.ui.tintBtn->setStyleSheet(QString("background-color: %1;").arg(s.tint.name()));
}

// Reads every widget into s.ctl.
void readControls(State& s) {
    const Parts& u = s.ui;
    s.ctl.adj.brightness = u.bright->value() / 100.0;
    s.ctl.adj.contrast = u.contrast->value() / 100.0;
    s.ctl.adj.saturation = u.sat->value() / 100.0;
    s.ctl.adj.hueShiftDeg = u.hue->value();
    s.ctl.adj.gamma = u.gamma->value() / 100.0;
    s.ctl.adj.tint[0] = s.tint.redF();
    s.ctl.adj.tint[1] = s.tint.greenF();
    s.ctl.adj.tint[2] = s.tint.blueF();
    s.ctl.adj.invert = u.invert->isChecked();
    s.ctl.normal = u.normal->isChecked();
    s.ctl.strength = u.strength->value() / 10.0;
    s.ctl.outW = u.width->value();
    s.ctl.outH = u.height->value();
}

void updatePreview(State& s) {
    std::vector<uint8_t> buf = applyPipeline(s.preview, s.pw, s.ph, s.ctl, s.pw, s.ph);
    if (buf.empty()) return;
    const QImage img(buf.data(), s.pw, s.ph, s.pw * 4, QImage::Format_RGBA8888);
    s.ui.preview->setPixmap(QPixmap::fromImage(img).scaled(kPreviewBoxW, kPreviewBoxH, Qt::KeepAspectRatio,
                                                           Qt::SmoothTransformation));
}

void refresh(State& s) {
    readControls(s);
    updatePreview(s);
}

void doSave(State& s) {
    readControls(s);
    const QFileInfo fi(s.path);
    const QString suggested = fi.absolutePath() + "/" + fi.completeBaseName() + "_adj.png";
    QString out = QFileDialog::getSaveFileName(s.ui.dlg, "Save adjusted image", suggested, "PNG image (*.png)");
    if (out.isEmpty()) return;
    if (!out.endsWith(".png", Qt::CaseInsensitive)) out += ".png";
    QString err;
    if (!writeAdjusted(s.src, s.ctl, out, err)) {
        QMessageBox::warning(s.ui.dlg, "Image Adjust", err);
        return;
    }
    s.saved = true;
    s.savedTo = out;
    s.ui.status->setText("Saved " + out);
}

// Builds the dialog (a child of parent) with every connection. The caller shows it or runs exec() and deletes it.
QDialog* buildDialog(QWidget* parent, const std::shared_ptr<State>& sp) {
    State& s = *sp;
    Parts& u = s.ui;

    auto* dlg = new QDialog(parent);
    dlg->setObjectName("imageAdjustDialog");
    u.dlg = dlg;
    dlg->setWindowTitle("Image Adjust - " + QFileInfo(s.path).fileName());
    auto* root = new QHBoxLayout(dlg);

    u.preview = new QLabel(dlg);
    u.preview->setObjectName("previewLabel");
    u.preview->setMinimumSize(kPreviewBoxW, kPreviewBoxH);
    u.preview->setAlignment(Qt::AlignCenter);
    u.preview->setToolTip("Preview (adjustments and normal map; resize applies on save)");
    root->addWidget(u.preview, 1);

    auto* side = new QVBoxLayout();
    root->addLayout(side);

    // Colour group.
    auto* colourBox = new QGroupBox("Colour", dlg);
    colourBox->setObjectName("colourGroup");
    auto* grid = new QGridLayout(colourBox);
    auto twoDigits = [](int v) { return QString::number(v / 100.0, 'f', 2); };
    u.bright = sliderRow(grid, 0, "brightness", "Brightness", -100, 100, 0, twoDigits);
    u.contrast = sliderRow(grid, 1, "contrast", "Contrast", 0, 300, 100, twoDigits);
    u.sat = sliderRow(grid, 2, "saturation", "Saturation", 0, 300, 100, twoDigits);
    u.hue = sliderRow(grid, 3, "hueShift", "Hue shift", -180, 180, 0, [](int v) { return QString("%1 deg").arg(v); });
    u.gamma = sliderRow(grid, 4, "gamma", "Gamma", 10, 500, 100, twoDigits);

    u.tintBtn = new QPushButton(dlg);
    u.tintBtn->setObjectName("tintButton");
    u.tintReset = new QPushButton("Reset tint", dlg);
    u.tintReset->setObjectName("tintReset");
    auto* tintLabel = new QLabel("Tint", dlg);
    tintLabel->setObjectName("tintLabel");
    grid->addWidget(tintLabel, 5, 0);
    grid->addWidget(u.tintBtn, 5, 1);
    grid->addWidget(u.tintReset, 5, 2);

    u.invert = new QCheckBox("Invert", dlg);
    u.invert->setObjectName("invert");
    grid->addWidget(u.invert, 6, 0, 1, 3);
    side->addWidget(colourBox);

    // Normal map group.
    auto* normalBox = new QGroupBox("Normal map", dlg);
    normalBox->setObjectName("normalGroup");
    auto* ng = new QGridLayout(normalBox);
    u.normal = new QCheckBox("Normal map from height (luminance)", dlg);
    u.normal->setObjectName("normalFromHeight");
    ng->addWidget(u.normal, 0, 0, 1, 3);
    u.strength = sliderRow(ng, 1, "normalStrength", "Strength", 1, 100, 10,
                           [](int v) { return QString::number(v / 10.0, 'f', 1); });
    u.strength->setEnabled(false);
    side->addWidget(normalBox);

    // Resize group.
    auto* sizeBox = new QGroupBox("Resize (output size)", dlg);
    sizeBox->setObjectName("sizeGroup");
    auto* sg = new QHBoxLayout(sizeBox);
    u.width = new QSpinBox(dlg);
    u.width->setObjectName("outWidth");
    u.height = new QSpinBox(dlg);
    u.height->setObjectName("outHeight");
    u.width->setRange(1, 16384);
    u.height->setRange(1, 16384);
    u.width->setValue(s.src.w);
    u.height->setValue(s.src.h);
    u.keepAspect = new QCheckBox("Keep aspect", dlg);
    u.keepAspect->setObjectName("keepAspect");
    u.keepAspect->setChecked(true);
    auto* wLabel = new QLabel("W", dlg);
    wLabel->setObjectName("widthLabel");
    auto* hLabel = new QLabel("H", dlg);
    hLabel->setObjectName("heightLabel");
    sg->addWidget(wLabel);
    sg->addWidget(u.width);
    sg->addWidget(hLabel);
    sg->addWidget(u.height);
    sg->addWidget(u.keepAspect);
    side->addWidget(sizeBox);

    // Buttons.
    side->addStretch(1);
    u.status = new QLabel(dlg);
    u.status->setObjectName("statusLabel");
    u.status->setWordWrap(true);
    side->addWidget(u.status);
    auto* buttons = new QHBoxLayout();
    auto* saveBtn = new QPushButton("Save As...", dlg);
    saveBtn->setObjectName("saveButton");
    auto* closeBtn = new QPushButton("Close", dlg);
    closeBtn->setObjectName("closeButton");
    buttons->addStretch(1);
    buttons->addWidget(saveBtn);
    buttons->addWidget(closeBtn);
    side->addLayout(buttons);

    setTintButton(s);

    // Connections. Sliders and checkboxes refresh the preview.
    for (QSlider* sl : {u.bright, u.contrast, u.sat, u.hue, u.gamma, u.strength}) {
        QObject::connect(sl, &QSlider::valueChanged, dlg, [sp]() { refresh(*sp); });
    }
    QObject::connect(u.invert, &QCheckBox::toggled, dlg, [sp](bool) { refresh(*sp); });
    QObject::connect(u.normal, &QCheckBox::toggled, dlg, [sp](bool on) {
        sp->ui.strength->setEnabled(on);
        refresh(*sp);
    });
    QObject::connect(u.tintBtn, &QPushButton::clicked, dlg, [sp]() {
        const QColor c = QColorDialog::getColor(sp->tint, sp->ui.dlg, "Tint colour");
        if (!c.isValid()) return;
        sp->tint = c;
        setTintButton(*sp);
        refresh(*sp);
    });
    QObject::connect(u.tintReset, &QPushButton::clicked, dlg, [sp]() {
        sp->tint = QColor(Qt::white);
        setTintButton(*sp);
        refresh(*sp);
    });

    // Keep the two size boxes in step when the aspect lock is on.
    QObject::connect(u.width, &QSpinBox::valueChanged, dlg, [sp](int v) {
        State& st = *sp;
        if (st.syncing || !st.ui.keepAspect->isChecked() || st.src.w <= 0) return;
        st.syncing = true;
        st.ui.height->setValue(std::max(1, static_cast<int>(std::lround(static_cast<double>(v) * st.src.h / st.src.w))));
        st.syncing = false;
    });
    QObject::connect(u.height, &QSpinBox::valueChanged, dlg, [sp](int v) {
        State& st = *sp;
        if (st.syncing || !st.ui.keepAspect->isChecked() || st.src.h <= 0) return;
        st.syncing = true;
        st.ui.width->setValue(std::max(1, static_cast<int>(std::lround(static_cast<double>(v) * st.src.w / st.src.h))));
        st.syncing = false;
    });

    QObject::connect(saveBtn, &QPushButton::clicked, dlg, [sp]() { doSave(*sp); });
    QObject::connect(closeBtn, &QPushButton::clicked, dlg, &QDialog::accept);

    readControls(s);
    updatePreview(s);
    return dlg;
}

}  // namespace

bool showImageAdjustDialog(QWidget* parent, const QString& imagePath, QString* savedPath) {
    QString path = imagePath;
    if (path.isEmpty()) {
        path = QFileDialog::getOpenFileName(parent, "Open image", QString(),
                                            "Images (*.png *.tga *.jpg *.jpeg *.bmp);;All files (*)");
        if (path.isEmpty()) return false;
    }

    auto sp = std::make_shared<State>();
    QString err;
    if (!prepare(*sp, path, err)) {
        QMessageBox::warning(parent, "Image Adjust",
                             "Could not read the image:\n" + path + (err.isEmpty() ? QString() : "\n" + err));
        return false;
    }

    QDialog* dlg = buildDialog(parent, sp);
    dlg->exec();
    const bool saved = sp->saved;
    if (saved && savedPath) *savedPath = sp->savedTo;
    delete dlg;
    return saved;
}

QDialog* createImageAdjustDialog(QWidget* parent, const QString& imagePath) {
    if (imagePath.isEmpty()) return nullptr;
    auto sp = std::make_shared<State>();
    QString err;
    if (!prepare(*sp, imagePath, err)) return nullptr;
    return buildDialog(parent, sp);
}

bool runImageAdjustHeadless(const QString& imagePath, const ImageAdjust& adjust, int resizeW, int resizeH,
                            bool normalFromHeight, double normalStrength, const QString& savePath, QString* err) {
    Source src;
    QString loadErr;
    if (!loadRgba(imagePath, src, loadErr)) {
        if (err) *err = loadErr.isEmpty() ? QString("Could not read the image") : loadErr;
        return false;
    }
    Controls ctl;
    ctl.adj = adjust;
    ctl.normal = normalFromHeight;
    ctl.strength = normalStrength;
    ctl.outW = resizeW > 0 ? resizeW : src.w;
    ctl.outH = resizeH > 0 ? resizeH : src.h;
    QString writeErr;
    if (!writeAdjusted(src, ctl, savePath, writeErr)) {
        if (err) *err = writeErr;
        return false;
    }
    return true;
}

}  // namespace iw8r::qt
