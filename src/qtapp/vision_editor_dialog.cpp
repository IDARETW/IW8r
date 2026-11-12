#include "vision_editor_dialog.h"

#include <string>
#include <utility>
#include <vector>

#include <QBrush>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <QTableWidget>
#include <QVBoxLayout>

#include "vision_fields.h"

namespace iw8r::qt {

namespace {

using Pairs = std::vector<std::pair<std::string, std::string>>;

QString qs(const std::string& s) { return QString::fromStdString(s); }

enum Column { kField = 0, kType, kDefault, kValue, kColumns };

// Trimmed Value text of a row.
std::string valueText(const QTableWidget* table, int r) {
    auto* it = table->item(r, kValue);
    return it ? it->text().trimmed().toStdString() : std::string();
}

// Collects the non-empty Values in table order (all rows, filtered or not). Returns how many are invalid.
int collectOverrides(const QTableWidget* table, Pairs& out) {
    const std::vector<VisionField>& fields = visionFields();
    int invalid = 0;
    out.clear();
    for (int r = 0; r < int(fields.size()); ++r) {
        const std::string v = valueText(table, r);
        if (v.empty()) continue;
        if (!checkVisionValue(fields[size_t(r)], v).empty()) ++invalid;
        out.emplace_back(fields[size_t(r)].name, v);
    }
    return invalid;
}

// Bold for set rows, red text and tooltip for invalid values.
void formatRow(QTableWidget* table, int r) {
    QSignalBlocker block(table);  // formatting must not re-enter itemChanged
    const VisionField& f = visionFields()[size_t(r)];
    const std::string v = valueText(table, r);
    const std::string reason = v.empty() ? std::string() : checkVisionValue(f, v);
    for (int c = 0; c < kColumns; ++c) {
        auto* it = table->item(r, c);
        if (!it) continue;
        QFont font = table->font();
        font.setBold(!v.empty());
        it->setFont(font);
    }
    auto* val = table->item(r, kValue);
    if (!val) return;
    if (reason.empty()) {
        val->setData(Qt::ForegroundRole, QVariant());
        val->setToolTip(QString("group: %1, type: %2").arg(qs(f.group), qs(f.type)));
    } else {
        val->setForeground(QBrush(Qt::red));
        val->setToolTip("<span style='color:#c00000'>" + qs(reason).toHtmlEscaped() + "</span>");
    }
}

// Shows the rows whose name contains `text` and whose group is `group` (empty group = all).
void applyFilter(QTableWidget* table, const QString& text, const QString& group) {
    const std::vector<VisionField>& fields = visionFields();
    for (int r = 0; r < int(fields.size()); ++r) {
        const VisionField& f = fields[size_t(r)];
        const bool groupOk = group.isEmpty() || group == qs(f.group);
        const bool nameOk = text.isEmpty() || qs(f.name).contains(text, Qt::CaseInsensitive);
        table->setRowHidden(r, !(groupOk && nameOk));
    }
}

}  // namespace

QDialog* createVisionEditorDialog(QWidget* parent, const QString& title, const QString& overrides) {
    const std::vector<VisionField>& fields = visionFields();
    const int rowCount = int(fields.size());

    auto* dlg = new QDialog(parent);
    dlg->setWindowTitle(title);
    dlg->setObjectName("visionEditorDialog");
    dlg->resize(780, 640);
    auto* lay = new QVBoxLayout(dlg);

    // Filter and group.
    auto* top = new QHBoxLayout();
    auto* filter = new QLineEdit(dlg);
    filter->setObjectName("visionFilter");
    filter->setPlaceholderText("Filter by field name");
    auto* group = new QComboBox(dlg);
    group->setObjectName("visionGroup");
    group->addItem("All");
    {
        std::vector<std::string> seen;
        for (const VisionField& f : fields) {
            bool known = false;
            for (const auto& g : seen) known = known || g == f.group;
            if (!known) {
                seen.push_back(f.group);
                group->addItem(qs(f.group));
            }
        }
    }
    top->addWidget(filter, 1);
    top->addWidget(new QLabel("Group", dlg));
    top->addWidget(group);
    lay->addLayout(top);

    // The table: one row per field, in converter order. Only the Value column is editable.
    auto* table = new QTableWidget(rowCount, kColumns, dlg);
    table->setObjectName("visionTable");
    table->setHorizontalHeaderLabels({"Field", "Type", "Default", "Value"});
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setSortingEnabled(false);
    lay->addWidget(table, 1);

    for (int r = 0; r < rowCount; ++r) {
        const VisionField& f = fields[size_t(r)];
        const QString tip = QString("group: %1, type: %2").arg(qs(f.group), qs(f.type));
        const QString texts[kColumns] = {qs(f.name), qs(f.type), qs(f.defaultText), QString()};
        for (int c = 0; c < kColumns; ++c) {
            auto* it = new QTableWidgetItem(texts[c]);
            it->setToolTip(tip);
            if (c != kValue) it->setFlags(it->flags() & ~Qt::ItemIsEditable);
            table->setItem(r, c, it);
        }
    }

    auto* countLabel = new QLabel(dlg);
    countLabel->setObjectName("visionCount");
    auto* msg = new QLabel(dlg);
    msg->setObjectName("visionMessage");
    msg->setWordWrap(true);
    auto* notice = new QLabel(dlg);  // load notes (unreadable or unknown fields); not cleared by validation
    notice->setObjectName("visionNotice");
    notice->setWordWrap(true);
    lay->addWidget(countLabel);
    lay->addWidget(msg);
    lay->addWidget(notice);

    auto* bottom = new QHBoxLayout();
    auto* clearBtn = new QPushButton("Clear All Values", dlg);
    clearBtn->setObjectName("visionClearAll");
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, dlg);
    box->setObjectName("visionButtons");
    QPushButton* okBtn = box->button(QDialogButtonBox::Ok);
    okBtn->setObjectName("visionOk");
    bottom->addWidget(clearBtn);
    bottom->addStretch(1);
    bottom->addWidget(box);
    lay->addLayout(bottom);

    // Counts set values and updates the labels. OK is disabled while a value is invalid.
    auto updateCount = [table, countLabel, msg, okBtn, rowCount]() {
        Pairs set;
        const int invalid = collectOverrides(table, set);
        countLabel->setText(QString("%1 of %2 fields set").arg(int(set.size())).arg(rowCount));
        msg->setText(invalid ? QString("%1 invalid value(s) in red. Fix or clear them before OK.").arg(invalid)
                             : QString());
        okBtn->setEnabled(invalid == 0);
    };

    auto applyNow = [table, filter, group]() {
        const QString g = group->currentIndex() > 0 ? group->currentText() : QString();
        applyFilter(table, filter->text().trimmed(), g);
    };

    // Load the current text into the Value column.
    Pairs pairs;
    std::string parseErr;
    QStringList dropped;
    QString loadNote;
    if (parseVisionOverrides(overrides.toStdString(), pairs, &parseErr)) {
        for (const auto& p : pairs) {
            const VisionField* f = findVisionField(p.first);
            if (!f) {
                dropped << qs(p.first);
                continue;
            }
            table->item(int(f - fields.data()), kValue)->setText(qs(p.second));
        }
    } else {
        loadNote = "The current text could not be read (" + qs(parseErr) + "). Values were not loaded.";
    }
    if (!dropped.isEmpty()) loadNote = "Unknown field(s) dropped: " + dropped.join(", ");
    notice->setText(loadNote);
    notice->setVisible(!loadNote.isEmpty());
    for (int r = 0; r < rowCount; ++r) formatRow(table, r);
    updateCount();
    applyNow();

    QObject::connect(table, &QTableWidget::itemChanged, dlg, [table, updateCount](QTableWidgetItem* it) {
        if (!it || it->column() != kValue) return;
        formatRow(table, it->row());
        updateCount();
    });
    QObject::connect(filter, &QLineEdit::textChanged, dlg, [applyNow](const QString&) { applyNow(); });
    QObject::connect(group, &QComboBox::currentIndexChanged, dlg, [applyNow](int) { applyNow(); });
    QObject::connect(clearBtn, &QPushButton::clicked, dlg, [table]() {
        for (int r = 0; r < table->rowCount(); ++r) {
            if (auto* it = table->item(r, kValue)) it->setText(QString());  // itemChanged refreshes the row
        }
    });

    QObject::connect(box, &QDialogButtonBox::accepted, dlg, [dlg, table]() {
        Pairs out;
        // The converter drops the whole override text when one entry is malformed, so refuse to write it.
        if (collectOverrides(table, out) != 0) return;
        dlg->accept();
    });
    QObject::connect(box, &QDialogButtonBox::rejected, dlg, &QDialog::reject);

    return dlg;
}

QString visionEditorResult(QDialog* dialog) {
    auto* table = dialog ? dialog->findChild<QTableWidget*>("visionTable") : nullptr;
    if (!table) return QString();
    Pairs out;
    if (collectOverrides(table, out) != 0) return QString();
    return qs(formatVisionOverrides(out));
}

bool showVisionEditorDialog(QWidget* parent, const QString& title, QString& overrides) {
    QDialog* dlg = createVisionEditorDialog(parent, title, overrides);
    const bool ok = dlg->exec() == QDialog::Accepted;
    if (ok) overrides = visionEditorResult(dlg);
    delete dlg;
    return ok;
}

}  // namespace iw8r::qt
