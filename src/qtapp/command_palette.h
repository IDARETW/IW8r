#pragma once
// Command palette (roadmap 9.13, Ctrl+Shift+P): type part of any menu command's name, Enter runs it.
#include <QDialog>
#include <QList>
#include <QString>

class QAction;
class QLineEdit;
class QListWidget;
class QMenuBar;
class QWidget;

namespace iw8r::qt {

struct PaletteEntry {
    QString path;     // "Compile > Time of Day > Dusk (low warm sun, long shadows)"
    QString shortcut;
    QAction* action = nullptr;
    bool enabled = true;  // false: listed greyed and not runnable (the command is unavailable right now)
};

// Every command reachable from the menu bar (submenus included), with its menu path; disabled ones too, marked
// enabled = false. With `commands` (the main window), its actions that are in no menu (shortcut-only commands) are
// listed as well, under "Other > ".
QList<PaletteEntry> paletteEntries(QMenuBar* bar, const QWidget* commands = nullptr);

// Fuzzy match: every query word must occur in `text` (case-insensitive) as a substring or as word initials ("tod"
// matches "Time of Day"). Returns -1 for no match, else a score where higher is better (whole words, earlier hits).
int paletteScore(const QString& query, const QString& text);

class CommandPalette : public QDialog {
    Q_OBJECT
public:
    // Lists the menu bar's commands and the parent's menu-less ones (MainWindow::addCommand adds every command to
    // the window).
    CommandPalette(QMenuBar* bar, QWidget* parent);
    void setQuery(const QString& q);  // also used by the GUI test
    int visibleCount() const;
    QString firstPath() const;  // the highlighted entry: the best enabled match
    bool runFirst();  // runs the highlighted command; false when the list is empty or the command is unavailable

private:
    void refill();
    QList<PaletteEntry> entries_;
    QLineEdit* edit_;
    QListWidget* list_;
};

}  // namespace iw8r::qt
