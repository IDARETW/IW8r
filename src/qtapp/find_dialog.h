// IW8 Radiant Suite (Qt shell) - Find / Replace materials (Radiant "Find and Replace") and Check Map.
#pragma once

#include "map_doctor.h"

#include <QDialog>

#include "editor_context.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QTableWidget;

namespace iw8r::qt {

class FindReplaceDialog : public QDialog {
    Q_OBJECT
public:
    explicit FindReplaceDialog(EditorContext* ctx, QWidget* parent = nullptr);
    void setFind(const QString& material);
    // Exposed for --gui-test.
    size_t selectMatches();
    size_t replaceAll();
    void setReplace(const QString& material);
    void setSubstring(bool on);
    void setSelectionOnly(bool on);

public slots:
    void refreshUsage();

private:
    EditorContext* ctx_;
    QLineEdit* find_;
    QLineEdit* replace_;
    QCheckBox* substring_;
    QCheckBox* selectionOnly_;
    QTableWidget* usage_;
    QLabel* info_;
};

}  // namespace iw8r::qt
namespace iw8r::qt {

// Find / Replace key values (roadmap 8.7): model replace (key "model") or any other entity key.
class KeyReplaceDialog : public QDialog {
    Q_OBJECT
public:
    explicit KeyReplaceDialog(EditorContext* ctx, QWidget* parent = nullptr);
    // Exposed for --gui-test.
    void set(const QString& key, const QString& find, const QString& replace, bool substring, bool selectionOnly);
    size_t selectMatches();
    size_t replaceAll();

public slots:
    void refreshUsage();

private:
    EditorContext* ctx_;
    QLineEdit* key_;
    QLineEdit* find_;
    QLineEdit* replace_;
    QCheckBox* substring_;
    QCheckBox* selectionOnly_;
    QTableWidget* usage_;
    QLabel* info_;
};

}  // namespace iw8r::qt
namespace iw8r {
class EntityDefs;
}
namespace iw8r::qt {
// Entity definitions used by Check Map (set once by the main window).
void setCheckMapEntityDefs(const EntityDefs* defs);
const EntityDefs* checkMapEntityDefs();  // null until the main window has loaded them

class CheckMapDialog : public QDialog {
    Q_OBJECT
public:
    explicit CheckMapDialog(EditorContext* ctx, QWidget* parent = nullptr);
    int problemCount() const;
    QStringList itemTexts() const;  // every listed line (tests)
    // Map Doctor (9.12): findings with an automatic fix are listed first ("[fix] ..."); Fix Selected / Fix All apply
    // them as one undo step and check again. Returns the number applied.
    size_t fixAll();
    size_t fixSelected();
    size_t fixableCount() const { return fixes_.size(); }

public slots:
    void recheck();

private:
    EditorContext* ctx_;
    QListWidget* list_;
    QLabel* info_;
    std::vector<DoctorFix> fixes_;  // index i = list row i (the fixable rows come first)
};

}  // namespace iw8r::qt
