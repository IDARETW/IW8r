#include "port_wizard_dialog.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

#include "settings.h"

namespace iw8r::qt {

namespace {
QString qs(const std::string& s) { return QString::fromStdString(s); }
}  // namespace

PortWizardDialog::PortWizardDialog(ToolPathFn tools, OpenMapFn openMap, QWidget* parent)
    : QDialog(parent), tools_(std::move(tools)), openMap_(std::move(openMap)) {
    setObjectName("portWizard");
    setWindowTitle("Port a CoD4 Map");
    resize(820, 640);
    auto* lay = new QVBoxLayout(this);
    auto* intro = new QLabel("Built CoD4 map (.ff): converted by x64-zt's iw3 to iw8 converter into IW8 zones. "
                             "CoD4 source map (.map): opened here, then built with Compile like any map.", this);
    intro->setWordWrap(true);
    lay->addWidget(intro);
    auto* form = new QFormLayout;
    auto browseRow = [&](QLineEdit*& edit, const QString& key, bool dir, const QString& filter) {
        auto* row = new QHBoxLayout;
        edit = new QLineEdit(Settings::instance().value("port/" + key).toString(), this);
        auto* b = new QPushButton("...", this);
        b->setFixedWidth(32);
        row->addWidget(edit, 1);
        row->addWidget(b);
        QLineEdit* e = edit;
        connect(b, &QPushButton::clicked, this, [this, e, dir, filter] {
            const QString f = dir ? QFileDialog::getExistingDirectory(this, "Choose Folder", e->text())
                                  : QFileDialog::getOpenFileName(this, "Choose File", e->text(), filter);
            if (!f.isEmpty()) e->setText(QDir::toNativeSeparators(f));
        });
        connect(edit, &QLineEdit::textChanged, this, [this, key](const QString& v) {
            Settings::instance().setValue("port/" + key, v);
            replan();
        });
        return row;
    };
    form->addRow("CoD4 map (.ff or .map)", browseRow(source_, "source", false, "CoD4 maps (*.ff *.map)"));
    mapName_ = new QLineEdit(Settings::instance().value("port/mapName").toString(), this);
    mapName_->setPlaceholderText("mp_name (4 to 15 characters)");
    connect(mapName_, &QLineEdit::textChanged, this, [this](const QString& v) {
        Settings::instance().setValue("port/mapName", v);
        replan();
    });
    form->addRow("IW8 map name", mapName_);
    form->addRow("CoD4 install folder", browseRow(cod4_, "cod4Root", true, {}));
    form->addRow("Output folder", browseRow(out_, "outDir", true, {}));
    lay->addLayout(form);
    planView_ = new QPlainTextEdit(this);
    planView_->setReadOnly(true);
    lay->addWidget(planView_, 1);
    log_ = new QPlainTextEdit(this);
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(5000);
    lay->addWidget(log_, 1);
    summary_ = new QLabel(this);
    summary_->setWordWrap(true);
    lay->addWidget(summary_);
    auto* buttons = new QHBoxLayout;
    run_ = new QPushButton("Run", this);
    auto* close = new QPushButton("Close", this);
    buttons->addStretch(1);
    buttons->addWidget(run_);
    buttons->addWidget(close);
    lay->addLayout(buttons);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(run_, &QPushButton::clicked, this, [this] {
        if (plan_.steps.empty()) return;
        if (plan_.steps.front().program.empty()) {  // route B: the editor itself opens and builds the .map
            if (openMap_) openMap_(source_->text());
            summary_->setText("Opened in the editor: check the materials, then Compile (F7 / Compile > Build).");
            return;
        }
        step_ = 0;
        runLog_.clear();
        log_->clear();
        run_->setEnabled(false);
        runNext();
    });
    replan();
}

void PortWizardDialog::setInput(const QString& source, const QString& mapName, const QString& cod4Root, const QString& outDir) {
    for (auto [e, v] : {std::pair{source_, source}, {mapName_, mapName}, {cod4_, cod4Root}, {out_, outDir}}) {
        QSignalBlocker b(e);
        e->setText(v);
    }
    replan();
}

void PortWizardDialog::replan() {
    PortInput in;
    in.path = QDir::toNativeSeparators(source_->text().trimmed()).toStdString();
    in.mapName = mapName_->text().trimmed().toStdString();
    in.cod4Root = cod4_->text().trimmed().toStdString();
    in.outDir = out_->text().trimmed().toStdString();
    if (tools_) {
        in.zonetoolExe = tools_("zonetool").toStdString();
        in.replayExe = tools_("replay").toStdString();
    }
    if (in.mapName.empty() && !in.path.empty())  // suggest mp_<stem>
        in.mapName = ("mp_" + QFileInfo(source_->text()).completeBaseName().toLower().remove(QRegularExpression("^mp_")))
                         .left(15).toStdString();
    plan_ = planPort(in);
    planView_->setPlainText(planText());
    run_->setEnabled(!plan_.steps.empty() && !proc_);
    run_->setText(!plan_.steps.empty() && plan_.steps.front().program.empty() ? "Open in Editor" : "Run");
}

QString PortWizardDialog::planText() const {
    QStringList out;
    const char* src = plan_.source == PortSource::BuiltFastFile ? "built CoD4 map (.ff), route A"
                      : plan_.source == PortSource::RadiantMap   ? "CoD4 source map (.map), route B"
                                                                 : "unknown (choose a .ff or .map)";
    out << QString("Source: %1").arg(src);
    for (const auto& e : plan_.errors) out << "error: " + qs(e);
    for (const auto& w : plan_.warnings) out << "note: " + qs(w);
    int n = 0;
    for (const auto& s : plan_.steps) {
        out << QString("%1. %2: %3").arg(++n).arg(qs(s.title), qs(s.explains));
        if (!s.program.empty()) {
            QStringList args;
            for (const auto& a : s.args) args << (QString::fromStdString(a).contains(' ') ? '"' + qs(a) + '"' : qs(a));
            out << "   " + qs(s.program) + " " + args.join(' ');
            if (!s.workDir.empty()) out << "   in " + qs(s.workDir);
        }
    }
    return out.join('\n');
}

void PortWizardDialog::runNext() {
    while (step_ < plan_.steps.size() && plan_.steps[step_].program.empty()) ++step_;
    if (step_ >= plan_.steps.size()) {
        summary_->setText(summarize(runLog_));
        run_->setEnabled(true);
        return;
    }
    const PortStep& s = plan_.steps[step_];
    if (!s.workDir.empty()) QDir().mkpath(qs(s.workDir));
    proc_ = new QProcess(this);
    proc_->setProcessChannelMode(QProcess::MergedChannels);
    if (!s.workDir.empty()) proc_->setWorkingDirectory(qs(s.workDir));
    QStringList args;
    for (const auto& a : s.args) args << qs(a);
    log_->appendPlainText(QString("== %1").arg(qs(s.title)));
    connect(proc_, &QProcess::readyRead, this, [this] {
        const QString t = QString::fromLocal8Bit(proc_->readAll());
        runLog_ += t;
        log_->appendPlainText(t.trimmed());
    });
    connect(proc_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus st) {
        proc_->deleteLater();
        proc_ = nullptr;
        if (st != QProcess::NormalExit || code != 0) {
            summary_->setText(QString("Step %1 failed (exit %2). ").arg(step_ + 1).arg(code) + summarize(runLog_));
            run_->setEnabled(true);
            return;
        }
        ++step_;
        runNext();
    });
    connect(proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (!proc_ || proc_->state() != QProcess::NotRunning) return;
        summary_->setText("Could not start " + proc_->program() + ": " + proc_->errorString());
        proc_->deleteLater();
        proc_ = nullptr;
        run_->setEnabled(true);
    });
    proc_->start(qs(s.program), args);
}

QString PortWizardDialog::summarize(const QString& log) const {
    const PortLogSummary s = summarizePortLog(log.toStdString());
    QString t = QString("%1: %2 asset(s) converted, %3 missing.")
                    .arg(s.ok ? "Converted" : "Converted with problems").arg(s.convertedAssets).arg(s.missingAssets);
    QStringList names;
    for (size_t i = 0; i < s.missing.size() && i < 12; ++i) names << qs(s.missing[i]);
    if (!names.isEmpty()) t += " Missing: " + names.join(", ") + (s.missing.size() > 12 ? ", ..." : "");
    if (!out_->text().isEmpty()) t += " Output: " + QDir::toNativeSeparators(out_->text() + "/output");
    return t;
}

}  // namespace iw8r::qt
