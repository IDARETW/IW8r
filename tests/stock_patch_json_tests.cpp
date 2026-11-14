#include "stock_install.h"
#include "stock_zone_edit.h"
#include "test_harness.h"

using namespace iw8r;

// The edits file names the saved stock patches of installed edits (research 88, "Stock patches"): the converter then
// reads those zones as the stock game has them.
TEST(stock_zone_edit_stock_patches_json) {
    const std::vector<StockPatchOverride> none;
    CHECK(stockEditJson("e.ents", {}, {}, nullptr, nullptr, &none) == "{\"entities\":\"e.ents\"}");
    const std::vector<StockPatchOverride> two = {{"mp_br_quarry.fp", "C:\\backup\\mp_br_quarry\\mp_br_quarry.fp.stock"}, {"techsets_mp_br_quarry.fp", ""}};
    CHECK(stockEditJson("e.ents", {}, {}, nullptr, nullptr, &two) ==
          "{\"entities\":\"e.ents\",\"stock_patches\":{\"mp_br_quarry.fp\":\"C:\\\\backup\\\\mp_br_quarry\\\\mp_br_quarry.fp.stock\","
          "\"techsets_mp_br_quarry.fp\":\"\"}}");
    const std::string loot = "[]";
    CHECK(stockEditJson("", {7}, {}, &loot, nullptr, &two).find("\"loot_points\":[],\"stock_patches\":{") != std::string::npos);
}

// Edits installed as whole zones (research 116): the edits file names the kept stock zone per zone file, and asks for
// no patch files.
TEST(stock_zone_edit_stock_zones_json) {
    const std::vector<StockPatchOverride> zones = {{"mp_a.ff", "E:\\game\\iw8r_stock\\mp_a.ff"}};
    CHECK(stockEditJson("e.ents", {}, {}, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &zones, false) ==
          "{\"entities\":\"e.ents\",\"stock_zones\":{\"mp_a.ff\":\"E:\\\\game\\\\iw8r_stock\\\\mp_a.ff\"},\"patch\":false}");
    const std::vector<StockPatchOverride> none;
    CHECK(stockEditJson("", {}, {}, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &none, false) == "{\"patch\":false}");
    CHECK(stockEditJson("", {}, {}, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &none) == "{}");
}
