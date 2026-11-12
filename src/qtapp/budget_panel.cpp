#include "budget_panel.h"

#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>

#include "build_report.h"

namespace iw8r::qt {

BudgetPanel::BudgetPanel(ReportPath path, QWidget* parent) : QWidget(parent), path_(std::move(path)) {
    setObjectName("budgetPanel");
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    header_ = new QLabel(this);
    header_->setWordWrap(true);
    lay->addWidget(header_);
    grid_ = new QGridLayout();
    lay->addLayout(grid_);
    auto* note = new QLabel("<span style='color:#9aa'>Native = a limit the build tool (x64-zt) enforces; guideline = an editor "
                            "threshold, not a measured engine limit. Figures come from the last build's extraction.</span>",
                            this);
    note->setWordWrap(true);
    lay->addWidget(note);
    lay->addStretch(1);
    refresh();
}

void BudgetPanel::refresh() {
    while (QLayoutItem* it = grid_->takeAt(0)) {
        if (QWidget* w = it->widget()) w->deleteLater();
        delete it;
    }
    gauges_ = over_ = 0;
    const QString file = path_ ? path_() : QString();
    BuildReport r;
    std::string err;
    if (file.isEmpty() || !QFileInfo::exists(file) || !loadBuildReport(file.toStdString(), r, &err)) {
        header_->setText("No build yet for this map: compile it (F7) and the budget appears here.");
        return;
    }
    header_->setText(QString("<b>%1</b>, built %2").arg(QString::fromStdString(r.mapId), QFileInfo(file).lastModified().toString("yyyy-MM-dd HH:mm")));
    int row = 0;
    for (const BudgetGauge& g : budgetGauges(r)) {
        auto* label = new QLabel(QString::fromStdString(g.label) + (g.native ? " <span style='color:#9aa'>(native)</span>" : ""), this);
        auto* bar = new QProgressBar(this);
        bar->setRange(0, 1000);
        const double f = g.fraction();
        bar->setValue(int(std::min(1.0, f) * 1000));
        bar->setTextVisible(false);
        bar->setMaximumHeight(12);
        const char* colour = f >= 1.0 ? "#d04040" : f >= 0.75 ? "#d0a030" : "#40a060";
        bar->setStyleSheet(QString("QProgressBar{background:#2a2d33;border:1px solid #444;border-radius:3px}"
                                   "QProgressBar::chunk{background:%1;border-radius:2px}").arg(colour));
        const QString unit = g.unit.empty() ? QString() : " " + QString::fromStdString(g.unit);
        auto fmt = [](double v) { return v >= 100 || v == double(int64_t(v)) ? QString::number(v, 'f', 0) : QString::number(v, 'f', 1); };
        auto* value = new QLabel(QString("%1%3 / %2%3 (%4%)").arg(fmt(g.value), fmt(g.limit), unit).arg(int(f * 100)), this);
        grid_->addWidget(label, row, 0);
        grid_->addWidget(bar, row, 1);
        grid_->addWidget(value, row, 2);
        ++row;
        ++gauges_;
        over_ += f >= 1.0;
    }
    grid_->setColumnStretch(1, 1);
}

}  // namespace iw8r::qt
