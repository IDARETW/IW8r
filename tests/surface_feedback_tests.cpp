#include "iw8_contents.h"
#include "test_harness.h"

#include <string>

using namespace iw8r;

TEST(surface_feedback_table) {
    // Every IW3 surface type has an impact family; names line up with iw3SurfaceTypeName.
    for (int t = 0; t <= 28; ++t) CHECK(std::string(surfaceFeedback(t).impactFamily).size() > 0);
    CHECK(std::string(iw3SurfaceTypeName(21)) == "wood" && std::string(surfaceFeedback(21).impactFamily) == "wood_solid");
    CHECK(std::string(iw3SurfaceTypeName(9)) == "glass" && std::string(surfaceFeedback(9).scrapeAlias) == "Step_scrape_glass");
    CHECK(std::string(surfaceFeedback(2).impactFamily) == "concrete");  // brick
    CHECK(std::string(surfaceFeedback(99).impactFamily).empty());
}
