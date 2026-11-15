// IW8 Radiant Suite (Qt shell) - 2D orthographic grid view (XY / XZ / YZ) drawn with QPainter.
// Radiant conventions: LMB drag on empty space creates a brush; LMB drag inside the selection moves it;
// LMB drag outside a selected brush resizes the faces facing the drag; Shift+LMB selects; in clipper
// mode (X) LMB places clip points (a third one tilts the plane); E mode drags edge midpoints of selected brushes;
// the terrain brush (LMB raises, Shift+LMB lowers selected patches) works in the XY top view; RMB click opens the
// context menu; RMB/MMB drag pans; wheel zooms. A press becomes a move, create or resize drag only once the mouse has
// moved 3 px (a click changes nothing); Escape or losing focus during a drag puts it back.
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
    // 2D zoom commands (View menu): one wheel step in or out about the view centre, and the whole map fitted in the
    // view (the open-map fit). They share the wheel's limits (EditorContext::clampGridScale).
    void zoomIn();
    void zoomOut();
    void fitAll();
    // Spacing of the minor grid lines as drawn: the grid size, doubled while the lines would be under 6 px apart.
    double drawnGridStep() const;
    bool dragActive() const { return drag_ != Drag::None; }  // a button is down on this view (also used by --gui-test)

signals:
    // Cursor position in world units, snapped; the view's depth axis is not known here and is 0.
    void mouseWorld(double x, double y, double z);
    // The cursor position as text for the status bar, the view's two axes only ("x 128  y -64"); empty when the
    // cursor leaves the view.
    void positionText(const QString& text);
    void mouseLeft();
    // An asset dragged from a browser was dropped at world (h, v) of this view (4.24).
    void assetDropped(const QString& kind, const QString& name, bool loop, double h, double v);

protected:
    void dragEnterEvent(QDragEnterEvent*) override;
    void dragMoveEvent(QDragMoveEvent*) override;
    void dropEvent(QDropEvent*) override;

protected:
    bool event(QEvent*) override;
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void leaveEvent(QEvent*) override;
    void focusOutEvent(QFocusEvent*) override;

private:
    // A camera move repaints only around the old and the new camera marker; any other camera notification (also used
    // as "repaint every view") repaints the whole view.
    void cameraChangedRepaint();
    QRect cameraMarkerRect() const;
    QRect lastCameraMarker_;
    double lastCam_[5] = {0, 0, 0, 0, 0};
    double lastView_[4] = {-1, 0, 0, 0};  // axis, centre h / v, scale at the last camera notification

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

    // Hit tests shared by the press and the hover cursor.
    std::optional<EdgeHandle> edgeHandleAt(const QPointF& pos) const;        // edge mode handle within 6 px
    std::vector<VertexHandle> vertexHandlesAt(const QPointF& pos) const;     // vertex mode handles within 5 px
    bool overSelection(double wh, double wv);                                // inside the selection box (+4 px): move
    std::vector<size_t> facingFaces(const ConvexBrush& b, const Vec3& centre, double wh, double wv) const;
    std::vector<ResizeTarget> resizeTargetsAt(double wh, double wv);         // Radiant face drag from (wh, wv)
    void updateHoverCursor(const QPointF& pos, Qt::KeyboardModifiers mods);
    // The selection's bounds and brushes, rebuilt when the document revision changes (selecting bumps it too):
    // the hover test runs on every mouse move, and bounds and brush planes are parsed from the map text.
    struct SelectionCache {
        uint64_t revision = 0;
        AABB bounds;
        std::vector<ResizeTarget> brushes;  // faces left empty
        std::vector<Vec3> centres;          // centre of each brush's bounds
    } selCache_;
    const SelectionCache& selectionCache();

    // Drags: the mouse button that holds the current one, starting a pending press, putting a drag back.
    Qt::MouseButtons dragButtons() const { return drag_ == Drag::Pan ? Qt::MouseButtons(Qt::RightButton | Qt::MiddleButton) : Qt::MouseButtons(Qt::LeftButton); }
    bool editDragActive() const { return drag_ != Drag::None && drag_ != Drag::Pan; }
    void startPendingDrag();
    void cancelDrag(const QString& status);
    static constexpr int kDragThreshold = 3;  // px the mouse must move before a press becomes a move / create / resize
    bool dragLive_ = true;                    // false while a move / create / resize press waits for that movement
    bool terrainChanged_ = false;             // the current terrain stroke changed a patch (it is one undo step)

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
