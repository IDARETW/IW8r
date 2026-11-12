#pragma once
// Script dock (roadmap 6.11 / 6.12): edits the map's own GSC (the file worldspawn iw8r_script names), with syntax
// colours, completion from the IW8 builtin list, structure checks and ready-made behaviours to insert.
#include <QWidget>

class QPlainTextEdit;
class QLabel;
class QListWidget;
class QCompleter;

namespace iw8r::qt {

class EditorContext;

class ScriptPanel : public QWidget {
    Q_OBJECT
public:
    explicit ScriptPanel(EditorContext* ctx, QWidget* parent = nullptr);

    // The script file of the open map: worldspawn iw8r_script resolved against the map folder ("" = none set).
    QString scriptPath() const;
    // Creates <map>.gsc next to the map from the template and points worldspawn iw8r_script at it (one undo step).
    // Returns false with *why when the map has not been saved yet or the file cannot be written.
    bool createScript(QString* why = nullptr);
    bool save(QString* why = nullptr);
    void reload();  // from disk (asks nothing; unsaved text is replaced)
    bool modified() const;
    QString text() const;
    void setText(const QString& t);
    // Appends a behaviour from scriptSnippets() (by id) unless its function is already defined. Returns true when added.
    bool insertSnippet(const QString& id);
    // Runs the structure checks and fills the issue list; returns the number of errors.
    int check();
    int issueCount() const;
    int builtinCount() const { return builtinCount_; }
    // 7.13: puts the cursor on 1-based line `line` and scrolls it into view (false when out of range).
    bool goToLine(int line);
    int currentLine() const;  // 1-based (tests)

private:
    void refreshHeader();
    EditorContext* ctx_;
    QPlainTextEdit* edit_;
    QLabel* header_;
    QListWidget* issues_;
    QCompleter* completer_ = nullptr;
    QString loadedPath_;
    int builtinCount_ = 0;
};

}  // namespace iw8r::qt
