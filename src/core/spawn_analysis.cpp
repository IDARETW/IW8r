// IW8 Radiant Suite - spawn analysis (see spawn_analysis.h).
#include "spawn_analysis.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <optional>

namespace iw8r {
namespace {

bool startsWith(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }
bool endsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}

// Same family as gameplay_checks: info_player_start, or mp_<mode>_spawn*. The mode is the text between "mp_" and "_spawn".
bool spawnMode(const std::string& cls, std::string& mode) {
    if (cls == "info_player_start") {
        mode.clear();
        return true;
    }
    if (!startsWith(cls, "mp_")) return false;
    const size_t at = cls.find("_spawn", 3);
    if (at == std::string::npos) return false;
    mode = cls.substr(3, at - 3);
    return true;
}

int teamOf(const std::string& cls) {
    if (cls.find("allies") != std::string::npos) return 1;
    if (cls.find("axis") != std::string::npos) return 2;
    return 0;
}

double yawOf(const Entity& e) {
    if (const std::string* a = e.value("angles")) {
        const std::vector<std::string> tok = splitTokens(*a);
        return tok.size() >= 2 ? std::strtod(tok[1].c_str(), nullptr) : 0.0;
    }
    if (const std::string* a = e.value("angle")) return std::strtod(a->c_str(), nullptr);
    return 0.0;
}

// Enemy: start spawns on opposite teams (both nonzero). Same mode is required by the callers.
bool isEnemy(const SpawnPoint& a, const SpawnPoint& b) {
    return a.start && b.start && a.team != 0 && b.team != 0 && a.team != b.team;
}

std::string num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.0f", v);
    return buf;
}

std::string label(const SpawnPoint& s) { return s.classname + " (entity " + std::to_string(s.entity) + ")"; }

}  // namespace

std::vector<SpawnPoint> collectSpawns(const MapFile& map) {
    std::vector<SpawnPoint> out;
    for (const Entity& e : map.entities) {
        const std::string cls = e.classname();
        std::string mode;
        if (!spawnMode(cls, mode)) continue;
        const std::optional<Vec3> o = e.origin();
        if (!o) continue;
        SpawnPoint s;
        s.entity = e.id;
        s.classname = cls;
        s.mode = mode;
        s.team = teamOf(cls);
        s.start = endsWith(cls, "_start");
        s.eye = *o + Vec3{0, 0, kSpawnEyeHeight};
        s.yaw = yawOf(e);
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<SpawnExposure> analyseSpawns(const std::vector<SpawnPoint>& spawns, const CollisionWorld& world,
                                         double maxDistance) {
    constexpr double kNone = std::numeric_limits<double>::infinity();
    std::vector<SpawnExposure> out(spawns.size());
    for (size_t i = 0; i < spawns.size(); ++i) {
        out[i].entity = spawns[i].entity;
        out[i].nearestEnemy = kNone;
        out[i].nearestFriendly = kNone;
    }

    // Each unordered pair is traced once; the result is symmetric.
    for (size_t j = 0; j < spawns.size(); ++j)
        for (size_t i = 0; i < j; ++i) {
            const SpawnPoint& a = spawns[i];
            const SpawnPoint& b = spawns[j];
            if (a.mode != b.mode) continue;
            const bool enemy = isEnemy(a, b);
            const double d = length(b.eye - a.eye);
            if (enemy) {
                out[i].nearestEnemy = std::min(out[i].nearestEnemy, d);
                out[j].nearestEnemy = std::min(out[j].nearestEnemy, d);
            } else {
                out[i].nearestFriendly = std::min(out[i].nearestFriendly, d);
                out[j].nearestFriendly = std::min(out[j].nearestFriendly, d);
            }
            if (d > maxDistance) continue;
            const CollisionWorld::Trace tr = world.traceRay(a.eye, b.eye);
            if (tr.fraction < 1.0 || tr.startSolid) continue;
            ++out[i].visibleAny;
            ++out[j].visibleAny;
            if (enemy) {
                ++out[i].visibleEnemies;
                ++out[j].visibleEnemies;
            }
        }
    return out;
}

std::vector<std::string> spawnWarnings(const std::vector<SpawnPoint>& spawns, const std::vector<SpawnExposure>& exposures) {
    std::vector<std::string> out;
    const size_t n = std::min(spawns.size(), exposures.size());
    for (size_t i = 0; i < n; ++i) {
        const SpawnPoint& s = spawns[i];
        const SpawnExposure& x = exposures[i];
        if (s.start && s.team != 0 && x.visibleEnemies >= kEnemySightWarnCount) {
            const char* enemyTeam = s.team == 1 ? "axis" : "allies";
            out.push_back(label(s) + " sees " + std::to_string(x.visibleEnemies) + " " + enemyTeam + " start spawns");
        }
        const double nearest = std::min(x.nearestEnemy, x.nearestFriendly);
        if (nearest < kMinSpawnSpacing)
            out.push_back(label(s) + " is " + num(nearest) + " units from another spawn (spawn trap risk)");
    }

    // Per mode: average distance over all allies x axis start pairs.
    std::map<std::string, std::pair<std::vector<const SpawnPoint*>, std::vector<const SpawnPoint*>>> byMode;
    for (const SpawnPoint& s : spawns) {
        if (!s.start) continue;
        if (s.team == 1) byMode[s.mode].first.push_back(&s);
        if (s.team == 2) byMode[s.mode].second.push_back(&s);
    }
    for (const auto& [mode, teams] : byMode) {
        if (teams.first.empty() || teams.second.empty()) continue;
        double sum = 0;
        for (const SpawnPoint* a : teams.first)
            for (const SpawnPoint* b : teams.second) sum += length(a->eye - b->eye);
        const double mean = sum / static_cast<double>(teams.first.size() * teams.second.size());
        if (mean < kMinTeamStartSpacing)
            out.push_back("mode " + mode + ": allies starts are on average " + num(mean) +
                          " units from axis starts (guideline " + num(kMinTeamStartSpacing) + " or more)");
    }
    return out;
}

std::vector<double> spawnDensity(const std::vector<SpawnPoint>& spawns, const AABB& bounds, int cellsX, int cellsY,
                                 double radius) {
    if (cellsX < 1 || cellsY < 1) return {};
    std::vector<double> grid(static_cast<size_t>(cellsX) * static_cast<size_t>(cellsY), 0.0);
    if (!bounds.valid() || !(radius > 0)) return grid;
    const double cw = (bounds.maxs.x - bounds.mins.x) / cellsX;
    const double ch = (bounds.maxs.y - bounds.mins.y) / cellsY;
    for (int cy = 0; cy < cellsY; ++cy)
        for (int cx = 0; cx < cellsX; ++cx) {
            const double x = bounds.mins.x + (cx + 0.5) * cw;
            const double y = bounds.mins.y + (cy + 0.5) * ch;
            double sum = 0;
            for (const SpawnPoint& s : spawns) {
                const double dx = s.eye.x - x;
                const double dy = s.eye.y - y;
                sum += std::max(0.0, 1.0 - std::sqrt(dx * dx + dy * dy) / radius);
            }
            grid[static_cast<size_t>(cy) * static_cast<size_t>(cellsX) + static_cast<size_t>(cx)] = sum;
        }
    return grid;
}

}  // namespace iw8r
