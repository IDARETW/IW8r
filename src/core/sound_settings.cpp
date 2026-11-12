#include "sound_settings.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "json_lite.h"

namespace iw8r {
namespace fs = std::filesystem;

namespace {

std::string num(double v) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.9g", v);
    return buf;
}

bool setErr(std::string* err, const std::string& msg) {
    if (err) *err = msg;
    return false;
}

// Reads a number field; returns false when the field is present but not a number.
bool readNumber(const json::Value& doc, const char* key, std::optional<double>& out, std::string* err) {
    const json::Value* v = doc.get(key);
    if (!v) return true;
    if (v->type != json::Value::Type::Number || !std::isfinite(v->num))
        return setErr(err, std::string("\"") + key + "\" must be a number");
    out = v->num;
    return true;
}

bool readBool(const json::Value& doc, const char* key, std::optional<bool>& out, std::string* err) {
    const json::Value* v = doc.get(key);
    if (!v) return true;
    if (v->type != json::Value::Type::Bool) return setErr(err, std::string("\"") + key + "\" must be true or false");
    out = v->b;
    return true;
}

}  // namespace

bool parseSoundSettings(const std::string& jsonText, SoundSettings& out, std::string* err) {
    out = SoundSettings{};
    std::string perr;
    const auto doc = json::parse(jsonText, &perr);
    if (!doc) return setErr(err, "not valid JSON" + (perr.empty() ? std::string() : ": " + perr));
    if (doc->type != json::Value::Type::Object) return setErr(err, "the top level must be an object");

    SoundSettings s;
    if (!readNumber(*doc, "volume", s.volume, err)) return false;
    if (!readNumber(*doc, "dist_min", s.distMin, err)) return false;
    if (!readNumber(*doc, "dist_max", s.distMax, err)) return false;
    if (!readBool(*doc, "loop", s.loop, err)) return false;
    if (!readBool(*doc, "two_d", s.twoD, err)) return false;

    if (s.volume && (*s.volume < 0 || *s.volume > 1)) return setErr(err, "\"volume\" must be between 0 and 1");
    if (s.distMin && *s.distMin < 0) return setErr(err, "\"dist_min\" must not be negative");
    if (s.distMax && *s.distMax < 0) return setErr(err, "\"dist_max\" must not be negative");
    if (s.distMin && s.distMax && *s.distMax < *s.distMin)
        return setErr(err, "\"dist_max\" must not be below \"dist_min\"");

    out = s;
    return true;
}

std::string soundSettingsJson(const SoundSettings& s) {
    std::string body;
    const auto add = [&body](const char* key, const std::string& value) {
        body += body.empty() ? "" : ",\n";
        body += std::string("  \"") + key + "\": " + value;
    };
    if (s.volume) add("volume", num(*s.volume));
    if (s.distMin) add("dist_min", num(*s.distMin));
    if (s.distMax) add("dist_max", num(*s.distMax));
    if (s.loop) add("loop", *s.loop ? "true" : "false");
    if (s.twoD) add("two_d", *s.twoD ? "true" : "false");
    if (body.empty()) return "{}\n";
    return "{\n" + body + "\n}\n";
}

std::string soundSettingsPathFor(const std::string& wavPath) {
    // The extension is the last dot after the last folder separator.
    const size_t slash = wavPath.find_last_of("/\\");
    size_t dot = wavPath.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) dot = wavPath.size();
    return wavPath.substr(0, dot) + ".iw8snd.json";
}

bool loadSoundSettingsFor(const std::string& wavPath, SoundSettings& out, std::string* err) {
    out = SoundSettings{};
    const std::string path = soundSettingsPathFor(wavPath);
    std::error_code ec;
    if (!fs::exists(path, ec)) return true;
    std::ifstream in(fs::path(path), std::ios::binary);
    if (!in) return setErr(err, "cannot read " + path);
    std::ostringstream text;
    text << in.rdbuf();
    std::string perr;
    if (!parseSoundSettings(text.str(), out, &perr)) {
        out = SoundSettings{};
        return setErr(err, perr);
    }
    return true;
}

void applySoundSettings(const SoundSettings& s, CustomSoundAlias& alias) {
    if (s.volume) alias.volume = *s.volume;
    if (s.distMin) alias.distMin = *s.distMin;
    if (s.distMax) alias.distMax = *s.distMax;
    if (s.loop) alias.looping = *s.loop;
    if (s.twoD) alias.ambient2d = *s.twoD;
}

}  // namespace iw8r
