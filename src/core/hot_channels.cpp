#include "hot_channels.h"

#include <algorithm>

#include <cstdio>

#include <cctype>
#include <cmath>
#include <cstdlib>

#include "map_text.h"

namespace iw8r {

namespace {

// Effect and alias names: the same safe characters as the map script (map_script.cpp safeEffectName).
bool safeName(const std::string& s, bool allowSlash) {
    if (s.empty() || s.size() > kHotChannelMaxLen) return false;
    for (char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        const bool ok = std::isalnum(u) || c == '_' || c == '-' || c == '.' || (allowSlash && c == '/');
        if (!ok) return false;
    }
    return true;
}

bool finite3(const double v[3]) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

std::string num(double v) { return formatNumber(v); }

std::string vec3(const double v[3]) { return num(v[0]) + " " + num(v[1]) + " " + num(v[2]); }

// Single-space split. An empty token (a double space) is kept, so the field count check rejects it.
std::vector<std::string> tokens(const std::string& value) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : value) {
        if (c == ' ') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

bool parseNum(const std::string& t, double& out) {
    if (t.empty()) return false;
    char* end = nullptr;
    out = std::strtod(t.c_str(), &end);
    return end == t.c_str() + t.size() && std::isfinite(out);
}

bool parseUint(const std::string& t, unsigned& out) {
    if (t.empty() || t.size() > 9) return false;
    unsigned v = 0;
    for (char c : t) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + unsigned(c - '0');
    }
    out = v;
    return true;
}

bool parseFlag(const std::string& t, bool& out) {
    if (t == "1") out = true;
    else if (t == "0") out = false;
    else return false;
    return true;
}

bool parseVec3(const std::vector<std::string>& t, size_t first, double out[3]) {
    for (size_t i = 0; i < 3; ++i)
        if (!parseNum(t[first + i], out[i])) return false;
    return true;
}

}  // namespace

std::string encodeHotFx(const std::vector<HotFx>& items, unsigned seq) {
    if (items.empty()) return "";
    std::string s = std::to_string(seq) + " fx " + std::to_string(items.size());
    for (const HotFx& f : items) {
        if (!safeName(f.effect, true) || !finite3(f.origin) || !finite3(f.angles)) return "";
        s += " " + f.effect + " " + vec3(f.origin) + " " + vec3(f.angles) + " " + (f.play ? "1" : "0");
    }
    if (s.size() > kHotChannelMaxLen) return "";
    return s;
}

std::string encodeHotSound(const HotSound& sound, unsigned seq) {
    if (!safeName(sound.alias, false) || !finite3(sound.origin)) return "";
    std::string s = std::to_string(seq) + " snd " + sound.alias + " " + vec3(sound.origin) + " " + (sound.loop ? "1" : "0") + " " +
                    (sound.stop ? "1" : "0");
    if (s.size() > kHotChannelMaxLen) return "";
    return s;
}

std::string encodeHotSun(const HotSun& sun, unsigned seq) {
    if (!finite3(sun.dir) || !std::isfinite(sun.intensity) || sun.intensity < 0) return "";
    for (float c : sun.color)
        if (!std::isfinite(c)) return "";
    const double color[3] = {double(sun.color[0]), double(sun.color[1]), double(sun.color[2])};
    std::string s = std::to_string(seq) + " sun " + vec3(sun.dir) + " " + vec3(color) + " " + num(sun.intensity);
    if (s.size() > kHotChannelMaxLen) return "";
    return s;
}

std::optional<std::vector<HotFx>> decodeHotFx(const std::string& value, unsigned* seq) {
    if (value.empty() || value.size() > kHotChannelMaxLen) return std::nullopt;
    const std::vector<std::string> t = tokens(value);
    unsigned s = 0, n = 0;
    if (t.size() < 3 || t[1] != "fx" || !parseUint(t[0], s) || !parseUint(t[2], n)) return std::nullopt;
    if (t.size() != 3 + 8 * size_t(n)) return std::nullopt;
    std::vector<HotFx> out;
    for (size_t i = 0; i < n; ++i) {
        const size_t k = 3 + 8 * i;
        HotFx f;
        if (!safeName(t[k], true)) return std::nullopt;
        f.effect = t[k];
        if (!parseVec3(t, k + 1, f.origin) || !parseVec3(t, k + 4, f.angles)) return std::nullopt;
        if (!parseFlag(t[k + 7], f.play)) return std::nullopt;
        out.push_back(f);
    }
    if (seq) *seq = s;
    return out;
}

std::optional<HotSound> decodeHotSound(const std::string& value, unsigned* seq) {
    if (value.empty() || value.size() > kHotChannelMaxLen) return std::nullopt;
    const std::vector<std::string> t = tokens(value);
    unsigned s = 0;
    if (t.size() != 8 || t[1] != "snd" || !parseUint(t[0], s)) return std::nullopt;
    HotSound h;
    if (!safeName(t[2], false)) return std::nullopt;
    h.alias = t[2];
    if (!parseVec3(t, 3, h.origin) || !parseFlag(t[6], h.loop) || !parseFlag(t[7], h.stop)) return std::nullopt;
    if (seq) *seq = s;
    return h;
}

std::optional<HotSun> decodeHotSun(const std::string& value, unsigned* seq) {
    if (value.empty() || value.size() > kHotChannelMaxLen) return std::nullopt;
    const std::vector<std::string> t = tokens(value);
    unsigned s = 0;
    if (t.size() != 9 || t[1] != "sun" || !parseUint(t[0], s)) return std::nullopt;
    HotSun h;
    double color[3] = {};
    if (!parseVec3(t, 2, h.dir) || !parseVec3(t, 5, color) || !parseNum(t[8], h.intensity)) return std::nullopt;
    if (h.intensity < 0) return std::nullopt;
    for (size_t i = 0; i < 3; ++i) h.color[i] = float(color[i]);
    if (seq) *seq = s;
    return h;
}

std::string hotChannelsGsc() {
    // Values are applied once each: the thread remembers the last string per channel, and the seq in front of every
    // value makes a new send a new string. At start the current dvar value is applied too (as the placement channel does).
    // Every name here is prefixed iw8r_ and the effect and sound handles live in level.iw8r_hotfx / level.iw8r_hotsnd.
    return R"GSC(// Hot channels (IW8 Radiant Live Link, roadmap 7.7 / 7.9). Start with: level thread iw8r_hot_channels();
// iw8r_fx  = "seq fx n [effect x y z pitch yaw roll play]*n"
// iw8r_snd = "seq snd alias x y z loop stop"
// (No sun channel: lerpsunangles / setsuncolorandintensity from a map script crashed Replay MP, owner test 2026-11-10.)
iw8r_hot_channels()
{
    if (!isdefined(level.iw8r_hotfx))
        level.iw8r_hotfx = [];
    level.iw8r_hotsnd = [];
    lastFx = "";
    lastSnd = "";
    for (;;)
    {
        wait 0.1;
        v = getdvar("iw8r_fx");
        if (isdefined(v) && v != "" && v != lastFx)
        {
            lastFx = v;
            iw8r_hot_fx(v);
        }
        v = getdvar("iw8r_snd");
        if (isdefined(v) && v != "" && v != lastSnd)
        {
            lastSnd = v;
            iw8r_hot_snd(v);
        }
    }
}

iw8r_hot_fx(v)
{
    t = strtok(v, " ");
    if (t.size < 3)
        return;
    n = int(t[2]);
    k = 3;
    for (i = 0; i < n; i += 1)
    {
        if (t.size < k + 8)
            return;
        fx = iw8r_hot_fx_get(t[k]);
        origin = (float(t[k + 1]), float(t[k + 2]), float(t[k + 3]));
        angles = (float(t[k + 4]), float(t[k + 5]), float(t[k + 6]));
        if (isdefined(fx) && int(t[k + 7]))
            playfx(fx, origin, anglestoforward(angles), anglestoup(angles));
        k += 8;
    }
}

// Only effects the map loaded in main() (level.iw8r_hotfx[name], filled by the map script) play: a loadfx after the
// level started is not safe, so an unknown name is ignored (undefined) instead of loaded late.
iw8r_hot_fx_get(name)
{
    return level.iw8r_hotfx[name];
}

iw8r_hot_snd(v)
{
    t = strtok(v, " ");
    if (t.size < 8)
        return;
    alias = t[2];
    origin = (float(t[3]), float(t[4]), float(t[5]));
    ent = level.iw8r_hotsnd[alias];
    if (int(t[7]))
    {
        if (isdefined(ent))
        {
            ent stoploopsound(alias);
            ent delete();
            level.iw8r_hotsnd[alias] = undefined;
        }
        return;
    }
    if (!int(t[6]))
    {
        playsoundatpos(origin, alias);
        return;
    }
    if (!isdefined(ent))
    {
        ent = spawn("script_origin", origin);
        level.iw8r_hotsnd[alias] = ent;
    }
    ent.origin = origin;
    ent stoploopsound(alias);
    ent playloopsound(alias);
}
)GSC";
}

}  // namespace iw8r

namespace iw8r {

HotSun hotSunFromKeys(const std::string& sundirection, const std::string& suncolor, const std::string& sunlight) {
    HotSun h;
    double a[3] = {0, 0, 0};
    std::sscanf(sundirection.c_str(), "%lf %lf %lf", &a[0], &a[1], &a[2]);
    for (int k = 0; k < 3; ++k) h.dir[k] = a[k];  // the key's angles, unchanged (the GSC eases the sun to them)
    float c[3] = {1, 1, 1};
    if (std::sscanf(suncolor.c_str(), "%f %f %f", &c[0], &c[1], &c[2]) == 3)
        for (int i = 0; i < 3; ++i) h.color[i] = std::clamp(c[i], 0.0f, 1.0f);
    else
        h.color[0] = h.color[1] = h.color[2] = 1;
    double l = 1;
    if (std::sscanf(sunlight.c_str(), "%lf", &l) != 1 || !(l >= 0)) l = 1;
    h.intensity = l;
    return h;
}
}  // namespace iw8r
