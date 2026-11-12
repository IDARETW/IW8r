#include "material_tints.h"
#include "test_harness.h"

using namespace iw8r;

TEST(material_tints_parse_and_apply) {
    MaterialTints t;
    const std::string nl(1, char(10)), tab(1, char(9));
    CHECK(t.parse("# comment" + nl + "mo/me_traffic_cone_01_top" + tab + "ED370F" + tab + "zone" + nl + "bad line" + nl + "x" + tab + "zz" + nl));
    CHECK(t.size() == 1);
    const auto c = t.find("MO/me_traffic_cone_01_top");
    CHECK(c && (*c)[0] == 0xED && (*c)[1] == 0x37 && (*c)[2] == 0x0F);
    CHECK(!t.find("mo/me_traffic_cone_01_base"));
    uint8_t px[8] = {152, 149, 145, 200, 128, 128, 128, 255};
    applyMaterialTint(px, 2, *c);
    CHECK(px[0] == 141 && px[1] == 32 && px[2] == 9 && px[3] == 200);  // orange, alpha kept
    uint8_t same[4] = {10, 20, 30, 40};
    applyMaterialTint(same, 1, {0xFF, 0xFF, 0xFF});
    CHECK(same[0] == 10 && same[1] == 20 && same[2] == 30 && same[3] == 40);  // FF FF FF is neutral
}
