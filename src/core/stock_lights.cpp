#include "stock_lights.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "json_lite.h"

namespace iw8r {

namespace {

constexpr double kPi = 3.14159265358979323846;

bool fail(std::string* err, const std::string& m) {
    if (err) *err = m;
    return false;
}

double num(const json::Value* v, double def = 0) {
    if (!v) return def;
    if (v->type == json::Value::Type::Number) return v->num;
    if (v->type == json::Value::Type::Bool) return v->b ? 1 : 0;
    if (v->type == json::Value::Type::Object) {  // {"value": n, "name": ...} enum form
        if (const json::Value* x = v->get("value")) return num(x, def);
    }
    return def;
}

// {"$union_views": true, "v": [...]} or a plain array.
void vec(const json::Value* v, float* out, int n) {
    if (!v) return;
    const json::Value* a = v->type == json::Value::Type::Object ? v->get("v") : v;
    if (!a || a->type != json::Value::Type::Array) return;
    for (int i = 0; i < n && size_t(i) < a->arr.size(); ++i) out[i] = float(num(&a->arr[size_t(i)]));
}

std::string str(const json::Value* v) {
    if (!v) return {};
    if (const std::string* s = v->string()) return *s;
    if (v->type == json::Value::Type::Object)
        if (const json::Value* s = v->get("string"))
            if (const std::string* t = s->string()) return *t;
    return {};
}

// Q3 / IW vectoangles and AnglesToAxis (x forward, y left, z up), for bulb lengths given in light space.
void axisFromDir(const double v[3], double axis[3][3]) {
    double yaw = 0, pitch = 0;
    if (v[0] == 0 && v[1] == 0) {
        pitch = v[2] > 0 ? 90 : 270;
    } else {
        yaw = std::atan2(v[1], v[0]) * 180 / kPi;
        if (yaw < 0) yaw += 360;
        pitch = std::atan2(v[2], std::sqrt(v[0] * v[0] + v[1] * v[1])) * 180 / kPi;
        if (pitch < 0) pitch += 360;
    }
    const double p = -pitch * kPi / 180, y = yaw * kPi / 180;
    const double sp = std::sin(p), cp = std::cos(p), sy = std::sin(y), cy = std::cos(y);
    const double fwd[3] = {cp * cy, cp * sy, -sp}, right[3] = {sy, -cy, 0}, up[3] = {sp * cy, sp * sy, cp};
    for (int k = 0; k < 3; ++k) axis[0][k] = fwd[k], axis[1][k] = -right[k], axis[2][k] = up[k];
}

double dot3(const double a[3], const double b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

}  // namespace

bool parseStockPrimaryLights(const std::string& comWorldJson, std::vector<StockPrimaryLight>& out, std::string* err) {
    out.clear();
    std::string jerr;
    const auto doc = json::parse(comWorldJson, &jerr);
    if (!doc) return fail(err, "ComWorld JSON: " + jerr);
    const json::Value* asset = doc->get("asset");
    const json::Value* fields = asset ? asset->get("fields") : nullptr;
    const json::Value* pl = fields ? fields->get("primaryLights") : nullptr;
    const json::Value* values = pl ? pl->get("values") : nullptr;
    if (!values || values->type != json::Value::Type::Array) return fail(err, "ComWorld export has no primaryLights values");
    out.reserve(values->arr.size());
    for (size_t i = 0; i < values->arr.size(); ++i) {
        const json::Value& v = values->arr[i];
        StockPrimaryLight l;
        l.index = int(i);
        l.type = int(num(v.get("type")));
        l.canUseShadowMap = num(v.get("canUseShadowMap")) != 0;
        l.needsDynamicShadows = num(v.get("needsDynamicShadows")) != 0;
        l.isVolumetric = num(v.get("isVolumetric")) != 0;
        l.intensity = float(num(v.get("intensity")));
        vec(v.get("colorLinearSrgb"), l.color, 3);
        vec(v.get("dir"), l.dir, 3);
        vec(v.get("up"), l.up, 3);
        vec(v.get("origin"), l.origin, 3);
        l.radius = float(num(v.get("radius")));
        l.bulbRadius = float(num(v.get("bulbRadius")));
        vec(v.get("bulbLength"), l.bulbLength, 3);
        l.cosHalfFovOuter = float(num(v.get("cosHalfFovOuter")));
        l.cosHalfFovInner = float(num(v.get("cosHalfFovInner")));
        l.cosHalfFovCollimation = float(num(v.get("cosHalfFovCollimation")));
        l.shadowNearPlaneBias = float(num(v.get("shadowNearPlaneBias")));
        l.shadowSoftness = float(num(v.get("shadowSoftness")));
        l.shadowBias = float(num(v.get("shadowBias")));
        l.distanceFalloff = float(num(v.get("distanceFalloff")));
        l.rotationLimit = float(num(v.get("rotationLimit"), 1));
        l.defName = str(v.get("defName"));
        out.push_back(std::move(l));
    }
    return true;
}

bool parseStockLightDef(const std::string& text, StockLightDef& out, std::string* err) {
    out = StockLightDef{};
    std::string jerr;
    const auto doc = json::parse(text, &jerr);
    if (!doc) return fail(err, "lightdef JSON: " + jerr);
    const json::Value* asset = doc->get("asset");
    const json::Value* f = asset ? asset->get("fields") : nullptr;
    if (!f) return fail(err, "lightdef export has no fields");
    out.name = str(f->get("name"));
    out.iesProfile = str(f->get("iesProfile"));
    out.firstPixel = int(num(f->get("firstPixel")));
    out.coordOffset = float(num(f->get("coordOffset")));
    out.coordScale = float(num(f->get("coordScale")));
    if (out.name.empty()) return fail(err, "lightdef export has no name");
    return true;
}

bool packStockLight(const StockPrimaryLight& L, const StockLightDef* def, StockGameLight& g) {
    g = StockGameLight{};
    if ((L.type != 2 && L.type != 3) || L.radius < 1.01f) return false;
    if (std::max({L.color[0], L.color[1], L.color[2]}) * L.intensity == 0) return false;
    if (L.type == 2 && L.cosHalfFovOuter == 1.0f) return false;
    const double bR = std::max(double(L.bulbRadius), 1.0), c = std::max(double(L.cosHalfFovCollimation), 0.0017453292);
    const double df = std::clamp(double(L.distanceFalloff), 0.1, 1.0);
    const double k = double(L.intensity) / ((1 - c) * (1 - c)) / (bR * bR);
    for (int i = 0; i < 3; ++i) {
        g.diffuse[i] = float(k * L.color[i]);
        g.dir[i] = L.dir[i];
        g.pos[i] = L.origin[i];
    }
    double bl[3] = {L.bulbLength[0], L.bulbLength[1], L.bulbLength[2]};
    if (bl[0] == 0 && bl[1] == 0 && bl[2] == 0) {
        bl[0] = bl[1] = bl[2] = 1.0 / 255.0;  // the game's own zero value: a sphere light
    } else if (L.rotationLimit < 1) {
        // Given in the light's frame: x forward (toward -dir), y left, z up.
        const double back[3] = {-L.dir[0], -L.dir[1], -L.dir[2]};
        double ax[3][3];
        axisFromDir(back, ax);
        double w[3];
        for (int k2 = 0; k2 < 3; ++k2) w[k2] = ax[0][k2] * bl[0] + ax[1][k2] * bl[1] + ax[2][k2] * bl[2];
        std::copy(w, w + 3, bl);
    }
    for (int i = 0; i < 3; ++i) g.bulbDir[i] = float(bl[i]);
    g.bulbRadius = float(bR);
    g.invRadius = float(1.0 / L.radius);
    g.collimationOffset = float(c * bR / std::sqrt(1 - c * c));
    g.packed = 0xFFu | (uint32_t(L.type) << 8) | (L.isVolumetric ? 1u << 30 : 0u);
    if (L.type == 2 || L.canUseShadowMap) {
        const double co = L.cosHalfFovOuter, x = 1.0 / (double(L.cosHalfFovInner) - co);
        g.spot[0] = float(x);
        g.spot[1] = float(-co * x);
        g.spot[2] = float(co * bR / std::sqrt(std::max(1 - co * co, 1e-12)));
        g.spot[3] = float(1.0 / df);
    } else {
        g.spot[0] = g.spot[1] = g.spot[2] = 0;
        g.spot[3] = float(1.0 / df);
    }
    if (def && !def->iesProfile.empty())
        g.iesLookup = uint32_t(floatToHalf(def->coordOffset)) | (uint32_t(floatToHalf(def->coordScale)) << 16);
    return true;
}

StockLightEval evalStockLightDiffuse(const StockGameLight& g, const double P[3], const double N[3],
                                     const std::function<float(float)>& iesFn) {
    StockLightEval r;
    const double pos[3] = {g.pos[0], g.pos[1], g.pos[2]}, bd[3] = {g.bulbDir[0], g.bulbDir[1], g.bulbDir[2]};
    const double bb = std::max(dot3(bd, bd), 1e-20);
    const auto segPoint = [&](const double p[3], double q[3]) {
        const double d[3] = {p[0] - pos[0], p[1] - pos[1], p[2] - pos[2]};
        const double t = std::clamp(dot3(d, bd) / bb, -1.0, 1.0);
        for (int k = 0; k < 3; ++k) q[k] = pos[k] + t * bd[k];
    };
    double Q[3];
    segPoint(P, Q);
    const double v[3] = {Q[0] - P[0], Q[1] - P[1], Q[2] - P[2]};
    const double d = std::sqrt(dot3(v, v));
    if (d < 1e-9) return r;
    const double L[3] = {v[0] / d, v[1] / d, v[2] / d};
    const bool isSpot = stockGameLightType(g) == 2;
    double Lc[3] = {L[0], L[1], L[2]};
    if (isSpot) {
        double a[3];
        for (int k = 0; k < 3; ++k) a[k] = Q[k] + g.dir[k] * g.spot[2] - P[k];
        const double la = std::sqrt(dot3(a, a));
        for (int k = 0; k < 3; ++k) Lc[k] = la > 0 ? a[k] / la : L[k];
    }
    const double dir[3] = {g.dir[0], g.dir[1], g.dir[2]};
    const double front = (!isSpot || dot3(v, dir) > 0) ? 1.0 : 0.0;
    const double cosA = dot3(dir, Lc), s2a = 1 - std::min(cosA * cosA, 1.0);
    const double u = cosA > 0 ? 0.5 * s2a : 1 - 0.5 * s2a;
    double prof = 1;
    if (iesFn) {
        const float off = halfToFloat(uint16_t(g.iesLookup & 0xFFFFu)), sc = halfToFloat(uint16_t(g.iesLookup >> 16));
        prof = iesFn(float(off + u * sc));
    }
    const double spotF = std::clamp(cosA * g.spot[0] + g.spot[1], 0.0, 1.0);
    const double w = std::clamp(g.spot[3] * (1 - d * g.invRadius), 0.0, 1.0) * (isSpot ? spotF : 1.0);
    const double att = prof * front * w * w * (3 - 2 * w);
    r.att = att, r.cosA = cosA, r.u = u;
    if (att <= 0) return r;
    // The representative point along the half vector of N and L.
    const double nl0 = dot3(N, L);
    double X[3];
    for (int k = 0; k < 3; ++k) X[k] = P[k] + (N[k] + L[k]) * (d / (1 + nl0));
    double Qd[3];
    segPoint(X, Qd);
    const double vd[3] = {Qd[0] - P[0], Qd[1] - P[1], Qd[2] - P[2]};
    const double dd = std::sqrt(dot3(vd, vd));
    if (dd < 1e-9) return r;
    const double Ld[3] = {vd[0] / dd, vd[1] / dd, vd[2] / dd};
    const double NdotL = dot3(N, Ld), s2 = double(g.bulbRadius) * g.bulbRadius / (dd * dd), y = NdotL / std::sqrt(s2);
    const double E = att * ((1 - y * y > 0) ? std::pow(std::clamp(s2 * (0.5 + 0.5 * y), 0.0, 1.0), 1.5)
                                            : std::clamp(NdotL * s2, 0.0, 1.0));
    for (int k = 0; k < 3; ++k) r.E[k] = E * g.diffuse[k];
    return r;
}

StockLightShadow stockLightShadowCamera(const StockPrimaryLight& L, const StockGameLight& g) {
    StockLightShadow s;
    if (!L.canUseShadowMap || L.radius < 1.01f) return s;
    const double co = std::clamp(double(L.cosHalfFovOuter), 0.001, 0.999), so = std::sqrt(1 - co * co);
    double dir[3] = {L.dir[0], L.dir[1], L.dir[2]};
    const double dl = std::sqrt(dot3(dir, dir));
    if (dl < 1e-6) return s;
    for (double& c : dir) c /= dl;
    // The bulb along the axis and its extent (research 106 section 4.2).
    const double bd[3] = {g.bulbDir[0], g.bulbDir[1], g.bulbDir[2]};
    const double bl = std::sqrt(dot3(bd, bd)), bR = g.bulbRadius;
    const double along = std::fabs(dot3(bd, dir));
    double ext[3] = {bd[0], bd[1], bd[2]};
    if (bl > 1e-9)
        for (int k = 0; k < 3; ++k) ext[k] += bd[k] / bl * bR;
    const double extLen = bl > 1e-9 ? std::sqrt(dot3(ext, ext)) : bR;
    const double offset = std::max(extLen - along, 0.0) * co / so + along;
    for (int k = 0; k < 3; ++k) {
        s.eye[k] = float(L.origin[k] + dir[k] * offset);
        s.forward[k] = float(-dir[k]);
    }
    // Roll from up, made perpendicular to the view direction (any perpendicular when up is along it).
    double up[3] = {L.up[0], L.up[1], L.up[2]};
    const double f[3] = {-dir[0], -dir[1], -dir[2]};
    double ud = dot3(up, f);
    for (int k = 0; k < 3; ++k) up[k] -= f[k] * ud;
    if (dot3(up, up) < 1e-8) {
        const double a[3] = {std::fabs(f[2]) < 0.9 ? 0.0 : 1.0, 0.0, std::fabs(f[2]) < 0.9 ? 1.0 : 0.0};
        ud = dot3(a, f);
        for (int k = 0; k < 3; ++k) up[k] = a[k] - f[k] * ud;
    }
    const double ul = std::sqrt(dot3(up, up));
    for (int k = 0; k < 3; ++k) s.up[k] = float(up[k] / ul);
    s.fov = float(2 * std::acos(co));
    s.zNear = float(std::max(offset + along + double(L.shadowNearPlaneBias), 0.5));
    s.zFar = float(std::max(offset + double(L.radius), double(s.zNear) + 1));
    const double soft = std::clamp(double(L.shadowSoftness), 0.0, 1.0);
    s.k = float((1 - soft) * 88 + soft);
    s.bias = std::clamp(L.shadowBias, 0.0f, 0.999f);
    s.cast = true;
    return s;
}

void stockLightShadowMatrix(const StockLightShadow& s, float out[16]) {
    const double f[3] = {s.forward[0], s.forward[1], s.forward[2]}, u[3] = {s.up[0], s.up[1], s.up[2]};
    // Left-handed view: x right, y up, z forward (right = up x forward).
    const double r[3] = {u[1] * f[2] - u[2] * f[1], u[2] * f[0] - u[0] * f[2], u[0] * f[1] - u[1] * f[0]};
    const double e[3] = {s.eye[0], s.eye[1], s.eye[2]};
    const double t = std::tan(0.5 * double(s.fov)), sc = 1.0 / std::max(t, 1e-6);
    const double n = s.zNear, fa = s.zFar, a = fa / (fa - n), b = -n * fa / (fa - n);
    // v * View: (dot(v - e, r), dot(v - e, u), dot(v - e, f), 1); then * Proj: (x sc, y sc, z a + b, z).
    const double V[4][4] = {{r[0], u[0], f[0], 0}, {r[1], u[1], f[1], 0}, {r[2], u[2], f[2], 0},
                            {-dot3(r, e), -dot3(u, e), -dot3(f, e), 1}};
    for (int i = 0; i < 4; ++i) {
        out[i * 4 + 0] = float(V[i][0] * sc);
        out[i * 4 + 1] = float(V[i][1] * sc);
        out[i * 4 + 2] = float(V[i][2] * a + V[i][3] * b);
        out[i * 4 + 3] = float(V[i][2]);
    }
}

}  // namespace iw8r
