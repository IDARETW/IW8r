// Tests for the texel density module (src/core/texel_density.h).
//
// Expected values come from the TexParams definition in tex_projection.h: u = S / width (+ offset), so one texture
// repeat spans |width| world units, and a texture that is textureWidthPx pixels across has
// textureWidthPx / |width| pixels per world unit along U (the same for V with height).
#include <array>
#include <cmath>
#include <utility>
#include <vector>

#include "../src/core/brush_ops.h"
#include "../src/core/texel_density.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }
bool nearF(float a, float b) { return std::fabs(a - b) <= 1e-5f; }

// A brush cube of side `size` centred on the origin. Every face gets the texture fields (width, height, rot)
// and a lightmap suffix. Texture fields are written after the brush is built, so the suffix text is only a
// placeholder for the lightmap tokens.
Primitive cube(double size, double width, double height, double rot = 0) {
    const double h = size / 2;
    Primitive p = *makeBrushPrimitive(
        makeBox(AABB{{-h, -h, -h}, {h, h, h}}, " caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0"), "\n");
    Primitive::TexFields f;
    f.width = width;
    f.height = height;
    f.xoff = 0;
    f.yoff = 0;
    f.rot = rot;
    p.setTextureFields(f, -1);
    return p;
}

}  // namespace

// 64-unit cube, texture 256 px across, texture width field 64 (one repeat = 64 units). Density along U is
// 256 / 64 = 4 px per unit. Each face is 64 x 64 so its area is 4096.
TEST(texel_cube_density_from_definition) {
    const Primitive p = cube(64, 64, 64);
    for (size_t f = 0; f < p.faces.size(); ++f) {
        const FaceDensity d = faceDensity(p, f, 256, 256, 16384);
        CHECK(near(d.texturePxPerUnit, 4.0));
        CHECK(near(d.area, 64.0 * 64.0));
    }
    CHECK(p.faces.size() == 6);
}

// Scale 1 case from the brief: a texture field of 256 units with a 256 px texture gives 1 px per unit.
TEST(texel_scale_one_is_one_px_per_unit) {
    const Primitive p = cube(64, 256, 256);
    const FaceDensity d = faceDensity(p, 0, 256, 256, 16384);
    CHECK(near(d.texturePxPerUnit, 1.0));
}

// Doubling the texture width field (the texture spans twice the units) halves the density: 4 -> 2.
TEST(texel_scaled_texture_halves_density) {
    const Primitive p4 = cube(64, 64, 64);
    const Primitive p2 = cube(64, 128, 128);
    const double d4 = faceDensity(p4, 0, 256, 256, 16384).texturePxPerUnit;
    const double d2 = faceDensity(p2, 0, 256, 256, 16384).texturePxPerUnit;
    CHECK(near(d4, 4.0));
    CHECK(near(d2, 2.0));
    CHECK(near(d2, d4 / 2.0));
    // 128 width at 256 px is exactly the default target, so it reads as 1.0 in the colour ramp.
    CHECK(near(d2 / kDefaultTexelTarget, 1.0));
}

// Rotation turns the U and V directions together, so the density is the same for any angle.
TEST(texel_rotation_does_not_change_density) {
    const double rots[] = {0, 15, 30, 45, 90, 179.5, -60};
    for (const double r : rots) {
        const Primitive p = cube(64, 64, 64, r);
        for (size_t f = 0; f < p.faces.size(); ++f) {
            CHECK(near(faceDensity(p, f, 256, 256, 16384).texturePxPerUnit, 4.0, 1e-7));
        }
    }
}

// Non-square texture and texel size: U = 256 / 64 = 4, V = 256 / 32 = 8. Geometric mean sqrt(4 * 8) = sqrt(32).
TEST(texel_geometric_mean_of_u_and_v) {
    const Primitive p = cube(64, 64, 32);
    const FaceDensity d = faceDensity(p, 0, 256, 256, 16384);
    CHECK(near(d.texturePxPerUnit, std::sqrt(32.0), 1e-9));
    // With matching pixel aspect (256 x 128 over 64 x 32) both directions are 4, so the mean is 4.
    const FaceDensity e = faceDensity(cube(64, 64, 32), 0, 256, 128, 16384);
    CHECK(near(e.texturePxPerUnit, 4.0));
}

// Width or height 0 are read as 128 by faceUV (tex_projection.h). With 256 px that gives 2 px per unit.
TEST(texel_zero_width_reads_as_128) {
    const Primitive p = cube(64, 0, 0);
    CHECK(near(faceDensity(p, 0, 256, 256, 16384).texturePxPerUnit, 2.0));
}

// Lightmap: the scale is returned as world units per texel when positive and finite; otherwise 0.
TEST(texel_lightmap_units_per_texel) {
    const Primitive p = cube(64, 64, 64);
    CHECK(near(faceDensity(p, 0, 256, 256, 16384).lightmapUnitsPerTexel, 16384.0));
    CHECK(near(faceDensity(p, 0, 256, 256, 2.5).lightmapUnitsPerTexel, 2.5));
    CHECK(near(faceDensity(p, 0, 256, 256, 0).lightmapUnitsPerTexel, 0.0));
    CHECK(near(faceDensity(p, 0, 256, 256, -4).lightmapUnitsPerTexel, 0.0));
    CHECK(near(faceDensity(p, 0, 256, 256, std::nan("")).lightmapUnitsPerTexel, 0.0));
}

// Degenerate inputs give zeros, never a crash.
TEST(texel_invalid_inputs_are_zero) {
    const Primitive p = cube(64, 64, 64);
    const FaceDensity bad = faceDensity(p, 99, 256, 256, 16384);  // face out of range
    CHECK(bad.texturePxPerUnit == 0 && bad.area == 0);
    const FaceDensity noTex = faceDensity(p, 0, 0, 256, 16384);  // texture width 0 pixels
    CHECK(noTex.texturePxPerUnit == 0);
    Primitive notBrush;  // kind Unknown
    const FaceDensity nb = faceDensity(notBrush, 0, 256, 256, 16384);
    CHECK(nb.texturePxPerUnit == 0 && nb.area == 0);
}

// Colour ramp: ratio 0.25 -> blue, 1 -> green, 4 -> red, and the limits 0.5 / 2 are the end colours.
TEST(texel_density_color_ramp) {
    const auto blue = densityColor(0.25, 1.0);
    CHECK(nearF(blue[0], 0.f) && nearF(blue[1], 0.f) && nearF(blue[2], 1.f));
    const auto green = densityColor(1.0, 1.0);
    CHECK(nearF(green[0], 0.f) && nearF(green[1], 1.f) && nearF(green[2], 0.f));
    const auto red = densityColor(4.0, 1.0);
    CHECK(nearF(red[0], 1.f) && nearF(red[1], 0.f) && nearF(red[2], 0.f));
    CHECK(densityColor(0.5, 1.0) == blue);
    CHECK(densityColor(2.0, 1.0) == red);
    // Halfway in log2 between blue (0.5) and green (1): log2(sqrt(0.5)) = -0.5, so the colour is (0, 0.5, 0.5).
    const auto mid = densityColor(std::sqrt(0.5), 1.0);
    CHECK(nearF(mid[0], 0.f) && nearF(mid[1], 0.5f) && nearF(mid[2], 0.5f));
    // The ramp scales with the target: value 4 at target 4 is green.
    const auto g2 = densityColor(4.0, 4.0);
    CHECK(nearF(g2[1], 1.f));
    // Invalid target gives grey.
    const auto grey = densityColor(1.0, 0.0);
    CHECK(nearF(grey[0], 0.5f) && nearF(grey[1], 0.5f) && nearF(grey[2], 0.5f));
}

// Stats: target 2 gives the band [1, 4]. Values 8 is above it, 1 is on the edge (inside).
TEST(texel_density_stats_outliers) {
    const std::vector<std::pair<double, double>> v = {{2, 1}, {2, 1}, {1, 1}, {8, 2}};
    const DensityStats s = densityStats(v, 2.0);
    CHECK(s.faces == 4);
    CHECK(s.outliers == 1);
    CHECK(near(s.min, 1.0));
    CHECK(near(s.max, 8.0));
    // Area weighted mean: (2 + 2 + 1 + 8 * 2) / (1 + 1 + 1 + 2) = 21 / 5.
    CHECK(near(s.mean, 4.2));
}

TEST(texel_density_stats_band_and_empty) {
    const std::vector<std::pair<double, double>> v = {{0.5, 1}, {2, 1}, {5, 1}};
    const DensityStats s = densityStats(v, 2.0);
    CHECK(s.outliers == 2);  // 0.5 is below 1, 5 is above 4
    CHECK(near(s.mean, 2.5));
    // Zero total area falls back to the plain average.
    const DensityStats z = densityStats({{2, 0}, {4, 0}}, 2.0);
    CHECK(near(z.mean, 3.0));
    CHECK(z.outliers == 0);  // band is [1, 4]; both 2 and 4 lie inside it
    // Empty input is all zeros.
    const DensityStats e = densityStats({}, 2.0);
    CHECK(e.faces == 0 && e.outliers == 0 && e.min == 0 && e.max == 0 && e.mean == 0);
}
