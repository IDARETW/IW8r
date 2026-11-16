// IW8 Radiant (Qt shell) - editor commands over the core generator modules: CSG merge, extrude,
// primitives, patch cap/weld/redisperse/split/terrain/noise/smooth, rope, model scatter, drop to floor.
// Each is one undo step and returns false (changing nothing) when it does not apply.
#pragma once

#include <QString>

#include "editor_context.h"

namespace iw8r::qt {

bool mergeSelectedBrushes(EditorContext& ctx);
bool extrudeSelectedFace(EditorContext& ctx, double distance);

enum class PrimitiveKind { Cone, Sphere, Stairs, Arch, Ramp };
bool createPrimitive(EditorContext& ctx, PrimitiveKind kind);  // selection bounds (replaced) or a box at the view centre

bool capSelectedPatches(EditorContext& ctx);
bool weldSelectedPatches(EditorContext& ctx);       // exactly two patches sharing an edge
bool redisperseSelectedPatches(EditorContext& ctx);
bool splitSelectedPatches(EditorContext& ctx);       // in the middle
bool createTerrain(EditorContext& ctx);              // 9x9 terrain mesh over the selection bounds or a 512 box
bool noiseSelectedPatches(EditorContext& ctx, double amplitude, uint32_t seed);
bool smoothSelectedPatches(EditorContext& ctx, int iterations);
// Height maps (8.14, core heightmap.*): the first selected patch. Import stretches the image's range to zMin..zMax;
// export writes a 16-bit PGM over the patch's own height range. One undo step for the edits.
bool importHeightMapToSelectedPatch(EditorContext& ctx, const QString& file, double zMin, double zMax, QString* err);
bool exportSelectedPatchHeightMap(EditorContext& ctx, const QString& file, QString* err);
bool erodeSelectedPatches(EditorContext& ctx, bool hydraulic, int iterations);
// Roadmap 8.14: the slope blend of every selected terrain patch written into its vertex alpha (one undo step).
// Returns how many patches changed.
int slopeBlendSelectedPatches(EditorContext& ctx, double startDeg, double endDeg, bool invert);

bool createRope(EditorContext& ctx);                 // between the first two selected point entities (or primitives' centres)
size_t scatterModels(EditorContext& ctx, const QString& model, int count, double radius, uint32_t seed);
// Model paint (8.3): scatters `count` static models (misc_model) on the brush tops within `radius` of `centre`, random
// yaw and 0.8-1.2 scale, one undo step. Returns how many were placed.
size_t paintModelsAt(EditorContext& ctx, const QString& model, const Vec3& centre, int count, double radius, uint32_t seed);
size_t dropSelectionToFloor(EditorContext& ctx);     // point entities onto the floor below them

}  // namespace iw8r::qt
