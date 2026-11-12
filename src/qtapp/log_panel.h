// IW8 Radiant Suite (Qt shell) - Console dock: the process-wide EditorLog (status, compile, Qt warnings) as a table with
// level and text filters, Clear, Copy all and auto-scroll.
#pragma once

#include <QWidget>

#include "editor_log.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QTableWidget;

namespace iw8r::qt {

class LogPanel : public QWidget {
    Q_OBJECT
public:
    explicit LogPanel(QWidget* parent = nullptr);

    int shownRows() const;       // rows that pass the filters
    QString shownText() const;   // those rows as "time level source text" lines
    QCheckBox* levelBox(LogLevel level) const;
    QLineEdit* filterEdit() const { return filter_; }
    bool matches(const LogEntry& e) const;  // level and text filters

private:
    void rebuild();
    void appendRow(const LogEntry& e);
    void updateInfo();

    QCheckBox* levels_[3] = {nullptr, nullptr, nullptr};
    QLineEdit* filter_ = nullptr;
    QCheckBox* autoScroll_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* info_ = nullptr;
};

}  // namespace iw8r::qt
