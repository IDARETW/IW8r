#include "sun_sky_dialog.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>

#include <cmath>
#include <cstdio>

#include "editor_context.h"
#include "sky_image.h"

namespace iw8r::qt {

namespace {
std::string num(double v) { return formatNumber(v); }
}  // namespace

SunSky currentSunSky(const EditorContext& ctx) {
    SunSky s;
    for (const auto& e : ctx.doc.map().entities) {
        if (!e.isWorldspawn()) continue;
        if (const std::string* d = e.value("sundirection")) {
            double p = 0, y = 0, r = 0;
            if (std::sscanf(d->c_str(), "%lf %lf %lf", &p, &y, &r) >= 2) s.pitch = p, s.yaw = y;
        }
        if (const std::string* c = e.value("suncolor")) {
            double r = 1, g = 1, b = 1;
            if (std::sscanf(c->c_str(), "%lf %lf %lf", &r, &g, &b) == 3) {
                s.color = Vec3{r, g, b};
                if (r > 1 || g > 1 || b > 1) s.color = s.color * (1.0 / 255.0);  // 0-255 form, as the compiler reads it
            }
        }
        for (const char* key : {"skyname", "sky"})
            if (const std::string* k = e.value(key); k && !k->empty()) {
                s.sky = QString::fromStdString(*k);
                break;
            }
        s.skyBrightness = double(parseSkyBrightness(e.value(kSkyBrightnessKey)));
        break;
    }
    return s;
}

bool applySunSky(EditorContext& ctx, const SunSky& s) {
    Entity* w = ctx.doc.worldspawn();
    if (!w) return false;
    const Id wid = w->id;
    const std::pair<std::string, std::string> want[] = {
        {"sundirection", num(s.pitch) + " " + num(s.yaw) + " 0"},
        {"suncolor", num(s.color.x) + " " + num(s.color.y) + " " + num(s.color.z)},
        {"skyname", s.sky.trimmed().toStdString()},
        {kSkyBrightnessKey, std::fabs(s.skyBrightness - 1.0) < 1e-6 ? std::string() : num(s.skyBrightness)}};
    bool changed = false;
    for (const auto& [k, v] : want) {
        const std::string* cur = w->value(k);
        changed |= v.empty() ? cur != nullptr : (!cur || *cur != v);
    }
    if (!changed) return false;
    ctx.doc.beginGroup("Sun & Sky");
    for (const auto& [k, v] : want) {
        const std::string* cur = ctx.doc.findEntity(wid)->value(k);
        if (v.empty()) {
            if (cur) ctx.doc.removeKey(wid, k);
        } else if (!cur || *cur != v) {
            ctx.doc.setKeyValue(wid, k, v);
        }
    }
    ctx.doc.endGroup();
    ctx.scene.invalidate();  // the lit preview reads the sun keys
    ctx.documentEdited("Sun & sky updated");
    return true;
}

SunSkyDialog::SunSkyDialog(const SunSky& s, const QStringList& skies, QWidget* parent) : QDialog(parent), color_(s.color) {
    setWindowTitle("Sun & Sky");
    auto* form = new QFormLayout(this);
    pitch_ = new QDoubleSpinBox(this);
    pitch_->setObjectName("sunPitch");
    pitch_->setRange(-90, 90);
    pitch_->setDecimals(1);
    pitch_->setValue(s.pitch);
    pitch_->setToolTip("Negative = above the horizon (-90 straight down from overhead)");
    yaw_ = new QDoubleSpinBox(this);
    yaw_->setObjectName("sunYaw");
    yaw_->setRange(-360, 360);
    yaw_->setDecimals(1);
    yaw_->setValue(s.yaw);
    colorButton_ = new QPushButton(this);
    colorButton_->setObjectName("sunColor");
    connect(colorButton_, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(QColor::fromRgbF(float(color_.x), float(color_.y), float(color_.z)), this, "Sun colour");
        if (!c.isValid()) return;
        color_ = Vec3{c.redF(), c.greenF(), c.blueF()};
        updateColorButton();
    });
    updateColorButton();
    sky_ = new QComboBox(this);
    sky_->setObjectName("skyName");
    sky_->setEditable(true);
    sky_->addItem("");
    sky_->addItems(skies);
    sky_->setCurrentText(s.sky);
    // A misspelled sky builds with no sky, so say so; it is not blocked (a sky of the map's own may be named). With no
    // stock list (game files not found) nothing is known, so nothing is said.
    skyWarning_ = new QLabel(this);
    skyWarning_->setObjectName("skyNameWarning");
    skyWarning_->setStyleSheet("color: #d08020");
    skyWarning_->setWordWrap(true);
    auto checkSky = [this, skies] {
        const QString t = sky_->currentText().trimmed();
        const bool unknown = !t.isEmpty() && !skies.isEmpty() && !skies.contains(t);
        skyWarning_->setText(unknown ? QString("'%1' is not a stock sky name. Check the spelling; the compile log says whether a sky was built.").arg(t)
                                     : QString());
        skyWarning_->setVisible(unknown);
    };
    connect(sky_, &QComboBox::currentTextChanged, this, checkSky);
    checkSky();
    form->addRow("Sun pitch (degrees)", pitch_);
    form->addRow("Sun yaw (degrees)", yaw_);
    form->addRow("Sun colour", colorButton_);
    form->addRow("Sky", sky_);
    form->addRow(skyWarning_);
    skyBrightness_ = new QDoubleSpinBox(this);
    skyBrightness_->setObjectName("skyBrightness");
    skyBrightness_->setRange(0.1, 16);
    skyBrightness_->setDecimals(2);
    skyBrightness_->setSingleStep(0.25);
    skyBrightness_->setValue(s.skyBrightness);
    skyBrightness_->setToolTip("The built sky is an 8-bit picture exposed so that its brightest clouds stay below white. Above 1 the "
                               "whole sky is brighter and its brightest parts go to white; below 1 it is darker.");
    form->addRow("Sky brightness", skyBrightness_);
    auto* note = new QLabel("The lit preview (Ctrl+L) shows the sun and draws its direction. A sky is built when its "
                            "cubemap can be exported; the compile log says so.", this);
    note->setWordWrap(true);
    note->setStyleSheet("color: gray");
    form->addRow(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(buttons);
}

void SunSkyDialog::updateColorButton() {
    const QColor c = QColor::fromRgbF(float(color_.x), float(color_.y), float(color_.z));
    colorButton_->setText(QString("%1 %2 %3").arg(color_.x, 0, 'f', 2).arg(color_.y, 0, 'f', 2).arg(color_.z, 0, 'f', 2));
    colorButton_->setStyleSheet(QString("background: %1; color: %2").arg(c.name(), c.lightness() > 128 ? "black" : "white"));
}

SunSky SunSkyDialog::value() const {
    SunSky s;
    s.pitch = pitch_->value();
    s.yaw = yaw_->value();
    s.color = color_;
    s.sky = sky_->currentText().trimmed();
    s.skyBrightness = skyBrightness_->value();
    return s;
}

void editSunSky(EditorContext& ctx, const QStringList& skies, QWidget* parent) {
    SunSkyDialog dlg(currentSunSky(ctx), skies, parent);
    if (dlg.exec() == QDialog::Accepted) applySunSky(ctx, dlg.value());
}

}  // namespace iw8r::qt
