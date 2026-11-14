// IW8 Radiant Suite - the lightmap bake settings of a map (worldspawn keys), read in one place.
//
// The build's bake (compile_iw8.cpp), the viewport's preview bake (View > Game Lighting) and the Compile panel's
// Bake Settings dialog all use these, so the three cannot drift apart.
//
//   iw8r_bake_lightmaps  "1": the build bakes the world's lightmap
//   iw8r_bake_sky        "r g b intensity": the sky colour (0..1 each) and its strength in sun units
//   iw8r_lightmap_texel  world units per lightmap texel (1 or more)
//   iw8r_bake_rays       sky rays per texel (1..1024)
// Pure core code.
#pragma once

#include <string>
#include <vector>

#include "geometry.h"
#include "map_text.h"

namespace iw8r {

struct MapBakeOptions {
    bool bake = false;
    Vec3 skyColor{0.55, 0.65, 0.8};  // a pale blue
    double skyIntensity = 0.35;      // irradiance of the open sky on an upward surface, in sun units
    double texelSize = 16;
    int skyRays = 32;
    std::vector<std::string> notes;  // values that could not be read (the default is used for each)

    bool skyIsDefault() const;
    bool texelIsDefault() const { return texelSize == 16; }
    bool raysAreDefault() const { return skyRays == 32; }
    Vec3 skyLight() const { return Vec3{skyColor.x * skyIntensity, skyColor.y * skyIntensity, skyColor.z * skyIntensity}; }
};

// `worldspawn` may be null (all defaults).
MapBakeOptions mapBakeOptions(const Entity* worldspawn);

// The worldspawn key values for these options, in key order iw8r_bake_sky, iw8r_lightmap_texel, iw8r_bake_rays.
// A value at its default is "" (the key is removed). Values are clamped to what the bake accepts.
struct BakeOptionKeys {
    std::string sky, texel, rays;
};
BakeOptionKeys bakeOptionKeys(const MapBakeOptions& o);

}  // namespace iw8r
