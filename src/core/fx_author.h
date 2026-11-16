// IW8 Radiant - authored effects (roadmap 8.2, research 67).
//
// The mapper's own particle effects: sprite emitters described in <map dir>/fx/<name>.iw8fx.json and built through the
// x64-zt converter's IW3 effect route: the build writes extracted/fx/<name>.iw3.json (the IW3 FxEffectDef graph the
// converter reads, iw3_fastfile.cpp ReadPreparedFx / FxElement) and, per element, an IW3 effect material
// (extracted/materials/<material>.json, ReadFxMaterial: effect_zfeather / effect_zfeather_add) with its colour image.
// The converter turns the graph into a Replay vfx when the map script loads the effect by name (fx_origin placements).
// Pure core code.
#pragma once

#include <array>
#include <string>
#include <vector>

namespace iw8r {

// One key of an element's size / colour curve over its life (t = 0 at birth, 1 at death).
struct AuthoredFxKey {
    double t = 0;
    double size = 8;
    std::array<double, 4> color{1, 1, 1, 1};
};

struct AuthoredFxElement {
    std::string name = "sprite";
    // "sprite" (a camera-facing particle with the image), "light" (an omni light whose radius is the size and whose
    // colour is the colour curve; no image or material) or "model" (the custom model `model`, the size curve is its
    // scale: 1 = as modelled).
    std::string kind = "sprite";
    std::string model;  // kind "model": a custom model name (OBJ / glTF in the map's model folders)
    std::string image;          // colour image (PNG / TGA / JPG), relative to the .iw8fx.json file
    bool additive = false;      // additive glow (fire, sparks) instead of alpha blended (smoke, dust)
    bool looping = true;        // spawns forever (count every interval) or once (count at start)
    int intervalMs = 100;       // looping: time between spawns
    int count = 1;              // particles per spawn (looping) or in total (one-shot)
    int lifeMs = 1500;          // particle life
    int lifeVarMs = 0;          // + up to this much at random
    double radius = 0;          // spawn inside a sphere of this radius (units)
    std::array<double, 3> velocityMin{0, 0, 20};   // world units per second
    std::array<double, 3> velocityMax{0, 0, 40};
    double gravity = 0;         // fraction of world gravity (negative rises)
    double sizeStart = 8, sizeEnd = 16;            // sprite size (units)
    std::array<double, 4> colorStart{1, 1, 1, 1};  // RGBA 0..1
    std::array<double, 4> colorEnd{1, 1, 1, 0};
    double spin = 0;            // radians per second
    // Multi-key curve (2 to 16 keys). Empty: the two keys sizeStart / colorStart at 0 and sizeEnd / colorEnd at 1.
    std::vector<AuthoredFxKey> keys;
};

// Size and colour at life fraction f (0..1): linear between the keys (sorted by t, clamped at the ends).
AuthoredFxKey sampleAuthoredFxKey(const AuthoredFxElement& e, double f);

struct AuthoredFx {
    std::string name;  // effect name used by fx_origin "fx" (e.g. iw8r/campfire)
    std::vector<AuthoredFxElement> elements;
};

// Reads / writes the editor file. parse fails (false, *err) on malformed JSON or values out of range.
bool parseAuthoredFx(const std::string& text, AuthoredFx& out, std::string* err = nullptr);
std::string authoredFxJson(const AuthoredFx& fx);

// Effect and material names: letters, digits, '_', '/' (no leading '/', no "..").
bool isValidAuthoredFxName(const std::string& name);

// The IW3 material and image names the build uses for element i.
std::string authoredFxMaterialName(const AuthoredFx& fx, size_t element);
std::string authoredFxImageName(const AuthoredFx& fx, size_t element);

// Replay period of an effect with looping elements, in milliseconds (0: every element is one-shot). The converter builds
// each element as one burst (no emitter life, no loop flag: research 50), so a looping element is written as a burst
// of period / interval particles whose spawn delays are spread over the period, and the map script plays the effect
// again every period (owner playtest 2026-11-11: a looping effect played once at map load and was never seen).
int authoredFxPeriodMs(const AuthoredFx& fx);
// The converter's IW3 effect graph (schema 1, asset_type iw3_fx).
std::string authoredFxIw3Json(const AuthoredFx& fx);
// The converter's IW3 effect material for element i (one clamped colour map, stateFlags 16, cameraRegion emissive).
std::string authoredFxMaterialJson(const AuthoredFx& fx, size_t element);

// Editor preview: particle state at `t` seconds of a deterministic simulation (seeded per element), positions relative
// to the effect origin. Up to maxParticles in total.
struct FxPreviewParticle {
    double pos[3];
    double size;
    double color[4];
    size_t element;
};
std::vector<FxPreviewParticle> simulateAuthoredFx(const AuthoredFx& fx, double t, size_t maxParticles = 4000);

}  // namespace iw8r
