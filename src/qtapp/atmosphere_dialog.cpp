#include "atmosphere_dialog.h"

#include <QCheckBox>
#include <QPushButton>
#include <QLineEdit>
#include <QFileDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "editor_context.h"

namespace iw8r::qt {

namespace {

QDoubleSpinBox* spin(QWidget* parent, double lo, double hi, double step, double v, int decimals = 2) {
    auto* s = new QDoubleSpinBox(parent);
    s->setDecimals(decimals);
    s->setRange(lo, hi);
    s->setSingleStep(step);
    s->setValue(v);
    return s;
}

QWidget* rgbRow(QWidget* parent, QDoubleSpinBox* (&out)[3], const Vec3& v, double hi) {
    auto* w = new QWidget(parent);
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    for (int i = 0; i < 3; ++i) {
        out[i] = spin(w, 0, hi, 0.05, v[i]);
        h->addWidget(out[i]);
    }
    return w;
}

}  // namespace

AtmosphereDialog::AtmosphereDialog(const Atmosphere& a, QWidget* parent) : QDialog(parent) {
    setWindowTitle("Atmosphere (fog and film grade)");
    auto* top = new QVBoxLayout(this);

    const ExpFog fog = a.fog.value_or(ExpFog{});
    fogBox_ = new QGroupBox("Exponential fog (setExpFog)", this);
    fogBox_->setObjectName("atmoFog");
    fogBox_->setCheckable(true);
    fogBox_->setChecked(a.fog.has_value());
    auto* ff = new QFormLayout(fogBox_);
    fogStart_ = spin(fogBox_, 0, 20000, 64, fog.start, 0);
    fogHalf_ = spin(fogBox_, 1, 70000, 64, fog.halfway, 0);
    ff->addRow("Start distance", fogStart_);
    ff->addRow("Halfway distance", fogHalf_);
    ff->addRow("Colour (RGB 0..1)", rgbRow(fogBox_, fogRgb_, fog.color, 1));
    top->addWidget(fogBox_);

    const FilmGrade film = a.film.value_or(FilmGrade{});
    filmBox_ = new QGroupBox("Film grade (vision r_film*)", this);
    filmBox_->setObjectName("atmoFilm");
    filmBox_->setCheckable(true);
    filmBox_->setChecked(a.film.has_value());
    auto* fm = new QFormLayout(filmBox_);
    contrast_ = spin(filmBox_, 0, 4, 0.05, film.contrast);
    brightness_ = spin(filmBox_, -1, 1, 0.05, film.brightness);
    desat_ = spin(filmBox_, 0, 1, 0.05, film.desaturation);
    invert_ = new QCheckBox(filmBox_);
    invert_->setChecked(film.invert);
    fm->addRow("Contrast", contrast_);
    fm->addRow("Brightness", brightness_);
    fm->addRow("Desaturation", desat_);
    fm->addRow("Invert", invert_);
    fm->addRow("Light tint", rgbRow(filmBox_, light_, film.lightTint, 4));
    fm->addRow("Dark tint", rgbRow(filmBox_, dark_, film.darkTint, 4));
    top->addWidget(filmBox_);

    auto* lutRow = new QWidget(this);
    auto* lutLay = new QHBoxLayout(lutRow);
    lutLay->setContentsMargins(0, 0, 0, 0);
    lut_ = new QLineEdit(QString::fromStdString(a.lut), lutRow);
    lut_->setObjectName("atmoLut");
    lut_->setPlaceholderText("colour grading LUT (.cube), relative to the map or absolute");
    auto* lutBrowse = new QPushButton("Browse...", lutRow);
    connect(lutBrowse, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, "Colour grading LUT", QString(), "3D LUT (*.cube)");
        if (!f.isEmpty()) lut_->setText(f);
    });
    lutLay->addWidget(new QLabel("Grading LUT", lutRow));
    lutLay->addWidget(lut_, 1);
    lutLay->addWidget(lutBrowse);
    top->addWidget(lutRow);

    // Replay vision overrides (5.6): named vision values the converter applies on top of the converted vision.
    auto* visRow = new QWidget(this);
    auto* visLay = new QHBoxLayout(visRow);
    visLay->setContentsMargins(0, 0, 0, 0);
    vision_ = new QLineEdit(QString::fromStdString(a.vision), visRow);
    vision_->setObjectName("atmoVision");
    vision_->setPlaceholderText("name=value; name=x y z   e.g. tonemapExposureAdjust=0.25");
    auto* visState = new QLabel(visRow);
    auto validate = [this, visState] {
        std::string err;
        const std::string t = vision_->text().trimmed().toStdString();
        const bool ok = t.empty() || !visionOverridesJson(t, &err).empty();
        visState->setText(ok ? QString() : QString("invalid"));
        visState->setToolTip(QString::fromStdString(err));
        vision_->setStyleSheet(ok ? QString() : QString("QLineEdit { border: 1px solid #c55; }"));
    };
    connect(vision_, &QLineEdit::textChanged, this, validate);
    validate();
    visLay->addWidget(new QLabel("Vision overrides", visRow));
    visLay->addWidget(vision_, 1);
    visLay->addWidget(visState);
    top->addWidget(visRow);
    auto* note = new QLabel("Exposure (EV) is in Map Info. A film grade wins over the LUT. Not yet checked in game; the converter log shows "
                            "\"iw3: exponential fog\" / \"iw3: film\" when they are read.", this);
    note->setWordWrap(true);
    top->addWidget(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    top->addWidget(buttons);
}

Atmosphere AtmosphereDialog::value() const {
    Atmosphere a;
    if (fogBox_->isChecked()) {
        ExpFog f;
        f.start = fogStart_->value();
        f.halfway = fogHalf_->value();
        f.color = Vec3{fogRgb_[0]->value(), fogRgb_[1]->value(), fogRgb_[2]->value()};
        a.fog = f;
    }
    if (filmBox_->isChecked()) {
        FilmGrade f;
        f.contrast = contrast_->value();
        f.brightness = brightness_->value();
        f.desaturation = desat_->value();
        f.invert = invert_->isChecked();
        f.lightTint = Vec3{light_[0]->value(), light_[1]->value(), light_[2]->value()};
        f.darkTint = Vec3{dark_[0]->value(), dark_[1]->value(), dark_[2]->value()};
        a.film = f;
    }
    a.lut = lut_->text().trimmed().toStdString();
    a.vision = vision_->text().trimmed().toStdString();
    return a;
}

Atmosphere currentAtmosphere(const EditorContext& ctx) {
    for (const auto& e : ctx.doc.map().entities)
        if (e.isWorldspawn()) return readAtmosphere(e);
    return {};
}

bool applyAtmosphere(EditorContext& ctx, const Atmosphere& a) {
    Entity* w = ctx.doc.worldspawn();
    if (!w) return false;
    const Id wid = w->id;
    const std::string want[4] = {a.fog ? formatFog(*a.fog) : std::string(), a.film ? formatFilm(*a.film) : std::string(), a.lut,
                                a.vision};
    const char* keys[4] = {kFogKey, kFilmKey, kLutKey, kVisionKey};
    bool changed = false;
    for (int i = 0; i < 4; ++i) {
        const std::string* cur = w->value(keys[i]);
        changed |= want[i].empty() ? cur != nullptr : (!cur || *cur != want[i]);
    }
    if (!changed) return false;
    ctx.doc.beginGroup("Atmosphere");
    for (int i = 0; i < 4; ++i) {
        const std::string* cur = ctx.doc.findEntity(wid)->value(keys[i]);
        if (want[i].empty()) {
            if (cur) ctx.doc.removeKey(wid, keys[i]);
        } else if (!cur || *cur != want[i]) {
            ctx.doc.setKeyValue(wid, keys[i], want[i]);
        }
    }
    ctx.doc.endGroup();
    ctx.documentEdited("Atmosphere updated (fog / film written on the next build)");
    return true;
}

void editAtmosphere(EditorContext& ctx, QWidget* parent) {
    AtmosphereDialog dlg(currentAtmosphere(ctx), parent);
    if (dlg.exec() == QDialog::Accepted) applyAtmosphere(ctx, dlg.value());
}

}  // namespace iw8r::qt
