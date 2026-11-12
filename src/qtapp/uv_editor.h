#pragma once
// UV editor (roadmap 2.27): the selected brush faces drawn in texture space over their tiled texture, with a handle on
// every vertex.
//  - Unwrap lays each selected brush out as a net (core uv_unwrap: a cube becomes a cross) so the texture runs across
//    shared edges; faces keep their true shapes at the current texel size.
//  - Dragging a vertex handle moves that UV; vertices that coincide (a shared corner of two faces in the net) move
//    together. A brush face stores one projection (scale, offset, rotation) in the .map format, not per-vertex UVs, so
//    the face is re-fitted to the moved vertex (least squares) and its other vertices follow as closely as the format
//    allows.
//  - Dragging empty space moves every face's texture, the wheel scales it, Ctrl+drag rotates it, Shift+wheel zooms,
//    middle-drag pans.
// Apply writes every changed face in one undo step (Document::editPrimitives); Reset drops all pending changes.
#include <QDialog>
#include <QImage>
#include <functional>
#include <vector>

#include "document.h"
#include "tex_projection.h"

class QLabel;

namespace iw8r::qt {

class EditorContext;

class UvEditor : public QDialog {
    Q_OBJECT
public:
    using ImageFor = std::function<QImage(const QString& material)>;
    UvEditor(EditorContext* ctx, ImageFor imageFor, QWidget* parent = nullptr);

    // Faces being edited (from the face selection, else every face of the selected brushes). Returns how many.
    int loadSelection();
    // Pending change applied to every face: offset in texture repeats, scale factor, rotation in degrees.
    void setPending(double du, double dv, double scale, double rotDeg);
    // Lays every selected brush out as a net (pending until Apply). Returns the faces laid out.
    int unwrap();
    // Moves the UV of vertex `vertex` of face `face` (index into the loaded faces) to `uv`, re-fitting that face and every
    // face with a coinciding vertex. Pending until Apply. False for a bad index.
    bool moveVertex(int face, int vertex, double u, double v);
    // The face's UVs with every pending change (tests, drawing).
    std::vector<std::array<double, 2>> faceUvs(int face) const;
    // Writes the pending changes (one undo step). False when nothing changes.
    bool apply();
    int faceCount() const { return int(faces_.size()); }
    // Patches (true per-vertex UVs: each control point stores its own texture pair).
    int patchCount() const { return int(patches_.size()); }
    std::vector<std::array<double, 2>> patchUvs(int patch) const;  // control points, row-major, with pending changes
    bool movePatchVertex(int patch, int ctrl, double u, double v);
    // Mesh layout (default): every selected brush unfolded into a net by its geometry (a cube as a cross), each face
    // showing its current texture; nothing changes until a vertex is dragged. Off: the faces in texture space.
    void setLayoutView(bool on);
    bool layoutView() const { return layout_; }
    // Layout view: drags vertex `vertex` of `face` by (dx, dy) world units in the net; the vertex's UV moves by the face's
    // inverse mapping (the texture slides under it). Tests.
    bool dragLayoutVertex(int face, int vertex, double dx, double dy);
    QString densityText() const;

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    struct Face {
        Id entity, item;
        size_t face;
        Vec3 normal;
        std::vector<Vec3> winding;
        TexParams tp;    // as in the map
        TexParams base;  // working params (unwrap / vertex edits), before the global move / scale / rotate
        QString material;
        std::vector<std::array<double, 2>> net;  // layout position of each winding vertex (world units, brushes side by side)
    };
    struct Patch {
        Id entity, item;
        int rows = 0, cols = 0;
        std::vector<Vec3> pos;
        std::vector<std::array<double, 2>> st0, base;  // as in the map / working (repeats), row-major
        QString material;
    };
    std::vector<Patch> patches_;
    TexParams edited(const Face& f) const;  // base with the pending global change
    void bakeGlobal();                      // folds the global change into every base
    QPointF toScreen(double u, double v) const;
    QPointF netToScreen(double x, double y) const;
    void computeLayout();
    // Affine UV -> net of a face from its first non-degenerate vertex triple: x = a u + b v + c, y = d u + e v + f.
    bool faceAffine(int face, double m[6]) const;
    std::array<double, 2> toUv(const QPointF& p) const;
    void fitView();
    void refreshInfo();
    // Handles at the same world point and UV as (face, vertex): the corner shared by faces of an unwrapped net.
    std::vector<std::pair<int, int>> coincident(int face, int vertex) const;
    void moveGroup(const std::vector<std::pair<int, int>>& group, double u, double v);

    EditorContext* ctx_;
    ImageFor imageFor_;
    std::vector<Face> faces_;
    Vec3 pivot_;  // world centre of the faces: scale and rotation keep its UV
    double du_ = 0, dv_ = 0, scale_ = 1, rot_ = 0;
    double view_ = 120;  // pixels per texture repeat
    QPointF centre_;     // UV at the canvas centre
    QPoint lastMouse_;
    bool layout_ = true;
    double netScale_ = 1;  // pixels per world unit in the layout view
    QPointF netCentre_;    // net point at the canvas centre
    std::array<double, 2> grabUv_{};  // layout drag: the grabbed vertex's UV and mouse position when the drag started
    QPointF grabMouse_;
    bool dragging_ = false, rotating_ = false, panning_ = false;
    // Vertex drag: the handles moved together (face, vertex) and the UVs of their faces when the drag started.
    std::vector<std::pair<int, int>> grabbed_;
    std::vector<std::pair<int, int>> grabbedPatch_;  // (patch, control point)
    std::array<double, 2> grabStart_{};
    QPoint pressPos_;
    QImage texture_;
    QLabel* info_ = nullptr;
};

}  // namespace iw8r::qt
