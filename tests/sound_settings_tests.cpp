#include "sound_settings.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

// Scratch folder under the project's out/ (relative to the repo root, where the test runner is started).
const fs::path kDir = "out/agent_snd_test";

void writeText(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream o(p, std::ios::binary);
    o << text;
}

}  // namespace

TEST(sound_settings_round_trip) {
    SoundSettings s;
    s.volume = 0.8;
    s.distMin = 120;
    s.distMax = 1500;
    s.loop = true;
    s.twoD = false;
    const std::string text = soundSettingsJson(s);
    CHECK(!text.empty() && text.back() == '\n');
    SoundSettings back;
    std::string err;
    CHECK(parseSoundSettings(text, back, &err));
    CHECK(err.empty());
    CHECK(back.volume && *back.volume == 0.8);
    CHECK(back.distMin && *back.distMin == 120);
    CHECK(back.distMax && *back.distMax == 1500);
    CHECK(back.loop && *back.loop == true);
    CHECK(back.twoD && *back.twoD == false);
}

TEST(sound_settings_json_writes_only_set_fields) {
    SoundSettings s;
    s.loop = true;
    const std::string text = soundSettingsJson(s);
    CHECK(text.find("\"loop\": true") != std::string::npos);
    CHECK(text.find("volume") == std::string::npos);
    CHECK(text.find("dist_min") == std::string::npos);
    CHECK(soundSettingsJson(SoundSettings{}) == "{}\n");
}

TEST(sound_settings_parses_spec_example) {
    SoundSettings s;
    std::string err;
    const bool ok = parseSoundSettings(R"({ "volume": 0.8, "dist_min": 120, "dist_max": 1500, "loop": true, "two_d": false })", s, &err);
    CHECK(ok);
    CHECK(s.volume && *s.volume == 0.8);
    CHECK(s.loop && *s.loop);
}

TEST(sound_settings_rejects_volume_out_of_range) {
    SoundSettings s;
    std::string err;
    CHECK(!parseSoundSettings(R"({"volume": 1.5})", s, &err));
    CHECK(!err.empty());
    CHECK(!parseSoundSettings(R"({"volume": -0.1})", s, nullptr));
    CHECK(parseSoundSettings(R"({"volume": 0})", s, nullptr));
    CHECK(parseSoundSettings(R"({"volume": 1})", s, nullptr));
}

TEST(sound_settings_rejects_negative_distance) {
    SoundSettings s;
    std::string err;
    CHECK(!parseSoundSettings(R"({"dist_min": -1})", s, &err));
    CHECK(!err.empty());
    CHECK(!parseSoundSettings(R"({"dist_max": -5})", s, nullptr));
}

TEST(sound_settings_rejects_max_below_min) {
    SoundSettings s;
    std::string err;
    CHECK(!parseSoundSettings(R"({"dist_min": 500, "dist_max": 400})", s, &err));
    CHECK(!err.empty());
    CHECK(parseSoundSettings(R"({"dist_min": 400, "dist_max": 400})", s, nullptr));
}

TEST(sound_settings_rejects_malformed_json_and_wrong_types) {
    SoundSettings s;
    std::string err;
    CHECK(!parseSoundSettings(R"({"volume": 0.5)", s, &err));
    CHECK(!err.empty());
    CHECK(!parseSoundSettings("[1, 2]", s, nullptr));
    CHECK(!parseSoundSettings(R"({"volume": "loud"})", s, nullptr));
    CHECK(!parseSoundSettings(R"({"loop": 1})", s, nullptr));
    CHECK(!parseSoundSettings(R"({"two_d": "yes"})", s, nullptr));
}

TEST(sound_settings_ignores_unknown_fields) {
    SoundSettings s;
    CHECK(parseSoundSettings(R"({"colour": "red", "volume": 0.25, "extra": {"a": 1}})", s, nullptr));
    CHECK(s.volume && *s.volume == 0.25);
    CHECK(!s.loop && !s.twoD && !s.distMin && !s.distMax);
}

TEST(sound_settings_path_is_same_stem) {
    CHECK(soundSettingsPathFor("E:\\map\\sounds\\fire_loop.wav") == "E:\\map\\sounds\\fire_loop.iw8snd.json");
    CHECK(soundSettingsPathFor("sounds/fire.loop.wav") == "sounds/fire.loop.iw8snd.json");
    CHECK(soundSettingsPathFor("sounds/noext") == "sounds/noext.iw8snd.json");
    CHECK(soundSettingsPathFor("dir.v2/noext") == "dir.v2/noext.iw8snd.json");
}

TEST(sound_settings_missing_sidecar_is_unset_and_valid) {
    const fs::path wav = kDir / "missing" / "fire_loop.wav";
    fs::create_directories(wav.parent_path());
    SoundSettings s;
    s.volume = 0.3;  // must be cleared
    std::string err;
    CHECK(loadSoundSettingsFor(wav.string(), s, &err));
    CHECK(err.empty());
    CHECK(!s.volume && !s.distMin && !s.distMax && !s.loop && !s.twoD);
    fs::remove_all(kDir / "missing");
}

TEST(sound_settings_invalid_sidecar_reports_error) {
    const fs::path dir = kDir / "invalid";
    writeText(dir / "bad.iw8snd.json", R"({"volume": 3})");
    SoundSettings s;
    std::string err;
    CHECK(!loadSoundSettingsFor((dir / "bad.wav").string(), s, &err));
    CHECK(!err.empty());
    CHECK(!s.volume);
    fs::remove_all(dir);
}

TEST(sound_settings_load_and_apply_overrides_only_set_fields) {
    const fs::path dir = kDir / "apply";
    writeText(dir / "fire_loop.iw8snd.json", R"({"volume": 0.8, "dist_max": 1500, "loop": true})");
    SoundSettings s;
    std::string err;
    CHECK(loadSoundSettingsFor((dir / "fire_loop.wav").string(), s, &err));
    CHECK(err.empty());

    CustomSoundAlias a;
    a.name = "fire_loop";
    a.fileName = "fire_loop.wav";
    a.volume = 1;
    a.distMin = 200;
    a.distMax = 1000;
    a.looping = false;
    a.ambient2d = true;
    applySoundSettings(s, a);
    CHECK(a.volume == 0.8);
    CHECK(a.distMax == 1500);
    CHECK(a.distMin == 200);   // not set in the sidecar, kept
    CHECK(a.looping);           // set to true
    CHECK(a.ambient2d);         // not set, kept
    CHECK(a.name == "fire_loop" && a.fileName == "fire_loop.wav");
    fs::remove_all(dir);
}
