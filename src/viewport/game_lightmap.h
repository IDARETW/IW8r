// IW8 Radiant Suite - the viewport's game look (View > Game Lighting): the editor's own lightmap bake of the open map,
// shaded as the game's world shader shades a built map (docs/research/95, "Editor maps in game").
//
// The same baker and settings as the build (core lightmap_bake, worldspawn iw8r_bake_*), on the camera's own
// polygons: brush faces and patches that the game draws get lightmap texels; models and other drawn geometry only
// cast shadows. The camera finds each vertex's lightmap UV by its position, its polygon's normal and its polygon's
// centre, so the result survives the camera rebuilding its vertex streams; polygons changed after the bake simply
// have no lightmap until the next bake.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "geometry.h"
#include "lightmap_bake.h"
#include "stock_lightmap.h"

namespace iw8r::app {

class SceneCache;

struct GameBakeInput {
    struct Poly {
        std::vector<Vec3> pts;
        Vec3 normal, centroid;
    };
    std::vector<Poly> polys;                 // lit by the lightmap
    std::vector<RayBvh::Triangle> occluders;  // cast shadows only
    BakeScene scene;                         // sun, sky, lights (surfaces are made from polys)
    BakeSettings settings;
    uint64_t key = 0;  // the scene state it was gathered from
};

struct GameLightmap {
    int width = 0, height = 0;
    // Four floats per texel, row-major: a = (ambient layer A rgb, sun visibility), b = (directional layer B rgb, 0),
    // d = (the directional light's world direction, 0). A and B are quantised as the game stores them (the IW3 pair's
    // log-encoded bytes, decoded as the converter's shader decodes them).
    std::vector<float> a, b, d;
    uint64_t key = 0;
    size_t charts = 0, texels = 0, polys = 0;
    double seconds = 0, texelSize = 0;
    std::string error;
    // The lightmap UV of a polygon's vertex (false: not baked).
    bool uvFor(const Vec3& pos, const Vec3& normal, const Vec3& centroid, float out[2]) const;
    static uint64_t vertexKey(const Vec3& pos, const Vec3& normal, const Vec3& centroid);
    std::unordered_map<uint64_t, std::array<float, 2>> uv;
};

// What the build would lightmap, from the camera's scene (call on the thread that owns the scene). drawClassFor: the
// stock draw class of a material (0 drawn, 1 shadow only, 2 hidden), null = all drawn. The scene's sun / sky / lights
// are set by the caller.
GameBakeInput gatherGameBake(const SceneCache& scene, const std::function<int(const std::string&)>& drawClassFor);

// A stock map's own lightmap (core stock_lightmap, research 94) as the renderer's three layers
// (SceneRenderer::setStockLightmap): a = (f C, 1), b = ((1 - f) C / max(D.z, .001), 0), d = (D, 0), D in tangent space.
struct StockLightmapLayers {
    int width = 0, height = 0;
    std::vector<float> a, b, d;
    // Per lightmap index: its rectangle in the layers (x, y, width, height, all 0..1); zero = not loaded.
    std::vector<std::array<float, 4>> rects;
};
std::shared_ptr<StockLightmapLayers> stockLightmapLayers(const StockLightmap& lm);
// Several lightmaps (a Warzone map's, by StockLightmap::index) packed into one atlas (shelves, 1-texel gaps).
std::shared_ptr<StockLightmapLayers> stockLightmapAtlas(const std::vector<StockLightmap>& lms);

// Bakes (any thread). Never null; `error` is set when nothing could be baked.
std::shared_ptr<GameLightmap> bakeGameLightmap(const GameBakeInput& in, const std::function<void(double)>& progress = {});

}  // namespace iw8r::app
