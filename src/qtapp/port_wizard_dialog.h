#pragma once
// CoD4 port wizard (roadmap 11.9, research 43): pick a built CoD4 map (.ff, route A through x64-zt's iw3 -> iw8
// converter) or a CoD4 .map (route B: open it here and compile), see the exact steps core port_wizard plans, run them
// with the log shown, then read the converter's summary (converted / missing assets).
#include <QDialog>
#include <functional>

#include "port_wizard.h"

class QLineEdit;
class QPlainTextEdit;
class QLabel;
class QPushButton;
class QProcess;

namespace iw8r::qt {

class PortWizardDialog : public QDialog {
    Q_OBJECT
public:
    using ToolPathFn = std::function<QString(const QString& key)>;  // MainWindow::toolPath ("zonetool", "replay")
    using OpenMapFn = std::function<void(const QString& path)>;
    PortWizardDialog(ToolPathFn tools, OpenMapFn openMap, QWidget* parent = nullptr);

    // Fills the inputs (tests and drag-in); replans.
    void setInput(const QString& source, const QString& mapName, const QString& cod4Root, const QString& outDir);
    const PortPlan& plan() const { return plan_; }
    QString planText() const;  // the step list as shown
    // Summarizes a finished converter log (also used by tests).
    QString summarize(const QString& log) const;

private:
    void replan();
    void runNext();

    ToolPathFn tools_;
    OpenMapFn openMap_;
    PortPlan plan_;
    QLineEdit *source_ = nullptr, *mapName_ = nullptr, *cod4_ = nullptr, *out_ = nullptr;
    QPlainTextEdit *planView_ = nullptr, *log_ = nullptr;
    QLabel* summary_ = nullptr;
    QPushButton* run_ = nullptr;
    QProcess* proc_ = nullptr;
    size_t step_ = 0;
    QString runLog_;
};

}  // namespace iw8r::qt
