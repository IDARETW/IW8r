// IW8 Radiant - basic shape models (Primitives > Shape Models): cube, sphere, cylinder, cone, torus, capsule,
// plane, wedge, pyramid, written as custom OBJ models (models/<name>.obj beside the map: Y-up, map units) so they
// build through the custom-model route like any other custom model (docs/research doc 21/27).
//
// Every shape is 64 units across with its origin at the centre of its base (it sits on the floor at z = 0); resize
// with the scale gizmo (modelscale). Faces wind counter-clockwise seen from outside and carry normals and UVs
// (one texture repeat per 64 units).
#pragma once

#include <string>
#include <vector>

namespace iw8r {

enum class ShapeKind { Cube, Sphere, Cylinder, Cone, Torus, Capsule, Plane, Wedge, Pyramid };

struct ShapeInfo {
    ShapeKind kind;
    const char* id;     // file stem part: cube, sphere, ...
    const char* label;  // menu text
};
const std::vector<ShapeInfo>& shapeKinds();

// Model name (file stem) for a shape with a material: iw8r_shape_<id>_<hash of material>.
std::string shapeModelName(ShapeKind kind, const std::string& material);

// OBJ text of the shape using `material` (usemtl).
std::string shapeObj(ShapeKind kind, const std::string& material);

}  // namespace iw8r
