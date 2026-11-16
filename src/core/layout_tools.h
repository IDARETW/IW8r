// IW8 Radiant - selection layout tools (roadmap 2.25): array and radial duplicate, mirror clone, align
// and distribute.
//
// Every call is ONE undo step on the Document. Duplicates leave their copies selected (as Radiant Clone does);
// align and distribute keep the selection. Each function returns false (or 0) and changes nothing when the
// selection or the arguments do not allow the operation.
//
// Objects: a point entity, a brush entity (with all its brushes) and each worldspawn brush count as one object
// for align and distribute. Copies of a brush entity are whole entities; copies of worldspawn brushes are brushes.
// Point entity origins move with their copies; a brush entity's origin and angles keys are left alone because its
// brushes already carry the transform (the same rule Arbitrary Rotation uses).
#pragma once

#include <cstddef>

#include "document.h"
#include "geometry.h"

namespace iw8r {

// Array duplicate: copy k (k = 1..count) of the selection is offset by step * k. Returns copies made (count), or 0.
size_t arrayDuplicate(Document& doc, const Vec3& step, int count);

// Radial duplicate about `center` around `axis` (0 x, 1 y, 2 z, right-handed). The result is count + 1 objects
// evenly spaced over totalDegrees, the original included:
//   full circle (totalDegrees >= 360): copy k at k * 360 / (count + 1), so the last gap closes back to the original;
//   partial arc (totalDegrees < 360):  copy k at k * totalDegrees / count, so the last copy lands on totalDegrees.
// Point entities get their origin rotated and their angles key (pitch yaw roll; Z = yaw, Y = pitch, X = roll, the
// slots Arbitrary Rotation uses) advanced by the same angle. A yaw-only `angle` key is advanced for Z rotations.
// Returns copies made (count), or 0 for count < 1, a bad axis, totalDegrees <= 0 or an empty selection.
size_t radialDuplicate(Document& doc, const Vec3& center, int axis, int count, double totalDegrees = 360);

// Mirror clone: a mirrored copy of the selection across the plane `axis` == planeCoord. Brush faces stay outward
// (flipBrush); patches are reflected and their facing reversed so it matches the reflected surface. Point entities
// get their origin reflected, their yaw mirrored, and roll negated; pitch changes only under a Z mirror. This is an
// approximation of the full orientation, not an exact rotation-matrix reflection. Returns true when copies were made.
bool mirrorClone(Document& doc, int axis, double planeCoord);

enum class AlignMode { Min, Center, Max };

// Aligns each selected object's bounds to the selection's bounds on `axis` (min, centre or max) by translation.
// The selection bounds are the union of the selected objects. Returns false when nothing moved.
bool alignSelection(Document& doc, int axis, AlignMode mode);

// Distributes 3 or more objects along `axis` by their centres: the first and last (by centre) stay, and the rest
// are placed so every gap between neighbouring bounding boxes is equal. Returns false for fewer than 3 objects
// or when nothing moved.
bool distributeSelection(Document& doc, int axis);

}  // namespace iw8r
