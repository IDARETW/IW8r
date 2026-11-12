#include <string>

#include "sun_flare.h"
#include "test_harness.h"

using namespace iw8r;

TEST(sun_flare_file_text) {
    std::string err;
    const auto t = sunFileText("16 0.6 0.4 0.2", -50, 45, &err);
    CHECK(t.has_value() && err.empty());
    if (!t) return;
    CHECK(t->find("r_sunsprite_shader \"sun\"") != std::string::npos);  // non-empty: ReadMapSun draws the sprite
    CHECK(t->find("r_sunsprite_size \"16\"") != std::string::npos);
    CHECK(t->find("r_sun_fx_position \"-50 45 0\"") != std::string::npos);
    CHECK(t->find("r_sunflare_max_alpha \"0.6\"") != std::string::npos && t->find("r_sunflare_max_size \"64\"") != std::string::npos);
    CHECK(t->find("r_sunblind_max_darken \"0.4\"") != std::string::npos && t->find("r_sunglare_max_lighten \"0.2\"") != std::string::npos);
    const auto d = sunFileText("12", -30, 120, &err);
    CHECK(d && d->find("r_sunflare_max_alpha \"0\"") != std::string::npos && d->find("r_sunblind_max_darken \"0.5\"") != std::string::npos);
    CHECK(!sunFileText("", 0, 0, &err) && !err.empty());
    CHECK(!sunFileText("100", 0, 0, &err));
    CHECK(!sunFileText("16 2", 0, 0, &err));
}
