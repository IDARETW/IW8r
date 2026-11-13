// IW8 Radiant Suite (Qt shell) - state shared by all views and panels.
#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <map>
#include <optional>
#include <vector>

#include "camera.h"
#include "document.h"
#include "scene.h"
#include "vertex_paint.h"
#include "reference_plan.h"

namespace iw8r::qt {

using app::Camera;
using app::SceneCache;

class EditorContext : public QObject {
    Q_OBJECT
public:
    Document doc;
    SceneCache scene;
    Camera cam;
    // Stock map reference in the 2D views (3.19): one backdrop image per view axis, empty when no reference is loaded.
    std::vector<ReferencePlan> referencePlans;

    int gridAxis = 0;  // 0 XY top, 1 XZ front, 2 YZ side
    // 2.21: locked patch control points, per patch (entity, item), row-major; editor-side only (the map format has
    // no lock field). Vertex moves leave locked points where they are.
    std::map<std::pair<Id, Id>, std::vector<bool>> patchLocks;
    // View > Heat Map in the top 2D view (5.15 / 6.18): 0 none, 1 light overlap at the camera height, 2 spawn density.
    int heatMap = 0;
    // View > Colour Grade Preview (3.15): iw8r_film / iw8r_lut applied in the camera.
    bool gradePreview = true;
    // View > Effect Preview in Camera (8.1): fx_origin entities play their effect's particles in the textured camera.
    bool effectPreview = false;
    double gridCenterH = 0, gridCenterV = 0, gridScale = 0.5;  // pixels per unit
    bool snap = true;
    bool showSpawns = true;
    // View > Entity Display (2.17): labels in the 2D views, facing arrows, model origin crosses.
    bool showEntityNames = false;
    bool showAngles = true;
    bool showModelOrigins = false;
    // Gizmo space (3.6): world axes, or the single selected entity's own axes (its angles) for moves.
    bool gizmoLocal = false;
    bool normalMaps = true;  // textured view: bump and gloss from the materials' normal maps (3.2)
    bool texturedView = true;
    bool fogPreview = true;  // camera fog from worldspawn iw8r_fog (View > Fog Preview)
    bool gizmo = true;          // translate gizmo on the selection in the camera view (G toggles)
    bool playing = false;       // camera Play preview: walk the map as the player (P / Esc)  // View > Textured: stock IW8 colour images on faces when available

    // Current IW8 material for new brushes and "Apply material" (a Replay 1.20 stock material name).
    std::string currentMaterial = "tm2co/me_plaster_barrier_fence_wall";
    // Model paint mode (8.3, Tools > Model Paint Mode): a left click in the camera scatters paintCount copies of
    // paintModel within paintRadius of the clicked surface. Empty = off.
    QString paintModel;
    // Vertex paint mode (8.11, Patch > Vertex Paint Mode): a left click in the camera paints the patch control points
    // within vertexBrush.radius of the clicked surface (core vertex_paint), one undo step per click.
    bool behaviourPreview = false;  // View > Behaviour Preview: iw8r_spin / iw8r_bob models move in the camera
    bool windPreview = false;  // View > Wind Preview (5.23): foliage sways in the textured camera
    bool vertexPaint = false;
    PaintBrush vertexBrush{{1.0f, 0.0f, 0.0f, 1.0f}, 96, 0.5, 0.5, kPaintAll};
    // Paints every patch near `at` with vertexBrush; returns the control points changed.
    int vertexPaintAt(const Vec3& at);
    // Decal mode (8.10, Brush > Decal Mode): a left click in the camera projects the current material as a decal onto
    // the clicked brush face (conformed over neighbouring faces, non-colliding patch pieces), one undo step per click.
    bool decalMode = false;
    double decalSize = 64, decalRotation = 0;
    // Places a decal where `ray` hits primitive `hit` at distance t; returns the pieces placed (0 = nothing).
    int decalAt(const Ray& ray, double t, const SelRef& hit);
    int paintCount = 5;
    double paintRadius = 128;
    // Clipper (Radiant X): two points in the 2D view, optionally a third point that tilts the plane (it sits one grid
    // step deeper along the view's depth axis); Ctrl+Enter flips which side is kept.
    bool clipMode = false;
    // Measure tool (2.19, Tools > Measure Tool): left-drag in a 2D view draws a ruler; the last one stays until the next.
    bool measureMode = false;
    bool measureValid = false;
    Vec3 measureA, measureB;  // world points (view-plane axes snapped to the grid)
    std::vector<Vec3> clipPoints;
    bool clipFlip = false;
    // Vertex mode (Radiant V): drag brush corners and patch control points of the selection in the 2D view.
    bool vertexMode = false;
    // Edge mode (Radiant E): drag the midpoint handle of an edge of a selected brush (exclusive with vertex mode).
    bool edgeMode = false;
    // Terrain brush: LMB raises (Shift lowers) the selected patches around the cursor in the XY top view.
    bool terrainBrush = false;
    // Brush mode while terrainBrush is on: Raise (LMB raises, Shift lowers), Smooth, Flatten (to the height of the
    // control point nearest the stroke start) or Noise.
    enum class TerrainBrushMode { Raise, Smooth, Flatten, Noise };
    TerrainBrushMode terrainBrushMode = TerrainBrushMode::Raise;
    double terrainRadius = 4;     // brush radius in grid units
    double terrainStrength = 0.5; // Smooth / Flatten: fraction of the way to the target per step (0..1)

    QSet<int> heldKeys;   // camera keys currently down (Qt::Key values)
    bool looking = false; // right mouse held in the camera view

    int axisH() const { return gridAxis == 2 ? 1 : 0; }
    int axisV() const { return gridAxis == 0 ? 1 : 2; }
    int axisD() const { return gridAxis == 0 ? 2 : (gridAxis == 1 ? 1 : 0); }

    // Radiant Region: the active box (nullopt = off). Primitives and point entities outside it are hidden in all views.
    std::optional<AABB> region;
    void refreshRegion();  // recomputes scene.hiddenByRegion from `region` (called by documentEdited)
    void refreshGameMode();  // recomputes scene.hiddenByMode from scene.gameMode (called by documentEdited)
    // Call after any document mutation (edits, undo, selection, load).
    void documentEdited(const QString& status = {});
    void cameraMoved();
    void setStatus(const QString& s);  // also written to the Console log (EditorLog, source "status")
    // Applies held camera keys (stock Radiant bindings; W/A/S/D/Q/E while looking).
    void tickCamera(double dt, bool shiftHeld);
    void frameSelection();

    // The clip plane from the two clip points and the 2D view's depth axis (nullopt until both points exist).
    std::optional<Plane> clipPlane() const;

signals:
    void playModeChanged(bool playing);
    void documentChanged();
    void cameraChanged();
    void statusMessage(const QString& s);
};

}  // namespace iw8r::qt
