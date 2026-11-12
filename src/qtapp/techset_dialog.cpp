#include "techset_dialog.h"

#include <string>
#include <vector>

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include "techset_detail.h"

namespace iw8r::qt {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

// Reads the export when it is a readable file; false when it is missing or unreadable.
bool readExport(const QString& path, std::string& text) {
    if (path.isEmpty() || !QFileInfo(path).isFile()) return false;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    text = QByteArray(f.readAll()).toStdString();
    return true;
}

}  // namespace

QDialog* createTechsetDialog(QWidget* parent, const QString& materialName, const QString& techsetName, const QString& techsetJsonPath) {
    auto* dlg = new QDialog(parent);
    dlg->setObjectName("techsetDialog");
    dlg->setWindowTitle(QStringLiteral("Technique Set"));
    dlg->resize(760, 520);

    auto* root = new QVBoxLayout(dlg);

    const std::string tsStd = techsetName.toStdString();
    const QString nameLine = QStringLiteral("Material: %1\nTechnique set: %2").arg(materialName, techsetName.isEmpty() ? QStringLiteral("(none)") : techsetName);
    auto* names = new QLabel(nameLine, dlg);
    names->setObjectName("techsetNames");
    names->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(names);

    // Plain-English lines from the name (research 66 grammar).
    const QString describe = qs(describeTechsetName(tsStd));
    auto* info = new QPlainTextEdit(dlg);
    info->setObjectName("techsetInfo");
    info->setReadOnly(true);
    info->setPlainText(describe);
    info->setMaximumHeight(150);
    root->addWidget(info);

    // Everything shown, as text for the Copy button.
    QString copyText = QStringLiteral("Material: %1\nTechnique set: %2\n\n%3\n").arg(materialName, techsetName.isEmpty() ? QStringLiteral("(none)") : techsetName, describe);

    std::string raw;
    if (readExport(techsetJsonPath, raw)) {
        TechsetDetail detail;
        std::string err;
        if (parseTechsetExport(raw, detail, &err)) {
            auto* tree = new QTreeWidget(dlg);
            tree->setObjectName("techsetTree");
            tree->setColumnCount(4);
            tree->setHeaderLabels({QStringLiteral("Technique"), QStringLiteral("Shaders"), QStringLiteral("Arguments"), QStringLiteral("State")});
            tree->setRootIsDecorated(false);
            tree->setAlternatingRowColors(true);
            for (const TechniqueDetail& t : detail.techniques) {
                QStringList shaders;
                for (const std::string& s : t.shaders) shaders << qs(s);
                const QString title = t.typeName.empty() ? qs(t.name) : qs(t.typeName);
                auto* item = new QTreeWidgetItem(tree, {title, shaders.join(QStringLiteral(", ")), QString::number(t.argumentCount), qs(t.stateSummary)});
                item->setToolTip(0, qs(t.name));
                copyText += QStringLiteral("\n%1\n  shaders: %2\n  arguments: %3\n  state: %4\n")
                                .arg(title, shaders.join(QStringLiteral(", ")), QString::number(t.argumentCount), qs(t.stateSummary));
            }
            tree->header()->setStretchLastSection(true);
            tree->header()->resizeSection(0, 280);
            tree->header()->resizeSection(1, 200);
            tree->header()->resizeSection(2, 80);
            root->addWidget(tree, 1);
            if (detail.techniques.empty()) {
                auto* none = new QLabel(QStringLiteral("The export has no techniques."), dlg);
                root->addWidget(none);
            }
            if (!detail.notes.empty()) {
                QStringList notes;
                for (const std::string& n : detail.notes) notes << qs(n);
                auto* nl = new QLabel(QStringLiteral("Notes: ") + notes.join(QStringLiteral("; ")), dlg);
                nl->setObjectName("techsetNotes");
                nl->setWordWrap(true);
                root->addWidget(nl);
                copyText += QStringLiteral("\nNotes: %1\n").arg(notes.join(QStringLiteral("; ")));
            }
        } else {
            auto* bad = new QLabel(QStringLiteral("The technique set export could not be read: %1").arg(qs(err)), dlg);
            bad->setObjectName("techsetUnreadable");
            bad->setWordWrap(true);
            root->addWidget(bad);
            copyText += QStringLiteral("\nThe technique set export could not be read: %1\n").arg(qs(err));
        }
    } else {
        auto* none = new QLabel(QStringLiteral("The technique set body has not been exported yet."), dlg);
        none->setObjectName("techsetNotExported");
        none->setWordWrap(true);
        root->addWidget(none);
        copyText += QStringLiteral("\nThe technique set body has not been exported yet.\n");
    }

    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    auto* copy = new QPushButton(QStringLiteral("Copy"), dlg);
    copy->setObjectName("techsetCopy");
    auto* close = new QPushButton(QStringLiteral("Close"), dlg);
    close->setObjectName("techsetClose");
    buttons->addWidget(copy);
    buttons->addWidget(close);
    root->addLayout(buttons);

    QObject::connect(copy, &QPushButton::clicked, dlg, [copyText]() {
        if (QClipboard* cb = QApplication::clipboard()) cb->setText(copyText);
    });
    QObject::connect(close, &QPushButton::clicked, dlg, [dlg]() { dlg->close(); });

    return dlg;
}

void showTechsetDialog(QWidget* parent, const QString& materialName, const QString& techsetName, const QString& techsetJsonPath) {
    QDialog* dlg = createTechsetDialog(parent, materialName, techsetName, techsetJsonPath);
    dlg->setAttribute(Qt::WA_DeleteOnClose);  // non-modal: closing it deletes it
    dlg->show();
}

}  // namespace iw8r::qt
