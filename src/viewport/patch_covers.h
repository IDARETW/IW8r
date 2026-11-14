// Flat patches lying on tool faces of brushes (a paint mesh on a caulked face, core convert_ops): the tool face
// under such a patch is not drawn, or the two would fight for every pixel. Shared by the camera's kept geometry
// (camera_geometry.cpp) and the reference builder it is checked against (scene.cpp buildCameraGeometry).
//
// A tool face counts as covered when it lies in the plane of one or more flat patches and every sample point of it
// (its corners and edge midpoints, pulled a little inward, and its centre) is inside a triangle of one of them. So a
// face fanned into several patches is covered by them together, and a patch whose box merely holds the face does
// not hide it.
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "scene.h"

namespace iw8r::app {

class PatchCovers {
public:
    // Finds the flat patches that touch a brush with tool faces. Returns a hash of them (0: none): a change means
    // every item's geometry has to be made again. The covers point into the items' geometry: valid until the scene
    // changes, so call this at every geometry update. IW8R_NO_PATCH_COVERS: finds none (a test switch).
    uint64_t find(const std::vector<const PrimGeom*>& items);
    bool any() const { return !covers_.empty(); }
    // True when the face (its winding and normal) lies wholly under the flat patches found.
    bool covered(const Winding& face, const Vec3& normal) const;

private:
    struct Cover {
        Vec3 n;
        double d = 0;
        AABB box;
        const std::vector<Winding>* tris = nullptr;  // the patch's tessellated triangles
    };
    struct Info {
        uint64_t stamp = 0;
        bool flat = false, tool = false;
        Vec3 n;
        double d = 0;
    };
    std::vector<Cover> covers_;
    std::unordered_map<const PrimGeom*, Info> info_;
};

}  // namespace iw8r::app
