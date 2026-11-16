// IW8 Radiant - mapper-side custom material library.
//
// A material definition is a small JSON file <library>/<name>.iw8mat.json (name may contain '/'):
//   {"name":"custom/brick_red", "color":"brick_red.png", "normal":"brick_red_n.png", "specular":"brick_s.png",
//    "surface":"brick", "alphaTest":false, "twoSided":false, "scale":[128,128]}
// Image paths are relative to the definition file (absolute paths are accepted). Only "color" is required.
// The compiler (compile_iw8.cpp) emits such a material as extracted/materials/<name>.json with DDS images
// (doc 21: what x64-zt accepts). Images are decoded with stb_image (PNG, TGA, JPG, BMP) or, for DDS, by a
// minimal uncompressed reader.
#pragma once

#include <memory>
#include "custom_shader.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace iw8r {

struct CustomMaterial {
    std::string name;                 // as written in the definition (the editor's material token)
    std::string colorImage;           // absolute path of the colour image (required)
    std::string normalImage;          // absolute path, empty when none
    std::string specImage;            // absolute path, empty when none (RGB reflectance, alpha gloss)
    int surfaceType = 0;              // IW3 surface type 1..28 (iw8_contents.h), 0 for default
    bool alphaTest = false;           // colour alpha is a cut-out mask
    bool twoSided = false;            // no back-face culling
    bool blend = false;               // alpha-blended (decals, glass): the colour alpha is coverage (techset tag _b0)
    double width = 128, height = 128; // default texture size in world units for the editor
    std::string defFile;              // absolute path of the definition file
    std::string buildName;            // material name written into the build ("" = name); stock textures use iw8r_stock/...
    // Shader state (roadmap 12.2 / 12.6, research 68: what the converter reads from a material definition).
    //  blendMode  0 = none (or alpha blending when `blend`), 1 = additive (glows, light shafts), 2 = multiply (stains)
    //  unlit      takes no light (signs, screens); emission > 1 makes it glow by that factor
    //  tint       multiplies the colour image (each 0..4); sortKey 9..40 layers decals (higher draws on top), 0 = default
    //  polygonOffset 0..2 pulls a coplanar layer off the surface under it
    int blendMode = 0;
    bool unlit = false;
    double emission = 0;
    double tint[3] = {1, 1, 1};
    int sortKey = 0;
    int polygonOffset = 0;
    // "shader": a custom surface shader file (core custom_shader, *.iw8shader.hlsl, relative to the definition):
    // the Game Lighting viewport and the build light the material with it. Null = the game's own lighting.
    std::shared_ptr<const CustomShader> shader;
    bool hasTint() const { return tint[0] != 1 || tint[1] != 1 || tint[2] != 1; }
};

class MaterialLibrary {
public:
    // Scans every root recursively for *.iw8mat.json. Returns false when any definition is invalid
    // (its message is appended to *err, one per line); the valid definitions are still loaded.
    // A name defined twice (case-insensitive) is an error.
    bool load(const std::vector<std::string>& roots, std::string* err);

    // Case-insensitive lookup by the editor's material token. nullptr when not a custom material.
    const CustomMaterial* find(const std::string& name) const;

    // Names in sorted (case-insensitive) order.
    std::vector<std::string> names() const;

    // Editor "Import Texture": writes <libraryRoot>/<name>.iw8mat.json for an existing image. The image is not
    // copied; "color" is the image path relative to the definition file. Fails when the image cannot be decoded,
    // the name is invalid, or the definition already exists.
    static bool createFromImage(const std::string& imagePath, const std::string& libraryRoot, const std::string& name,
                                std::string* err);

private:
    std::map<std::string, CustomMaterial> byKey_;  // lower-case name -> definition
};

// Decodes an image file to 8-bit RGBA (w*h*4 bytes, top-left origin). PNG/TGA/JPG/BMP through stb_image;
// DDS through a reader for uncompressed 8/16/24/32-bit files (top mip only). Compressed DDS is refused.
bool decodeImageFile(const std::string& path, int& w, int& h, std::vector<uint8_t>& rgba, std::string* err);

// Encodes an image as the DDS the converter accepts (doc 21 section 4): uncompressed B8G8R8A8, 2D, full mip chain,
// sides resized to powers of two no larger than 2048. Normal maps are written opaque. Other maps keep alpha when
// keepAlpha is set (alpha test, specular gloss) and are opaque otherwise.
bool encodeDdsFromImage(const std::string& imagePath, bool normalMap, bool keepAlpha, std::vector<uint8_t>& dds,
                        std::string* err);

// Writes encodeDdsFromImage's output to ddsPath (parent folders are created).
bool writeDdsFromImage(const std::string& imagePath, const std::string& ddsPath, bool normalMap, std::string* err,
                       bool keepAlpha = false);

// Hash used for tokens in generated names (FNV-1a 32 bit over the bytes), as 8 lower-case hex digits.
std::string customNameHash(const std::string& text);

// True when the name is a valid material or library name: [A-Za-z0-9_./-], no "..", "//", leading or trailing
// '/', and not under "mc/" (the converter's stub namespace).
bool isValidCustomMaterialName(const std::string& name);

}  // namespace iw8r
