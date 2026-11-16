// IW8 Radiant - per-vertex colour and alpha painting on patch control points (roadmap 8.11, research 57).
//
// Representation (the one the map format has, no new field):
//   The patch vertex line carries an optional colour block between the position and the texture pair:
//       v x y z c r g b a t s t ls lt
//   (samples/iw8/iw8_sample_room.map:167 `v 128 128 0 c 255 255 255 255 t 64 0 4 0`). Values are integers 0..255 in
//   R G B A order, the bytes x64-zt reads per world vertex ("color", iw3_fastfile.cpp:2264-2273, 2445-2447).
//   A control point without the block is white and opaque (255 255 255 255), the converter's default
//   (iw3_fastfile.cpp:119). Painting never touches the xyz or texture tokens of a vertex; a colour is written by
//   inserting the block after the position, or by overwriting the four values when it exists.
//   Control points are addressed by their index in Primitive::verts (file order, row-major for a complete grid,
//   the same order as patchGrid()).
//
// Brush colour: brushes have no vertices in the map format (faces are planes, map_text.h BrushFace), so there is
// nothing to paint on a brush. paintPatchVertices returns 0 for a non-patch.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "geometry.h"
#include "map_text.h"

namespace iw8r {

using Rgba8 = std::array<std::uint8_t, 4>;

// Colour of a control point with no colour block (x64-zt's default vertex colour).
inline constexpr Rgba8 kDefaultVertexRgba{255, 255, 255, 255};

// Channel mask bits for PaintBrush::channelMask.
enum PaintChannel : int { kPaintR = 1, kPaintG = 2, kPaintB = 4, kPaintA = 8, kPaintAll = 15 };

struct PaintBrush {
    float rgba[4] = {1.0f, 1.0f, 1.0f, 1.0f};  // target colour per channel, 0..1 (stored as 0..255, rounded)
    double radius = 0;          // world units; control points at distance >= radius are never changed
    double falloff = 0;         // 0 = hard: full weight out to the radius; 1 = soft: smooth from the centre to the edge
    double strength = 1;        // 0..1: blend amount at full weight
    int channelMask = kPaintAll;  // bit0 r, bit1 g, bit2 b, bit3 a; other channels are left as they are
};

// Weight in 0..1 of a control point at `distance` from the brush centre. Hard (falloff 0): 1 inside the radius.
// Otherwise 1 out to radius * (1 - falloff), then a smoothstep down to 0 at the radius. 0 at or beyond the radius,
// and for an invalid brush (non-finite or non-positive radius).
double brushWeight(const PaintBrush& b, double distance);

// Colour of every control point, in Primitive::verts order. A missing or malformed block reads as white.
// Empty when the primitive is not a patch.
std::vector<Rgba8> readPatchColours(const Primitive& p);

// True when control point `index` has a well-formed colour block in the text.
bool hasPatchColour(const Primitive& p, size_t index);

// Writes control point `index` to `c`. Overwrites the four values of an existing block, else inserts one after the
// position. Rewrites only that vertex's line, keeps every other byte, and refreshes the parsed views. Returns false
// for a non-patch or an out-of-range index.
bool setPatchVertexColour(Primitive& p, size_t index, const Rgba8& c);

// Blends the colour of the control points within `b.radius` of `center`. Each changed channel moves toward the brush
// colour by strength * brushWeight(distance), rounded to the nearest byte. Returns how many control points changed
// (a control point whose rounded colour stays the same is not counted or rewritten).
int paintPatchVertices(Primitive& patch, const Vec3& center, const PaintBrush& b);

// Roadmap 8.14: the slope blend of a terrain patch written into the vertex alpha (the layer blend of a layered
// material): alpha 0 where the surface is flatter than startDeg, 255 where it is steeper than endDeg, linear between
// (heightmap.h slopeBlendAlpha); `invert` swaps them. Red, green and blue stay. Returns how many control points
// changed; -1 when the patch is not a complete grid or the angles are not valid (endDeg > startDeg).
int applySlopeBlendAlpha(Primitive& patch, double startDeg, double endDeg, bool invert = false);

// True when any control point of the patch has a colour block (painted or authored).
bool patchHasColours(const Primitive& p);
// Per-point colours of tessellatePrimitive(p, subdiv), interpolated with the same Bezier grid as the positions (the
// colours are tessellated as if they were positions). Empty when the patch is not a complete grid. Build (8.11): the
// world JSON vertex `color` the converter reads (research 57).
std::vector<Rgba8> tessellatePatchColours(const Primitive& p, int subdiv);

}  // namespace iw8r
