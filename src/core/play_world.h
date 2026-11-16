// IW8 Radiant Suite - the collision of the play preview as the built map has it (research 131 gap 9).
//
// CollisionWorld::build reads the map's own brushes only. The built map also collides with:
//   - the contents of misc_prefab placements (the build flattens prefabs before it compiles: core prefab.h);
//   - placed models (misc_model, and script_model the converter keeps static): one convex brush per convex piece of
//     the model (core convex_hull.h), or an oriented box when the model gives no hull, as compile_iw8.cpp does; which
//     models and which shape by the stock models' own collision (model_physics.h): a stock model without a physics
//     asset (most bushes, grass) is walked through, one with a physics asset blocks with that asset's pieces.
// buildPlayWorld adds both, so walking the map in the editor meets what a player meets in game.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "collision_world.h"
#include "map_text.h"
#include "model_import.h"

namespace iw8r {

struct PlayWorldOptions {
    std::string mapPath;                    // misc_prefab files resolve next to it
    std::vector<std::string> prefabRoots;   // and in these folders
    // The model of a placement (the scene's model source); null or a null result: the model gives no collision.
    std::function<std::shared_ptr<const ImportedModel>(const Entity&)> modelOf;
    double modelScale = 1.0;                // file units -> map units, as the build's modelScale
    int maxPieces = 4, maxPlanes = 24;      // CompileOptions::modelCollisionMaxPieces / MaxPlanes
    // Brush entities left out of the static world (the play preview's movers, PlayBehaviours::moves).
    std::function<bool(const Entity&)> skip;
    // The stock model collision catalogue (CompileOptions::modelPhysics). Null: ModelPhysicsCatalog::shared(), the
    // one the editor's build uses; noPhysicsRule: every model by its render mesh (the build without the catalogue).
    const class ModelPhysicsCatalog* physics = nullptr;
    bool noPhysicsRule = false;
};

struct PlayWorldStats {
    size_t prefabs = 0;      // misc_prefab placements expanded
    size_t models = 0;       // placed models that collide
    size_t hullPieces = 0;   // convex brushes from their hulls
    size_t boxes = 0;        // models with no hull: an oriented box
    size_t physicsModels = 0;  // of `models`: placed from their stock physics pieces (counted in hullPieces too)
    size_t walkThrough = 0;    // placed models with no collision by the stock rule (no physics asset, iw8r_collision 0)
    std::vector<std::string> warnings;
};

// Whether the build gives a placed model player collision (misc_model always; script_model unless the converter keeps
// it a live entity or it has iw8r_no_collision 1), by the rule of compile_iw8.cpp ("staysEntity").
bool playModelCollides(const Entity& e, const MapFile& map);

// The play preview's collision: the map's brushes, the prefabs' contents and the placed models.
void buildPlayWorld(const MapFile& map, const PlayWorldOptions& options, CollisionWorld& out, PlayWorldStats* stats = nullptr);

}  // namespace iw8r
