// How a stock IW8 material draws (roadmap 3.17), from its technique set name. Replay techset names carry the blend
// mode as the token before the "t.." table token: "rpl" replace (opaque), "alt" alpha test (foliage, grates), "bad"
// blended (decals, glass, effects). Among the blended sets, the converter's own unlit effect sets use "tca" for
// additive and "ta0" for alpha (x64-zt iw3_render_assets.cpp, BuildFxMaterialDefinition), while lit "bad_tca" sets are
// alpha-blended decals (same file, the lit blended decal comment), so only unlit "bad_tca" is read as additive.
#pragma once

#include <string>

namespace iw8r {

enum class MaterialBlend { Opaque, AlphaTest, Blend, Additive };

// Opaque when the name has no recognised blend token (custom materials, empty, ",null").
MaterialBlend materialBlendFromTechset(const std::string& techset);
const char* materialBlendName(MaterialBlend b);  // "opaque", "alpha test", "blend", "additive"
// True when the name carries one of the blend tokens (rpl, alt, bad, bar before a t.. table token).
bool materialBlendKnown(const std::string& techset);

}  // namespace iw8r
