// IW8 Radiant - shared Radiant/Quake texture projection.
//
// One implementation for the compiler (compile_iw8.cpp), the surface inspector and the editor tools.
// The maths is the one compile_iw8.cpp used before this module existed; results are identical.
//
// Field meanings (docs/research/02_iwmap4_format.md, section 3 "Brush block", face token table and
// "Numeric fields"): the five numbers after the material are N1 width (texture scale X), N2 height
// (texture scale Y, sign carries orientation), N3 x offset, N4 y offset and N5 rotation in degrees.
// The doc infers these meanings from value distributions; the projection below is the compiler's
// convention and the doc does not spell out the formula. N6 (after the first five) is not used here.
//
// Projection of a face with normal n (normalized inside every function):
//   base axes from the dominant component of n:
//     Z: s = ( 1, 0, 0)  t = ( 0,-1, 0)
//     X: s = ( 0, 1, 0)  t = ( 0, 0,-1)
//     Y: s = ( 1, 0, 0)  t = ( 0, 0,-1)
//   a = dot(p, s), b = dot(p, t); rotated by rot degrees:
//     S = a cos(rot) - b sin(rot),  T = a sin(rot) + b cos(rot)
//   u = S / width + xoff / width,  v = T / height + yoff / height
//   width or height of 0 is read as 128 (the compiler's default size).
//
// Surface-inspector semantics (fit, justify, natural size) follow the Radiant behaviour of the same
// names: fit spans the face with a given number of repeats, justify aligns an edge to a texel boundary.
#pragma once

#include <array>
#include <functional>
#include <optional>
#include <vector>

#include "geometry.h"
#include "map_text.h"

namespace iw8r {

// The five suffix fields after the material (see the header comment for their meaning).
// The defaults are the compiler's fallback for a face that has no texture fields.
struct TexParams {
    double width = 128, height = 128, xoff = 0, yoff = 0, rot = 0;
};

// Reads the five fields of brush face `face` through Primitive::textureFields. nullopt when the face has
// no such fields (use a default TexParams for the compiler's fallback) or the index is out of range.
std::optional<TexParams> texParamsOfFace(const Primitive& p, size_t face);

// World-space unit directions of +u and +v for the face (the dominant-axis base axes rotated by rot).
// The texel scale is in width / height, not in these vectors.
struct TexBasis {
    Vec3 s, t;
};
TexBasis texBasis(const Vec3& normal, const TexParams& tp);

// (u, v) of a world point on the face. Same numbers as the compiler's projectPoint.
std::array<double, 2> faceUV(const Vec3& normal, const TexParams& tp, const Vec3& point);

// Unit tangent of the face along +u (gradient of s projected into the face plane). Same as the compiler.
Vec3 faceTangent(const Vec3& normal, const TexParams& tp);

// Texture lock. Returns the fields a face needs AFTER transform `xf` so that every point keeps its old
// (u, v): if P is a point of the old face, faceUV(newNormal, result, xf(P)) == faceUV(oldNormal, oldTp, P).
// `xf` must be affine (translate, rotate, scale, flip); its linear part is recovered from xf itself and the
// new normal is the inverse transpose of that linear part. The map is fitted from the three points, which
// must not be collinear. The result is decomposed into width / height / rotation / offsets; width keeps the
// sign of oldTp.width and fields that did not change are returned exactly as they were.
// nullopt when the new mapping has shear (|cos| of the angle between the uv axes > 1e-3), when the points
// or the transform are degenerate, or when the decomposition does not reproduce the fit. Callers keep the
// old fields in that case.
std::optional<TexParams> lockTexture(const Vec3& oldNormal, const TexParams& oldTp, const std::array<Vec3, 3>& oldPoints,
                                     const std::function<Vec3(const Vec3&)>& xf);

// Surface inspector: sets width / height so the texture spans the winding timesU x timesV times
// (u spans exactly timesU, v exactly timesV), and sets the offsets so the minimum u and v are 0.
// Rotation and the sign of width / height are kept. Returns tp unchanged for an empty winding or a
// winding with no extent in u or v.
TexParams fitTexture(const Vec3& normal, const TexParams& tp, const std::vector<Vec3>& winding, double timesU = 1,
                     double timesV = 1);

// Surface inspector: the default texel size, width = height = 64, keeping offsets and rotation.
// 64 is the most common N1 / N2 value in the sample corpus (docs/research/02_iwmap4_format.md, section 3,
// numeric fields) and the sample map has `ch_tile_floor05 64 64 0 0 0 0`.
TexParams naturalTexture(const TexParams& tp);

// Surface inspector alignment. Left / Right put the minimum / maximum u on an integer; Top / Bottom do the
// same for v (Top = minimum v, Bottom = maximum v); Center puts the midpoint of u and of v on an integer.
// Only the offsets change, and the integer chosen is the nearest one.
enum class Justify { Left, Right, Top, Bottom, Center };
TexParams justifyTexture(const Vec3& normal, const TexParams& tp, const std::vector<Vec3>& winding, Justify j);

// Writes the five fields into brush face `face` with Primitive::setTextureFields. Only those five tokens
// change; the rest of the face line is kept byte for byte. False when the face does not exist or has no
// texture fields.
bool applyTexParams(Primitive& p, size_t face, const TexParams& tp);

// Texture lock across a transform of a whole brush: face i of `after` gets the fields that keep the (u,v) that
// face i of `before` had at the same material points, mapped by `xf`. Faces are matched by index (move, rotate,
// flip and scale keep the face list); faces that cannot be represented keep their fields. Returns faces locked.
size_t lockPrimitiveTextures(const Primitive& before, Primitive& after, const std::function<Vec3(const Vec3&)>& xf);

}  // namespace iw8r
