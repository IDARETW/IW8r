// Asset Usage dialog (roadmap 12.9). See asset_usage_dialog.h.
#include "asset_usage_dialog.h"

#include <QClipboard>
#include <QColor>
#include <QDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include <string>
#include <vector>

#include "asset_usage.h"

namespace iw8r::qt {

namespace {

std::vector<std::string> toStd(const QStringList& list) {
    std::vector<std::string> out;
    out.reserve(static_cast<size_t>(list.size()));
    for (const QString& s : list) out.push_back(s.toUtf8().toStdString());
    return out;
}

QString cell(const std::string& s) { return QString::fromUtf8(s.data(), static_cast<int>(s.size())); }

// An empty table with the given headers. Sorting is switched on by the callers after the rows are filled.
QTableWidget* newTable(const QStringList& headers) {
    auto* t = new QTableWidget(0, static_cast<int>(headers.size()));
    t->setHorizontalHeaderLabels(headers);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setAlternatingRowColors(true);
    t->verticalHeader()->setVisible(false);
    t->horizontalHeader()->setStretchLastSection(true);
    return t;
}

// Name and Uses, most used first. Uses is stored as a number so the column sorts numerically.
QTableWidget* usageTable(const std::vector<AssetUse>& list, const char* objectName) {
    QTableWidget* t = newTable({QStringLiteral("Name"), QStringLiteral("Uses")});
    t->setObjectName(QString::fromUtf8(objectName));
    t->setSortingEnabled(false);
    t->setRowCount(static_cast<int>(list.size()));
    for (int r = 0; r < static_cast<int>(list.size()); ++r) {
        t->setItem(r, 0, new QTableWidgetItem(cell(list[static_cast<size_t>(r)].name)));
        auto* uses = new QTableWidgetItem();
        uses->setData(Qt::DisplayRole, static_cast<qlonglong>(list[static_cast<size_t>(r)].count));
        t->setItem(r, 1, uses);
    }
    t->setSortingEnabled(true);
    t->sortByColumn(1, Qt::DescendingOrder);
    return t;
}

// Kind, Name, Used and Path. Rows nothing uses are tinted amber.
QTableWidget* ownTable(const std::vector<OwnAssetFile>& files) {
    QTableWidget* t = newTable({QStringLiteral("Kind"), QStringLiteral("Name"), QStringLiteral("Used"),
                                QStringLiteral("Path")});
    t->setObjectName(QStringLiteral("ownFilesTable"));
    t->setSortingEnabled(false);
    t->setRowCount(static_cast<int>(files.size()));
    const QColor amber(255, 220, 140);
    for (int r = 0; r < static_cast<int>(files.size()); ++r) {
        const OwnAssetFile& f = files[static_cast<size_t>(r)];
        QString kind = cell(f.kind);
        if (!kind.isEmpty()) kind[0] = kind[0].toUpper();
        QTableWidgetItem* items[4] = {
            new QTableWidgetItem(kind),
            new QTableWidgetItem(cell(f.name)),
            new QTableWidgetItem(f.used ? QStringLiteral("yes") : QStringLiteral("no")),
            new QTableWidgetItem(cell(f.path)),
        };
        for (int c = 0; c < 4; ++c) {
            if (!f.used) items[c]->setBackground(amber);
            t->setItem(r, c, items[c]);
        }
    }
    t->setSortingEnabled(true);
    return t;
}

// The table as tab-separated text: a header line, then one line per visible row.
QString tableText(const QTableWidget* t) {
    QStringList lines;
    QStringList header;
    for (int c = 0; c < t->columnCount(); ++c) {
        const QTableWidgetItem* h = t->horizontalHeaderItem(c);
        header << (h ? h->text() : QString());
    }
    lines << header.join(QLatin1Char('\t'));
    for (int r = 0; r < t->rowCount(); ++r) {
        QStringList row;
        for (int c = 0; c < t->columnCount(); ++c) {
            QString v = t->item(r, c) ? t->item(r, c)->text() : QString();
            row << v.replace(QLatin1Char('\t'), QLatin1Char(' ')).replace(QLatin1Char('\n'), QLatin1Char(' '));
        }
        lines << row.join(QLatin1Char('\t'));
    }
    return lines.join(QLatin1Char('\n'));
}

}  // namespace

QDialog* createAssetUsageDialog(QWidget* parent, const iw8r::MapFile& map, const QStringList& materialRoots,
                                const QStringList& modelRoots, const QStringList& soundRoots,
                                const QStringList& fxRoots) {
    const iw8r::MapAssetUsage usage = iw8r::collectAssetUsage(map);
    const std::vector<iw8r::OwnAssetFile> own = iw8r::findOwnAssetFiles(usage, toStd(materialRoots), toStd(modelRoots),
                                                                       toStd(soundRoots), toStd(fxRoots));
    int unused = 0;
    for (const iw8r::OwnAssetFile& f : own)
        if (!f.used) ++unused;

    auto* dlg = new QDialog(parent);
    dlg->setObjectName(QStringLiteral("assetUsageDialog"));
    dlg->setWindowTitle(QStringLiteral("Asset Usage"));
    dlg->resize(760, 520);

    auto* tabs = new QTabWidget(dlg);
    tabs->setObjectName(QStringLiteral("assetUsageTabs"));
    std::vector<QTableWidget*> tables;  // same order as the tabs
    auto addUsageTab = [&](const std::vector<iw8r::AssetUse>& list, const char* title, const char* objectName) {
        QTableWidget* t = usageTable(list, objectName);
        tables.push_back(t);
        tabs->addTab(t, QString::fromUtf8(title));
    };
    addUsageTab(usage.materials, "Materials", "materialsTable");
    addUsageTab(usage.models, "Models", "modelsTable");
    addUsageTab(usage.prefabs, "Prefabs", "prefabsTable");
    addUsageTab(usage.effects, "Effects", "effectsTable");
    addUsageTab(usage.sounds, "Sounds", "soundsTable");

    auto* ownPage = new QWidget(tabs);
    ownPage->setObjectName(QStringLiteral("ownFilesPage"));
    auto* ownLayout = new QVBoxLayout(ownPage);
    QString summary;
    if (own.empty()) {
        summary = QStringLiteral("No asset files of the map's own were found in the configured folders.");
    } else {
        summary = QStringLiteral("%1 of %2 of the map's own files are not used by this map.")
                      .arg(unused)
                      .arg(static_cast<int>(own.size()));
    }
    auto* summaryLabel = new QLabel(summary, ownPage);
    summaryLabel->setObjectName(QStringLiteral("ownFilesSummary"));
    summaryLabel->setWordWrap(true);
    ownLayout->addWidget(summaryLabel);
    QTableWidget* ownT = ownTable(own);
    ownLayout->addWidget(ownT);
    tables.push_back(ownT);
    tabs->addTab(ownPage, QStringLiteral("Own files"));

    auto* buttons = new QHBoxLayout;
    auto* copyButton = new QPushButton(QStringLiteral("Copy as text"), dlg);
    copyButton->setObjectName(QStringLiteral("copyAsTextButton"));
    auto* closeButton = new QPushButton(QStringLiteral("Close"), dlg);
    closeButton->setObjectName(QStringLiteral("closeButton"));
    buttons->addWidget(copyButton);
    buttons->addStretch(1);
    buttons->addWidget(closeButton);

    auto* root = new QVBoxLayout(dlg);
    root->addWidget(tabs);
    root->addLayout(buttons);

    // The lambda holds copies (tab widget pointer and table pointers), not references to this frame's locals.
    QObject::connect(copyButton, &QPushButton::clicked, dlg, [tabs, tables]() {
        const int i = tabs->currentIndex();
        if (i < 0 || i >= static_cast<int>(tables.size())) return;
        if (QClipboard* cb = QGuiApplication::clipboard()) cb->setText(tableText(tables[static_cast<size_t>(i)]));
    });
    QObject::connect(closeButton, &QPushButton::clicked, dlg, &QDialog::accept);

    return dlg;
}

void showAssetUsageDialog(QWidget* parent, const iw8r::MapFile& map, const QStringList& materialRoots,
                          const QStringList& modelRoots, const QStringList& soundRoots, const QStringList& fxRoots) {
    QDialog* dlg = createAssetUsageDialog(parent, map, materialRoots, modelRoots, soundRoots, fxRoots);
    dlg->exec();
    delete dlg;
}

}  // namespace iw8r::qt
