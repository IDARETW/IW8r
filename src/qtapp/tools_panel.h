// IW8 Radiant Suite (Qt shell) - the Tools side bar panel (owner 2026-11-15: "a lot of whats in the Tools > menu should
// be integrated as new sidebar items or into existing sidebar items").
//
// The Tools menu's commands in groups (map checks, placement, materials, models / effects / sounds, gameplay,
// automation, stock assets, editor), each a button with a one-line description under it, filtered by the search box.
// The buttons run the menu's own QActions (one command, two places; checkable ones show their state). The asset
// panels also get the commands of their kind (AssetBrowser::setToolActions).
#pragma once

#include <QList>
#include <QString>
#include <QWidget>

class QAction;
class QLineEdit;
class QVBoxLayout;

namespace iw8r::qt {

class ToolsPanel : public QWidget {
    Q_OBJECT
public:
    struct Tool {
        QAction* action = nullptr;
        QString label;        // short button text ("" = the action's text without its "(...)" and "...")
        QString description;  // one line under the button
    };
    explicit ToolsPanel(QWidget* parent = nullptr);
    void addGroup(const QString& title, const QList<Tool>& tools);
    void clear();
    void setFilter(const QString& text);  // words of the label, description or group
    int toolCount() const;                // buttons (tests)
    int visibleToolCount() const;
    QStringList groups() const;

private:
    QLineEdit* search_ = nullptr;
    QVBoxLayout* list_ = nullptr;
    struct Row {
        QWidget* widget;
        QString hay;
        QWidget* group;
    };
    QList<Row> rows_;
    QList<QPair<QString, QWidget*>> groups_;
};

}  // namespace iw8r::qt
