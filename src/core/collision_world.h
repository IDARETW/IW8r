// IW8 Radiant Suite - editor-side collision world for the play preview: swept axis-aligned boxes against the
// map's convex brushes and patch facets (the classic Quake/CoD box trace). It uses the same contents rules as
// the IW8 build (iw8_contents.h): solid and player-clip brushes block the player, triggers / hints / weapon
// clips / nonColliding brushes do not.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "geometry.h"
#include "map_text.h"

namespace iw8r {

class CollisionWorld {
public:
    struct Solid {
        std::vector<Plane> planes;  // outward, inside is dot(n, x) <= d (includes axial bevels)
        AABB bounds;
        uint32_t contents = 1;
        bool patch = false;
        bool ladder = false;  // a face uses the ladder tool material (climbable vertical faces)
    };
    struct Trace {
        double fraction = 1.0;  // 0..1 of the move completed
        Vec3 end;
        Plane plane;            // the surface hit (valid when fraction < 1)
        bool startSolid = false, allSolid = false;
        int solid = -1;         // index into solids()
    };

    // Rebuilds from the map: worldspawn / func_group / script_brushmodel brushes that block players, and patch
    // triangles as thin convex facets. `patchSubdiv` matches the renderer (segments per sub-patch).
    void build(const MapFile& map, int patchSubdiv = 4);
    // The same, leaving out the brushes of the entities `skip` names (movers, added with addMover instead).
    void build(const MapFile& map, int patchSubdiv, const std::function<bool(const Entity&)>& skip);

    // A moving brush entity (a door, a lift, a spinning brush model): its blocking brushes become a group whose
    // solids follow setMoverPose. Returns the group (-1 when the entity has no blocking brush).
    int addMover(const Entity& e);
    // Places a group: every point p of its solids goes to pivot + R (p - pivot) + move (R row-major, a rotation).
    void setMoverPose(int group, const double R[3][3], const Vec3& pivot, const Vec3& move);
    // The bounds of a group as built (its rest pose).
    AABB moverBounds(int group) const;
    const std::vector<Solid>& solids() const { return solids_; }
    AABB bounds() const { return bounds_; }

    // Sweeps the box [mins, maxs] (relative to the moving point) from start to end.
    Trace traceBox(const Vec3& start, const Vec3& end, const Vec3& mins, const Vec3& maxs) const;
    // A ray (zero-size box).
    Trace traceRay(const Vec3& start, const Vec3& end) const { return traceBox(start, end, Vec3{}, Vec3{}); }
    // Upright capsule (the Replay player shape, walk simulator profile): radius r, total height h, feet at the point.
    Trace traceCapsule(const Vec3& start, const Vec3& end, double r, double h) const;

    // Adds one convex solid (outward planes, inside dot(n, x) <= d) that blocks the player, as a placed model's hull
    // piece does in the built map (core play_world). False when the planes enclose nothing.
    bool addConvex(std::vector<Plane> planes, uint32_t contents = 1) {
        const size_t before = solids_.size();
        addSolid(std::move(planes), contents, false);
        return solids_.size() > before;
    }

    // Contents mask the player collides with (solid | player clip).
    static constexpr uint32_t kPlayerMask = 0x1u | 0x10000u;

private:
    void addSolid(std::vector<Plane> planes, uint32_t contents, bool patch, bool ladder = false);
    void traceThrough(const Solid& s, int index, const Vec3& start, const Vec3& end, const Vec3& mins, const Vec3& maxs,
                      Trace& tr, double capsuleRadius = 0) const;
    Trace traceShape(const Vec3& start, const Vec3& end, const Vec3& mins, const Vec3& maxs, double capsuleRadius) const;
    std::vector<Solid> solids_;
    AABB bounds_;
    struct Mover {
        std::vector<size_t> solids;
        std::vector<std::vector<Plane>> planes;  // rest pose
        std::vector<AABB> bounds;                // rest pose
        AABB all;
    };
    std::vector<Mover> movers_;
    void addBrushesOf(const Entity& e, int patchSubdiv);
};

}  // namespace iw8r
