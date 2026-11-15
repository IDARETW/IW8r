#include "iw8_mapents.h"
#include "test_harness.h"

#include <filesystem>
#include <fstream>

using namespace iw8r;
namespace fs = std::filesystem;

// Research 132 gap 3: a stock map's own placed effects (createfx one-shots and loops) are read from its MapEnts.
TEST(mapents_placed_effects) {
    const fs::path dir = fs::temp_directory_path() / "iw8r_mapents_fx";
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path file = dir / "mp_test.d3dbsp.0.asset.json";
    {
        std::ofstream f(file, std::ios::binary);
        f << R"({"format":"mw19-asset-json","profile":"replay-1.20","pool":"map_ents","asset":{"fields":{
"name":{"string":"maps/mp/mp_test.d3dbsp"},"entityString":{"bytes":"7b7d0a00"},"numEntityChars":4,
"clientSideEffects":{"oneshotEffectCount":3,"oneshotEffects":{"count":3,"values":[
 {"origin":{"v":[-927.9,1726.9,6.0]},"angles":{"v":[0,90,0]},"radius":0.0,"delayMsec":-4000,
  "effect":{"particleSystemDef":{"asset_reference":true,"name":"vfx/iw8_mp/level/shipment/vfx_shipment_knee_high_mist"}},
  "effectSound":{"name":null},"aliasList":null},
 {"origin":{"v":[8597.5,6886.4,1319.3]},"angles":{"v":[0,0,0]},"radius":35000.0,"delayMsec":250,
  "effect":{"particleSystemDef":{"name":"vfx/iw8_mp/level/shipment/vfx_shipment_boat_exhaust"}},
  "effectSound":{"name":"amb_boat_engine"}},
 {"origin":{"v":[0,0,0]},"angles":{"v":[0,0,0]},"effect":{"particleSystemDef":null}}]}},
"createFxEffectTotal":179}}})";
    }
    Iw8MapEntsDump dump;
    std::string err;
    CHECK(loadActsMapEnts(file.string(), "", dump, &err));
    CHECK(dump.effects.size() == 2);  // the record without an effect is left out
    if (dump.effects.size() == 2) {
        const Iw8PlacedEffect& a = dump.effects[0];
        CHECK(a.effect == "vfx/iw8_mp/level/shipment/vfx_shipment_knee_high_mist" && a.sound.empty());
        CHECK(std::abs(a.origin.x + 927.9) < 1e-6 && std::abs(a.origin.z - 6.0) < 1e-6 && a.angles.y == 90);
        CHECK(a.delayMs == -4000 && a.radius == 0);
        const Iw8PlacedEffect& b = dump.effects[1];
        CHECK(b.sound == "amb_boat_engine" && b.radius == 35000 && b.delayMs == 250);
    }
    CHECK(dump.sections["createFxEffects"] == 179);
    fs::remove_all(dir, ec);

    // The real Shipment export, when it is on this machine: its 63 one-shot effects.
    const fs::path ship = "C:/iwp/stk/mw19replay/mp_shipment/assets/map_ents/maps/mp/mp_shipment.d3dbsp.5.asset.json";
    if (!fs::exists(ship)) {
        test::skipf("  SKIP mp_shipment MapEnts export not found\n");
        return;
    }
    Iw8MapEntsDump real;
    CHECK(loadActsMapEnts(ship.string(), "", real, &err));
    CHECK(real.effects.size() == 63);
    size_t mist = 0;
    for (const auto& e : real.effects) mist += e.effect.find("knee_high_mist") != std::string::npos;
    CHECK(mist > 0);
}
