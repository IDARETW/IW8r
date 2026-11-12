#include "../src/core/stock_vision.h"
#include "test_harness.h"

using namespace iw8r;

TEST(stock_vision_edits_values_only) {
    const std::string text = "NONMKTKNNM \"mp_shipment_clut\"\r\nMSTOLQOTKN \"25000\"\r\nRPNOKKL \"600\"\r\nNMLMMNQLSQ \"0.85 0.92 1\"\r\n";
    const auto v = readVisionValues(text);
    CHECK(v.size() == 4 && v.at(kVisionFogStart) == "600" && v.at(kVisionFogColor) == "0.85 0.92 1");
    int changed = 0;
    std::vector<std::string> missing;
    const std::string out = applyVisionEdits(text, {{kVisionFogStart, "200"}, {kVisionFogDensity, "25000"}, {"NOPE", "1"}}, &changed, &missing);
    CHECK(changed == 1);  // the density was already 25000
    CHECK(missing.size() == 1 && missing[0] == "NOPE");
    CHECK(out == "NONMKTKNNM \"mp_shipment_clut\"\r\nMSTOLQOTKN \"25000\"\r\nRPNOKKL \"200\"\r\nNMLMMNQLSQ \"0.85 0.92 1\"\r\n");
    CHECK(out.find("NOPE") == std::string::npos);  // never added
}
