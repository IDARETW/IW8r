// IW8 Radiant - prefabs: misc_prefab references, instancing, explode and save-as-prefab.
//
// A misc_prefab point entity places the contents of another .map file ("model" "prefabs/<path>.map")
// at "origin", rotated by "angles" (pitch yaw roll, degrees, Quake/CoD convention) and optionally
// scaled by "modelscale". Brush and patch text is regenerated through brush_ops; every other line of
// a face (suffix: material and texture fields) is kept byte-exact.
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "document.h"
#include "geometry.h"
#include "map_text.h"
#include "prefab_bundle.h"

namespace iw8r {

// Row-major 3x3 matrix (rotations and scales).
struct Mat3 {
    double m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    Vec3 operator*(const Vec3& v) const;
    Mat3 operator*(const Mat3& o) const;
};

// Per-instance overrides (roadmap 8.8), keys on the misc_prefab entity:
//   "iw8r_ovr:<selector>:<key>" "<value>"   sets <key> on the prefab entities the selector picks; an empty value
//                                           removes the key. Selector: a targetname, "class=<classname>", or "*"
//                                           (every entity of the prefab, worldspawn excluded).
//   "iw8r_mat:<from>" "<to>"                 swaps a material on every brush face and patch of the prefab (variants:
//                                           the same crate stack in another wood).
// Origin and angles of prefab entities are placement and cannot be overridden this way.
inline constexpr const char* kPrefabKeyOverridePrefix = "iw8r_ovr:";
inline constexpr const char* kPrefabMaterialSwapPrefix = "iw8r_mat:";

struct PrefabKeyOverride {
    std::string selector, key, value;
};

struct PrefabInstance {
    std::string model;
    Vec3 origin;
    Vec3 angles;           // pitch yaw roll, degrees
    double scale = 1;      // "modelscale"; missing, unparsable or <= 0 means 1
    std::vector<PrefabKeyOverride> overrides;
    std::vector<std::pair<std::string, std::string>> materialSwaps;  // from -> to
    std::string pid;       // "iw8r_pid": the placement id of a prefab bundle ("" when missing or not valid)
};

// Parses a misc_prefab point entity. nullopt when the entity is not misc_prefab or has no "model".
std::optional<PrefabInstance> prefabInstance(const Entity& e);

// Finds the prefab file. Tries <map dir>/<model>, then each ancestor directory of the map (and each
// ancestor named map_source, plus <ancestor>/map_source/<model>), then each root/<model> and
// root/map_source/<model>. Returns the first existing regular file, or "" when none exists.
// A model that names a prefab bundle folder (<name>.iw8prefab) gives the contents file a build with the default
// features uses (prefab.map, or the bundle's fallback when it needs it), so callers that only want the geometry need
// no change and draw what is built; resolvePrefabReference (prefab_bundle.h) tells bundles and plain files apart.
std::string resolvePrefabPath(const std::string& mapPath, const std::string& model, const std::vector<std::string>& searchRoots);

// Rotation for "pitch yaw roll" in degrees: R = Rz(yaw) * Ry(pitch) * Rx(roll). Quarter turns are exact.
// Pitch 0 yaw 90 roll 0 maps +X to +Y.
Mat3 anglesToMatrix(const Vec3& pitchYawRoll);

// Transformed copies of a prefab map's contents, placed at the instance. Worldspawn primitives go to
// worldPrims. Every other entity becomes a key/value list (origin and angles transformed, other keys
// verbatim) plus its primitives (brush entities: transformed like worldspawn).
// Point entities without an origin key get one; angles are added when the instance is rotated.
// Carried over unchanged: brushes whose planes cannot be rebuilt (invalid brushes) and patch lines
// other than their control points.
struct PrefabContents {
    std::vector<Primitive> worldPrims;
    std::vector<std::pair<std::vector<std::pair<std::string, std::string>>, std::vector<Primitive>>> entities;
};
PrefabContents instantiatePrefab(const MapFile& prefab, const PrefabInstance& inst, const std::string& eol);

// Explode: replaces the misc_prefab point entity by the instantiated contents. The public Document API
// has no single transaction covering entity insertion and removal, so the explode is a fixed sequence
// of undoable steps: one edit for all worldspawn primitives (when there are any), one addEntity per
// other prefab entity, then one deleteSelection for the misc_prefab entity. Undo therefore takes
// (worldspawn primitives ? 1 : 0) + other entities + 1 steps. Contents are appended to the map.
// Returns false (and sets err) when the entity is not a prefab, or the map has no worldspawn.
// scope (optional, for a bundle placement): the exploded entities get the names the build would give them.
bool explodePrefab(Document& doc, Id prefabEntity, const MapFile& prefab, std::string* err, const PrefabNameScope* scope = nullptr);

// Build-time flattening: a copy of the map in which every misc_prefab is replaced by its instantiated contents
// (prefab world primitives as one func_group entity, other entities as they are), prefabs inside prefabs included
// (up to 8 levels; each prefab resolves relative to its own file). A prefab that cannot be found or parsed is left
// out with a warning. Returns false only when the result does not parse back (err says why).
bool flattenPrefabs(const MapFile& in, const std::string& mapPath, const std::vector<std::string>& roots, MapFile& out,
                    std::vector<std::string>* warnings, std::string* err = nullptr);

// The same, for maps that place prefab bundles (docs/PREFAB_BUNDLES.md). For each bundle placement it picks the main
// or the fallback contents (choosePrefabContents with `support`), expands them, and rewrites the local names with
// the placement id (key iw8r_pid; a placement without a usable one gets "a<N>" and a warning). `info` receives what
// the later build steps need: the scripts with one start per placement, and the bundles' own asset folders.
// A bare .map prefab is expanded exactly as flattenPrefabs always did; when two placements of one share a linked
// name, a warning says so. Returns false when the result does not parse, or two bundle folders use one id.
// flattenPrefabs is this function with the default build support and `info` thrown away.
bool flattenPrefabBundles(const MapFile& in, const std::string& mapPath, const std::vector<std::string>& roots,
                          const PrefabBuildSupport& support, MapFile& out, PrefabFlattenInfo& info,
                          std::vector<std::string>* warnings, std::string* err = nullptr);

// Save selection as prefab: an iwmap 4 text whose worldspawn holds the selected world primitives. A selected brush
// entity (or the selected brushes of one) stays an entity with its keys, and selected point entities are kept;
// everything is translated by -origin.
std::string selectionToPrefabText(const Document& doc, const Vec3& origin);

}  // namespace iw8r
