// IW8 Radiant Suite - editor-side collision world for the play preview: swept axis-aligned boxes against the
// map's convex brushes and patch facets (the classic Quake/CoD box trace). It uses the same contents rules as
// the IW8 build (iw8_contents.h): solid and player-clip brushes block the player, triggers / hints / weapon
// clips / nonColliding brushes do not.
#pragma once

#include <cstdint>
#include <unordered_map>

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
    // Upright flat-bottomed 8-sided prism, bottom at the point: radius r (to the corners), height h, corners on the x
    // and y axes. Replay's player ground shape (collision type 1, research 140 section 8): Physics_CharacterCastPacked
    // builds it as a 16-vertex hull (8 around, top and bottom, world aligned).
    Trace traceCylinder(const Vec3& start, const Vec3& end, double r, double h) const;

    // Adds one convex solid (outward planes, inside dot(n, x) <= d) that blocks the player, as a placed model's hull
    // piece does in the built map (core play_world). False when the planes enclose nothing.
    bool addConvex(std::vector<Plane> planes, uint32_t contents = 1) {
        const size_t before = solids_.size();
        addSolid(std::move(planes), contents, false);
        return solids_.size() > before;
    }

    // Adds one triangle as a thin facet (its plane, a back plane `thickness` behind it and the three edge planes) that
    // blocks the player: a stock map's world surfaces in the play preview. Bounds come from the corners (no windings),
    // so many triangles add quickly. False for a degenerate triangle.
    bool addTriangle(const Vec3& a, const Vec3& b, const Vec3& c, double thickness = 1.0, uint32_t contents = 1);

    // Contents mask the player collides with (solid | player clip).
    static constexpr uint32_t kPlayerMask = 0x1u | 0x10000u;

private:
    void addSolid(std::vector<Plane> planes, uint32_t contents, bool patch, bool ladder = false);
    void traceThrough(const Solid& s, int index, const Vec3& start, const Vec3& end, const Vec3& mins, const Vec3& maxs,
                      Trace& tr, double capsuleRadius = 0, bool prism = false) const;
    Trace traceShape(const Vec3& start, const Vec3& end, const Vec3& mins, const Vec3& maxs, double capsuleRadius,
                     bool prism = false) const;
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
    // The broad phase over many solids: x/y cells of kGridCell units holding the static solids that overlap them,
    // built on the first trace after a change (mutable: traces are const). Movers' solids are tested every trace.
    static constexpr double kGridCell = 256.0;
    static constexpr size_t kGridFrom = 2048;  // solids before the grid is used
    mutable std::unordered_map<int64_t, std::vector<uint32_t>> grid_;
    mutable std::vector<uint32_t> gridStamp_;
    mutable uint32_t gridTick_ = 0;
    mutable size_t gridSolids_ = 0;  // solids_.size() when the grid was built
    mutable std::vector<uint32_t> moverSolidList_;
    void buildGrid() const;
};

}  // namespace iw8r
