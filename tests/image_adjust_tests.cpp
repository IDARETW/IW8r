#include <cstdlib>
#include <vector>

#include "image_adjust.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

std::vector<uint8_t> px(std::initializer_list<int> v) {
    std::vector<uint8_t> out;
    for (int x : v) out.push_back(static_cast<uint8_t>(x));
    return out;
}

std::vector<uint8_t> solid(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    std::vector<uint8_t> out(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < out.size(); i += 4) {
        out[i] = r;
        out[i + 1] = g;
        out[i + 2] = b;
        out[i + 3] = a;
    }
    return out;
}

// Returns the adjusted single pixel.
std::vector<uint8_t> onePixel(std::vector<uint8_t> in, const ImageAdjust& a) {
    adjustImage(in, 1, 1, a);
    return in;
}

}  // namespace

TEST(image_adjust_identity_leaves_pixels_unchanged) {
    ImageAdjust a;
    CHECK(isIdentity(a));
    std::vector<uint8_t> img = px({0, 1, 2, 3, 128, 64, 200, 255, 255, 0, 17, 99, 7, 250, 130, 0});
    const auto before = img;
    adjustImage(img, 2, 2, a);
    CHECK(img == before);
}

TEST(image_adjust_non_identity_is_detected) {
    ImageAdjust a;
    a.hueShiftDeg = 360.0;  // a full turn is still identity
    CHECK(isIdentity(a));
    a.tint[1] = 0.9;
    CHECK(!isIdentity(a));
    ImageAdjust b;
    b.channelMap[0] = 1;
    CHECK(!isIdentity(b));
}

TEST(image_adjust_brightness_clamps) {
    ImageAdjust up;
    up.brightness = 1.0;
    auto out = onePixel(px({100, 200, 0, 77}), up);
    CHECK(out[0] == 255 && out[1] == 255 && out[2] == 255);
    CHECK(out[3] == 77);  // alpha untouched

    ImageAdjust down;
    down.brightness = -1.0;
    out = onePixel(px({100, 200, 0, 77}), down);
    CHECK(out[0] == 0 && out[1] == 0 && out[2] == 0);
    CHECK(out[3] == 77);
}

TEST(image_adjust_saturation_zero_gives_grey) {
    ImageAdjust a;
    a.saturation = 0.0;
    auto out = onePixel(px({255, 0, 0, 255}), a);
    CHECK(out[0] == out[1]);
    CHECK(out[1] == out[2]);
    CHECK(std::abs(out[0] - 54) <= 1);  // 0.2126 * 255
    CHECK(out[3] == 255);
}

TEST(image_adjust_invert_flips_colour_not_alpha) {
    ImageAdjust a;
    a.invert = true;
    auto out = onePixel(px({10, 200, 255, 128}), a);
    CHECK(out[0] == 245);
    CHECK(out[1] == 55);
    CHECK(out[2] == 0);
    CHECK(out[3] == 128);
}

TEST(image_adjust_tint_multiplies_channels) {
    ImageAdjust a;
    a.tint[0] = 0.5;
    a.tint[1] = 1.0;
    a.tint[2] = 0.0;
    auto out = onePixel(px({200, 200, 200, 9}), a);
    CHECK(std::abs(out[0] - 100) <= 1);
    CHECK(out[1] == 200);
    CHECK(out[2] == 0);
    CHECK(out[3] == 9);
}

TEST(image_adjust_channel_swap_moves_red_and_blue) {
    ImageAdjust a;
    a.channelMap[0] = 2;  // out R <- in B
    a.channelMap[1] = 1;
    a.channelMap[2] = 0;  // out B <- in R
    a.channelMap[3] = 3;
    auto out = onePixel(px({1, 2, 3, 4}), a);
    CHECK(out[0] == 3);
    CHECK(out[1] == 2);
    CHECK(out[2] == 1);
    CHECK(out[3] == 4);
}

TEST(image_adjust_channel_map_constants_and_alpha_source) {
    ImageAdjust a;
    a.channelMap[0] = 4;  // constant 0
    a.channelMap[1] = 5;  // constant 255
    a.channelMap[2] = 0;  // R
    a.channelMap[3] = 0;  // alpha takes red
    auto out = onePixel(px({30, 40, 50, 60}), a);
    CHECK(out[0] == 0);
    CHECK(out[1] == 255);
    CHECK(out[2] == 30);
    CHECK(out[3] == 30);
}

TEST(image_adjust_hue_shift_120_turns_red_into_green) {
    ImageAdjust a;
    a.hueShiftDeg = 120.0;
    auto out = onePixel(px({255, 0, 0, 255}), a);
    CHECK(std::abs(out[0] - 0) <= 2);
    CHECK(std::abs(out[1] - 255) <= 2);
    CHECK(std::abs(out[2] - 0) <= 2);
    CHECK(out[3] == 255);

    // Greys have no hue, so a shift leaves them alone.
    auto grey = onePixel(px({90, 90, 90, 255}), a);
    CHECK(grey[0] == 90 && grey[1] == 90 && grey[2] == 90);
}

TEST(image_adjust_gamma_above_one_brightens_midtones) {
    ImageAdjust a;
    a.gamma = 2.0;
    auto out = onePixel(px({64, 64, 64, 255}), a);
    CHECK(std::abs(out[0] - 128) <= 1);  // 255 * (64/255)^0.5
    CHECK(out[3] == 255);
}

TEST(image_adjust_resize_sizes_and_bad_input) {
    auto src = solid(8, 6, 10, 20, 30, 255);
    std::vector<uint8_t> dst;
    CHECK(resizeImage(src, 8, 6, 4, 3, dst));
    CHECK(dst.size() == 4u * 3u * 4u);
    CHECK(resizeImage(src, 8, 6, 16, 12, dst));
    CHECK(dst.size() == 16u * 12u * 4u);
    CHECK(resizeImage(src, 8, 6, 5, 7, dst));
    CHECK(dst.size() == 5u * 7u * 4u);

    CHECK(!resizeImage(src, 8, 6, 0, 3, dst));
    CHECK(!resizeImage(src, 8, 6, 4, -1, dst));
    CHECK(!resizeImage(src, 7, 6, 4, 3, dst));  // wrong source size
}

TEST(image_adjust_resize_keeps_solid_colour_solid) {
    std::vector<uint8_t> dst;
    auto big = solid(8, 8, 40, 120, 200, 255);
    CHECK(resizeImage(big, 8, 8, 3, 3, dst));  // box filter down
    bool ok = dst.size() == 3u * 3u * 4u;
    for (size_t i = 0; i < dst.size(); i += 4) {
        ok = ok && dst[i] == 40 && dst[i + 1] == 120 && dst[i + 2] == 200 && dst[i + 3] == 255;
    }
    CHECK(ok);

    auto small = solid(2, 2, 40, 120, 200, 255);
    CHECK(resizeImage(small, 2, 2, 5, 7, dst));  // bilinear up
    ok = dst.size() == 5u * 7u * 4u;
    for (size_t i = 0; i < dst.size(); i += 4) {
        ok = ok && dst[i] == 40 && dst[i + 1] == 120 && dst[i + 2] == 200 && dst[i + 3] == 255;
    }
    CHECK(ok);
}

TEST(image_adjust_normal_flat_height_is_straight_up) {
    auto flat = solid(4, 4, 90, 90, 90, 255);
    std::vector<uint8_t> n;
    CHECK(normalMapFromHeight(flat, 4, 4, 1.0, false, n));
    CHECK(n.size() == 4u * 4u * 4u);
    bool ok = true;
    for (size_t i = 0; i < n.size(); i += 4) {
        ok = ok && n[i] == 128 && n[i + 1] == 128 && n[i + 2] == 255 && n[i + 3] == 255;
    }
    CHECK(ok);
}

TEST(image_adjust_normal_slope_tilts_the_right_way) {
    // Height rises to the right: the normal leans left, so red drops below 128.
    std::vector<uint8_t> ramp;
    const int w = 8, h = 8;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint8_t v = static_cast<uint8_t>(x * 32);
            ramp.insert(ramp.end(), {v, v, v, 255});
        }
    }
    std::vector<uint8_t> n;
    CHECK(normalMapFromHeight(ramp, w, h, 1.0, false, n));
    const size_t mid = (3 * w + 4) * 4;  // interior pixel
    CHECK(n[mid] < 128);
    CHECK(std::abs(n[mid + 1] - 128) <= 1);  // no vertical slope
    CHECK(n[mid + 2] > 128);

    // Height rises toward the top row: the normal leans down, so green is below 128.
    std::vector<uint8_t> up;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint8_t v = static_cast<uint8_t>((h - 1 - y) * 32);
            up.insert(up.end(), {v, v, v, 255});
        }
    }
    CHECK(normalMapFromHeight(up, w, h, 1.0, false, n));
    CHECK(n[mid] >= 127 && n[mid] <= 129);
    CHECK(n[mid + 1] < 128);

    // Stronger strength gives a steeper normal (smaller blue).
    std::vector<uint8_t> strong;
    CHECK(normalMapFromHeight(ramp, w, h, 4.0, false, strong));
    CHECK(strong[mid] < 128);
    CHECK(strong[mid + 2] < 255);
}

TEST(image_adjust_normal_rejects_bad_size) {
    std::vector<uint8_t> n;
    CHECK(!normalMapFromHeight(solid(2, 2, 0, 0, 0, 255), 3, 2, 1.0, true, n));
    CHECK(!normalMapFromHeight(solid(2, 2, 0, 0, 0, 255), 0, 2, 1.0, true, n));
}
