// IW8 Radiant Suite (Qt shell) - 2D orthographic grid view (XY / XZ / YZ) drawn with QPainter.
// Radiant conventions: LMB drag on empty space creates a brush; LMB drag inside the selection moves it;
// LMB drag outside a selected brush resizes the faces facing the drag; Shift+LMB selects; in clipper
// mode (X) LMB places clip points (a third one tilts the plane); E mode drags edge midpoints of selected brushes;
// the terrain brush (LMB raises, Shift+LMB lowers selected patches) works in the XY top view; RMB click opens the
// context menu; RMB/MMB drag pans; wheel zooms.
#pragma once

#include <QWidget>

#include "brush_ops.h"
#include "brush_ops2.h"
#include "editor_context.h"
#include "patch_tools.h"

namespace iw8r::qt {

class GridView : public QWidget {
    Q_OBJECT
public:
    explicit GridView(EditorContext* ctx, QWidget* parent = nullptr);
    QPointF worldToWidget(double h, double v) const;  // also used by --gui-test
    double widgetToWorldH(double x) const;
    double widgetToWorldV(double y) const;
    // Four-view layout (3.22): a view with a fixed axis (0 XY, 1 XZ, 2 YZ) keeps its own axis, pan and zoom; -1 (the
    // main 2D view) follows the context (Ctrl+Tab). Its event handlers swap its state into the context and back.
    void setFixedAxis(int axis);
    int fixedAxis() const { return fixedAxis_; }

signals:
    void mouseWorld(double x, double y, double z);
    // An asset dragged from a browser was dropped at world (h, v) of this view (4.24).
    void assetDropped(const QString& kind, const QString& name, bool loop, double h, double v);

protected:
    void dragEnterEvent(QDragEnterEvent*) override;
    void dragMoveEvent(QDragMoveEvent*) override;
    void dropEvent(QDropEvent*) override;

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;

private:
    // A camera move repaints only around the old and the new camera marker; any other camera notification (also used
    // as "repaint every view") repaints the whole view.
    void cameraChangedRepaint();
    QRect cameraMarkerRect() const;
    QRect lastCameraMarker_;
    double lastCam_[5] = {0, 0, 0, 0, 0};

    int fixedAxis_ = -1;
    // Heat map cache (View > Heat Map): rebuilt when the mode, the document or (light overlap) the camera height changes.
public:
    struct HeatMap {
        int mode = 0;
        int cellsX = 0, cellsY = 0;
        double minX = 0, minY = 0, maxX = 0, maxY = 0;
        std::vector<double> values;  // row-major, row = y
        double maxValue = 0;
    };
    const HeatMap& heatMap();  // also used by the GUI test
private:
    HeatMap heat_;
    uint64_t heatKey_ = 0;
    double ownCenterH_ = 0, ownCenterV_ = 0, ownScale_ = 0.5;
    int scopeDepth_ = 0;
    struct SavedView {
        int axis = 0;
        double h = 0, v = 0, scale = 0.5;
    } saved_;
    // Swaps this view's own axis / pan / zoom into the context for the duration of one handler (nesting-safe).
    struct AxisScope {
        GridView* v;
        explicit AxisScope(GridView* view);
        ~AxisScope();
    };
    enum class Drag { None, Pan, Move, Create, Resize, Vertex, Edge, Terrain, Measure };
    struct ResizeTarget {
        Id entity = kNoId, item = kNoId;
        ConvexBrush original;
        std::vector<size_t> faces;
        const Primitive* source = nullptr;
    };
    // Edge mode handle: one edge of a selected brush, with the brush it belongs to.
    struct EdgeHandle {
        Id entity = kNoId, item = kNoId;
        ConvexBrush brush;
        BrushEdge edge;
        Vec3 mid;
    };
    std::vector<EdgeHandle> selectionEdgeHandles() const;
    void terrainStamp(double h, double v);  // one raise/lower step of the terrain brush at (h, v)
    double snapped(double v) const;
    void showContextMenu(const QPoint& globalPos, double wh, double wv);
    std::vector<ConvexBrush> resizePreview() const;
    AABB createBox() const;

    EditorContext* ctx_;
    Drag drag_ = Drag::None;
    QPointF lastPos_, pressPos_;
    double dragStartH_ = 0, dragStartV_ = 0, dragNowH_ = 0, dragNowV_ = 0;
    std::vector<ResizeTarget> resize_;
    std::vector<VertexHandle> vertexDrag_;  // handles under the press point (all depths, Radiant 2D pick)
    std::optional<EdgeHandle> edgeDrag_;    // edge under the press point (the one nearest the viewer)
    double terrainSign_ = 1;                 // +1 raises, -1 lowers (Shift)
    double terrainFlatZ_ = 0;                // Flatten target height, taken at the stroke start
    unsigned terrainSeed_ = 1;               // Noise mode: seed for the next step
    double stampH_ = 0, stampV_ = 0;         // where the last terrain step was applied
    QPointF hover_;                          // last cursor position in the widget (terrain brush outline)
    Vec3 vertexDelta() const;
};

}  // namespace iw8r::qt
