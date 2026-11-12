// IW8 Radiant Suite - File > Save Selection As Prefab (docs/PREFAB_BUNDLES.md): the dialog that names a new prefab
// bundle and lists what will travel with it: the entities and brushes, the mapper's own material, model, sound and
// effect files that are copied into the bundle, and the stock assets it only names. The main window does the saving.
#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

#include "prefab_bundle.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTreeWidget;

namespace iw8r::qt {

class PrefabSaveDialog : public QDialog {
public:
    // exec() returns this when the mapper asked for the old single .map file instead of a bundle.
    static constexpr int kPlainMap = 2;
    // folderFor(id, category, &exists): where a bundle with this id and category is written, and whether one is
    // already there.
    using FolderFn = std::function<QString(const QString& id, const QString& category, bool* exists)>;
    // scan: what prefabBundleFromSelection found for the selection (the lists shown; nothing is written).
    PrefabSaveDialog(const PrefabSaveResult& scan, FolderFn folderFor, QWidget* parent = nullptr);

    QString id() const;
    QString title() const;
    QString description() const;
    QString category() const;
    QStringList tags() const;
    bool replaceSelection() const;
    std::vector<Id> tickedLinkedEntities() const;  // linked objects outside the selection the mapper ticked

    // What the list shows, one text per line ("section: text"), for the GUI test.
    QStringList travelLines() const;
    QString folderText() const;  // the line that says where the bundle is saved

private:
    void refreshFolder();
    FolderFn folderFor_;
    QLineEdit* title_;
    QLineEdit* id_;
    QPlainTextEdit* description_;
    QLineEdit* category_;
    QLineEdit* tags_;
    QCheckBox* replace_;
    QTreeWidget* list_;
    QLabel* folder_;
    class QPushButton* save_;
    bool idEdited_ = false;
};

}  // namespace iw8r::qt
