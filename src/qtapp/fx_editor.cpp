#include "fx_editor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QRadialGradient>
#include <QHeaderView>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace iw8r::qt {

// Side view (X right, Z up) of the effect, played in a loop; 1 unit = 2 px around the origin near the bottom.
class FxPreviewWidget : public QWidget {
public:
    explicit FxPreviewWidget(const AuthoredFx* fx, QWidget* parent) : QWidget(parent), fx_(fx) {
        setMinimumSize(320, 260);
        clock_.start();
        auto* t = new QTimer(this);
        connect(t, &QTimer::timeout, this, [this] { update(); });
        t->start(33);
    }
    int lastCount = 0;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(24, 26, 31));
        p.setRenderHint(QPainter::Antialiasing);
        const double t = std::fmod(clock_.elapsed() / 1000.0, 6.0);
        const auto parts = simulateAuthoredFx(*fx_, t, 3000);
        lastCount = int(parts.size());
        const QPointF origin(width() * 0.5, height() * 0.85);
        const double scale = 2.0;
        p.setPen(QColor(70, 74, 82));
        p.drawLine(QPointF(0, origin.y()), QPointF(width(), origin.y()));
        for (const auto& q : parts) {
            if (q.element < fx_->elements.size() && fx_->elements[q.element].kind == "light") {
                // An omni light: a faint glow of its radius around the effect origin.
                p.setCompositionMode(QPainter::CompositionMode_Plus);
                const double r = std::max(2.0, q.size * scale);
                QColor col = QColor::fromRgbF(std::clamp(q.color[0], 0.0, 1.0), std::clamp(q.color[1], 0.0, 1.0),
                                              std::clamp(q.color[2], 0.0, 1.0), 0.35 * std::clamp(q.color[3], 0.0, 1.0));
                QRadialGradient g(origin, r);
                g.setColorAt(0, col);
                col.setAlphaF(0);
                g.setColorAt(1, col);
                p.setPen(Qt::NoPen);
                p.setBrush(g);
                p.drawEllipse(origin, r, r);
                continue;
            }
            const bool add = q.element < fx_->elements.size() && fx_->elements[q.element].additive;
            p.setCompositionMode(add ? QPainter::CompositionMode_Plus : QPainter::CompositionMode_SourceOver);
            const QPointF c(origin.x() + q.pos[0] * scale, origin.y() - q.pos[2] * scale);
            const double r = std::max(1.0, q.size * scale * 0.5);
            QColor col = QColor::fromRgbF(std::clamp(q.color[0], 0.0, 1.0), std::clamp(q.color[1], 0.0, 1.0),
                                          std::clamp(q.color[2], 0.0, 1.0), std::clamp(q.color[3], 0.0, 1.0));
            QRadialGradient g(c, r);
            g.setColorAt(0, col);
            col.setAlphaF(0);
            g.setColorAt(1, col);
            p.setPen(Qt::NoPen);
            p.setBrush(g);
            p.drawEllipse(c, r, r);
        }
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        p.setPen(QColor(150, 150, 150));
        p.drawText(8, 16, QString("%1 particles, t = %2 s (side view, 1 unit = 2 px)").arg(parts.size()).arg(t, 0, 'f', 1));
    }

private:
    const AuthoredFx* fx_;
    QElapsedTimer clock_;
};

namespace {
QDoubleSpinBox* dspin(QWidget* parent, double lo, double hi, double step, int decimals = 2) {
    auto* s = new QDoubleSpinBox(parent);
    s->setRange(lo, hi);
    s->setSingleStep(step);
    s->setDecimals(decimals);
    return s;
}
QSpinBox* ispin(QWidget* parent, int lo, int hi, int step) {
    auto* s = new QSpinBox(parent);
    s->setRange(lo, hi);
    s->setSingleStep(step);
    return s;
}
}  // namespace

FxEditorDialog::FxEditorDialog(const QString& fxFolder, std::function<void(const QString&)> place, QWidget* parent)
    : QDialog(parent), folder_(QFileInfo(fxFolder).absoluteFilePath()), place_(std::move(place)) {
    setObjectName("fxEditorDialog");
    setWindowTitle("Effect Editor");
    auto* top = new QHBoxLayout(this);
    // Effects
    auto* left = new QVBoxLayout;
    left->addWidget(new QLabel("Effects (" + QDir::toNativeSeparators(folder_) + ")", this));
    effects_ = new QListWidget(this);
    left->addWidget(effects_);
    auto* newB = new QPushButton("New...", this);
    auto* saveB = new QPushButton("Save", this);
    auto* placeB = new QPushButton("Place in Map", this);
    placeB->setToolTip("An fx_origin with this effect at the view centre (the build makes it a Replay vfx)");
    left->addWidget(newB);
    left->addWidget(saveB);
    left->addWidget(placeB);
    top->addLayout(left, 1);
    // Elements + form
    auto* mid = new QVBoxLayout;
    mid->addWidget(new QLabel("Elements", this));
    elements_ = new QListWidget(this);
    elements_->setMaximumHeight(110);
    mid->addWidget(elements_);
    auto* er = new QHBoxLayout;
    auto* addE = new QPushButton("Add", this);
    auto* delE = new QPushButton("Remove", this);
    er->addWidget(addE);
    er->addWidget(delE);
    mid->addLayout(er);
    auto* form = new QFormLayout;
    elemName_ = new QLineEdit(this);
    form->addRow("Name", elemName_);
    kind_ = new QComboBox(this);
    kind_->setObjectName("fxElementKind");
    kind_->addItem("Sprite (particles with the image)", "sprite");
    kind_->addItem("Light (omni: size = radius, colour curve)", "light");
    kind_->addItem("Model (a custom model; size = scale)", "model");
    form->addRow("Kind", kind_);
    model_ = new QLineEdit(this);
    model_->setObjectName("fxElementModel");
    model_->setPlaceholderText("custom model name (models folder beside the map)");
    form->addRow("Model", model_);
    auto* imgRow = new QHBoxLayout;
    image_ = new QLineEdit(this);
    image_->setPlaceholderText("(soft round particle)");
    auto* browse = new QPushButton("...", this);
    browse->setMaximumWidth(30);
    imgRow->addWidget(image_);
    imgRow->addWidget(browse);
    form->addRow("Image", imgRow);
    additive_ = new QCheckBox("Additive (glow)", this);
    looping_ = new QCheckBox("Looping", this);
    auto* flags = new QHBoxLayout;
    flags->addWidget(additive_);
    flags->addWidget(looping_);
    form->addRow(flags);
    interval_ = ispin(this, 1, 600000, 10);
    interval_->setSuffix(" ms");
    count_ = ispin(this, 1, 1000, 1);
    life_ = ispin(this, 1, 600000, 50);
    life_->setSuffix(" ms");
    lifeVar_ = ispin(this, 0, 600000, 50);
    lifeVar_->setSuffix(" ms");
    form->addRow("Spawn every", interval_);
    form->addRow("Count", count_);
    form->addRow("Life", life_);
    form->addRow("Life + random", lifeVar_);
    radius_ = dspin(this, 0, 100000, 1);
    form->addRow("Spawn radius", radius_);
    auto* vmin = new QHBoxLayout;
    auto* vmax = new QHBoxLayout;
    for (int a = 0; a < 3; ++a) {
        velMin_[a] = dspin(this, -100000, 100000, 5, 1);
        velMax_[a] = dspin(this, -100000, 100000, 5, 1);
        vmin->addWidget(velMin_[a]);
        vmax->addWidget(velMax_[a]);
    }
    form->addRow("Velocity min (x y z /s)", vmin);
    form->addRow("Velocity max", vmax);
    gravity_ = dspin(this, -100, 100, 0.05);
    form->addRow("Gravity (fraction)", gravity_);
    sizeStart_ = dspin(this, 0, 100000, 1);
    sizeEnd_ = dspin(this, 0, 100000, 1);
    form->addRow("Size start", sizeStart_);
    form->addRow("Size end", sizeEnd_);
    auto* cs = new QHBoxLayout;
    auto* ce = new QHBoxLayout;
    for (int c = 0; c < 4; ++c) {
        colStart_[c] = dspin(this, 0, 1, 0.05);
        colEnd_[c] = dspin(this, 0, 1, 0.05);
        cs->addWidget(colStart_[c]);
        ce->addWidget(colEnd_[c]);
    }
    form->addRow("Colour start (r g b a)", cs);
    form->addRow("Colour end", ce);
    spin_ = dspin(this, -1000, 1000, 0.1);
    form->addRow("Spin (rad/s, preview)", spin_);
    mid->addLayout(form);
    // Curve keys (multi-key size / colour over the life); empty uses the start / end values above.
    mid->addWidget(new QLabel("Curve keys over the life (t 0..1, size, r g b a); empty = start -> end above", this));
    keys_ = new QTableWidget(0, 6, this);
    keys_->setObjectName("fxCurveKeys");
    keys_->setHorizontalHeaderLabels({"t", "size", "r", "g", "b", "a"});
    keys_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    keys_->verticalHeader()->hide();
    keys_->setMaximumHeight(130);
    mid->addWidget(keys_);
    auto* kr = new QHBoxLayout;
    auto* addK = new QPushButton("Add Key", this);
    auto* delK = new QPushButton("Remove Key", this);
    kr->addWidget(addK);
    kr->addWidget(delK);
    mid->addLayout(kr);
    connect(addK, &QPushButton::clicked, this, [this] {
        if (element_ < 0 || element_ >= int(fx_.elements.size())) return;
        auto& e = fx_.elements[size_t(element_)];
        if (e.keys.empty()) {
            // Start from the current start / end so the curve looks the same until a key is changed.
            addKeyRow(0, e.sizeStart, e.colorStart[0], e.colorStart[1], e.colorStart[2], e.colorStart[3]);
            addKeyRow(1, e.sizeEnd, e.colorEnd[0], e.colorEnd[1], e.colorEnd[2], e.colorEnd[3]);
        }
        const AuthoredFxKey m = sampleAuthoredFxKey(e, 0.5);
        addKeyRow(0.5, m.size, m.color[0], m.color[1], m.color[2], m.color[3]);
        readKeys();
    });
    connect(delK, &QPushButton::clicked, this, [this] {
        const int r = keys_->currentRow();
        if (r < 0) return;
        keys_->removeRow(r);
        if (keys_->rowCount() == 1) keys_->setRowCount(0);  // one key is not a curve: back to start -> end
        readKeys();
    });
    connect(keys_, &QTableWidget::cellChanged, this, [this] { readKeys(); });
    top->addLayout(mid, 2);
    // Preview
    auto* right = new QVBoxLayout;
    preview_ = new FxPreviewWidget(&fx_, this);
    right->addWidget(preview_, 1);
    status_ = new QLabel(this);
    status_->setWordWrap(true);
    right->addWidget(status_);
    top->addLayout(right, 2);

    connect(effects_, &QListWidget::currentTextChanged, this, [this](const QString& n) {
        if (!n.isEmpty() && n != current_) openEffect(n);
    });
    connect(elements_, &QListWidget::currentRowChanged, this, [this](int r) { showElement(r); });
    connect(newB, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString n = QInputDialog::getText(this, "New Effect", "Effect name (letters, digits, _ and /):", QLineEdit::Normal,
                                                "iw8r/my_effect", &ok);
        if (ok && !n.isEmpty() && !newEffect(n)) status_->setText("Not a valid effect name: " + n);
    });
    connect(saveB, &QPushButton::clicked, this, [this] {
        status_->setText(save() ? "Saved " + QDir::toNativeSeparators(fileFor(current_)) : "Nothing to save (open or create an effect)");
    });
    connect(placeB, &QPushButton::clicked, this, [this] {
        if (current_.isEmpty()) return;
        save();
        if (place_) place_(current_);
        status_->setText("Placed an fx_origin with " + current_ + " (compile to build it as a Replay vfx)");
    });
    connect(addE, &QPushButton::clicked, this, [this] {
        if (current_.isEmpty()) return;
        AuthoredFxElement e;
        e.name = "element" + QString::number(fx_.elements.size() + 1).toStdString();
        fx_.elements.push_back(e);
        fillElements();
        elements_->setCurrentRow(int(fx_.elements.size()) - 1);
    });
    connect(delE, &QPushButton::clicked, this, [this] {
        if (element_ < 0 || element_ >= int(fx_.elements.size()) || fx_.elements.size() <= 1) return;
        fx_.elements.erase(fx_.elements.begin() + element_);
        fillElements();
        elements_->setCurrentRow(0);
    });
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, "Particle Image", folder_, "Images (*.png *.tga *.jpg *.bmp)");
        if (f.isEmpty()) return;
        // Kept beside the effect: copied into the fx folder when it is elsewhere.
        QString rel = QDir(QFileInfo(fileFor(current_)).absolutePath()).relativeFilePath(f);
        if (rel.startsWith("..")) {
            const QString dest = QFileInfo(fileFor(current_)).absolutePath() + "/" + QFileInfo(f).fileName();
            QFile::copy(f, dest);
            rel = QFileInfo(f).fileName();
        }
        image_->setText(rel);
        readForm();
    });
    // Any form change updates the element (and the preview, which reads fx_ every frame).
    const auto hook = [this](auto* w, auto signal) { connect(w, signal, this, [this] { readForm(); }); };
    hook(elemName_, &QLineEdit::editingFinished);
    hook(kind_, &QComboBox::currentIndexChanged);
    hook(model_, &QLineEdit::editingFinished);
    hook(image_, &QLineEdit::editingFinished);
    hook(additive_, &QCheckBox::toggled);
    hook(looping_, &QCheckBox::toggled);
    for (QSpinBox* s : {interval_, count_, life_, lifeVar_}) hook(s, &QSpinBox::valueChanged);
    for (QDoubleSpinBox* s : {radius_, gravity_, sizeStart_, sizeEnd_, spin_}) hook(s, &QDoubleSpinBox::valueChanged);
    for (int a = 0; a < 3; ++a) hook(velMin_[a], &QDoubleSpinBox::valueChanged), hook(velMax_[a], &QDoubleSpinBox::valueChanged);
    for (int c = 0; c < 4; ++c) hook(colStart_[c], &QDoubleSpinBox::valueChanged), hook(colEnd_[c], &QDoubleSpinBox::valueChanged);
    resize(1180, 640);
    refreshList();
    if (effects_->count()) effects_->setCurrentRow(0);
}

QString FxEditorDialog::fileFor(const QString& name) const { return folder_ + "/" + name + ".iw8fx.json"; }

QStringList FxEditorDialog::effectNames() const {
    QStringList out;
    QDirIterator it(folder_, {"*.iw8fx.json"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString rel = QDir(folder_).relativeFilePath(it.next());
        rel.chop(int(QString(".iw8fx.json").size()));
        out << rel;
    }
    out.sort();
    return out;
}

void FxEditorDialog::refreshList() {
    const QString keep = current_;
    effects_->blockSignals(true);
    effects_->clear();
    effects_->addItems(effectNames());
    for (int i = 0; i < effects_->count(); ++i)
        if (effects_->item(i)->text() == keep) effects_->setCurrentRow(i);
    effects_->blockSignals(false);
}

bool FxEditorDialog::openEffect(const QString& name) {
    QFile f(fileFor(name));
    if (!f.open(QIODevice::ReadOnly)) return false;
    AuthoredFx fx;
    std::string err;
    if (!parseAuthoredFx(f.readAll().toStdString(), fx, &err)) {
        status_->setText(name + ": " + QString::fromStdString(err));
        return false;
    }
    fx.name = name.toStdString();
    fx_ = std::move(fx);
    current_ = name;
    fillElements();
    elements_->setCurrentRow(0);
    status_->setText("Opened " + name);
    return true;
}

bool FxEditorDialog::newEffect(const QString& name) {
    if (!isValidAuthoredFxName(name.toStdString())) return false;
    AuthoredFx fx;
    fx.name = name.toStdString();
    AuthoredFxElement smoke;
    smoke.name = "smoke";
    smoke.intervalMs = 150;
    smoke.lifeMs = 2500;
    smoke.lifeVarMs = 500;
    smoke.radius = 6;
    smoke.velocityMin = {-6, -6, 30};
    smoke.velocityMax = {6, 6, 60};
    smoke.sizeStart = 10;
    smoke.sizeEnd = 40;
    smoke.colorStart = {0.6, 0.6, 0.6, 0.7};
    smoke.colorEnd = {0.4, 0.4, 0.4, 0};
    fx.elements.push_back(smoke);
    fx_ = fx;
    current_ = name;
    if (!save()) return false;
    refreshList();
    fillElements();
    elements_->setCurrentRow(0);
    return true;
}

bool FxEditorDialog::save() {
    if (current_.isEmpty()) return false;  // the form writes into fx_ as it changes (readForm)
    const QString path = fileFor(current_);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    fx_.name = current_.toStdString();
    f.write(QByteArray::fromStdString(authoredFxJson(fx_)));
    return true;
}

int FxEditorDialog::keyRows() const { return keys_->rowCount(); }

void FxEditorDialog::addKeyRow(double t, double size, double r, double g, double b, double a) {
    const bool was = keys_->blockSignals(true);
    const int row = keys_->rowCount();
    keys_->insertRow(row);
    const double v[6] = {t, size, r, g, b, a};
    for (int c = 0; c < 6; ++c) keys_->setItem(row, c, new QTableWidgetItem(QString::number(v[c], 'g', 4)));
    keys_->blockSignals(was);
}

void FxEditorDialog::readKeys() {
    if (loading_ || element_ < 0 || element_ >= int(fx_.elements.size())) return;
    std::vector<AuthoredFxKey> keys;
    for (int r = 0; r < keys_->rowCount(); ++r) {
        double v[6] = {0, 0, 0, 0, 0, 0};
        for (int c = 0; c < 6; ++c)
            if (QTableWidgetItem* it = keys_->item(r, c)) v[c] = it->text().toDouble();
        AuthoredFxKey k;
        k.t = std::clamp(v[0], 0.0, 1.0);
        k.size = std::max(0.0, v[1]);
        for (int c = 0; c < 4; ++c) k.color[size_t(c)] = std::clamp(v[2 + c], 0.0, 1.0);
        keys.push_back(k);
    }
    std::stable_sort(keys.begin(), keys.end(), [](const AuthoredFxKey& a, const AuthoredFxKey& b) { return a.t < b.t; });
    if (keys.size() > 16) keys.resize(16);
    fx_.elements[size_t(element_)].keys = keys.size() >= 2 ? keys : std::vector<AuthoredFxKey>{};
}

int FxEditorDialog::previewParticles() const { return int(simulateAuthoredFx(fx_, 1.5, 3000).size()); }

void FxEditorDialog::fillElements() {
    elements_->blockSignals(true);
    elements_->clear();
    for (const auto& e : fx_.elements) elements_->addItem(QString::fromStdString(e.name) + (e.additive ? "  (glow)" : ""));
    elements_->blockSignals(false);
    element_ = -1;
}

void FxEditorDialog::showElement(int index) {
    element_ = index;
    if (index < 0 || index >= int(fx_.elements.size())) return;
    loading_ = true;
    const auto& e = fx_.elements[size_t(index)];
    elemName_->setText(QString::fromStdString(e.name));
    kind_->setCurrentIndex(e.kind == "light" ? 1 : e.kind == "model" ? 2 : 0);
    model_->setText(QString::fromStdString(e.model));
    model_->setEnabled(e.kind == "model");
    image_->setText(QString::fromStdString(e.image));
    additive_->setChecked(e.additive);
    looping_->setChecked(e.looping);
    interval_->setValue(e.intervalMs);
    interval_->setEnabled(e.looping);
    count_->setValue(e.count);
    life_->setValue(e.lifeMs);
    lifeVar_->setValue(e.lifeVarMs);
    radius_->setValue(e.radius);
    for (int a = 0; a < 3; ++a) velMin_[a]->setValue(e.velocityMin[size_t(a)]), velMax_[a]->setValue(e.velocityMax[size_t(a)]);
    gravity_->setValue(e.gravity);
    sizeStart_->setValue(e.sizeStart);
    sizeEnd_->setValue(e.sizeEnd);
    for (int c = 0; c < 4; ++c) colStart_[c]->setValue(e.colorStart[size_t(c)]), colEnd_[c]->setValue(e.colorEnd[size_t(c)]);
    spin_->setValue(e.spin);
    keys_->blockSignals(true);
    keys_->setRowCount(0);
    for (const auto& k : e.keys) addKeyRow(k.t, k.size, k.color[0], k.color[1], k.color[2], k.color[3]);
    keys_->blockSignals(false);
    loading_ = false;
}

void FxEditorDialog::readForm() {
    if (loading_ || element_ < 0 || element_ >= int(fx_.elements.size())) return;
    auto& e = fx_.elements[size_t(element_)];
    e.name = elemName_->text().toStdString();
    e.kind = kind_->currentData().toString().toStdString();
    e.model = model_->text().trimmed().toStdString();
    model_->setEnabled(e.kind == "model");
    e.image = image_->text().toStdString();
    e.additive = additive_->isChecked();
    e.looping = looping_->isChecked();
    interval_->setEnabled(e.looping);
    e.intervalMs = interval_->value();
    e.count = count_->value();
    e.lifeMs = life_->value();
    e.lifeVarMs = lifeVar_->value();
    e.radius = radius_->value();
    for (int a = 0; a < 3; ++a) e.velocityMin[size_t(a)] = velMin_[a]->value(), e.velocityMax[size_t(a)] = velMax_[a]->value();
    e.gravity = gravity_->value();
    e.sizeStart = sizeStart_->value();
    e.sizeEnd = sizeEnd_->value();
    for (int c = 0; c < 4; ++c) e.colorStart[size_t(c)] = colStart_[c]->value(), e.colorEnd[size_t(c)] = colEnd_[c]->value();
    e.spin = spin_->value();
    if (QListWidgetItem* it = elements_->item(element_)) it->setText(QString::fromStdString(e.name) + (e.additive ? "  (glow)" : ""));
}

}  // namespace iw8r::qt
