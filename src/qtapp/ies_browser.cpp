#include "ies_browser.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>

#include <cmath>

#include "settings.h"

namespace iw8r::qt {

IesPolarWidget::IesPolarWidget(QWidget* parent) : QWidget(parent) { setMinimumSize(320, 300); }

void IesPolarWidget::setProfile(const IesProfile* p) {
    curve_.clear();
    texels_.clear();
    if (p && !p->verticalAngles.empty()) {
        curve_ = iesPolarCurve(*p, 181);
        vmin_ = p->verticalAngles.front();
        vmax_ = p->verticalAngles.back();
        texels_ = iesReplayTexels(*p);
    }
    update();
}

void IesPolarWidget::paintEvent(QPaintEvent*) {
    QPainter g(this);
    g.fillRect(rect(), QColor(30, 32, 38));
    g.setRenderHint(QPainter::Antialiasing, true);
    const int stripH = 22;
    const QRectF area = QRectF(rect()).adjusted(10, 10, -10, -(stripH + 22));
    // Light points down (0 degrees = nadir, as LM-63 type C); the centre is the fixture.
    const QPointF c(area.center().x(), area.top() + 12);
    const double r = std::min(area.width() / 2.0, area.height() - 14);
    g.setPen(QPen(QColor(70, 74, 84), 1));
    for (int ring = 1; ring <= 4; ++ring) g.drawEllipse(c, r * ring / 4.0, r * ring / 4.0);
    for (int a = 0; a <= 180; a += 30) {
        const double t = a * 3.14159265358979 / 180.0;
        g.drawLine(c, c + QPointF(std::sin(t) * r, std::cos(t) * r));
        g.drawLine(c, c + QPointF(-std::sin(t) * r, std::cos(t) * r));
    }
    if (curve_.empty()) {
        g.setPen(QColor(150, 155, 165));
        g.drawText(area, Qt::AlignCenter, "Pick a profile");
        return;
    }
    // The C0 plane, mirrored to both sides.
    QPainterPath right, left;
    for (size_t i = 0; i < curve_.size(); ++i) {
        const double v = (vmin_ + (vmax_ - vmin_) * double(i) / double(curve_.size() - 1)) * 3.14159265358979 / 180.0;
        const QPointF pr = c + QPointF(std::sin(v) * r * curve_[i], std::cos(v) * r * curve_[i]);
        const QPointF pl = c + QPointF(-std::sin(v) * r * curve_[i], std::cos(v) * r * curve_[i]);
        if (i == 0) right.moveTo(pr), left.moveTo(pl);
        else right.lineTo(pr), left.lineTo(pl);
    }
    g.setPen(QPen(QColor(255, 200, 80), 2));
    g.drawPath(right);
    g.drawPath(left);
    // Replay's 128-texel segment (what the game's spot samples), as a grey strip.
    const QRectF strip(area.left(), rect().bottom() - stripH - 8, area.width(), stripH);
    for (size_t j = 0; j < texels_.size(); ++j) {
        const int v = std::clamp(texels_[j], 0, 255);
        g.fillRect(QRectF(strip.left() + strip.width() * double(j) / double(texels_.size()), strip.top(),
                          strip.width() / double(texels_.size()) + 1, strip.height()),
                   QColor(v, v, v));
    }
    g.setPen(QColor(150, 155, 165));
    g.drawText(QRectF(strip.left(), strip.top() - 16, strip.width(), 14), Qt::AlignLeft, "Replay segment (axis -> edge of the cone)");
}

QString IesBrowserDialog::defaultFolder() {
    QDir d(QCoreApplication::applicationDirPath());
    for (int up = 0; up < 4; ++up, d.cdUp())
        if (QFileInfo(d.filePath("data/ies")).isDir()) return QDir::cleanPath(d.filePath("data/ies"));
    return {};
}

IesBrowserDialog::IesBrowserDialog(EditorContext* ctx, QWidget* parent) : QDialog(parent), ctx_(ctx) {
    setObjectName("iesBrowserDialog");
    setWindowTitle("IES Profile Browser");
    resize(760, 460);
    auto* lay = new QVBoxLayout(this);
    auto* row = new QHBoxLayout();
    list_ = new QListWidget(this);
    list_->setMinimumWidth(240);
    plot_ = new IesPolarWidget(this);
    row->addWidget(list_);
    row->addWidget(plot_, 1);
    lay->addLayout(row, 1);
    info_ = new QLabel(this);
    info_->setWordWrap(true);
    lay->addWidget(info_);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* folder = box->addButton("Folder...", QDialogButtonBox::ActionRole);
    auto* use = box->addButton("Use for Selected Lights", QDialogButtonBox::ActionRole);
    lay->addWidget(box);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(folder, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, "IES folder", dir_);
        if (!d.isEmpty()) setFolder(d);
    });
    connect(use, &QPushButton::clicked, this, [this] {
        const int n = applyToSelection();
        info_->setText(n ? QString("iw8r_ies set on %1 light(s) (one undo step)").arg(n) : QString("Select light or light_spot entities first"));
    });
    connect(list_, &QListWidget::currentTextChanged, this, [this](const QString& f) { selectProfile(f); });
    setFolder(Settings::instance().value("ies/folder", defaultFolder()).toString());
}

void IesBrowserDialog::setFolder(const QString& dir) {
    dir_ = dir;
    Settings::instance().setValue("ies/folder", dir);
    list_->clear();
    list_->addItems(QDir(dir).entryList({"*.ies", "*.IES"}, QDir::Files, QDir::Name));
    if (list_->count()) list_->setCurrentRow(0);
    else {
        plot_->setProfile(nullptr);
        info_->setText("No .ies files in " + dir + " (Folder... picks another; data/ies has a starter set)");
    }
}

int IesBrowserDialog::profileCount() const { return list_->count(); }

bool IesBrowserDialog::selectProfile(const QString& fileName) {
    const QString path = QDir(dir_).filePath(fileName);
    QFile f(path);
    std::string err;
    IesProfile p;
    if (!f.open(QIODevice::ReadOnly) || !parseIesFile(QString::fromUtf8(f.readAll()).toStdString(), p, &err)) {
        plot_->setProfile(nullptr);
        info_->setText(fileName + ": " + QString::fromStdString(err.empty() ? std::string("cannot read") : err));
        selected_.clear();
        return false;
    }
    profile_ = p;
    selected_ = path;
    plot_->setProfile(&profile_);
    const auto items = list_->findItems(fileName, Qt::MatchExactly);
    if (!items.isEmpty() && list_->currentItem() != items.front()) list_->setCurrentItem(items.front());
    info_->setText(QString("%1: %2 vertical x %3 horizontal angles, vertical %4 to %5 degrees. The spot light's own cone still limits it; "
                           "Replay ships two profiles of its own (IES_16 flashlight, ies_nvg_flashlight).")
                       .arg(fileName).arg(p.verticalAngles.size()).arg(p.horizontalAngles.size())
                       .arg(p.verticalAngles.front()).arg(p.verticalAngles.back()));
    return true;
}

QString IesBrowserDialog::selectedPath() const { return selected_; }

int IesBrowserDialog::applyToSelection() {
    if (selected_.isEmpty()) return 0;
    // Relative to the map folder when the file is under it (portable maps), else absolute.
    QString value = selected_;
    if (!ctx_->doc.path().empty()) {
        const QString mapDir = QFileInfo(QString::fromStdString(ctx_->doc.path())).absolutePath();
        const QString rel = QDir(mapDir).relativeFilePath(selected_);
        if (!rel.startsWith("..")) value = rel;
    }
    int n = 0;
    ctx_->doc.beginGroup("Set IES Profile");
    for (const auto& r : ctx_->doc.selection()) {
        const Entity* e = ctx_->doc.findEntity(r.entity);
        if (!e) continue;
        const std::string c = e->classname();
        if (c != "light" && c != "light_spot" && c != "light_omni") continue;
        n += ctx_->doc.setKeyValue(r.entity, "iw8r_ies", value.toStdString()) ? 1 : 0;
    }
    ctx_->doc.endGroup();
    if (n) ctx_->documentEdited();
    return n;
}

}  // namespace iw8r::qt
