// IW8 Radiant (Qt shell) - the Map Check side bar panel (owner 2026-11-15: "we don't need a separate tools menu,
// what we need to do with that is implement any appropriate tools into already-existing menus or into new menus").
//
// The map's Check Map findings in the flush panel layout (panel_chrome.h): the header with the count, a re-check
// button and the dots menu holding the map's check tools (Leak Check, Clear Leak Line, Asset Usage, Unused Own Assets,
// the Check Map window); chips by kind (fixable, gameplay, entities, lighting, build, leak); one row per finding with
// its severity; the strip says the whole finding, with Fix (Map Doctor findings) and Go To (a finding that names an
// entity or a position: selects it and moves the views there). Checked again a moment after the map changes while
// the panel is shown.
#pragma once

#include <QList>
#include <QStringList>
#include <QWidget>
#include <vector>

#include "editor_context.h"
#include "map_doctor.h"

class QAction;
class QListWidget;
class QMenu;
class QPushButton;
class QTimer;
class QToolButton;

namespace iw8r::qt {

class PanelHeader;
class ChipBar;
class DetailsStrip;

class MapCheckPanel : public QWidget {
    Q_OBJECT
public:
    explicit MapCheckPanel(EditorContext* ctx, QWidget* parent = nullptr);
    // The check tools of the Tools menu, in the dots menu (and Leak Check as a header button).
    void setTools(const QList<QAction*>& actions);
    QList<QAction*> tools() const { return tools_; }
    void recheck();
    int findingCount() const;
    int visibleCount() const;
    QStringList lines() const { return lines_; }
    QString countText() const;
    QString stripTitle() const;
    QString stripFacts() const;
    void setChip(const QString& id);  // "all", "fix", "gameplay", "entities", "lighting", "build", "leak", "map"
    void selectRow(int row);
    size_t fixAll();
    // The kind of a Check Map line (its chip) and whether it is an error, a warning, a note or a fix.
    static QString kindOf(const QString& line);
    static QString severityOf(const QString& line);

protected:
    void showEvent(QShowEvent* e) override;

private:
    void updateStrip();
    void applyChip();
    void goTo(int row);
    EditorContext* ctx_;
    PanelHeader* header_ = nullptr;
    ChipBar* chips_ = nullptr;
    QListWidget* list_ = nullptr;
    DetailsStrip* strip_ = nullptr;
    QMenu* menu_ = nullptr;
    QToolButton* leak_ = nullptr;
    QPushButton* fix_ = nullptr;
    QPushButton* go_ = nullptr;
    QTimer* again_ = nullptr;
    QList<QAction*> tools_;
    QStringList lines_;
    std::vector<DoctorFix> fixes_;
    QString summary_;
    uint64_t checkedRevision_ = uint64_t(-1);
};

}  // namespace iw8r::qt
