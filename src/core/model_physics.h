// IW8 Radiant Suite - which placed models collide, and with what shape, by the stock models' own collision
// (docs/research/142_static_model_collision.md: verified in Replay 1.20 with the IDA database, ACTS exports and the
// game's Havok type compendium, 2026-11-15).
//
// The game (Replay 1.20) gives a static model collision only through the clipmap's static model collision lists, and an
// entry there is exactly the XModel's physicsAsset / detailCollision pair: StaticModels_AddClipmapCollisionShapes puts
// the physics asset's Havok shapes in the main physics worlds and the detail collision (else the physics asset) in the
// detail worlds. A model without a physics asset (most bushes, grass and flowers) has no collision at all. The player's
// movement mask (0x2014011, G_ActiveMP_ClientThinkInternal) misses every stock prop body's contents (0xB180, 0x2180,
// 0x420B180, ...): in stock maps clip brushes keep the player out of props. Own maps have no such brushes, so the build
// gives a model whose stock XModel has a physics asset (solid in game for weapons and physics) player collision of
// that physics asset's shape, and a model without one none, as in game.
//
// data/asset_catalog/iw8_model_physics.tsv (tools/build_model_physics_catalog.py) holds, per stock model, its physics
// asset and detail collision names, and per physics asset its body contents and convex pieces (model space, inches,
// Z-up) where they could be read ("mesh" where not: the render mesh's hull stands in).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "geometry.h"

namespace iw8r {

class CollisionWorld;
struct Entity;
struct StockModelInstance;

struct ModelPhysicsShape {
    uint32_t contents = 0;                  // OR of the physics asset's body contents
    std::vector<std::vector<Vec3>> pieces;  // convex pieces, model space (inches, Z-up)
    // The triangles of its compressed mesh shapes (hknpCompressedMeshShape), model space (inches, Z-up).
    std::vector<Vec3> meshVerts;
    std::vector<std::array<uint32_t, 3>> meshTris;
    // Read from the catalogue: convex pieces or triangles (else "mesh": the render mesh stands in).
    bool readable() const { return !pieces.empty() || !meshTris.empty(); }
};

struct ModelPhysicsInfo {
    std::string physicsAsset;                 // empty: no physics asset (no collision in game)
    std::string detailCollision;              // empty: none
    const ModelPhysicsShape* shape = nullptr;  // the physics asset's record (null when the catalogue has none)
};

class ModelPhysicsCatalog {
public:
    // Reads the catalogue file (lines "M <model> <physics> <detail>" and "P <physics> <contents> <pieces> <triangles>
    // <note>"; the older "P <physics> <contents> <pieces> <note>" too).
    bool load(const std::string& path, std::string* err = nullptr);
    void loadText(const std::string& text);
    // A model by the name a map gives it ("tm/foliage_alder_bush_large_01"); a leading ',' or '~' is ignored, and a
    // name the catalogue does not hold is looked up without its folder (the stock lists name models without it).
    const ModelPhysicsInfo* find(const std::string& model) const;
    size_t size() const { return models_.size(); }
    size_t shapeCount() const { return shapes_.size(); }
    // The catalogue beside the program: data/asset_catalog/iw8_model_physics.tsv in its folder or up to five above
    // (the executable's folder, else `startDir`); empty when not found. Loaded once.
    static const ModelPhysicsCatalog& shared(const std::string& startDir = std::string());

private:
    std::unordered_map<std::string, ModelPhysicsInfo> models_;
    std::unordered_map<std::string, std::string> byBase_;  // name without folder -> catalogue name (first seen)
    std::unordered_map<std::string, ModelPhysicsShape> shapes_;
};

enum class ModelCollisionSource {
    None,          // no player collision: walk through it (no stock physics asset, or switched off on the placement)
    PhysicsShape,  // the stock physics asset's convex pieces
    RenderMesh,    // the render mesh's convex pieces (or its box): custom models, unread or unknown stock physics
};

struct ModelCollisionChoice {
    ModelCollisionSource source = ModelCollisionSource::RenderMesh;
    const ModelPhysicsShape* shape = nullptr;  // PhysicsShape: the pieces
    const char* why = "";                      // for logs
};

// The rule, in order: the placement's iw8r_collision key ("0": none, "1": the render mesh), iw8r_no_collision 1
// (none); the mapper's own model (custom): the render mesh; no catalogue: the render mesh (the build before the rule);
// a stock model the catalogue does not hold: the render mesh; one without a physics asset: none; one whose physics
// asset's pieces were read: those; else the render mesh.
ModelCollisionChoice chooseModelCollision(const std::string& model, const Entity* placement, bool customModel,
                                          const ModelPhysicsCatalog* catalog);

// Outward planes (dot(n, x) <= d) of each convex piece, model space Z-up inches; a piece with more than maxPlanes
// planes is simplified (simplifyHull; maxPlanes < 4: as built), a sliver made half a unit thick, an open or oversized
// one replaced by its box. With meshPieces, the shape's triangles add up to maxPieces convex pieces of their own
// (convexPieces, as a render mesh would; the build's collision is convex brushes); without, they are left to the caller
// (the play preview adds them as facets).
std::vector<std::vector<Plane>> physicsShapePlanes(const ModelPhysicsShape& shape, size_t maxPlanes, int maxPieces = 4,
                                                   bool meshPieces = true);

// Planes in Z-up model space placed at origin + axis * (scale * p) (axis: the model's X, Y and Z in world space, unit
// columns), as a convex piece of a placement.
std::vector<Plane> placePlanes(const std::vector<Plane>& piece, const Vec3& origin, const Vec3 (&axis)[3], double scale);

// Stock map props in the play preview: the collision of every placement within `radius` (x / y distance, the model's
// size included; radius <= 0: all) of `around`, by chooseModelCollision; a physics shape's triangles become facets
// (CollisionWorld::addTriangle), its convex pieces convex solids. `renderPieces` gives a model's render-mesh
// pieces (Z-up model space) for a stock model whose physics is not read or not catalogued, or null while they are not
// ready (the placement is then counted as pending and left out). `skip` leaves placements out (props the document
// hides: their collision goes with them in the rebuilt zone). Each call adds every placement in range: call it on a
// world built without them.
struct StockPropCollisionStats {
    size_t considered = 0;   // placements in range
    size_t physics = 0;      // placed from their stock physics pieces
    size_t renderMesh = 0;   // placed from their render-mesh pieces
    size_t walkThrough = 0;  // no collision (no stock physics asset)
    size_t pending = 0;      // render-mesh pieces not ready
    size_t solids = 0;       // convex solids added
    size_t facets = 0;       // physics mesh triangles added as facets
};
StockPropCollisionStats addStockPropCollision(
    const std::vector<StockModelInstance>& props, CollisionWorld& world, const Vec3& around, double radius,
    const ModelPhysicsCatalog& catalog, size_t maxPlanes,
    const std::function<const std::vector<std::vector<Plane>>*(const std::string& model)>& renderPieces = {},
    const std::function<bool(const StockModelInstance&)>& skip = {});

}  // namespace iw8r
