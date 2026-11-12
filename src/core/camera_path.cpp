#include "camera_path.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>

#include "json_lite.h"

namespace iw8r {
namespace {

// Centripetal knots advance by |P(i+1) - P(i)|^0.5. Coincident points (duplicated end points, stacked keys) get this floor
// so the Barry-Goldman weights never divide by zero.
constexpr double kMinKnotStep = 1e-3;

bool isSorted(const std::vector<CameraKey>& keys) {
    for (size_t i = 1; i < keys.size(); ++i)
        if (keys[i].time < keys[i - 1].time) return false;
    return true;
}

// Returns `path` itself when its keys are already time ordered, otherwise a sorted copy held in `scratch`.
// Keeps the common case copy-free.
const CameraPath& ordered(const CameraPath& path, CameraPath& scratch) {
    if (isSorted(path.keys)) return path;
    scratch = path;
    std::stable_sort(scratch.keys.begin(), scratch.keys.end(),
                     [](const CameraKey& a, const CameraKey& b) { return a.time < b.time; });
    return scratch;
}

// Shortest signed angle difference in (-180, 180].
double wrapDelta(double d) {
    d = std::fmod(d, 360.0);
    if (d > 180.0) d -= 360.0;
    if (d <= -180.0) d += 360.0;
    return d;
}

// Angle in [0, 360).
double wrapYaw(double y) {
    double r = std::fmod(y, 360.0);
    if (r < 0) r += 360.0;
    return r;
}

double knotStep(const Vec3& a, const Vec3& b) { return std::max(std::sqrt(length(b - a)), kMinKnotStep); }

// Barry-Goldman evaluation of the centripetal Catmull-Rom piece p1..p2 at knot value u in [t1, t2].
// Works for Vec3 and for scalar yaw/pitch. The curve interpolates p1 at t1 and p2 at t2.
template <class T>
T barryGoldman(const T& p0, const T& p1, const T& p2, const T& p3, double t0, double t1, double t2, double t3, double u) {
    const T a1 = p0 * ((t1 - u) / (t1 - t0)) + p1 * ((u - t0) / (t1 - t0));
    const T a2 = p1 * ((t2 - u) / (t2 - t1)) + p2 * ((u - t1) / (t2 - t1));
    const T a3 = p2 * ((t3 - u) / (t3 - t2)) + p3 * ((u - t2) / (t3 - t2));
    const T b1 = a1 * ((t2 - u) / (t2 - t0)) + a2 * ((u - t0) / (t2 - t0));
    const T b2 = a2 * ((t3 - u) / (t3 - t1)) + a3 * ((u - t1) / (t3 - t1));
    return b1 * ((t2 - u) / (t2 - t1)) + b2 * ((u - t1) / (t2 - t1));
}

// Closing leg of a loop (last key -> first key). Its duration is chosen so it runs at the same average speed as the open
// part of the path. This needs no stored field, and a retimed path keeps constant speed around the loop as well.
double loopClosingTime(const std::vector<CameraKey>& k) {
    const size_t n = k.size();
    double openDist = 0;
    for (size_t i = 1; i < n; ++i) openDist += length(k[i].pos - k[i - 1].pos);
    const double openTime = k[n - 1].time - k[0].time;
    const double closeDist = length(k[0].pos - k[n - 1].pos);
    if (openDist <= 0 || openTime <= 0) return 0;  // stationary or instantaneous: no time to spend on the closing leg
    return closeDist * openTime / openDist;
}

// Duration of a time-ordered path.
double durationOrdered(const CameraPath& p) {
    const auto& k = p.keys;
    if (k.size() < 2) return 0;
    const double open = k.back().time - k.front().time;
    return p.loop ? open + loopClosingTime(k) : open;
}

// Sampling of a time-ordered path.
CameraKey sampleOrdered(const CameraPath& p, double t) {
    const auto& k = p.keys;
    const size_t n = k.size();
    if (n == 0) return {};
    if (n == 1) return k[0];

    const double t0 = k.front().time;
    const double tLast = k.back().time;
    const double dur = durationOrdered(p);

    // Loops wrap t into [0, dur); open paths clamp it.
    double rel;
    if (p.loop) {
        rel = dur > 0 ? std::fmod(t, dur) : 0.0;
        if (rel < 0) rel += dur;
    } else {
        rel = std::clamp(t, 0.0, dur);
    }
    const double ta = t0 + rel;

    if (!p.loop && ta >= tLast) {
        CameraKey out = k.back();
        out.time = ta;
        return out;
    }

    // Segment j runs from key j to its successor. Index n-1 is the loop closing leg. For equal times, the last key with
    // that time wins, so a duplicate time means the later key is the one shown.
    size_t j;
    if (ta >= tLast) {
        j = n - 1;
    } else {
        auto it = std::upper_bound(k.begin(), k.end(), ta, [](double v, const CameraKey& key) { return v < key.time; });
        j = it == k.begin() ? 0 : size_t(it - k.begin()) - 1;
    }
    const size_t nxt = (j + 1 < n) ? j + 1 : 0;
    const double ts = k[j].time;
    const double te = (j + 1 < n) ? k[j + 1].time : t0 + dur;
    const double dt = te - ts;
    const double s = dt > 0 ? std::clamp((ta - ts) / dt, 0.0, 1.0) : 0.0;

    // Control keys: wrap for loops, repeat the end key for open paths (so the spline ends are not extrapolated).
    auto idx = [&](long long i) -> size_t {
        if (p.loop) return size_t(((i % static_cast<long long>(n)) + static_cast<long long>(n)) % static_cast<long long>(n));
        return size_t(std::clamp<long long>(i, 0, static_cast<long long>(n) - 1));
    };
    const CameraKey& a = k[idx(static_cast<long long>(j) - 1)];
    const CameraKey& b = k[j];
    const CameraKey& c = k[nxt];
    const CameraKey& d = k[idx(static_cast<long long>(nxt) + 1)];

    CameraKey out;
    out.time = ta;
    if (s <= 0) {  // exact pass-through at keys (also covers duplicate times)
        out.pos = b.pos;
        out.yaw = wrapYaw(b.yaw);
        out.pitch = b.pitch;
        return out;
    }
    if (s >= 1) {
        out.pos = c.pos;
        out.yaw = wrapYaw(c.yaw);
        out.pitch = c.pitch;
        return out;
    }

    const double t1 = knotStep(a.pos, b.pos);
    const double t2 = t1 + knotStep(b.pos, c.pos);
    const double t3 = t2 + knotStep(c.pos, d.pos);
    const double u = t1 + s * (t2 - t1);  // time fraction mapped linearly onto the knot span of this piece

    out.pos = barryGoldman(a.pos, b.pos, c.pos, d.pos, 0.0, t1, t2, t3, u);
    out.pitch = barryGoldman(a.pitch, b.pitch, c.pitch, d.pitch, 0.0, t1, t2, t3, u);

    // Yaw: unwrap the neighbours relative to b so the spline never crosses the 0/360 seam the long way round.
    const double U1 = b.yaw;
    const double U0 = U1 + wrapDelta(a.yaw - b.yaw);
    const double U2 = U1 + wrapDelta(c.yaw - b.yaw);
    const double U3 = U2 + wrapDelta(d.yaw - c.yaw);
    double yaw = barryGoldman(U0, U1, U2, U3, 0.0, t1, t2, t3, u);
    // Clamp to this leg's shortest arc, which removes any spline overshoot past it.
    yaw = std::clamp(yaw, std::min(U1, U2), std::max(U1, U2));
    out.yaw = wrapYaw(yaw);
    return out;
}

void appendNumber(std::string& out, double v) {
    if (!std::isfinite(v)) v = 0;  // JSON has no NaN or infinity
    char buf[64];
    const auto res = std::to_chars(buf, buf + sizeof buf, v);  // shortest form that round-trips exactly
    out.append(buf, res.ptr);
}

void appendString(std::string& out, const std::string& s) {
    out += '"';
    for (const unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof b, "\\u%04x", unsigned(c));
                    out += b;
                } else {
                    out += char(c);  // UTF-8 bytes pass through unchanged
                }
        }
    }
    out += '"';
}

// Reads an optional finite number member. A missing member keeps the default; a present but bad one is an error.
bool optNumber(const json::Value& obj, const char* name, double& out, std::string& why) {
    const json::Value* v = obj.get(name);
    if (!v) return true;
    const auto n = v->number();
    if (!n || !std::isfinite(*n)) {
        why = std::string(name) + " must be a finite number";
        return false;
    }
    out = *n;
    return true;
}

bool parseKey(const json::Value& v, CameraKey& key, std::string& why) {
    if (v.type != json::Value::Type::Object) {
        why = "key is not an object";
        return false;
    }
    const json::Value* pos = v.get("pos");
    if (!pos || pos->type != json::Value::Type::Array || pos->arr.size() != 3) {
        why = "pos must be [x, y, z]";
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        const auto n = pos->arr[size_t(i)].number();
        if (!n || !std::isfinite(*n)) {
            why = "pos components must be finite numbers";
            return false;
        }
        key.pos[i] = *n;
    }
    return optNumber(v, "yaw", key.yaw, why) && optNumber(v, "pitch", key.pitch, why) && optNumber(v, "time", key.time, why);
}

bool parsePath(const json::Value& v, CameraPath& path, std::string& why) {
    if (v.type != json::Value::Type::Object) {
        why = "path is not an object";
        return false;
    }
    if (const json::Value* nv = v.get("name")) {
        const std::string* s = nv->string();
        if (!s) {
            why = "name must be a string";
            return false;
        }
        path.name = *s;
    }
    if (const json::Value* lv = v.get("loop")) {
        if (lv->type != json::Value::Type::Bool) {
            why = "loop must be a boolean";
            return false;
        }
        path.loop = lv->b;
    }
    const json::Value* keys = v.get("keys");
    if (!keys || keys->type != json::Value::Type::Array) {
        why = "keys must be an array";
        return false;
    }
    path.keys.clear();
    for (size_t i = 0; i < keys->arr.size(); ++i) {
        CameraKey key;
        std::string kwhy;
        if (!parseKey(keys->arr[i], key, kwhy)) {
            why = "keys[" + std::to_string(i) + "]: " + kwhy;
            return false;
        }
        path.keys.push_back(key);
    }
    return true;
}

}  // namespace

double cameraPathDuration(const CameraPath& path) {
    CameraPath scratch;
    return durationOrdered(ordered(path, scratch));
}

CameraKey sampleCameraPath(const CameraPath& path, double t) {
    CameraPath scratch;
    return sampleOrdered(ordered(path, scratch), t);
}

void retimeByDistance(CameraPath& path, double unitsPerSecond) {
    auto& k = path.keys;
    if (k.size() < 2 || !(unitsPerSecond > 0)) return;
    // Times follow the cumulative polyline length. A loop's closing leg then runs at the same speed, because
    // loopClosingTime() derives its duration from the open part's average speed.
    const double t0 = k.front().time;
    double dist = 0;
    for (size_t i = 1; i < k.size(); ++i) {
        dist += length(k[i].pos - k[i - 1].pos);
        k[i].time = t0 + dist / unitsPerSecond;
    }
}

std::vector<CameraKey> cameraPathFrames(const CameraPath& path, double fps) {
    std::vector<CameraKey> frames;
    if (!(fps > 0)) return frames;
    CameraPath scratch;
    const CameraPath& p = ordered(path, scratch);
    // The epsilon keeps a duration * fps that is 2.9999999999 by float error from losing its last frame.
    const long long last = static_cast<long long>(std::floor(durationOrdered(p) * fps + 1e-9));
    frames.reserve(size_t(last + 1));
    for (long long i = 0; i <= last; ++i) frames.push_back(sampleOrdered(p, double(i) / fps));
    return frames;
}

std::string cameraPathsToJson(const std::vector<CameraPath>& paths) {
    std::string out = "{\n  \"format\": \"iw8r-campaths\",\n  \"version\": 1,\n  \"paths\": [";
    for (size_t pi = 0; pi < paths.size(); ++pi) {
        const CameraPath& p = paths[pi];
        out += pi ? ",\n    {" : "\n    {";
        out += "\"name\": ";
        appendString(out, p.name);
        out += p.loop ? ", \"loop\": true" : ", \"loop\": false";
        out += ", \"keys\": [";
        for (size_t ki = 0; ki < p.keys.size(); ++ki) {
            const CameraKey& k = p.keys[ki];
            out += ki ? ", {\"pos\": [" : "{\"pos\": [";
            appendNumber(out, k.pos.x);
            out += ", ";
            appendNumber(out, k.pos.y);
            out += ", ";
            appendNumber(out, k.pos.z);
            out += "], \"yaw\": ";
            appendNumber(out, k.yaw);
            out += ", \"pitch\": ";
            appendNumber(out, k.pitch);
            out += ", \"time\": ";
            appendNumber(out, k.time);
            out += "}";
        }
        out += "]}";
    }
    out += paths.empty() ? "]\n}\n" : "\n  ]\n}\n";
    return out;
}

bool cameraPathsFromJson(const std::string& text, std::vector<CameraPath>& out, std::string* err) {
    auto fail = [&](const std::string& msg) {
        if (err) *err = msg;
        return false;
    };
    std::string perr;
    const auto root = json::parse(text, &perr);
    if (!root) return fail("invalid JSON: " + perr);
    const json::Value* list = root->type == json::Value::Type::Array ? &*root : root->get("paths");
    if (!list || list->type != json::Value::Type::Array) return fail("expected a \"paths\" array");

    // Parse into a temporary so `out` is untouched when any entry is bad.
    std::vector<CameraPath> parsed;
    parsed.reserve(list->arr.size());
    for (size_t i = 0; i < list->arr.size(); ++i) {
        CameraPath path;
        std::string why;
        if (!parsePath(list->arr[i], path, why)) return fail("paths[" + std::to_string(i) + "]: " + why);
        parsed.push_back(std::move(path));
    }
    out = std::move(parsed);
    return true;
}

}  // namespace iw8r
