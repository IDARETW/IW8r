// IW8 Radiant (Qt shell) - in-app help (roadmap 9.11): Help > Search Help over the Markdown guides in docs/
// (core help_index), and the First Map Guide dock: the tutorial's steps ticked from the open map, each with a button
// that runs the editor command that helps.
#pragma once

#include <QDialog>
#include <QWidget>

#include <functional>
#include <vector>

#include "editor_context.h"
#include "help_index.h"

class QLineEdit;
class QListWidget;
class QTextBrowser;
class QVBoxLayout;

namespace iw8r::qt {

class HelpSearchDialog : public QDialog {
    Q_OBJECT
public:
    explicit HelpSearchDialog(QWidget* parent = nullptr);
    void search(const QString& query);  // also used by the GUI test
    int resultCount() const;
    QString shownText() const;  // the section shown for the current result
    size_t sectionCount() const { return sections_.size(); }
    static QString docsDir();   // <repo or install>/docs ("" when not found)

private:
    std::vector<HelpSection> sections_;
    std::vector<HelpHit> hits_;
    QLineEdit* query_;
    QListWidget* results_;
    QTextBrowser* view_;
};

class FirstMapGuide : public QWidget {
    Q_OBJECT
public:
    using RunCommand = std::function<void(const QString& command)>;
    using BuildState = std::function<std::pair<bool, bool>()>;  // compiled, deployed
    FirstMapGuide(EditorContext* ctx, RunCommand run, BuildState state, QWidget* parent = nullptr);
    void refresh();
    int doneCount() const;
    int stepCount() const { return int(steps_.size()); }

private:
    EditorContext* ctx_;
    RunCommand run_;
    BuildState state_;
    std::vector<FirstMapStep> steps_;
    QVBoxLayout* list_;
};

}  // namespace iw8r::qt
