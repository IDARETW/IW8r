// The game's own fog and volumetric air as a preset (core atmosphere: worldspawn iw8r_haze, research 126), the vision
// text the viewport reads from it (core game_look), an own map's sky brightness (core sky_image) and a map shader's
// filter with the screen (core custom_shader).
#include "atmosphere.h"
#include "custom_shader.h"
#include "game_fog.h"
#include "game_look.h"
#include "map_text.h"
#include "sky_image.h"
#include "test_harness.h"

#include <cmath>
#include <filesystem>

using namespace iw8r;

namespace {
// The value text of `name` in "name=value; ..." ("" when absent).
std::string valueOf(const std::string& text, const std::string& name) {
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find(';', at);
        if (end == std::string::npos) end = text.size();
        std::string item = text.substr(at, end - at);
        at = end + 1;
        const size_t b = item.find_first_not_of(' ');
        if (b == std::string::npos) continue;
        item = item.substr(b);
        if (item.compare(0, name.size() + 1, name + "=") == 0) return item.substr(name.size() + 1);
    }
    return {};
}

// A worldspawn with the given key lines, parsed from map text.
Entity worldspawnOf(const std::string& keys) {
    Id next = 1;
    ParseResult pr = parseMap("iwmap 4\n{\n\"classname\" \"worldspawn\"\n" + keys + "}\n", next);
    return pr.map && !pr.map->entities.empty() ? pr.map->entities.front() : Entity{};
}

// A repo-relative file, found by walking up from the working directory (tests run from the repo or from bin/<mode>).
std::string repoFile(const std::string& rel) {
    std::filesystem::path p = std::filesystem::current_path();
    for (int i = 0; i < 8; ++i) {
        if (std::filesystem::exists(p / rel)) return (p / rel).string();
        if (!p.has_parent_path() || p.parent_path() == p) break;
        p = p.parent_path();
    }
    return rel;
}
}  // namespace

TEST(haze_key_and_presets) {
    CHECK(hazePresets().size() >= 5);
    for (const HazePreset& p : hazePresets()) {
        const auto h = parseHaze(p.id);
        CHECK(h && h->preset == p.id && h->amount == 1.0);
        CHECK(formatHaze(*h) == p.id);
        CHECK(std::string(p.label).size() > 3 && std::string(p.about).size() > 20);
    }
    const auto h = parseHaze("desert 0.7");
    CHECK(h && h->preset == "desert" && std::fabs(h->amount - 0.7) < 1e-12 && formatHaze(*h) == "desert 0.7");
    CHECK(!parseHaze("") && !parseHaze("nosuch") && !parseHaze("desert 0") && !parseHaze("desert 9") && !parseHaze("desert x") &&
          !parseHaze("desert 1 2"));
    // The worldspawn key: read with the rest of the atmosphere; a bad value is reported and ignored.
    std::string warn;
    Atmosphere a = readAtmosphere(worldspawnOf("\"iw8r_haze\" \"fog 1.5\"\n\"iw8r_fog\" \"100 2000 0.5 0.5 0.5\"\n"), &warn);
    CHECK(a.haze && a.haze->preset == "fog" && a.fog && warn.empty());
    a = readAtmosphere(worldspawnOf("\"iw8r_haze\" \"thick\"\n"), &warn);
    CHECK(!a.haze && warn.find("iw8r_haze") != std::string::npos && warn.find("desert") != std::string::npos);
}

TEST(haze_vision_text) {
    const Vec3 sun{0.5, 0.5, 0.7071};
    const std::string t = hazeVisionText(Haze{"desert", 1.0}, 11.7, sun, 64);
    // The atmospheric fog is on, every field parses as an override, and the build's JSON takes it.
    CHECK(valueOf(t, "fogAtmosphericScattering") == "1");
    std::string err;
    CHECK(!visionOverridesJson(t, &err).empty() && err.empty());
    CHECK(valueOf(t, "fogSunDir") == "0.5 0.5 0.7071");
    CHECK(valueOf(t, "fogHeightFogBaseHeight") == "64" && valueOf(t, "volumetricHeightFogBaseHeight") == "64");
    CHECK(std::stod(valueOf(t, "volumetricAirDensity")) > 0 && std::stod(valueOf(t, "veilStrength")) > 0);
    // A sun at the stock level keeps the preset's brightness; a sun a twelfth as strong (a built map's) lowers the
    // fog's absolute brightness by the same ratio: log2(11.7) stops.
    const double evStock = std::stod(valueOf(t, "fogInscatteringEv"));
    const double evBuilt = std::stod(valueOf(hazeVisionText(Haze{"desert", 1.0}, 1.0, sun, 64), "fogInscatteringEv"));
    CHECK(std::fabs((evStock - evBuilt) - std::log2(11.7)) < 1e-3);
    // The amount scales the densities: half the half-distance, twice the air; colours and brightness stay.
    const std::string d = hazeVisionText(Haze{"desert", 2.0}, 11.7, sun, 64);
    CHECK(std::fabs(std::stod(valueOf(d, "fogHalfPlaneDistance")) * 2 - std::stod(valueOf(t, "fogHalfPlaneDistance"))) < 1e-6);
    CHECK(std::fabs(std::stod(valueOf(d, "volumetricAirDensity")) - 2 * std::stod(valueOf(t, "volumetricAirDensity"))) < 1e-9);
    CHECK(valueOf(d, "fogRayleighAlbedo") == valueOf(t, "fogRayleighAlbedo") && valueOf(d, "fogInscatteringEv") == valueOf(t, "fogInscatteringEv"));
    CHECK(hazeVisionText(Haze{"nosuch", 1.0}, 1.0, sun, 0).empty());
    // Every preset gives a different haze.
    CHECK(hazeVisionText(Haze{"clear", 1.0}, 1.0, sun, 0) != hazeVisionText(Haze{"fog", 1.0}, 1.0, sun, 0));
}

TEST(haze_merge_with_user_vision) {
    // The user's fields win over the preset's, their other fields are added, the order of the rest is kept.
    CHECK(mergeVisionText("a=1; b=2 3 4; c=5", "b=9; d=7") == "a=1; c=5; b=9; d=7");
    CHECK(mergeVisionText("", "x=1") == "x=1" && mergeVisionText("x=1", "") == "x=1" && mergeVisionText("", "").empty());
    CHECK(mergeVisionText("a = 1;b=2", "a=3") == "b=2; a=3");
    const std::string haze = hazeVisionText(Haze{"desert", 1.0}, 1.0, Vec3{0, 0, 1}, 0);
    const std::string merged = mergeVisionText(haze, "volumetricAirDensity=0; tonemapExposureAdjust=0.5");
    CHECK(valueOf(merged, "volumetricAirDensity") == "0" && valueOf(merged, "tonemapExposureAdjust") == "0.5");
    CHECK(valueOf(merged, "fogAtmosphericScattering") == "1");
    std::string err;
    CHECK(!visionOverridesJson(merged, &err).empty());
}

TEST(haze_as_viewport_vision) {
    // The viewport reads the haze the way it reads a stock vision: only the fog, veil and volumetric fields pass.
    const std::string haze = hazeVisionText(Haze{"desert", 1.0}, 1.0, Vec3{0.6, 0, 0.8}, 32);
    const std::string vision = hazeVisionFromOverrides(haze + "; tonemapExposureAdjust=1; sunColor=1 1 1");
    CHECK(!vision.empty() && vision.find("tonemap") == std::string::npos && vision.find("MKKLQTSKSO \"1\"") != std::string::npos);
    const GameFog fog = gameFogFromVision(vision);
    CHECK(fog.on && std::fabs(fog.halfPlane - 5000) < 1e-6 && std::fabs(fog.heightBase - 32) < 1e-6 && fog.mieBlend > 0);
    const GamePostFx fx = gamePostFxFromVision(vision);
    CHECK(std::fabs(fx.veil - 0.09) < 1e-9 && std::fabs(fx.volAir - 0.012) < 1e-9 && std::fabs(fx.volHeightBase - 32) < 1e-6 && fx.volSun > 0);
    const GameFogRefdef refdef = gameFogRefdef(gameFogVisionFromText(vision));
    CHECK(refdef.atmospheric);
    // Tokens pass as they are; other fields give nothing.
    CHECK(hazeVisionFromOverrides("NSKNLMKQSR=0.02") == "NSKNLMKQSR \"0.02\"\n");
    CHECK(hazeVisionFromOverrides("tonemapExposureAdjust=1").empty() && hazeVisionFromOverrides("").empty());
}

TEST(sky_brightness_key) {
    bool bad = true;
    CHECK(parseSkyBrightness(nullptr, &bad) == 1.0f && !bad);
    const std::string two = "2", low = "0.05", junk = "bright", spaced = "3.5 ";
    CHECK(parseSkyBrightness(&two, &bad) == 2.0f && !bad);
    CHECK(parseSkyBrightness(&spaced, &bad) == 3.5f && !bad);
    CHECK(parseSkyBrightness(&low, &bad) == 1.0f && bad);
    CHECK(parseSkyBrightness(&junk, &bad) == 1.0f && bad);
}

TEST(custom_shader_screen_filter) {
    const std::string plain = "float3 iw8rFilter(float3 colour) { return colour; }";
    const std::string screen = plain + "\nfloat3 iw8rFilterScreen(float3 colour, float2 pixel, float viewDepth, float pixelSize) {\n"
                                       "    return iw8rFilter(colour) * (iw8rSceneDepth(pixel) > viewDepth ? 1.0 : 0.5);\n}\n";
    CHECK(!customShaderHasScreenFilter(plain) && customShaderHasScreenFilter(screen));
    CHECK(!customShaderHasScreenFilter("// float3 iw8rFilterScreen(float3 c, float2 p, float d, float s)\n" + plain));
    CustomShader s;
    s.name = "look";
    s.code = plain;
    CHECK(customShaderScreenFilterProgramSource(s).empty());
    s.code = screen;
    // The program the build splices in screen mode: the scene depth at t0 as the game stores it, SV_Position as the
    // second input, the pixel size from the depth image's width.
    const std::string program = customShaderScreenFilterProgramSource(s);
    CHECK(program.find("register(t0)") != std::string::npos && program.find("* 6.0") != std::string::npos);
    CHECK(program.find("float iw8rSceneDepth(float2 pixel)") < program.find("float3 iw8rFilterScreen("));
    CHECK(program.find("SV_Position") != std::string::npos && program.find("iw8rSceneDepthStep") != std::string::npos);
    CHECK(program.find("GetDimensions") != std::string::npos);
    // The viewport's filter variant says that the shader has the screen function.
    const std::string base = "float4 ps_tex(PSIn i) : SV_TARGET { return 0; }\n";
    CHECK(customShaderFilterViewportSource(base, "float4 ps_tex(PSIn i) : SV_TARGET {", s).find("#define IW8R_FILTER_SCREEN 1\n") != std::string::npos);
    s.code = plain;
    CHECK(customShaderFilterViewportSource(base, "float4 ps_tex(PSIn i) : SV_TARGET {", s).find("IW8R_FILTER_SCREEN") == std::string::npos);
    // The shipped toon shader has all three functions and its surface part reads the scene depth.
    CustomShader toon;
    std::string err;
    if (readCustomShader(repoFile("data/shaders/toon.iw8shader.hlsl"), toon, &err)) {
        CHECK(customShaderHasSurface(toon.code) && customShaderHasFilter(toon.code) && customShaderHasScreenFilter(toon.code));
        CHECK(customShaderUsesSceneDepth(toon.code) && customShaderUsesEdges(toon.code));
    } else {
        CHECK(false);
    }
}
