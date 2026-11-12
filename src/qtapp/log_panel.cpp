#include "log_panel.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColor>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace iw8r::qt {

namespace {
const char* kLevelColor[] = {"#dcdee2", "#e8b04b", "#e66a6a"};  // info, warning, error
}

LogPanel::LogPanel(QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    auto* top = new QHBoxLayout();
    for (int i = 0; i < 3; ++i) {
        const LogLevel lv = LogLevel(i);
        const char* labels[] = {"Info", "Warnings", "Errors"};
        levels_[i] = new QCheckBox(labels[i], this);
        levels_[i]->setChecked(true);
        levels_[i]->setObjectName("consoleLevel" + EditorLog::levelName(lv));
        connect(levels_[i], &QCheckBox::toggled, this, [this] { rebuild(); });
        top->addWidget(levels_[i]);
    }
    filter_ = new QLineEdit(this);
    filter_->setObjectName("consoleFilter");
    filter_->setPlaceholderText("filter text (source or message)");
    filter_->setClearButtonEnabled(true);
    connect(filter_, &QLineEdit::textChanged, this, [this] { rebuild(); });
    top->addWidget(filter_, 1);
    autoScroll_ = new QCheckBox("Auto-scroll", this);
    autoScroll_->setChecked(true);
    top->addWidget(autoScroll_);
    auto* clear = new QPushButton("Clear", this);
    clear->setObjectName("consoleClear");
    connect(clear, &QPushButton::clicked, this, [this] {
        EditorLog::instance().clear();
        rebuild();
    });
    auto* copy = new QPushButton("Copy all", this);
    copy->setObjectName("consoleCopy");
    copy->setToolTip("Copies the rows that match the filters");
    connect(copy, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(shownText());
    });
    top->addWidget(copy);
    top->addWidget(clear);
    lay->addLayout(top);

    table_ = new QTableWidget(0, 4, this);
    table_->setObjectName("consoleTable");
    table_->setHorizontalHeaderLabels({"Time", "Level", "Source", "Text"});
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setWordWrap(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setColumnWidth(0, 96);
    table_->setColumnWidth(1, 76);
    table_->setColumnWidth(2, 80);
    QFont mono("Consolas");
    mono.setStyleHint(QFont::Monospace);
    table_->setFont(mono);
    lay->addWidget(table_, 1);

    info_ = new QLabel(this);
    info_->setStyleSheet("color: gray");
    lay->addWidget(info_);

    connect(&EditorLog::instance(), &EditorLog::added, this, [this](const LogEntry& e) {
        if (!matches(e)) {
            updateInfo();
            return;
        }
        appendRow(e);
    }, Qt::QueuedConnection);
    connect(&EditorLog::instance(), &EditorLog::cleared, this, [this] { rebuild(); }, Qt::QueuedConnection);
    rebuild();
}

QCheckBox* LogPanel::levelBox(LogLevel level) const { return levels_[int(level)]; }

bool LogPanel::matches(const LogEntry& e) const {
    if (!levels_[int(e.level)]->isChecked()) return false;
    const QString f = filter_->text().trimmed();
    if (f.isEmpty()) return true;
    return e.text.contains(f, Qt::CaseInsensitive) || e.source.contains(f, Qt::CaseInsensitive);
}

void LogPanel::appendRow(const LogEntry& e) {
    const int row = table_->rowCount();
    table_->insertRow(row);
    const QColor color(kLevelColor[int(e.level)]);
    const QString cells[4] = {e.time.toString("HH:mm:ss.zzz"), EditorLog::levelName(e.level), e.source, e.text};
    for (int c = 0; c < 4; ++c) {
        auto* item = new QTableWidgetItem(cells[c]);
        item->setForeground(color);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        table_->setItem(row, c, item);
    }
    // Keep the table in step with the ring buffer.
    while (table_->rowCount() > EditorLog::kCapacity) table_->removeRow(0);
    if (autoScroll_->isChecked()) table_->scrollToBottom();
    updateInfo();
}

void LogPanel::rebuild() {
    table_->setUpdatesEnabled(false);
    table_->setRowCount(0);
    for (const LogEntry& e : EditorLog::instance().entries())
        if (matches(e)) appendRow(e);
    table_->setUpdatesEnabled(true);
    if (autoScroll_->isChecked()) table_->scrollToBottom();
    updateInfo();
}

void LogPanel::updateInfo() {
    info_->setText(QString("%1 shown, %2 in the log (last %3)")
                       .arg(table_->rowCount())
                       .arg(EditorLog::instance().count())
                       .arg(EditorLog::kCapacity));
}

int LogPanel::shownRows() const { return table_->rowCount(); }

QString LogPanel::shownText() const {
    QStringList lines;
    for (int r = 0; r < table_->rowCount(); ++r) {
        QStringList cells;
        for (int c = 0; c < 4; ++c) cells << table_->item(r, c)->text();
        lines << cells.join('\t');
    }
    return lines.join('\n');
}

}  // namespace iw8r::qt
