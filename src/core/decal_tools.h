// IW8 Radiant Suite - decal tool (roadmap 8.10, phase A; research docs/research/46_decals_volumetrics_glass.md section 4.4).
//
// A decal is a flat 2 x 2 "mesh" patch lying on a surface, offset a little along the surface normal, with an
// alpha-tested custom material. It is built from today's converter with no fork change.
//
// Non-colliding: the editor's collision world skips a patch whose text contains "nonColliding"
// (collision_world.cpp). placeDecal writes a body line `contents nonColliding;` after the material line of
// the patch (the same words brush flags use; brush_flags.cpp does not read patch bodies). Whether the
// converter honours that line on a patch is UNVERIFIED in this repo.
//
// Facing: a patch faces cross(column direction, row direction) (patch_ops.h). Every decal faces the surface
// normal it was built with.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "document.h"
#include "geometry.h"
#include "map_text.h"
#include "patch_ops.h"

namespace iw8r {

// One flat 2 x 2 "mesh" patch centred on hitPoint, lying in the plane of surfaceNormal (normalized here), lifted
// by `offset` along that normal (0.5 is the recommended value). width runs along the in-plane axis u, height
// along v, and the in-plane frame is rotated by rotationDeg about the normal (right-handed). st runs 0..1 over the
// decal: s along width, t along height. Returns nullopt for a non-finite argument, width <= 0, height <= 0, a zero
// normal or an empty material.
//
// With rotationDeg = 0 a +Z normal gives u = +X and v = +Y, so the corners are hitPoint +/- (width/2, height/2).
// For a +Z normal the front faces +Z.
std::optional<PatchGrid> makeDecalPatch(const Vec3& hitPoint, const Vec3& surfaceNormal, double width, double height,
                                        double rotationDeg, double offset, const std::string& material);

// Decal that follows the geometry it covers. The decal rectangle (same frame as makeDecalPatch, without the offset)
// is projected along the normal onto every worldspawn brush face that:
//   - is a solid, rendered face (collides, material not a tool texture: nodraw, caulk, clip, hint, skip, sky, trigger,
//     water),
//   - has an outward normal within 45 degrees of `normal` (steeper faces are not painted), and
//   - lies within max(width, height) of the decal plane along the normal (faces further away are ignored).
// The projected face polygon is clipped against the decal rectangle, and each clipped piece is lifted onto its own
// face plane and offset along that face's normal. So a decal that runs over a step down onto a lower floor gets one
// patch on each level. Decal pieces are NOT occluded by other faces: a face under another face inside the depth band
// is painted too.
//
// Each piece becomes one 2 x 2 patch: a quad when the clipped polygon has 4 vertices, otherwise one degenerate
// 2 x 2 patch per fan triangle (corners repeated, which is a triangle under bilinear interpolation). st is the
// decal's own 0..1 coordinate at each vertex, so texture runs continuously across the pieces.
//
// Overhangs past an edge onto a steep face are dropped, not wrapped. If no face is found (for example a hit on a
// patch or a brush that does not collide), the result is the single planar patch from makeDecalPatch. Empty when
// makeDecalPatch rejects the arguments.
std::vector<PatchGrid> makeConformingDecal(const MapFile& map, const Vec3& hitPoint, const Vec3& normal, double width,
                                           double height, double rotationDeg, double offset,
                                           const std::string& material);

// Adds the patches to worldspawn as ONE undo step ("Place Decal") and selects them. nonColliding writes the
// `contents nonColliding;` body line (see the header comment). Returns false when there is no worldspawn, no patch
// or nothing changed.
bool placeDecal(Document& doc, const std::vector<PatchGrid>& patches, bool nonColliding);

}  // namespace iw8r
