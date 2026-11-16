// IW8 Radiant (Qt shell) - Build Budget dock (roadmap 1.16): the last build's figures (core build_report) as bars
// against their limits: native limits x64-zt enforces and editor guidelines, green / amber / red. Read from
// <build root>/last_build_report.json, refreshed after every build.
#pragma once

#include <QWidget>

#include <functional>

class QLabel;
class QGridLayout;

namespace iw8r::qt {

class BudgetPanel : public QWidget {
    Q_OBJECT
public:
    using ReportPath = std::function<QString()>;  // where the last build report is
    explicit BudgetPanel(ReportPath path, QWidget* parent = nullptr);
    void refresh();
    int gaugeCount() const { return gauges_; }
    int overCount() const { return over_; }

private:
    ReportPath path_;
    QLabel* header_;
    QGridLayout* grid_;
    int gauges_ = 0, over_ = 0;
};

}  // namespace iw8r::qt
