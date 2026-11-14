#include "bake_options.h"
#include "map_text.h"
#include "test_harness.h"

using namespace iw8r;

namespace {
Entity worldspawnWith(std::initializer_list<std::pair<const char*, const char*>> keys) {
    std::string text = "{\n\"classname\" \"worldspawn\"\n";
    for (const auto& [k, v] : keys) text += std::string("\"") + k + "\" \"" + v + "\"\n";
    text += "}\n";
    Id next = 1;
    const ParseResult pr = parseMap("iwmap 4\n" + text, next);
    CHECK(pr.map && pr.map->entities.size() == 1);
    return pr.map && !pr.map->entities.empty() ? pr.map->entities[0] : Entity{};
}
}  // namespace

// Plan 1.10: the lightmap bake settings are worldspawn keys with one reader for the build, the viewport and the
// Compile panel's dialog.
TEST(bake_options_defaults_and_keys) {
    const MapBakeOptions none = mapBakeOptions(nullptr);
    CHECK(!none.bake && none.skyIsDefault() && none.texelIsDefault() && none.raysAreDefault() && none.notes.empty());
    CHECK(none.skyIntensity == 0.35 && none.texelSize == 16 && none.skyRays == 32);
    const BakeOptionKeys nk = bakeOptionKeys(none);
    CHECK(nk.sky.empty() && nk.texel.empty() && nk.rays.empty());  // defaults write no key

    const Entity w = worldspawnWith({{"iw8r_bake_lightmaps", "1"}, {"iw8r_bake_sky", "0.9 0.6 0.4 0.5"}, {"iw8r_lightmap_texel", "8"}, {"iw8r_bake_rays", "64"}});
    const MapBakeOptions o = mapBakeOptions(&w);
    CHECK(o.bake && o.notes.empty());
    CHECK(o.skyColor.x == 0.9 && o.skyColor.y == 0.6 && o.skyColor.z == 0.4 && o.skyIntensity == 0.5);
    CHECK(o.texelSize == 8 && o.skyRays == 64 && !o.skyIsDefault() && !o.texelIsDefault() && !o.raysAreDefault());
    const Vec3 light = o.skyLight();
    CHECK(light.x == 0.45 && light.y == 0.3 && light.z == 0.2);
    // What the dialog writes reads back as the same settings.
    const BakeOptionKeys k = bakeOptionKeys(o);
    CHECK(k.sky == "0.9 0.6 0.4 0.5" && k.texel == "8" && k.rays == "64");
    const Entity back = worldspawnWith({{"iw8r_bake_sky", k.sky.c_str()}, {"iw8r_lightmap_texel", k.texel.c_str()}, {"iw8r_bake_rays", k.rays.c_str()}});
    const MapBakeOptions again = mapBakeOptions(&back);
    CHECK(again.skyColor.x == o.skyColor.x && again.skyIntensity == o.skyIntensity && again.texelSize == o.texelSize && again.skyRays == o.skyRays);
    CHECK(!again.bake);
}

TEST(bake_options_bad_values_and_limits) {
    const Entity w = worldspawnWith({{"iw8r_bake_sky", "blue"}, {"iw8r_lightmap_texel", "0.5"}, {"iw8r_bake_rays", "many"}});
    const MapBakeOptions o = mapBakeOptions(&w);
    CHECK(o.notes.size() == 3 && o.skyIsDefault() && o.texelIsDefault() && o.raysAreDefault());
    CHECK(o.notes[0].find("iw8r_bake_sky 'blue' is not 'r g b intensity'") != std::string::npos);
    const Entity neg = worldspawnWith({{"iw8r_bake_sky", "1 1 1 -2"}, {"iw8r_bake_rays", "5000"}});
    const MapBakeOptions n = mapBakeOptions(&neg);
    CHECK(n.notes.size() == 1 && n.skyIsDefault() && n.skyRays == 1024);  // a negative sky is refused, rays are capped
    const Entity five = worldspawnWith({{"iw8r_bake_sky", "1 1 1 1 1"}});
    CHECK(mapBakeOptions(&five).notes.size() == 1);
    // Out-of-range dialog values are clamped to what the bake accepts.
    MapBakeOptions wild;
    wild.skyColor = Vec3{2, -1, 0.5};
    wild.skyIntensity = 99;
    wild.texelSize = 0;
    wild.skyRays = 100000;
    const BakeOptionKeys k = bakeOptionKeys(wild);
    CHECK(k.sky == "1 0 0.5 16" && k.texel == "1" && k.rays == "1024");
}
