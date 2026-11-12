#include <cmath>
#include <string>
#include <vector>

#include "audio_zones.h"
#include "json_lite.h"
#include "map_text.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

const char* kMap =
    "iwmap 4\n"
    "{\n\"classname\" \"worldspawn\"\n\"iw8r_reverb\" \"iw8_ext_open_space_med_1\"\n\"iw8r_ambient\" \"amb_test_loop\"\n}\n"
    "{\n\"classname\" \"trigger_multiple\"\n\"iw8r_audio_zone\" \"Hall\"\n\"iw8r_reverb\" \"iw8_lg_room 0.9 0.6 1.5\"\n"
    "\"iw8r_audio_priority\" \"50\"\n\"iw8r_audio_interior\" \"1\"\n"
    "{\n"
    " ( 0 0 0 ) ( 0 128 0 ) ( 0 0 64 ) trigger 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
    " ( 256 0 0 ) ( 256 0 64 ) ( 256 128 0 ) trigger 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
    " ( 0 0 0 ) ( 0 0 64 ) ( 256 0 0 ) trigger 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
    " ( 0 128 0 ) ( 256 128 0 ) ( 0 128 64 ) trigger 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
    " ( 0 0 0 ) ( 256 0 0 ) ( 0 128 0 ) trigger 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
    " ( 0 0 64 ) ( 0 128 64 ) ( 256 0 64 ) trigger 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
    "}\n}\n"
    "{\n\"classname\" \"trigger_multiple\"\n\"iw8r_audio_zone\" \"nobrush\"\n}\n"
    "{\n\"classname\" \"script_origin\"\n\"iw8r_audio_zone\" \"wrong\"\n\"origin\" \"0 0 0\"\n}\n";

long long inum(const json::Value* v) { return v && v->number() ? (long long)*v->number() : -999; }

}  // namespace

TEST(audio_zones_hash_matches_stock_bank) {
    // Stock mp_shipment.all: roomTypeId of these room types (ACTS export).
    CHECK(soundHashName("underwater") == 3399760612u);
    CHECK(soundHashName("iw8_bus_interior") == 1194974377u);
    CHECK(soundHashName("UNDERWATER") == soundHashName("underwater"));
    CHECK(reverbRooms().size() == 59 && findReverbRoom("iw8_lg_room") && !findReverbRoom("bogus_room"));
}

TEST(audio_zones_read_and_write) {
    Id next = 1;
    const ParseResult pr = parseMap(kMap, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    AABB world;
    world.add(Vec3{-1000, -1000, -100});
    world.add(Vec3{1000, 1000, 500});
    std::vector<std::string> warn;
    const AudioSetup s = readAudioSetup(*pr.map, "mp_test", world, &warn);
    CHECK(s.mapZone.roomType == "iw8_ext_open_space_med_1");
    CHECK(s.zones.size() == 1);
    if (s.zones.size() != 1) return;
    const AudioZone& hall = s.zones[0];
    CHECK(hall.name == "hall" && hall.roomType == "iw8_lg_room" && hall.dry == 0.9 && hall.wet == 0.6 && hall.fade == 1.5 &&
          hall.priority == 50);
    CHECK(hall.bounds.valid() && hall.bounds.mins.x == 0 && hall.bounds.maxs.x == 256 && hall.bounds.maxs.z == 64);
    bool noBrush = false, notTrigger = false, ambient = false;
    for (const auto& w : warn) {
        ambient |= w.find("is not one of the map's own sounds") != std::string::npos;
        noBrush |= w.find("no brush") != std::string::npos;
        notTrigger |= w.find("only works on a trigger") != std::string::npos;
    }
    CHECK(noBrush && notTrigger && ambient);
    std::vector<std::string> w2;
    AudioSetup bad = readAudioSetup(*parseMap("iwmap 4\n{\n\"classname\" \"worldspawn\"\n\"iw8r_reverb\" \"nope\"\n}\n", next).map, "mp_t",
                                    world, &w2);
    CHECK(bad.mapZone.roomType.empty() && w2.size() == 1);

    // soundtable.json: ids by hash, reverb indexes in range, the converter's zone fields.
    std::string err;
    const auto table = json::parse(soundTableJson(s), &err);
    CHECK(table.has_value());
    if (!table) return;
    CHECK(table->get("map")->str == "mp_test");
    const json::Value* zones = table->get("zones");
    const json::Value* reverbs = table->get("reverbs");
    CHECK(zones && zones->arr.size() == 2 && reverbs && reverbs->arr.size() == 2);
    const json::Value& z0 = zones->arr[0];
    CHECK(z0.get("zoneName")->str == "mp_test" && (unsigned long long)inum(z0.get("id")) == soundHashName("mp_test") &&
          inum(z0.get("stateId")) == 0 && inum(z0.get("reverbIndex")) == 0 && z0.get("ambientStream")->str.empty());
    const json::Value& z1 = zones->arr[1];
    CHECK(inum(z1.get("reverbIndex")) == 1 && (unsigned long long)inum(reverbs->arr[1].get("id")) == soundHashName("hall_reverb"));
    CHECK((unsigned long long)inum(reverbs->arr[1].get("roomTypeId")) == soundHashName("iw8_lg_room"));
    for (const json::Value& z : zones->arr)
        for (const char* k : {"numMix", "numDisableFullOcc", "numPlayerBreathStates"}) CHECK(inum(z.get(k)) == 0);
    // Stock occlusion / filter presets: the map zone "default" (83 + 1 rows), the interior hall "interior" (84 + 4).
    CHECK(z0.get("occlusionName")->str == "default" && inum(z0.get("startOcclusionIndex")) == 0 && inum(z0.get("numOcclusion")) == 83 &&
          inum(z0.get("startFilterIndex")) == 0 && inum(z0.get("numFilter")) == 1);
    CHECK(z1.get("occlusionName")->str == "interior" && inum(z1.get("startOcclusionIndex")) == 83 && inum(z1.get("numOcclusion")) == 84 &&
          inum(z1.get("startFilterIndex")) == 1 && inum(z1.get("numFilter")) == 4);
    const json::Value* occ = table->get("occlusion_filters");
    const json::Value* fil = table->get("filters");
    CHECK(occ && occ->arr.size() == 167 && fil && fil->arr.size() == 5);
    // Row ids as in the stock bank: the first "default" row (amb_elm_ext_unres_3d) is 219010565.
    CHECK(occ && occ->arr[0].get("entChannel")->str == "amb_elm_ext_unres_3d" && inum(occ->arr[0].get("id")) == 219010565);
    CHECK(fil && inum(fil->arr[0].get("id")) == 3301937107LL);

    // clienttriggers.json: one map-wide trigger at priority 0, the hall centred on its box.
    const auto trig = json::parse(clientTriggersJson(s), &err);
    CHECK(trig.has_value());
    if (!trig) return;
    const json::Value* t = trig->get("triggers");
    CHECK(t && t->arr.size() == 2);
    if (!t || t->arr.size() != 2) return;
    CHECK(inum(t->arr[0].get("type")) == 2 && *t->arr[0].get("priority")->number() == 0 && t->arr[0].get("audio")->str == "mp_test");
    const json::Value& h = t->arr[1];
    CHECK(h.get("audio")->str == "hall" && *h.get("origin")->arr[0].number() == 128 && *h.get("origin")->arr[2].number() == 32);
    const json::Value& hull = h.get("hulls")->arr[0];
    CHECK(*hull.get("halfSize")->arr[0].number() == 128 && *hull.get("halfSize")->arr[1].number() == 64 &&
          hull.get("slabs")->arr.empty());
}

// 6.22: ambient one-shots of the map's own sounds: def / event / element ids as in the stock bank, weights normalised,
// the zone points at its def; stock names are left out with a warning.
TEST(audio_zones_ambient_oneshots) {
    Id next = 1;
    const ParseResult pr =
        parseMap("iwmap 4\n{\n\"classname\" \"worldspawn\"\n\"iw8r_ambient_oneshots\" \"4 9 bird_a:3 bird_b elm_distant_metal\"\n}\n", next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    AABB world;
    world.add(Vec3{-100, -100, -100});
    world.add(Vec3{100, 100, 100});
    std::vector<std::string> warn;
    const AudioSetup s = readAudioSetup(*pr.map, "mp_t", world, &warn, [](const std::string& n) { return n.rfind("bird_", 0) == 0; });
    CHECK(s.any() && s.mapZone.oneShots.size() == 2 && s.mapZone.oneShotMin == 4 && s.mapZone.oneShotMax == 9);
    CHECK(warn.size() == 1 && warn[0].find("elm_distant_metal") != std::string::npos);
    std::string err;
    const auto t = json::parse(soundTableJson(s), &err);
    CHECK(t.has_value());
    if (!t) return;
    const json::Value* defs = t->get("ambient_defs");
    const json::Value* events = t->get("ambient_events");
    const json::Value* elements = t->get("ambient_elements");
    CHECK(defs && defs->arr.size() == 1 && events && events->arr.size() == 2 && elements && elements->arr.size() == 2);
    if (!defs || defs->arr.size() != 1 || !events || events->arr.size() != 2) return;
    CHECK(defs->arr[0].get("name")->str == "mp_t_elements" && (unsigned long long)inum(defs->arr[0].get("id")) == soundHashName("mp_t_elements"));
    CHECK(*defs->arr[0].get("timeMin")->number() == 4 && *defs->arr[0].get("timeMax")->number() == 9 && inum(defs->arr[0].get("numEvents")) == 2);
    CHECK((unsigned long long)inum(events->arr[0].get("id")) == soundHashName("mp_t_elementsbird_a") &&
          std::fabs(*events->arr[0].get("weight")->number() - 0.75) < 1e-9);
    CHECK((unsigned long long)inum(elements->arr[1].get("id")) == soundHashName("bird_b") && elements->arr[1].get("aliasName")->str == "bird_b");
    const json::Value& z0 = t->get("zones")->arr[0];
    CHECK(inum(z0.get("ambientDefIndex")) == 0 && z0.get("ambientName")->str == "mp_t_elements");
    // Stock values check the id scheme: def "exterior_elements" of mp_shipment.all is 1385228915, its event for
    // elm_distant_metal 2766575005, the element 688215947.
    CHECK(soundHashName("exterior_elements") == 1385228915u && soundHashName("exterior_elementselm_distant_metal") == 2766575005u &&
          soundHashName("elm_distant_metal") == 688215947u);
}
