// IW8 Radiant Suite (Qt shell) - patch (curve/mesh) tools and vertex editing on the current selection.
// Every operation is one undo step; it returns false (and changes nothing) when it does not apply.
#pragma once

#include <functional>
#include <optional>
#include <vector>

#include "editor_context.h"
#include "patch_ops.h"

namespace iw8r::qt {

enum class PatchShape { Flat3x3, Flat5x5, Cylinder, Bevel, Endcap };

// Radiant "Patch > Create ...": built from the selected brushes' bounds (the brushes are replaced),
// or a grid-sized box at the 2D view centre when nothing is selected. Flat patches face the 2D view.
bool createPatch(EditorContext& ctx, PatchShape shape);
// Applies `f` to every selected patch (nullopt from f = leave that patch alone).
bool editSelectedPatches(EditorContext& ctx, const std::function<std::optional<PatchGrid>(const PatchGrid&)>& f,
                         const std::string& name);
bool invertSelectedPatches(EditorContext& ctx);
bool transposeSelectedPatches(EditorContext& ctx);
bool naturalTextureSelectedPatches(EditorContext& ctx);
bool insertPatchRowCol(EditorContext& ctx, bool row);  // in the middle of the patch
bool removePatchRowCol(EditorContext& ctx, bool row);
bool thickenSelectedPatches(EditorContext& ctx, double amount, bool seams);
size_t selectedPatchCount(EditorContext& ctx);

// Vertex mode (Radiant V): control points of selected patches and corners of selected brushes.
struct VertexHandle {
    Vec3 pos;
    Id entity = kNoId, item = kNoId;
    bool patch = false;
    size_t index = 0;  // patch control point index (row-major); unused for brushes
    bool locked = false;  // patch control point locked (2.21): drawn differently and never moved
};
std::vector<VertexHandle> selectionVertexHandles(EditorContext& ctx);
// Moves the given handles by delta as one undo step. Brushes that would become invalid are left
// unchanged and counted in *rejected.
bool moveVertexHandles(EditorContext& ctx, const std::vector<VertexHandle>& handles, const Vec3& delta, size_t* rejected = nullptr,
                       size_t* lockedSkipped = nullptr);

// 2.21 Patch > Lock Border / Lock Corners / Unlock Vertices on the selected patches (ctx.patchLocks). Returns the
// number of patches changed.
enum class PatchLockMode { Border, Corners, Unlock };
size_t lockSelectedPatchVertices(EditorContext& ctx, PatchLockMode mode);

// 2.21 Patch Inspector: the control points of the first selected patch, and writing edited values back (one undo
// step). Values per point: x y z s t. False when the count does not match or nothing changed.
std::optional<PatchGrid> firstSelectedPatchGrid(EditorContext& ctx);
bool applyPatchPoints(EditorContext& ctx, const std::vector<std::array<double, 5>>& points);

}  // namespace iw8r::qt
