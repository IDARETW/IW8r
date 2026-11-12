#include "material_preview.h"
#include "material_preview_widget.h"
#include "material_editor.h"

#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

#include <cmath>

#include "iw8_contents.h"

namespace iw8r::qt {

namespace {

QImage loadAny(const QString& path) {
    if (path.isEmpty()) return {};
    QImage img(path);
    if (!img.isNull()) return img.convertToFormat(QImage::Format_RGBA8888);
    // DDS and other formats the stb decoder of the build reads.
    int w = 0, h = 0;
    std::vector<uint8_t> px;
    std::string err;
    if (!decodeImageFile(path.toStdString(), w, h, px, &err) || w <= 0 || h <= 0) return {};
    return QImage(px.data(), w, h, w * 4, QImage::Format_RGBA8888).copy();
}

}  // namespace

QImage MaterialEditorDialog::renderPreview(const QImage& color, const QImage& normal, const QImage& spec, bool alphaTest, bool blend, int size) {
    // Left: a sphere lit from the upper left (lambert + gloss highlight from the specular alpha); right: a flat wall
    // with the texture repeated twice. Checker behind, so cut-outs and blending show.
    QImage out(size * 2, size, QImage::Format_RGBA8888);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size * 2; ++x) {
            const bool on = ((x / 12) + (y / 12)) % 2 == 0;
            out.setPixelColor(x, y, on ? QColor(70, 72, 78) : QColor(50, 52, 58));
        }
    auto sample = [](const QImage& img, double u, double v, QColor fallback) {
        if (img.isNull()) return fallback;
        u -= std::floor(u), v -= std::floor(v);
        return img.pixelColor(std::clamp(int(u * img.width()), 0, img.width() - 1), std::clamp(int(v * img.height()), 0, img.height() - 1));
    };
    const double L[3] = {-0.5, -0.6, 0.62};
    const double ll = std::sqrt(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
    auto shade = [&](int px, int py, double nx, double ny, double nz, double u, double v) {
        QColor c = sample(color, u, v, QColor(160, 160, 160));
        double a = c.alphaF();
        if (alphaTest && a < 0.5) return;
        if (!blend) a = 1;
        // Normal map: tangent-space xy from red / green when present (the editor's slope maps too).
        if (!normal.isNull()) {
            const QColor n = sample(normal, u, v, QColor(128, 128, 255));
            nx += (n.redF() * 2 - 1) * 0.6;
            ny += (n.greenF() * 2 - 1) * 0.6;
            const double l = std::sqrt(nx * nx + ny * ny + nz * nz);
            nx /= l, ny /= l, nz /= l;
        }
        const double ndl = std::max(0.0, (nx * L[0] + ny * L[1] + nz * L[2]) / ll);
        const double gloss = spec.isNull() ? 0.2 : sample(spec, u, v, QColor(0, 0, 0, 51)).alphaF();
        // Half vector with the eye at +z.
        double hx = L[0] / ll, hy = L[1] / ll, hz = L[2] / ll + 1;
        const double hl = std::sqrt(hx * hx + hy * hy + hz * hz);
        const double ndh = std::max(0.0, (nx * hx + ny * hy + nz * hz) / hl);
        const double sp = std::pow(ndh, 8 + 120 * gloss) * gloss * 0.6;
        const double k = 0.25 + 0.85 * ndl;
        const QColor bg = out.pixelColor(px, py);
        auto ch = [&](double base, double back) { return std::clamp((base * k + sp) * a + back * (1 - a), 0.0, 1.0); };
        out.setPixelColor(px, py, QColor::fromRgbF(float(ch(c.redF(), bg.redF())), float(ch(c.greenF(), bg.greenF())), float(ch(c.blueF(), bg.blueF()))));
    };
    const double r = size * 0.45, cx = size * 0.5, cy = size * 0.5;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const double dx = (x - cx) / r, dy = (y - cy) / r;
            const double d2 = dx * dx + dy * dy;
            if (d2 > 1) continue;
            const double dz = std::sqrt(1 - d2);
            const double u = 0.5 + std::atan2(dx, dz) / (2 * 3.14159265358979) * 2, v = 0.5 + std::asin(dy) / 3.14159265358979;
            shade(x, y, dx, dy, dz, u, v);
        }
    for (int y = int(size * 0.1); y < int(size * 0.9); ++y)
        for (int x = size + int(size * 0.1); x < size * 2 - int(size * 0.1); ++x) {
            const double u = double(x - size - size * 0.1) / (size * 0.8) * 2, v = double(y - size * 0.1) / (size * 0.8) * 2;
            shade(x, y, 0, 0, 1, u, v);
        }
    return out;
}

MaterialEditorDialog::MaterialEditorDialog(const QString& libraryRoot, const CustomMaterial* existing, QWidget* parent)
    : QDialog(parent), root_(libraryRoot) {
    setObjectName("materialEditorDialog");
    setWindowTitle(existing ? "Edit Custom Material" : "New Custom Material");
    auto* outer = new QHBoxLayout(this);
    auto* form = new QFormLayout();
    name_ = new QLineEdit(this);
    name_->setPlaceholderText("e.g. custom/brick_red (lower case, a-z 0-9 _ /)");
    form->addRow("Name", name_);
    const char* labels[3] = {"Colour image", "Normal map", "Specular (gloss in alpha)"};
    for (int i = 0; i < 3; ++i) {
        auto* row = new QHBoxLayout();
        images_[i] = new QLineEdit(this);
        auto* pick = new QPushButton("...", this);
        pick->setMaximumWidth(30);
        row->addWidget(images_[i], 1);
        row->addWidget(pick);
        form->addRow(labels[i], row);
        connect(pick, &QPushButton::clicked, this, [this, i] {
            const QString f = QFileDialog::getOpenFileName(this, "Image", QFileInfo(images_[i]->text()).absolutePath(),
                                                           "Images (*.png *.tga *.jpg *.jpeg *.bmp *.dds);;All files (*)");
            if (!f.isEmpty()) setImage(i, f);
        });
        connect(images_[i], &QLineEdit::editingFinished, this, [this] { refreshPreview(); });
    }
    surface_ = new QComboBox(this);
    for (int t = 0; t <= 28; ++t) surface_->addItem(QString::fromLatin1(iw3SurfaceTypeName(t)));
    form->addRow("Surface type (footsteps, impacts)", surface_);
    auto* feedback = new QLabel(this);
    feedback->setStyleSheet("color:#9aa");
    form->addRow("", feedback);
    auto showFeedback = [feedback](int t) {
        const SurfaceFeedback fb = surfaceFeedback(t);
        feedback->setText(QString("impacts: _impact/%1/%2").arg(fb.impactFamily).arg(*fb.scrapeAlias ? QString("; scrape: ") + fb.scrapeAlias : QString()));
    };
    connect(surface_, &QComboBox::currentIndexChanged, this, showFeedback);
    showFeedback(0);
    alphaTest_ = new QCheckBox("Alpha test (cut-outs: foliage, fences)", this);
    blend_ = new QCheckBox("Blend (decals, glass: alpha is coverage)", this);
    twoSided_ = new QCheckBox("Two-sided", this);
    form->addRow(alphaTest_);
    form->addRow(blend_);
    form->addRow(twoSided_);
    // Shader state (12.2). Presets set the controls below; every control can then be changed on its own.
    {
        auto* preset = new QComboBox(this);
        preset->addItems({"Shader preset...", "Opaque", "Cut-out (foliage, fences)", "Blended (glass, decals)", "Additive glow (light shafts, screens)",
                          "Multiply (stains, shadows)", "Decal on a surface", "Unlit sign", "Emissive (glows)"});
        form->addRow("Preset", preset);
        blendMode_ = new QComboBox(this);
        blendMode_->addItems({"From the boxes above", "Additive (adds light)", "Multiply (darkens)"});
        form->addRow("Blend mode", blendMode_);
        unlit_ = new QCheckBox("Unlit (takes no light)", this);
        form->addRow(unlit_);
        emission_ = new QDoubleSpinBox(this);
        emission_->setRange(0, 64);
        emission_->setSingleStep(0.5);
        emission_->setToolTip("Above 1 the material glows by that factor; 0 or 1 = no glow");
        form->addRow("Emission", emission_);
        auto* tintRow = new QHBoxLayout();
        for (int i = 0; i < 3; ++i) {
            tint_[i] = new QDoubleSpinBox(this);
            tint_[i]->setRange(0, 4);
            tint_[i]->setSingleStep(0.05);
            tint_[i]->setValue(1);
            tint_[i]->setPrefix(i == 0 ? "R " : i == 1 ? "G " : "B ");
            tintRow->addWidget(tint_[i]);
        }
        form->addRow("Tint (multiplies the colour)", tintRow);
        sortKey_ = new QSpinBox(this);
        sortKey_->setRange(0, 40);
        sortKey_->setSpecialValueText("default");
        sortKey_->setToolTip("9 to 40 layers decals on the same surface: the higher number draws on top");
        form->addRow("Decal sort order", sortKey_);
        polygonOffset_ = new QComboBox(this);
        polygonOffset_->addItems({"None", "Small", "Large"});
        polygonOffset_->setToolTip("Pulls a layer that lies flat on another surface off it, so the two do not flicker");
        form->addRow("Depth offset", polygonOffset_);
        connect(preset, &QComboBox::activated, this, [this, preset](int i) {
            if (i <= 0) return;
            alphaTest_->setChecked(i == 2);
            blend_->setChecked(i == 3 || i == 6);
            blendMode_->setCurrentIndex(i == 4 ? 1 : i == 5 ? 2 : 0);
            unlit_->setChecked(i == 4 || i == 7 || i == 8);
            emission_->setValue(i == 8 ? 4 : 0);
            sortKey_->setValue(i == 6 ? 12 : 0);
            polygonOffset_->setCurrentIndex(i == 6 ? 1 : 0);
            twoSided_->setChecked(i == 2 || i == 3 || i == 4);
            preset->setCurrentIndex(0);
            refreshPreview();
        });
    }
    width_ = new QDoubleSpinBox(this);
    height_ = new QDoubleSpinBox(this);
    for (QDoubleSpinBox* s : {width_, height_}) {
        s->setRange(1, 65536);
        s->setValue(128);
        s->setDecimals(1);
    }
    auto* sz = new QHBoxLayout();
    sz->addWidget(width_);
    sz->addWidget(new QLabel("x", this));
    sz->addWidget(height_);
    form->addRow("Texture size (world units)", sz);
    for (QCheckBox* c : {alphaTest_, blend_}) connect(c, &QCheckBox::toggled, this, [this] { refreshPreview(); });
    connect(alphaTest_, &QCheckBox::toggled, this, [this](bool on) { if (on) blend_->setChecked(false); });
    connect(blend_, &QCheckBox::toggled, this, [this](bool on) { if (on) alphaTest_->setChecked(false); });
    auto* box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, [this] {
        QString err;
        if (!save(&err).isEmpty()) accept();
        else previewLabel_->setText(err);
    });
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(box);
    outer->addLayout(form, 1);
    // Right column: the live shader preview (12.12, owner request 2026-11-11) above the flat texture preview.
    auto* right = new QVBoxLayout();
    live_ = new MaterialPreviewWidget(this);
    live_->setObjectName("materialLivePreview");
    right->addWidget(live_);
    auto* liveNote = new QLabel("Live preview: the editor's own renderer with the game's lighting terms (sun, ambient, specular from the "
                                "specular map). Not shown: shadows, reflections from probes, fog and the colour grade.", this);
    liveNote->setWordWrap(true);
    liveNote->setStyleSheet("color:#9aa");
    right->addWidget(liveNote);
    previewLabel_ = new QLabel(this);
    previewLabel_->setMinimumSize(400, 200);
    previewLabel_->setAlignment(Qt::AlignCenter);
    right->addWidget(previewLabel_);
    outer->addLayout(right);
    // Every shader control redraws the previews at once.
    connect(twoSided_, &QCheckBox::toggled, this, [this] { refreshPreview(); });
    connect(unlit_, &QCheckBox::toggled, this, [this] { refreshPreview(); });
    connect(blendMode_, &QComboBox::currentIndexChanged, this, [this] { refreshPreview(); });
    connect(emission_, &QDoubleSpinBox::valueChanged, this, [this] { refreshPreview(); });
    for (QDoubleSpinBox* t : tint_) connect(t, &QDoubleSpinBox::valueChanged, this, [this] { refreshPreview(); });
    if (existing) {
        name_->setText(QString::fromStdString(existing->name));
        name_->setReadOnly(true);
        images_[0]->setText(QString::fromStdString(existing->colorImage));
        images_[1]->setText(QString::fromStdString(existing->normalImage));
        images_[2]->setText(QString::fromStdString(existing->specImage));
        surface_->setCurrentIndex(std::clamp(existing->surfaceType, 0, 28));
        alphaTest_->setChecked(existing->alphaTest);
        blend_->setChecked(existing->blend);
        twoSided_->setChecked(existing->twoSided);
        blendMode_->setCurrentIndex(std::clamp(existing->blendMode, 0, 2));
        unlit_->setChecked(existing->unlit);
        emission_->setValue(existing->emission);
        for (int i = 0; i < 3; ++i) tint_[i]->setValue(existing->tint[i]);
        sortKey_->setValue(existing->sortKey);
        polygonOffset_->setCurrentIndex(std::clamp(existing->polygonOffset, 0, 2));
        width_->setValue(existing->width);
        height_->setValue(existing->height);
    }
    refreshPreview();
}

void MaterialEditorDialog::setName(const QString& n) { name_->setText(n); }
void MaterialEditorDialog::setImage(int slot, const QString& path) {
    if (slot < 0 || slot > 2) return;
    images_[slot]->setText(path);
    refreshPreview();
}
void MaterialEditorDialog::setFlags(bool alphaTest, bool blend, bool twoSided) {
    alphaTest_->setChecked(alphaTest);
    blend_->setChecked(blend && !alphaTest);
    twoSided_->setChecked(twoSided);
}
void MaterialEditorDialog::setSurface(const QString& name) {
    const int i = surface_->findText(name);
    if (i >= 0) surface_->setCurrentIndex(i);
}
void MaterialEditorDialog::setShaderState(int blendMode, bool unlit, double emission, double tintR, double tintG, double tintB, int sortKey,
                                          int polygonOffset) {
    blendMode_->setCurrentIndex(std::clamp(blendMode, 0, 2));
    unlit_->setChecked(unlit);
    emission_->setValue(emission);
    tint_[0]->setValue(tintR);
    tint_[1]->setValue(tintG);
    tint_[2]->setValue(tintB);
    sortKey_->setValue(sortKey);
    polygonOffset_->setCurrentIndex(std::clamp(polygonOffset, 0, 2));
}
QImage MaterialEditorDialog::livePreview() const { return live_ ? live_->image() : QImage(); }
void MaterialEditorDialog::setScale(double w, double h) {
    width_->setValue(w);
    height_->setValue(h);
}

void MaterialEditorDialog::refreshPreview() {
    previewImage_ = renderPreview(loadAny(images_[0]->text()), loadAny(images_[1]->text()), loadAny(images_[2]->text()),
                                  alphaTest_->isChecked(), blend_->isChecked(), 200);
    previewLabel_->setPixmap(QPixmap::fromImage(previewImage_));
    if (live_) {
        MaterialPreviewInput in;
        in.color = loadPreviewImage(images_[0]->text().trimmed());
        in.normal = loadPreviewImage(images_[1]->text().trimmed());
        in.specular = loadPreviewImage(images_[2]->text().trimmed());
        in.alphaTest = alphaTest_->isChecked();
        in.blend = blend_->isChecked();
        in.twoSided = twoSided_->isChecked();
        in.unlit = unlit_ && unlit_->isChecked();
        in.blendMode = blendMode_ ? blendMode_->currentIndex() : 0;
        in.emission = emission_ ? emission_->value() : 0;
        for (int i = 0; i < 3; ++i) in.tint[i] = tint_[i] ? tint_[i]->value() : 1.0;
        live_->setMaterial(in);
    }
}

QString MaterialEditorDialog::jsonText(QString* err) const {
    const QString n = name_->text().trimmed();
    if (!isValidCustomMaterialName(n.toStdString())) {
        if (err) *err = "the name must be lower case a-z 0-9 _ / (e.g. custom/brick_red)";
        return {};
    }
    if (images_[0]->text().trimmed().isEmpty()) {
        if (err) *err = "a colour image is required";
        return {};
    }
    const QString stem = QString(n).replace('/', "__");
    QJsonObject o;
    o["name"] = n;
    const char* keys[3] = {"color", "normal", "specular"};
    const char* suffix[3] = {"_c", "_n", "_s"};
    for (int i = 0; i < 3; ++i) {
        const QString src = images_[i]->text().trimmed();
        if (src.isEmpty()) continue;
        // Inside the library folder: relative; outside: the copy save() makes next to the definition.
        const QString rel = QDir(root_).relativeFilePath(src);
        o[keys[i]] = rel.startsWith("..") || QFileInfo(rel).isAbsolute() ? stem + suffix[i] + "." + QFileInfo(src).suffix().toLower() : rel;
    }
    if (surface_->currentIndex() > 0) o["surface"] = surface_->currentText();
    if (alphaTest_->isChecked()) o["alphaTest"] = true;
    if (blend_->isChecked()) o["blend"] = true;
    if (twoSided_->isChecked()) o["twoSided"] = true;
    if (blendMode_->currentIndex() == 1) o["blendMode"] = "add";
    if (blendMode_->currentIndex() == 2) o["blendMode"] = "multiply";
    if (unlit_->isChecked()) o["unlit"] = true;
    if (emission_->value() > 1) o["emission"] = emission_->value();
    if (tint_[0]->value() != 1 || tint_[1]->value() != 1 || tint_[2]->value() != 1)
        o["tint"] = QJsonArray{tint_[0]->value(), tint_[1]->value(), tint_[2]->value()};
    if (sortKey_->value() > 0) o["sortKey"] = sortKey_->value();
    if (polygonOffset_->currentIndex() > 0) o["polygonOffset"] = polygonOffset_->currentIndex();
    if (width_->value() != 128 || height_->value() != 128) o["scale"] = QJsonArray{width_->value(), height_->value()};
    return QString::fromUtf8(QJsonDocument(o).toJson());
}

QString MaterialEditorDialog::save(QString* err) {
    const QString json = jsonText(err);
    if (json.isEmpty()) return {};
    QDir().mkpath(root_);
    const QJsonObject o = QJsonDocument::fromJson(json.toUtf8()).object();
    const char* keys[3] = {"color", "normal", "specular"};
    for (int i = 0; i < 3; ++i) {
        const QString src = images_[i]->text().trimmed();
        if (src.isEmpty() || !o.contains(keys[i])) continue;
        const QString dst = QDir(root_).filePath(o[keys[i]].toString());
        if (QFileInfo(dst).absoluteFilePath() == QFileInfo(src).absoluteFilePath()) continue;
        QDir().mkpath(QFileInfo(dst).absolutePath());
        QFile::remove(dst);
        if (!QFile::copy(src, dst)) {
            if (err) *err = "cannot copy " + src + " to " + dst;
            return {};
        }
    }
    const QString stem = QString(name_->text().trimmed()).replace('/', "__");
    const QString path = QDir(root_).filePath(stem + ".iw8mat.json");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(json.toUtf8()) < 0) {
        if (err) *err = "cannot write " + path;
        return {};
    }
    saved_ = path;
    return path;
}

}  // namespace iw8r::qt
