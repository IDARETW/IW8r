// Tests for the material preview renderer (core/material_preview.h). Small sizes so they run fast.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "material_preview.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

constexpr int kSize = 48;
constexpr int kMid = kSize / 2;
// The dark grey background (background 1) in bytes.
constexpr int kBgR = 40, kBgG = 42, kBgB = 46;

PreviewImage solid(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    PreviewImage img;
    img.width = w;
    img.height = h;
    img.rgba.resize(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < img.rgba.size(); i += 4) {
        img.rgba[i] = r;
        img.rgba[i + 1] = g;
        img.rgba[i + 2] = b;
        img.rgba[i + 3] = a;
    }
    return img;
}

// Unlit white material on the dark grey background: the simplest input.
MaterialPreviewInput whiteUnlit() {
    MaterialPreviewInput in;
    in.color = solid(1, 1, 255, 255, 255, 255);
    in.unlit = true;
    in.background = 1;
    return in;
}

int chan(const PreviewImage& img, int x, int y, int c) {
    return img.rgba[(static_cast<size_t>(y) * img.width + x) * 4 + c];
}

bool isBackground(const PreviewImage& img, int x, int y) {
    return chan(img, x, y, 0) == kBgR && chan(img, x, y, 1) == kBgG && chan(img, x, y, 2) == kBgB;
}

bool allBackground(const PreviewImage& img) {
    for (int y = 0; y < img.height; ++y)
        for (int x = 0; x < img.width; ++x)
            if (!isBackground(img, x, y)) return false;
    return true;
}

}  // namespace

TEST(material_preview_is_deterministic) {
    MaterialPreviewInput in;
    in.color.width = in.color.height = 4;
    for (int i = 0; i < 16; ++i)
        in.color.rgba.insert(in.color.rgba.end(), {uint8_t(i * 15), uint8_t(255 - i * 9), uint8_t(i * 3), uint8_t(255)});
    in.normal = solid(2, 2, 200, 130, 255, 255);
    in.specular = solid(2, 2, 120, 120, 120, 180);
    const PreviewImage a = renderMaterialPreview(in, kSize);
    const PreviewImage b = renderMaterialPreview(in, kSize);
    CHECK(a.width == kSize && a.height == kSize);
    CHECK(a.rgba == b.rgba);
}

TEST(material_preview_output_is_opaque_and_sized) {
    const PreviewImage img = renderMaterialPreview(whiteUnlit(), kSize);
    CHECK(img.width == kSize && img.height == kSize);
    CHECK(img.rgba.size() == static_cast<size_t>(kSize) * kSize * 4);
    bool opaque = true;
    for (size_t i = 3; i < img.rgba.size(); i += 4) opaque = opaque && img.rgba[i] == 255;
    CHECK(opaque);
    CHECK(renderMaterialPreview(whiteUnlit(), 0).rgba.empty());
}

TEST(white_unlit_sphere_is_white_at_centre_and_corner_is_background) {
    const PreviewImage img = renderMaterialPreview(whiteUnlit(), kSize);
    CHECK(chan(img, kMid, kMid, 0) == 255);
    CHECK(chan(img, kMid, kMid, 1) == 255);
    CHECK(chan(img, kMid, kMid, 2) == 255);
    CHECK(isBackground(img, 0, 0));
    CHECK(isBackground(img, kSize - 1, kSize - 1));
}

TEST(tint_red_makes_the_centre_red) {
    MaterialPreviewInput in = whiteUnlit();
    in.tint[0] = 1, in.tint[1] = 0, in.tint[2] = 0;
    const PreviewImage img = renderMaterialPreview(in, kSize);
    CHECK(chan(img, kMid, kMid, 0) > 200);
    CHECK(chan(img, kMid, kMid, 1) < 10);
    CHECK(chan(img, kMid, kMid, 2) < 10);
}

TEST(additive_over_dark_background_is_never_darker) {
    MaterialPreviewInput in = whiteUnlit();
    in.color = solid(1, 1, 128, 128, 128, 255);
    in.blendMode = 1;
    const PreviewImage img = renderMaterialPreview(in, kSize);
    bool never_darker = true;
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x)
            for (int c = 0; c < 3; ++c) never_darker = never_darker && chan(img, x, y, c) >= (c == 0 ? kBgR : c == 1 ? kBgG : kBgB);
    CHECK(never_darker);
    CHECK(chan(img, kMid, kMid, 0) > kBgR + 60);  // grey 128 added to the background
}

TEST(multiply_is_never_brighter_than_background) {
    MaterialPreviewInput in = whiteUnlit();
    in.color = solid(1, 1, 128, 128, 128, 255);
    in.blendMode = 2;
    const PreviewImage img = renderMaterialPreview(in, kSize);
    bool never_brighter = true;
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x)
            for (int c = 0; c < 3; ++c) never_brighter = never_brighter && chan(img, x, y, c) <= (c == 0 ? kBgR : c == 1 ? kBgG : kBgB);
    CHECK(never_brighter);
    CHECK(chan(img, kMid, kMid, 0) < kBgR);
}

TEST(alpha_test_with_zero_alpha_shows_only_background) {
    MaterialPreviewInput in = whiteUnlit();
    in.color = solid(1, 1, 255, 255, 255, 0);
    in.alphaTest = true;
    CHECK(allBackground(renderMaterialPreview(in, kSize)));
}

TEST(alpha_blend_half_is_between_background_and_colour) {
    MaterialPreviewInput in = whiteUnlit();
    in.color = solid(1, 1, 255, 255, 255, 128);
    in.blend = true;
    const PreviewImage img = renderMaterialPreview(in, kSize);
    const int r = chan(img, kMid, kMid, 0);
    CHECK(r > kBgR + 60);
    CHECK(r < 240);
}

TEST(lit_sphere_is_brighter_on_the_sun_side) {
    // Object unrotated, sun straight in front of the eye (yaw 0) or straight behind (yaw 180), at the horizon.
    MaterialPreviewInput in;
    in.color = solid(1, 1, 128, 128, 128, 255);
    in.background = 1;
    in.yawDeg = 0, in.pitchDeg = 0;
    in.sunPitchDeg = 0;
    in.sunYawDeg = 0;
    const PreviewImage front = renderMaterialPreview(in, kSize);
    in.sunYawDeg = 180;
    const PreviewImage back = renderMaterialPreview(in, kSize);
    CHECK(chan(front, kMid, kMid, 0) > chan(back, kMid, kMid, 0) + 40);
}

TEST(higher_gloss_gives_brighter_specular_peak) {
    // Black albedo, so only the specular lobe shows. A white reflectance with gloss 0.8 against gloss 0.
    auto peak = [](uint8_t gloss) {
        MaterialPreviewInput in;
        in.color = solid(1, 1, 0, 0, 0, 255);
        in.specular = solid(1, 1, 255, 255, 255, gloss);
        in.background = 1;
        const PreviewImage img = renderMaterialPreview(in, 64);
        int best = 0;
        for (int y = 0; y < img.height; ++y)
            for (int x = 0; x < img.width; ++x) best = std::max(best, chan(img, x, y, 0));
        return best;
    };
    const int hi = peak(204), lo = peak(0);
    CHECK(hi > lo + 20);
    CHECK(lo > 0);
}

TEST(emission_four_is_brighter_than_emission_zero) {
    MaterialPreviewInput in = whiteUnlit();
    in.color = solid(1, 1, 128, 128, 128, 255);
    const int base = chan(renderMaterialPreview(in, kSize), kMid, kMid, 0);
    in.emission = 4;
    const int glow = chan(renderMaterialPreview(in, kSize), kMid, kMid, 0);
    CHECK(glow > base + 30);
}

TEST(one_sided_plane_seen_from_behind_shows_only_background) {
    MaterialPreviewInput in = whiteUnlit();
    in.shape = PreviewShape::Plane;
    in.yawDeg = 180, in.pitchDeg = 0;
    in.twoSided = false;
    CHECK(allBackground(renderMaterialPreview(in, kSize)));
}

TEST(two_sided_plane_seen_from_behind_is_drawn) {
    MaterialPreviewInput in = whiteUnlit();
    in.shape = PreviewShape::Plane;
    in.yawDeg = 180, in.pitchDeg = 0;
    in.twoSided = true;
    const PreviewImage img = renderMaterialPreview(in, kSize);
    CHECK(!allBackground(img));
    CHECK(chan(img, kMid, kMid, 0) > 200);
}

TEST(cube_and_plane_render_something_at_the_centre) {
    MaterialPreviewInput cube = whiteUnlit();
    cube.shape = PreviewShape::Cube;
    const PreviewImage c = renderMaterialPreview(cube, kSize);
    CHECK(chan(c, kMid, kMid, 0) > 200);

    MaterialPreviewInput plane = whiteUnlit();
    plane.shape = PreviewShape::Plane;
    plane.yawDeg = 0, plane.pitchDeg = 0;
    const PreviewImage p = renderMaterialPreview(plane, kSize);
    CHECK(chan(p, kMid, kMid, 0) > 200);
}

TEST(sample_wraps_and_bilinear_filters) {
    PreviewImage img;
    img.width = 2;
    img.height = 1;
    img.rgba = {0, 0, 0, 255, 255, 255, 255, 255};
    double out[4];
    samplePreviewImage(img, 0.25, 0.5, out);  // centre of texel 0
    CHECK(std::fabs(out[0]) < 1e-6);
    samplePreviewImage(img, 0.75, 0.5, out);  // centre of texel 1
    CHECK(std::fabs(out[0] - 1.0) < 1e-6);
    samplePreviewImage(img, 1.0, 0.5, out);  // halfway between texel 1 and the wrapped texel 0
    CHECK(std::fabs(out[0] - 0.5) < 1e-6);
    samplePreviewImage(img, 0.0, 0.5, out);  // the same seam from the other side
    CHECK(std::fabs(out[0] - 0.5) < 1e-6);
    CHECK(std::fabs(out[3] - 1.0) < 1e-6);
}

TEST(sample_of_empty_image_is_white) {
    PreviewImage empty;
    double out[4] = {0, 0, 0, 0};
    samplePreviewImage(empty, 0.3, 0.9, out);
    CHECK(out[0] == 1 && out[1] == 1 && out[2] == 1 && out[3] == 1);
}
