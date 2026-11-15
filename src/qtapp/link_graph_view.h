// IW8 Radiant Suite (Qt shell) - Links dock (roadmap 6.25): the map's target / targetname and script_linkto chains as
// a node view (core link_graph + link_graph_layout). Boxes show classname and targetname, arrows point from the entity
// holding the key to the one it names; checkLinks problems are drawn red. Click selects the entity (Ctrl adds), a click
// on empty space clears the selection, double-click also centres the camera on it; wheel zooms, right or middle (or left
// on empty space) drag pans, F fits the graph. A node's tooltip has its full names, links and problems. Rebuilt when the
// map changes.
#pragma once

#include <QStringList>
#include <QWidget>

#include <map>
#include <set>

#include "editor_context.h"
#include "link_graph.h"
#include "link_graph_layout.h"

namespace iw8r::qt {

class LinkGraphView : public QWidget {
    Q_OBJECT
public:
    explicit LinkGraphView(EditorContext* ctx, QWidget* parent = nullptr);
    void rebuild();                     // from the current document (called on document changes)
    size_t nodeCount() const { return layout_.nodes.size(); }
    size_t edgeCount() const { return graph_.edges.size(); }
    size_t issueCount() const { return issues_.size(); }
    // Widget position of an entity's box centre (tests); invalid point when the entity has no node.
    QPointF nodeCenter(Id entity) const;
    // F: zoom and pan so the whole graph fits the view (never larger than zoom 1).
    void fitView();
    double zoom() const { return zoom_; }

protected:
    bool event(QEvent*) override;  // F before the shortcuts; node tooltips
    void keyPressEvent(QKeyEvent*) override;
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    QPointF toWidget(double x, double y) const;
    Id nodeAt(const QPointF& p) const;
    EditorContext* ctx_;
    LinkGraph graph_;
    LinkGraphLayout layout_;
    std::map<Id, size_t> nodeIndex_;  // entity -> index in layout_.nodes
    std::set<Id> issues_;             // entities with a checkLinks error or warning
    std::map<Id, QStringList> issueText_;  // their messages (tooltip)
    std::map<Id, QString> label_;     // "classname\ntargetname"
    double zoom_ = 1.0;
    QPointF pan_{40, 30};
    QPoint lastMouse_;
    QPoint pressAt_;
    bool panning_ = false;
    bool clickInEmpty_ = false;  // a left press on empty space: a release without a drag clears the selection
    uint64_t builtRevision_ = ~0ull;
};

}  // namespace iw8r::qt
