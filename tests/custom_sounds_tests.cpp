#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

#include "custom_sounds.h"
#include "json_lite.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

TEST(custom_sounds_names_and_lookup) {
    CHECK(isValidCustomSoundName("fire_loop_01") && !isValidCustomSoundName("Fire") && !isValidCustomSoundName("a/b") &&
          !isValidCustomSoundName(""));
    const fs::path dir = fs::path(test::tmpDir()) / "custom_sounds";
    fs::create_directories(dir);
    std::ofstream(dir / "river.wav") << "RIFF";
    std::ofstream(dir / "wind.mp3") << "ID3";
    CHECK(findCustomSound({dir.string()}, "river").find("river.wav") != std::string::npos);
    CHECK(findCustomSound({dir.string()}, "wind").find("wind.mp3") != std::string::npos);
    CHECK(findCustomSound({dir.string()}, "missing").empty() && findCustomSound({dir.string()}, "../river").empty());
}

// The converter's IW3 alias schema (x64-zt iw3_sound.cpp ReadZoneAliases): flags bit 0 looping, bits 8-13 channel.
TEST(custom_sounds_alias_json) {
    CustomSoundAlias a;
    a.name = "river";
    a.fileName = "river.wav";
    a.looping = true;
    a.ambient2d = true;
    a.volume = 0.8;
    a.distMax = 1500;
    std::string err;
    const auto j = json::parse(customSoundAliasJson(a), &err);
    CHECK(j.has_value());
    if (!j) return;
    CHECK(j->get("name")->str == "river" && j->get("aliases")->arr.size() == 1);
    const json::Value& al = j->get("aliases")->arr[0];
    CHECK(al.get("file")->get("name")->str == "river.wav" && al.get("file")->get("directory")->str == "iw8r");
    const unsigned flags = unsigned(*al.get("flags")->number());
    CHECK((flags & 1u) == 1u && ((flags >> 8) & 63u) == 24u);  // looping, IW3 channel "ambient"
    // The converter multiplies a looping alias by 0.18: the written volume divides that out (0.8 / 0.18).
    CHECK(std::fabs(*al.get("volume")->get("min")->number() - 0.8 / 0.18) < 1e-6 && *al.get("distance")->get("max")->number() == 1500);
    CHECK(converterRoleGain("river", true) == 0.18 && converterRoleGain("door_creak", false) == 0.45 &&
          converterRoleGain("campfire_pop", false) == 0.35 && converterRoleGain("chirp", false) == 1.0);
    a.looping = false;
    a.ambient2d = false;
    const auto k = json::parse(customSoundAliasJson(a), &err);
    CHECK(k && (unsigned(*k->get("aliases")->arr[0].get("flags")->number()) == (1u << 8)));  // one-shot, channel "auto"
}
