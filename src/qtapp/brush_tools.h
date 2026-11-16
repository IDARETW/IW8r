// IW8 Radiant (Qt shell) - brush tool operations on the current selection (one undo step each).
#pragma once

#include <QStringList>
#include <optional>
#include <string>
#include <vector>

#include "brush_ops.h"
#include "editor_context.h"

namespace iw8r::qt {

struct SelectedBrush {
    Id entity = kNoId;
    Id item = kNoId;
    ConvexBrush brush;
    const Primitive* source = nullptr;
};

std::vector<SelectedBrush> selectedBrushes(EditorContext& ctx);
// Rebuilds a brush primitive from new planes while keeping the original's non-face lines
// (leading comment, layer, contents, toolFlags) in place.
std::optional<Primitive> rebuildBrush(const Primitive& original, const ConvexBrush& b, const std::string& eol);
// Face suffix for new faces: the current IW8 material with default texture/lightmap fields.
std::string faceSuffix(const EditorContext& ctx);

bool createBoxBrush(EditorContext& ctx, const AABB& box);
bool hollowSelection(EditorContext& ctx);
bool subtractSelection(EditorContext& ctx);  // selected brushes cut every other world brush they touch
bool clipSelection(EditorContext& ctx, const Plane& keep, bool split);
bool rotateSelection(EditorContext& ctx, int axis, double degrees);
bool scaleSelection(EditorContext& ctx, const Vec3& factors);  // about the selection pivot; texture lock aware
// Rotates brushes and point entities (origin about `pivot`; Z rotation turns yaw) as one undo step.
bool rotateSelectionAbout(EditorContext& ctx, int axis, double degrees, const Vec3& pivot);
// Scale gizmo: brushes and patches scale by `factor` along `axis` (0..2) about `pivot`; model entities (misc_model,
// script_model, misc_prefab) take a uniform modelscale *= factor (IW8 models scale uniformly) and their origins move
// along the axis about the pivot. One undo step.
bool scaleSelectionAbout(EditorContext& ctx, int axis, double factor, const Vec3& pivot);
// Snap to grid (2.19): selected brushes/patches move together so their bounds minimum lands on the grid; selected
// point entities snap their origins. One undo step; false when nothing moved.
bool snapSelectionToGrid(EditorContext& ctx);

// Radiant select modes: the selected brushes are a template volume; it is deleted (one undo step) and
// everything matching it becomes the selection. Tall modes ignore the 2D view's depth axis.
enum class SelectMode { CompleteTall, PartialTall, Touching, Inside };
size_t selectByTemplate(EditorContext& ctx, SelectMode mode);
// target -> targetname chain of the selected entities, both directions (Radiant "Select Connected").
size_t selectConnected(EditorContext& ctx);
bool flipSelection(EditorContext& ctx, int axis);
bool prismFromSelection(EditorContext& ctx, int sides);
bool applyMaterialToSelection(EditorContext& ctx, const std::string& material);
std::string materialOfSelection(EditorContext& ctx);  // first face material of the first selected primitive

// Stock IW8 entity classes (data/iw8_entity_classes.json), most common first.
const QStringList& stockClassNames();
// Classes that are brush entities in stock maps (have brushes / trigger_* / script_brushmodel).
QStringList brushEntityClasses();

}  // namespace iw8r::qt
