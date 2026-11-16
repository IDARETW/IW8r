#pragma once
// Effect sprite images (research 138 section 6, research 139): how a particle material of a stock effect becomes the
// image its sprites draw with. Pure helpers, shared by the side bar's effect preview (vfx_preview.h); the camera's
// View > Effect Preview has the same rules in its own copy (main_window.cpp, the setParticleMaterials lambda, and
// CameraView::particleTexture), which can call these instead:
//   - vfxSpriteMaterial()      = the lambda given to CameraView::setParticleMaterials (material cache entry -> kind,
//                                images, atlas, emissive scale);
//   - composeVfxSprite()       = CameraView::particleTexture's lit / unlit image (opacity from slot 27 or slot 68,
//                                unlit opaque, the soft round edge of a sprite without an edge of its own);
//   - prepareVfxDistortion()   = particleTexture's distortion image (read linear, strength moved into blue).
#include <QString>

#include <memory>
#include <string>
#include <vector>

#include "material_cache.h"
#include "texture_decode.h"

namespace iw8r::qt {

// An effect material as a particle sprite draws (the fields of CameraView::ParticleMaterial).
struct VfxSpriteMaterial {
    bool ready = false;        // the material cache has an entry (failed or not)
    bool failed = false;       // the cache could not export it
    bool skip = false;         // not drawn (a distortion set without its image or strength)
    bool distortion = false;   // research 139 section 2: the scene behind, moved by the slot 157 image x constant 127
    std::string distortionFile;
    float distScale[2] = {0, 0};
    bool flare = false;        // an ecf/ flare set (research 139 section 1): additive, on screen
    bool screenMask = false;   // a flare set with a slot 145 mask (a dirty lens piece)
    std::string maskFile;
    bool lit = true;           // lit sets (albedo, alpha blend); unlit sets add their emissive image
    std::string colorFile, alphaFile, emissiveFile;
    std::string densityFile;   // slot 68 (a transmittance): alpha x (1 - T) (lit sets without slot 27)
    int cols = 1, rows = 1;    // the atlas (constant 94)
    float emissive[3] = {1, 1, 1};  // constant 36 of an unlit set
};

// The material cache entry of `material` as a sprite material (ready = true). cacheRoot: MaterialCache::cacheRoot(),
// where a flare set's slot 145 image is looked for.
VfxSpriteMaterial vfxSpriteMaterial(const QString& material, const MaterialCache::Entry& e, const QString& cacheRoot);

// The image the sprite draws, composed in place in `main` (the colour image of a lit set, else the emissive image):
// lit = its opacity from the slot 27 mask (its alpha when that varies, else its red) or the slot 68 transmittance,
// else the colour image's own alpha; unlit = opaque. A sprite whose images give it no edge (lit and opaque, or unlit
// and bright up to its cells' borders) gets a soft round edge in each atlas cell (*softened = true).
void composeVfxSprite(const VfxSpriteMaterial& pm, app::Rgba8Image& main, const app::Rgba8Image* mask, const app::Rgba8Image* density,
                      bool* softened = nullptr);

// A distortion set's slot 157 image as its shader reads it: linear when stored sRGB, the strength mask in blue (the
// effect images keep it in alpha).
void prepareVfxDistortion(app::Rgba8Image& img, bool srgb);

// A decoded sprite: the composed image and its smaller levels (each half the size of the one before, down to cells of
// two texels), for the side bar's software rasterizer.
struct VfxSprite {
    VfxSpriteMaterial mat;
    std::vector<app::Rgba8Image> levels;
    bool softened = false;
};

// Decodes and composes the sprite of `pm` (blocking: a worker thread's job). Null when the material has no image to
// draw or its image cannot be read (*why says which).
std::shared_ptr<VfxSprite> loadVfxSprite(const VfxSpriteMaterial& pm, std::string* why = nullptr);

}  // namespace iw8r::qt
