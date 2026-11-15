// IW8 Radiant Suite - render/pick cache built from the Document.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "document.h"
#include "iw8_mapents.h"
#include "model_import.h"
#include "render.h"
#include "stock_world.h"
#include "collision_world.h"
#include "collision_preview.h"
#include "face_edges.h"
#include "umbra_view.h"

namespace iw8r::app {

struct PrimGeom {
    enum Kind { Brush, Patch, PointEntity };
    SelRef ref;
    uint64_t rev = 0;
    uint64_t stamp = 0;  // unique for every build of an item's geometry (SceneCache::sync): what the camera's cache compares
    uint64_t seen = 0;   // the sync that last listed this item (items not listed are dropped from the cache)
    // Brushes and patches: the owner's keys that change how they are drawn (classname, iw8r_brush_visual), which do
    // not move the item's rev; the item is built again when they change.
    uint64_t look = 0;
    Kind kind = Brush;
    std::vector<Winding> polys;  // brush faces, tessellated patch triangles, entity box faces
    // The faces as their meshes (core face_edges), parallel to polys, for brushes (empty for other items). A textured
    // poly is drawn as its mesh: the same surface without the parts that lie under a coplanar face of another brush,
    // and (SceneCache::faceEdges) with every vertex knowing its distance to the face's edges. An empty mesh: the face
    // is wholly under other faces and not drawn. edgeSet / edgeKey: SceneCache::updateFaceEdges.
    std::vector<EdgeFaceMesh> polyEdges;
    int edgeSet = 0;
    uint64_t edgeKey = 0;
    std::vector<std::pair<Vec3, Vec3>> edges;  // explicit wireframe (patch control net); empty = polygon outlines
    std::vector<Vec3> faceNormals;
    std::vector<Plane> planes;   // brush picking
    AABB bounds;
    uint32_t color = 0xffffffff; // ABGR (R in low byte), from material or classname
    std::string label;           // first material or classname
    bool brushEntity = false;    // primitive owned by a non-worldspawn entity
    Vec3 arrowFrom, arrowTo;     // facing arrow for point entities with angles
    bool hasArrow = false;
    bool prefab = false;         // misc_prefab drawn with its instantiated contents (picked by bounds)
    bool volume = false;         // every face a trigger/clip/hint/skip tool material: drawn as a wireframe box
    bool mesh = false;           // model triangles: no per-triangle outlines unless selected or in wireframe
    uint32_t contentsColor = 0xff60a0e0;  // CameraMode::Contents colour (ABGR)
    // CameraMode::Collision (3.13): the player collides with it in the build. Structural / detail brushes, player clip
    // volumes, patches and placed models (the build gives them collision boxes or hulls) do; non-colliding brushes,
    // trigger / hint / skip volumes, triggers and point entity markers do not.
    bool collides = false;
    // Textured view: material and texture coordinates per poly (brush faces via tex_projection, patches by
    // projection). Empty for point entities and prefabs.
    std::vector<std::string> polyMaterial;
    std::vector<std::vector<std::array<float, 2>>> polyUV;
    // Density mode: sqrt(u repeats x v repeats) per world unit of each brush face (texel_density with a 1x1 texture).
    std::vector<float> polyRepeatsPerUnit;
    // The lightmap density of each brush face (core lightmap_fields lightmapDensityOfSuffix); empty: 1 everywhere.
    std::vector<float> polyLmDensity;
    // Painted patch vertex colours (8.11): one ABGR tint per poly (the mean of its corners), empty when unpainted.
    std::vector<uint32_t> polyTint;
    // A prefab with models inside (2026-11-12): polys [0, meshFrom) are brush and patch faces, [meshFrom, markerFrom)
    // model triangles (drawn like `mesh`: no outlines unless selected or in wireframe), [markerFrom, end) boxes of
    // inner point entities and of models not loaded yet (editor markers: no sun shadow, left out of captures).
    // Both are kNoPoly for every other item.
    static constexpr uint32_t kNoPoly = 0xffffffffu;
    uint32_t meshFrom = kNoPoly, markerFrom = kNoPoly;
    bool meshPoly(size_t i) const { return mesh || (i >= meshFrom && i < markerFrom); }
    bool markerPoly(size_t i) const { return i >= markerFrom; }
    bool hasMeshPolys() const { return mesh || (meshFrom < markerFrom && meshFrom < polys.size()); }
    // What a prefab's contents resolved to when it was built: the prefab itself (entity null), inner prefabs and inner
    // models. SceneCache::sync builds the prefab again when one of them resolves differently (a model that finished
    // loading, a prefab file that was read again), as a directly placed model is built again when its model arrives.
    struct SourceDep {
        std::shared_ptr<const Entity> entity;  // the inner entity as the sources see it (null: the placed prefab)
        std::shared_ptr<const void> resolved;  // what the source gave (kept, so the address cannot be reused)
        bool prefab = false;                   // asked of prefabSource, else of modelSource
    };
    std::vector<SourceDep> deps;
};

uint32_t colorForName(const std::string& name);
uint32_t colorForClass(const std::string& classname);

// View > Camera Mode (exclusive). Textured and Fullbright draw textures when the camera passes a texture lookup;
// Flat is per-face shade; Wireframe draws edges only; Fullbright is unshaded; Normals colours faces by normal.
// Contents colours faces by what the build makes of the item (3.13): structural grey, detail green, non-colliding
// blue, weapon clip yellow, brush entities purple, patches teal, models and point entities orange.
// Density (5.21) colours brush faces by texture pixels per world unit against densityTarget: green on target, blue
// at half or less, red at twice or more; patches, models and entities grey.
enum class CameraMode { Textured, Flat, Wireframe, Fullbright, Normals, Contents, Density, Collision };

struct GameLightmap;  // game_lightmap.h

struct GameLightmap;
// The textured triangles of poly `i` of an item: its face mesh when the item has one (PrimGeom::polyEdges; nothing for
// an empty mesh, a face wholly under other faces), else the winding as a fan. `uv` are the winding's texture
// coordinates (the mesh's are the same affine function of the position), `centre` the winding's centre when gameLm is
// given (its lightmap UVs are kept per face corner).
// propLight: `tan` is the light of a model's triangle (SceneCache::modelLights), marked by the lightmap UV (-2, -2).
void appendTexturedFace(const PrimGeom& g, size_t i, const Vec3& n, const Vec3& tan, uint32_t colour, const GameLightmap* gameLm,
                        const Vec3& centre, std::vector<GpuTexVertex>& out, bool propLight = false);
// The light of poly `i` of an item when it is a model triangle with a light in `scene.modelLights` (Game Lighting).
bool modelTriangleLight(const class SceneCache& scene, const PrimGeom& g, size_t i, const Vec3& n, Vec3& light);

class SceneCache {
public:
    // Rebuilds only items whose id/rev changed since the last sync.
    void sync(const Document& doc);
    void clear();
    const std::vector<const PrimGeom*>& items() const { return list_; }
    // Nearest hit along the ray (brush planes, patch quads, entity boxes).
    std::optional<SelRef> pick(const Ray& ray, double* tOut = nullptr) const;
    // Every hit along the ray, nearest first (Radiant Alt+Shift click cycles through them).
    std::vector<SelRef> pickAll(const Ray& ray) const;
    // Nearest brush face along the ray (Radiant Ctrl+Shift+click face selection).
    std::optional<FaceRef> pickFace(const Ray& ray, double* tOut = nullptr) const;

    // IW8 compiled spawnList records shown as a read-only overlay (not part of the document).
    std::vector<Iw8SpawnRecord> spawns;
    // Leak pointfile (Tools > Leak Check): drawn as a line in both views until cleared.
    std::vector<Vec3> pointfile;
    // Lit preview (View > Lit Preview): ambient + worldspawn sun + light entities, per face, no shadows.
    struct PreviewLight {
        Vec3 pos, color{1, 1, 1}, dir;  // dir: spot axis (unit), zero for omni
        double radius = 400, cosOuter = -2, cosInner = -2;
        bool volumetric = false;  // iw8r_volumetric: the lamp lights the volumetric air
    };
    bool litPreview = false;
    bool litShadows = true;
    // View > Game Lighting (game_lightmap.h): textured faces are shaded by the renderer as the game shades them; vertex
    // colours carry only tints, and vertices of baked polygons carry their lightmap UV.
    // Game Lighting: the editor's dark outlines of unselected textured brush faces (off: the view shows the surfaces
    // as the game does; View > Game Lighting Options > Brush Outlines).
    bool gameOutlines = false;
    bool gameLook = false;
    // A stock map reference is shown (set by the camera): its lit preview is the game's lighting, as the editor's own
    // light bake has nothing to light there (owner report 2026-11-12: Lit Preview alone was blown out on Speedball).
    bool gameLookAuto = false;
    bool gameLookActive() const { return gameLook || (gameLookAuto && litPreview); }
    std::shared_ptr<const GameLightmap> gameLightmap;
    // Game Lighting on an own map: the light of every placed model (by its entity), as the 56 samples the build's
    // light grid holds at its place (core light_grid). A model's triangles carry the light of their normal in the
    // vertex tangent with the lightmap UV (-2, -2), as stock props carry their light grid box. modelLightsRev moves
    // when the table changes (the camera's geometry is then made again).
    std::unordered_map<Id, std::array<std::array<float, 3>, 56>> modelLights;
    uint64_t modelLightsRev = 0;
    // The renderer's sun shadow map handles sun shadows per pixel (set by the camera); the per-face ray test is skipped.
    bool gpuShadows = false;
    // Set by buildCameraGeometry: the first N flat triangle vertices cast sun shadows; the rest are markers, tool faces
    // and volumes (not drawn by the game).
    mutable size_t shadowCasterVertices = 0;
    // Set by buildCameraGeometry: the first N flat triangle vertices are shadow-only (stock nodraw materials such as
    // foliage *_shadowcaster meshes, research 66): in the sun shadow pass, never drawn on screen. N <= shadowCasterVertices.
    mutable size_t shadowOnlyVertices = 0;
    double exposure = 1.5;  // lit preview exposure (View > Lit Preview Brightness)
    // Lit preview colours are encoded for the renderer (light / 2, sun share in alpha) in the shaded modes only.
    bool litEncoded() const {
        return litPreview && (cameraMode == CameraMode::Textured || cameraMode == CameraMode::Flat || cameraMode == CameraMode::Fullbright);
    }
    // Textured faces leave light entities to the renderer's per-pixel pass (3.14); set by the camera with litPreview.
    bool pixelLights = false;
    bool captureMode = false;
    // Selection highlight (3.13, View > Selection Highlight): tint colour (ABGR) and how strongly faces take it.
    uint32_t selectionTint = 0xff3030ffu;
    double selectionTintAmount = 0.5;
    bool skyActive = false;
    // Normal map of a material for the textured view (3.2): a texture id, or -1 (none / not loaded yet).
    std::function<int(const std::string& material)> normalFor;
    // Emissive map of a material for the textured view (3.17 / 5.24): a texture id, or -1.
    std::function<int(const std::string& material)> emissiveFor;
    // Specular map of an own material (TexturedBatch::specTexture): a texture id, or -1.
    std::function<int(const std::string& material)> specFor;
    // The material's custom surface shader (SceneRenderer::createCustomShader id) or -1 (core custom_shader).
    std::function<int(const std::string& material)> shaderFor;
    // A custom shader in use reads the edge inputs: brush faces get their edge meshes (PrimGeom::polyEdges). Call
    // invalidate() after changing it. faceEdgesWanted (the shell: does a shader in use read the edge inputs?) sets it
    // at every full sync.
    bool faceEdges = false;
    std::function<bool()> faceEdgesWanted;
    // Is a drawn material opaque for sure (no cut-out, no blending)? A brush of such materials hides the faces inside
    // or against it, which are then not drawn (core face_edges hiddenCovers). Null or false: hides nothing.
    std::function<bool(const std::string& material)> opaqueFor;
    // How a material draws in the textured view (3.17): 0 opaque / alpha test, 1 alpha blend, 2 additive (null = all 0).
    std::function<int(const std::string& material)> blendFor;
    // Foliage materials (5.23 wind preview): their textured batches are marked to sway (null = none).
    std::function<bool(const std::string& material)> foliageFor;
    // How a stock material draws (core techset_info MaterialDraw): 0 drawn, 1 shadow only, 2 hidden (null = all drawn).
    std::function<int(const std::string& material)> drawClassFor;
    // Tags of a stock model (3.18), model-space Z-up inches (xmodel_info): drawn as yellow crosses on selected models.
    std::function<std::vector<Vec3>(const std::string& model)> tagsFor;
    // Density mode: a material's colour texture size in pixels ({0, 0} when unknown: 512 is assumed).
    std::function<std::pair<int, int>(const std::string& material)> textureSizeFor;
    double densityTarget = 2.0;  // texture pixels per world unit drawn green    // the camera draws the worldspawn sky: sky tool faces are left out so it shows through  // probe capture: no entity markers, tool volumes or overlays, only what the game draws
    // Build preview (View > Show Compiled Collision): the last extraction's collision hulls, drawn depth-tested.
    std::vector<CollisionHullEdges> collisionPreview;
    // View > Umbra Visibility: the camera's cell, its portals and the visible cells around it (core umbra_view). The
    // version moves with every change (the camera's geometry key reads it).
    std::vector<UmbraLine> tomeLines;
    uint64_t tomeLinesVersion = 0;
    std::vector<std::pair<Vec3, Vec3>> navLines;  // View > Show Navigation: the last build's navmesh areas, nodes and links
    std::vector<std::pair<Vec3, Vec3>> compiledWorld;  // View > Show Compiled World: the last build's render triangles  // lit preview: a face whose centre cannot see the sun gets no sun light
    // Collision world used for the lit preview's sun shadows, rebuilt when the document changes.
    const CollisionWorld* shadowWorld(const Document& doc) const;
    CameraMode cameraMode = CameraMode::Textured;  // independent of litPreview
    // Lit preview brightness (View > Lit Preview Brightness, saved). The game's exposure lifts shadowed areas well above a
    // plain "texture x light" sum, so the base is bright and light entities add on top with a soft roll-off.
    double ambient = 0.55;   // faces the sun does not reach
    double sunGain = 0.75;   // sun on a face turned straight at it (times suncolor)
    double lightGain = 0.6;  // light entities (times colour x intensity), smooth falloff to the radius
    Vec3 sunToward{0.45, 0.45, 0.77}, sunColor{1, 1, 1};
    std::vector<PreviewLight> lights;

    // Visibility: hidden item/entity ids (Radiant H / Shift+H) and hidden classname prefixes (View > Filter).
    // Hidden things are neither drawn nor pickable. Call invalidate() after changing them.
    std::set<Id> hidden;
    std::set<Id> hiddenByLayer;  // items of layers switched off in the Layers dock
    std::set<Id> hiddenByRegion; // primitives and point entities outside the active Region (see core/region.h)
    std::vector<std::pair<Vec3, Vec3>> regionLines;  // the active Region's box as 12 edges (empty: no region)
    // View > Game Mode (core game_mode_view): the gametype shown ("" = every mode) and the entities it removes.
    std::string gameMode;
    std::set<Id> hiddenByMode;
    std::vector<std::string> hiddenClassPrefixes;
    void invalidate() { docRevision_ = 0; }
    uint64_t generation() const { return generation_; }  // bumps whenever sync() rebuilds the item list
    // target -> targetname connection lines (Radiant entity connections), rebuilt on sync.
    const std::vector<std::pair<Vec3, Vec3>>& connections() const { return connections_; }
    // Prefab contents for misc_prefab entities ("model" "prefabs/x.map"); null = draw a plain box.
    // The shell resolves and caches the files; call invalidate() after reloading them.
    std::function<std::shared_ptr<const MapFile>(const Entity&)> prefabSource;
    // Custom models (doc 22) for script_model / misc_model: drawn as their mesh at origin/angles; null = box.
    // `modelScale` matches the compile's OBJ -> map unit scale.
    std::function<std::shared_ptr<const ImportedModel>(const Entity&)> modelSource;
    double modelScale = 1.0;
    // The first model of a stock scriptable definition ("" = unknown or none): a `scriptable` entity without a model
    // key is drawn with it, as a misc_model with that model key would be (scriptable_model.h). Null = boxes.
    std::function<std::string(const std::string& definition)> scriptableModelFor;
    // View > Behaviour Preview: seconds since it was switched on (-1 = off). Models with iw8r_spin / iw8r_bob /
    // iw8r_lift and script_brushmodels with those keys or iw8r_door are drawn where the map script would have them at
    // that time (owner request 2026-11-11). Call invalidate() each tick.
    double behaviourTime = -1;
    // The play preview's moving brush entities (core PlayBehaviours::poses): where each one is now. Every point p of
    // the entity is drawn at pivot + R (p - pivot) + move. Empty when not playing. Call invalidate() after changing it.
    struct PlayPose {
        double R[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        Vec3 pivot, move;
    };
    std::map<Id, PlayPose> playPoses;
    // The destroyed state of an exploder (a model with targetname "exploder", as the S&D template's destroyed crate):
    // hidden in game until the exploder fires, so drawn as a small marker unless this is set. Call invalidate() after
    // changing it.
    bool showExploderPieces = false;

    // Helpers of the map script's behaviour that are not an item of their own (research 131 gaps 3 and 15): the
    // damage cylinder of an fx_origin with iw8r_damage (always, depth-tested), the use area the build gives a door that
    // no trigger opens and the line from a teleporter to its destination (on top, while one of the two entities is
    // selected). Rebuilt on sync; appendHelperLines draws them.
    struct HelperLine {
        Vec3 a, b;
        uint32_t rgba = 0xffffffffu;
        Id owner = kNoId, other = kNoId;  // shown while either is selected (unless always)
        bool always = false;
    };
    const std::vector<HelperLine>& helperLines() const { return helperLines_; }

private:
    void updateFaceEdges();
    std::unordered_map<Id, PrimGeom> cache_;
    std::vector<const PrimGeom*> list_;
    std::vector<const PrimGeom*> prevList_;  // the list of the sync before (order hint), only inside sync()
    std::vector<std::pair<Vec3, Vec3>> connections_;
    std::vector<HelperLine> helperLines_;
    uint64_t docRevision_ = 0;
    uint64_t generation_ = 0;
    uint64_t stampCounter_ = 0;
    uint64_t fingerprint_ = 0;  // ids and revs of every item at the last full sync
    uint64_t visit_ = 0;
    mutable std::shared_ptr<CollisionWorld> shadowWorld_;
    mutable uint64_t shadowRevision_ = ~0ull;
    const Document* doc_ = nullptr;
};

// SceneCache::helperLines as camera lines: the `always` ones into `lines` (depth-tested), the others into `overlay`
// while their owner or other entity is selected. Nothing in probe capture mode.
void appendHelperLines(const SceneCache& scene, const Document& doc, std::vector<GpuVertex>& lines, std::vector<GpuVertex>& overlay);

// Stock map reference geometry (3.9) as flat-shaded light-grey triangles. Meshes whose material names a sky are
// skipped when `skipSky`. Returns the number of triangles written.
size_t buildReferenceTris(const StockWorld& world, std::vector<GpuVertex>& out, bool skipSky = true);

// Textured stock reference (3.19): one batch per stock material with the world's decoded texture coordinates, for the
// textured camera. Returns the triangle count; 0 (and no batches) above maxTriangles or when the world has no UVs.
struct ReferenceTexBatch {
    std::string material;
    std::vector<GpuTexVertex> tris;
    // 0: the material's base (layer 0); L >= 1: layer L of a layered stock material (tw4 / twc4, research 97), drawn over
    // the layers below it with its own texture coordinates (uv); its tangent (from its uv) has length 1 + its blend weight.
    int layer = 0;
    // A stock decal volume's projection (research 99): material = "\x05<colour image>"; the vertex uv is the material
    // UV, tangent (x, y, z) = box UV and angle fade, lu / lv the receiver's lightmap UV. Drawn blended after the world.
    bool decalVolume = false;
    std::string decalMask;  // the mask image ("<mask>~atlasbc4"), "" when none
    float decalA = 1, decalB = 0;  // the blend-map remap: op = m * saturate(m * A + B)
    float decalTint[3] = {1, 1, 1};
    int decalOrder = 0;
    int decalCollection = -1;  // the volume's decal volume collection (the Umbra view culls by it), -1 unknown
    // A super-terrain layer near the camera (research 103), drawn with the decals: material = "\x05<colour image>",
    // decalMask = its alpha + reveal image ("" none), the weight in the tangent length (1 + w), the layer's UV as uv.
    bool terrainLayer = false;
    uint32_t terrainFlags = 0;  // the layer shader's flags: 16 reveal ramp, 8 alpha (research 97 / 103)
    float terrainK = 0;
    bool terrainBase = false;  // the patch's base layer: writes depth (it replaces the coarse terrain cut away there)
};
size_t buildReferenceTexBatches(const StockWorld& world, std::vector<ReferenceTexBatch>& out, bool skipSky = true,
                                size_t maxTriangles = 3'000'000);

// Triangles (shaded faces), depth-tested edge lines, and always-on-top selection lines for the
// 3D view. Compiled IW8 spawns are drawn as read-only wire boxes when `showSpawns`.
void buildCameraGeometry(const SceneCache& scene, const Document& doc, bool showSpawns, std::vector<GpuVertex>& tris,
                         std::vector<GpuVertex>& lines, std::vector<GpuVertex>& overlay);
// Textured variant: polys whose material resolves to a texture id (>= 0) go into `textured` batches instead of `tris`.
void buildCameraGeometry(const SceneCache& scene, const Document& doc, bool showSpawns, std::vector<GpuVertex>& tris,
                         std::vector<GpuVertex>& lines, std::vector<GpuVertex>& overlay,
                         const std::function<int(const std::string&)>& textureFor, std::vector<TexturedBatch>& textured);

// ---------------------------------------------------------------------------------------------------------------
// The camera's geometry kept between frames (docs/research/85). It holds what buildCameraGeometry would make, as
// vertex streams in the same order, and brings them up to date item by item:
//  - an item whose geometry or selection state changed is made again and written over its old vertices in place
//    (a moved brush, a selected or deselected item: nothing else is touched);
//  - when an item's vertex counts change, or items are added or removed, only the streams those items are in are put
//    together again (the vertices of the other items are copied, not made again);
//  - a change of the view settings (mode, lit preview, lights, textures arriving) makes everything again.
// Each stream records the vertex ranges that changed, so the renderer uploads only those.

class CameraGeometry {
public:
    using TextureFn = std::function<int(const std::string&)>;
    CameraGeometry();
    ~CameraGeometry();
    // Brings the streams up to date. textureFor: null = no textured batches. force: make everything again (textures
    // still arriving, a material image changed, a capture). Returns true when any stream changed.
    bool update(const SceneCache& scene, const Document& doc, bool showSpawns, const TextureFn* textureFor, bool force);

    // Flat triangles in draw order: shadowOnly is drawn in the sun shadow pass only; casters and faceSel in both
    // passes; noCast (markers, tool faces, volumes) on screen only.
    GeoStream<GpuVertex> shadowOnly, casters, faceSel, noCast;
    // Depth-tested lines: the items' outlines, then helpers (probe volumes, navigation, spawns, the world axes).
    GeoStream<GpuVertex> lines, linesMisc;
    // On-top lines in draw order: the sun arrow, the selected items' outlines, then face outlines, tags, the leak
    // line and entity connections. (The camera adds its gizmo after these.)
    GeoStream<GpuVertex> sunArrow, selLines, overlayMisc;
    const std::vector<SceneBatch*>& batches() const { return order_; }
    // Changes whenever something that casts a sun shadow moved, appeared or went (not on a selection change).
    uint64_t casterVersion() const { return casterVersion_; }
    const AABB& sceneBounds() const { return bounds_; }  // the box around every item, as of the last update

    // Counters (tests, benchmark): updates that made everything again, that put streams together again, and items
    // written in place.
    size_t fullBuilds() const { return fullBuilds_; }
    size_t relayouts() const { return relayouts_; }
    size_t itemsUpdatedInPlace() const { return inPlace_; }
    size_t itemsGenerated() const { return generated_; }
    // The streams as buildCameraGeometry's vectors (tests: both must agree). Hidden line slices are left out.
    void flatten(std::vector<GpuVertex>& tris, std::vector<GpuVertex>& lines, std::vector<GpuVertex>& overlay,
                 std::vector<TexturedBatch>& textured) const;
    void clear();  // forget everything (the next update makes it all again)

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
    std::vector<SceneBatch*> order_;
    uint64_t casterVersion_ = 1;
    AABB bounds_;
    size_t fullBuilds_ = 0, relayouts_ = 0, inPlace_ = 0, generated_ = 0;
};

}  // namespace iw8r::app
