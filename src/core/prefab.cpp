#include "prefab.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
#include <system_error>

#include "brush_ops.h"

namespace iw8r {

namespace fs = std::filesystem;

namespace {

constexpr double kPi = 3.14159265358979323846;

bool isWs(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; }

// Whitespace tokens with their separators, so a line can be rewritten token by token (same split as the
// primitive parser in map_text.cpp, so token indices from Primitive::verts apply here).
struct Toks {
    std::vector<std::string> ws;  // ws[i] precedes tok[i]; ws.back() is trailing whitespace
    std::vector<std::string> tok;
    std::string join() const {
        std::string s;
        for (size_t i = 0; i < tok.size(); ++i) s += ws[i] + tok[i];
        s += ws.back();
        return s;
    }
};

Toks tokenizeLine(const std::string& t) {
    Toks r;
    size_t i = 0;
    while (true) {
        size_t a = i;
        while (i < t.size() && isWs(t[i])) ++i;
        r.ws.push_back(t.substr(a, i - a));
        if (i >= t.size()) break;
        a = i;
        while (i < t.size() && !isWs(t[i])) ++i;
        r.tok.push_back(t.substr(a, i - a));
    }
    return r;
}

bool parseDouble(const std::string& s, double& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    out = std::strtod(s.c_str(), &end);
    return end == s.c_str() + s.size();
}

std::optional<Vec3> parseVec3(const std::string& s) {
    const auto tk = splitTokens(s);
    if (tk.size() < 3) return std::nullopt;
    Vec3 v;
    for (int a = 0; a < 3; ++a)
        if (!parseDouble(tk[size_t(a)], v[a])) return std::nullopt;
    return v;
}

std::string vecText(const Vec3& v) { return formatNumber(v.x) + " " + formatNumber(v.y) + " " + formatNumber(v.z); }

// sin/cos in degrees; exact for multiples of 90 so axis-aligned turns carry no round-off.
void sinCosDeg(double deg, double& s, double& c) {
    const double q = deg / 90.0;
    if (std::fabs(q - std::round(q)) < 1e-12) {
        const long long k = ((std::llround(q) % 4) + 4) % 4;
        static const double cs[4] = {1, 0, -1, 0};
        static const double sn[4] = {0, 1, 0, -1};
        c = cs[k];
        s = sn[k];
        return;
    }
    const double r = deg * kPi / 180.0;
    s = std::sin(r);
    c = std::cos(r);
}

Mat3 fromRows(double a, double b, double c, double d, double e, double f, double g, double h, double i) {
    Mat3 r;
    r.m[0][0] = a; r.m[0][1] = b; r.m[0][2] = c;
    r.m[1][0] = d; r.m[1][1] = e; r.m[1][2] = f;
    r.m[2][0] = g; r.m[2][1] = h; r.m[2][2] = i;
    return r;
}

Mat3 rotX(double deg) {
    double s, c;
    sinCosDeg(deg, s, c);
    return fromRows(1, 0, 0, 0, c, -s, 0, s, c);
}
Mat3 rotY(double deg) {
    double s, c;
    sinCosDeg(deg, s, c);
    return fromRows(c, 0, s, 0, 1, 0, -s, 0, c);
}
Mat3 rotZ(double deg) {
    double s, c;
    sinCosDeg(deg, s, c);
    return fromRows(c, -s, 0, s, c, 0, 0, 0, 1);
}

// Euler angles (pitch, yaw, roll in degrees) of a rotation built by anglesToMatrix().
Vec3 matrixToAngles(const Mat3& M) {
    const double sp = std::clamp(-M.m[2][0], -1.0, 1.0);
    const double pitch = std::asin(sp);
    double yaw, roll;
    if (std::fabs(sp) > 1 - 1e-12) {  // gimbal lock: roll is folded into yaw
        roll = 0;
        yaw = std::atan2(-M.m[0][1], M.m[1][1]);
    } else {
        roll = std::atan2(M.m[2][1], M.m[2][2]);
        yaw = std::atan2(M.m[1][0], M.m[0][0]);
    }
    auto snap = [](double deg) {
        const double r = std::round(deg);
        return (std::fabs(deg - r) < 1e-9 ? r : deg) + 0.0;  // + 0.0 turns -0 into 0
    };
    return {snap(pitch * 180.0 / kPi), snap(yaw * 180.0 / kPi), snap(roll * 180.0 / kPi)};
}

bool isZeroVec(const Vec3& a) { return a.x == 0 && a.y == 0 && a.z == 0; }

std::string lowerStr(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

Vec3 cleanZero(const Vec3& v) {
    auto f = [](double x) { return std::fabs(x) < 1e-12 ? 0.0 : x + 0.0; };
    return {f(v.x), f(v.y), f(v.z)};
}

std::string joinTokens(const std::vector<std::string>& tk) {
    std::string out;
    for (size_t i = 0; i < tk.size(); ++i) {
        if (i) out += ' ';
        out += tk[i];
    }
    return out;
}

// Placement of one instance: point -> offset + R * (scale * point).
struct Placement {
    double scale = 1;
    Mat3 rot;
    Vec3 offset;
    Vec3 angles;  // instance pitch yaw roll, for the brush_ops rotations

    Vec3 point(const Vec3& p) const { return cleanZero(offset + rot * (p * scale)); }
    Vec3 vec(const Vec3& v) const { return cleanZero(rot * (v * scale)); }
};

Placement placementOf(const PrefabInstance& inst) {
    Placement pl;
    pl.scale = inst.scale > 0 ? inst.scale : 1;
    pl.rot = anglesToMatrix(inst.angles);
    pl.offset = inst.origin;
    pl.angles = inst.angles;
    return pl;
}

// Brush: planes go through brush_ops (scale, then roll, pitch, yaw about the prefab origin, then offset).
// The new plane points come from makeBrushPrimitive; they replace only the face lines, so layer,
// contents and comment lines and each face's suffix stay as written.
Primitive transformBrush(const Primitive& p, const Placement& pl, const std::string& eol) {
    ConvexBrush b = brushFromPrimitive(p);
    if (!isValidBrush(b)) return p;  // cannot rebuild the planes: carried over unchanged
    const Vec3 zero{};
    if (pl.scale != 1) b = scaleBrush(b, {pl.scale, pl.scale, pl.scale}, zero);
    b = rotateBrush(b, 0, pl.angles.z, zero);  // roll about X
    b = rotateBrush(b, 1, pl.angles.x, zero);  // pitch about Y
    b = rotateBrush(b, 2, pl.angles.y, zero);  // yaw about Z
    b = translateBrush(b, pl.offset);
    const auto gen = makeBrushPrimitive(b, eol);
    if (!gen) return p;
    if (gen->faces.size() != p.faces.size()) {
        Primitive out = *gen;
        out.id = p.id;
        return out;
    }
    Primitive out = p;
    for (size_t k = 0; k < p.faces.size(); ++k) {
        const std::string& g = gen->lines[gen->faces[k].line].text;
        const size_t first = g.find('(');
        out.lines[p.faces[k].line].text = p.faces[k].indent + g.substr(first);
    }
    out.dirty = true;
    reparsePrimitive(out);
    return out;
}

// Patch: each control point is transformed; only the three numbers of each "v" token are rewritten.
Primitive transformPatch(const Primitive& p, const Placement& pl) {
    Primitive out = p;
    std::map<size_t, Toks> touched;
    for (const PatchVertex& v : p.verts) {
        const Vec3 np = pl.point(v.pos);
        auto it = touched.find(v.line);
        if (it == touched.end()) it = touched.emplace(v.line, tokenizeLine(out.lines[v.line].text)).first;
        it->second.tok[v.token + 1] = formatNumber(np.x);
        it->second.tok[v.token + 2] = formatNumber(np.y);
        it->second.tok[v.token + 3] = formatNumber(np.z);
    }
    for (auto& [li, tk] : touched) out.lines[li].text = tk.join();
    out.dirty = true;
    reparsePrimitive(out);
    return out;
}

Primitive transformPrimitive(const Primitive& p, const Placement& pl, const std::string& eol) {
    if (p.kind == PrimKind::Brush) return transformBrush(p, pl, eol);
    if (p.kind == PrimKind::Patch) return transformPatch(p, pl);
    return p;
}

// Key/value list of a prefab entity with origin, angles and spatial behaviour keys transformed.
std::vector<std::pair<std::string, std::string>> transformKeys(const Entity& e, const PrefabInstance& inst,
                                                               const Placement& pl, bool hasPrims) {
    std::vector<std::pair<std::string, std::string>> kvs;
    bool haveOrigin = false, haveAngles = false;
    const Mat3 instRot = anglesToMatrix(inst.angles);
    const std::string cls = lowerStr(e.classname());
    for (const auto& it : e.items) {
        const KeyValueLine* kv = it.kv();
        if (!kv) continue;
        const std::string name = kv->name();
        std::string value = kv->value;
        if (name == "origin") {
            haveOrigin = true;
            if (auto o = parseVec3(value)) value = vecText(pl.point(*o));
        } else if (name == "angles") {
            haveAngles = true;
            if (auto a = parseVec3(value)) value = vecText(matrixToAngles(instRot * anglesToMatrix(*a)));
        } else if (name == "iw8r_hinge") {
            if (auto p = parseVec3(value)) value = vecText(pl.point(*p));
        } else if (name == "iw8r_door") {
            auto tk = splitTokens(value);
            if (tk.size() >= 4 && lowerStr(tk[0]) == "slide") {
                Vec3 d;
                if (parseDouble(tk[1], d.x) && parseDouble(tk[2], d.y) && parseDouble(tk[3], d.z)) {
                    const Vec3 nd = pl.vec(d);
                    tk[1] = formatNumber(nd.x);
                    tk[2] = formatNumber(nd.y);
                    tk[3] = formatNumber(nd.z);
                    value = joinTokens(tk);
                }
            }
        } else if (name == "iw8r_jump") {
            auto tk = splitTokens(value);
            if (tk.size() >= 3) {
                Vec3 v;
                if (parseDouble(tk[0], v.x) && parseDouble(tk[1], v.y) && parseDouble(tk[2], v.z)) {
                    const Vec3 nv = pl.vec(v);
                    tk[0] = formatNumber(nv.x);
                    tk[1] = formatNumber(nv.y);
                    tk[2] = formatNumber(nv.z);
                    value = joinTokens(tk);
                }
            }
        } else if (cls == "trigger_radius" && (name == "radius" || name == "height") && pl.scale != 1.0) {
            auto tk = splitTokens(value);
            double d = 0;
            if (!tk.empty() && parseDouble(tk[0], d)) {
                tk[0] = formatNumber(d * pl.scale);
                value = joinTokens(tk);
            }
        } else if (cls == "fx_origin" && name == "iw8r_damage" && pl.scale != 1.0) {
            // fx_origin iw8r_damage "<amount> [radius] [interval seconds]"
            auto tk = splitTokens(value);
            double r = 0;
            if (tk.size() >= 2 && parseDouble(tk[1], r)) {
                tk[1] = formatNumber(r * pl.scale);
                value = joinTokens(tk);
            }
        } else if ((name == "iw8r_lift" || name == "iw8r_bob") && pl.scale != 1.0) {
            // iw8r_lift "<dz> [time] [wait]", iw8r_bob "<height> [seconds]"
            auto tk = splitTokens(value);
            double d = 0;
            if (!tk.empty() && parseDouble(tk[0], d)) {
                tk[0] = formatNumber(d * pl.scale);
                value = joinTokens(tk);
            }
        }
        kvs.push_back({name, value});
    }
    if (!hasPrims) {  // point entities: a missing origin means 0 0 0, so it lands on the instance origin
        if (!haveOrigin) kvs.push_back({"origin", vecText(pl.point(Vec3{}))});
        if (!haveAngles && !isZeroVec(inst.angles)) kvs.push_back({"angles", vecText(matrixToAngles(instRot))});
    }
    return kvs;
}

EntityItem primItem(Primitive p, Id id) {
    EntityItem it;
    it.id = id;
    p.id = id;
    it.v = std::move(p);
    return it;
}

EntityItem kvItem(const std::string& key, const std::string& value, const std::string& eol, Id id) {
    EntityItem it;
    it.id = id;
    it.v = makeKeyValue(key, value, eol);
    return it;
}

}  // namespace

// ------------------------------------------------------------------ Mat3

Vec3 Mat3::operator*(const Vec3& v) const {
    return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
            m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
            m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
}

Mat3 Mat3::operator*(const Mat3& o) const {
    Mat3 r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            double s = 0;
            for (int k = 0; k < 3; ++k) s += m[i][k] * o.m[k][j];
            r.m[i][j] = s;
        }
    return r;
}

// R = Rz(yaw) * Ry(pitch) * Rx(roll): the Quake/CoD AngleVectors convention.
Mat3 anglesToMatrix(const Vec3& pitchYawRoll) {
    return rotZ(pitchYawRoll.y) * rotY(pitchYawRoll.x) * rotX(pitchYawRoll.z);
}

// ------------------------------------------------------------------ instances

std::optional<PrefabInstance> prefabInstance(const Entity& e) {
    if (e.classname() != "misc_prefab") return std::nullopt;
    const std::string* model = e.value("model");
    if (!model || model->empty()) return std::nullopt;
    PrefabInstance inst;
    inst.model = *model;
    inst.origin = e.origin().value_or(Vec3{});
    if (const std::string* a = e.value("angles"))
        if (auto v = parseVec3(*a)) inst.angles = *v;
    if (const std::string* s = e.value("modelscale")) {
        const auto tk = splitTokens(*s);
        double d = 0;
        if (!tk.empty() && parseDouble(tk[0], d) && d > 0) inst.scale = d;
    }
    for (const auto& it : e.items) {
        const KeyValueLine* kv = it.kv();
        if (!kv) continue;
        const std::string name = kv->name();
        if (name.rfind(kPrefabKeyOverridePrefix, 0) == 0) {
            const std::string rest = name.substr(std::string(kPrefabKeyOverridePrefix).size());
            const size_t colon = rest.rfind(':');
            if (colon == std::string::npos || colon == 0 || colon + 1 >= rest.size()) continue;
            const std::string key = rest.substr(colon + 1);
            if (key == "origin" || key == "angles" || key == "classname") continue;  // placement / identity stay
            inst.overrides.push_back({rest.substr(0, colon), key, kv->value});
        } else if (name.rfind(kPrefabMaterialSwapPrefix, 0) == 0) {
            const std::string from = name.substr(std::string(kPrefabMaterialSwapPrefix).size());
            if (!from.empty() && !kv->value.empty()) inst.materialSwaps.push_back({from, kv->value});
        } else if (name == kPrefabPlacementIdKey) {
            if (isValidPrefabPlacementId(kv->value)) inst.pid = kv->value;
        }
    }
    return inst;
}

std::string resolvePrefabPath(const std::string& mapPath, const std::string& model, const std::vector<std::string>& searchRoots) {
    // The search order lives in resolvePrefabReference (prefab_bundle.cpp). A bundle folder gives the contents file
    // a build with the default features uses (its fallback when it has one and needs it), so what is drawn is what
    // is built.
    const PrefabRef ref = resolvePrefabReference(mapPath, model, searchRoots);
    if (ref.kind != PrefabRef::Kind::Bundle) return ref.mapFile;
    PrefabBundle bundle;
    if (!loadPrefabBundle(ref.path, bundle, nullptr)) return ref.mapFile;
    return choosePrefabContents(bundle, PrefabBuildSupport{}).mapFile;
}

namespace {

void swapMaterials(Primitive& p, const PrefabInstance& inst) {
    if (inst.materialSwaps.empty()) return;
    auto to = [&](const std::string& m) -> const std::string* {
        for (const auto& [from, dest] : inst.materialSwaps)
            if (from == m) return &dest;
        return nullptr;
    };
    if (p.kind == PrimKind::Patch) {
        if (const std::string* d = to(p.patchMaterial)) p.setMaterial(*d, -1);
        return;
    }
    for (size_t i = 0; i < p.faces.size(); ++i)
        if (const std::string* d = to(p.faces[i].material())) p.setMaterial(*d, int(i));
}

bool selectorPicks(const std::string& selector, const Entity& e) {
    if (selector == "*") return true;
    if (selector.rfind("class=", 0) == 0) return e.classname() == selector.substr(6);
    const std::string* tn = e.value("targetname");
    return tn && *tn == selector;
}

}  // namespace

PrefabContents instantiatePrefab(const MapFile& prefab, const PrefabInstance& inst, const std::string& eol) {
    PrefabContents out;
    const Placement pl = placementOf(inst);
    for (const Entity& e : prefab.entities) {
        std::vector<Primitive> prims;
        for (const auto& it : e.items)
            if (const Primitive* p = it.prim()) {
                prims.push_back(transformPrimitive(*p, pl, eol));
                swapMaterials(prims.back(), inst);
            }
        if (e.isWorldspawn()) {
            for (auto& p : prims) out.worldPrims.push_back(std::move(p));
            continue;
        }
        const bool hasPrims = !prims.empty();
        auto keys = transformKeys(e, inst, pl, hasPrims);
        // Per-instance overrides, in the order they are written on the instance (later ones win).
        for (const PrefabKeyOverride& o : inst.overrides) {
            if (!selectorPicks(o.selector, e)) continue;
            auto it = std::find_if(keys.begin(), keys.end(), [&](const auto& kv) { return kv.first == o.key; });
            if (o.value.empty()) {
                if (it != keys.end()) keys.erase(it);
            } else if (it != keys.end()) {
                it->second = o.value;
            } else {
                keys.push_back({o.key, o.value});
            }
        }
        out.entities.push_back({std::move(keys), std::move(prims)});
    }
    return out;
}

namespace {

std::string quoteValue(const std::string& v) {
    std::string out;
    for (char c : v) out += c == '"' ? '\'' : c;  // map values cannot hold a double quote
    return out;
}

// One bundle kind of the build: the folder as read, and which contents are built.
struct BundleKind {
    PrefabBundle bundle;
    PrefabContentsChoice choice;
    bool ok = false;      // readable and placeable
    size_t use = 0;       // index in PrefabFlattenInfo::bundles
    std::string script;   // snippet text ("" = none)
};

// State of one flatten run, shared by every nesting level.
struct FlattenRun {
    const std::vector<std::string>& roots;
    const PrefabBuildSupport& support;
    std::vector<std::string>* warnings;
    PrefabFlattenInfo* info;
    std::map<std::string, BundleKind> kinds;      // by bundle folder
    std::map<std::string, std::string> idFolder;  // bundle id -> the folder that uses it
    std::string fatal;                            // set when the build must stop (one id, two folders)

    void warn(const std::string& w) {
        if (warnings) warnings->push_back(w);
    }
};

// One script start, in the coordinates of the level that produced it.
struct ScriptStart {
    std::string folder, pid;
    Vec3 origin, angles;
};

// What a nesting level hands to the placement that contains it.
struct LevelResult {
    std::set<std::string> scoped;      // names this level made unique with a placement id
    std::vector<ScriptStart> starts;
};

BundleKind& bundleKind(FlattenRun& run, const std::string& folder, const std::string& model) {
    const auto it = run.kinds.find(folder);
    if (it != run.kinds.end()) return it->second;
    BundleKind& k = run.kinds[folder];
    std::string why;
    if (!loadPrefabBundle(folder, k.bundle, &why)) {
        run.warn(why + "; left out of the build");
        return k;
    }
    const std::string& id = k.bundle.manifest.id;
    const auto [have, fresh] = run.idFolder.emplace(id, folder);
    if (!fresh && run.fatal.empty())
        run.fatal = "two prefab bundles use the id '" + id + "': " + have->second + " and " + folder +
                    " (give one of them another id, or place only one of them)";
    k.choice = choosePrefabContents(k.bundle, run.support);
    k.ok = true;
    for (const std::string& w : k.bundle.warnings) run.warn("prefab bundle '" + id + "': " + w);
    if (!k.choice.scriptFile.empty()) {
        if (!readFile(k.choice.scriptFile, k.script)) {
            run.warn("prefab bundle '" + id + "': its script cannot be read; built without it");
        } else {
            for (const std::string& p : checkPrefabScript(k.script, k.bundle.manifest)) run.warn("prefab bundle '" + id + "' script: " + p);
        }
    }
    if (run.info) {
        k.use = run.info->bundles.size();
        PrefabBundleUse u;
        u.id = id;
        u.folder = folder;
        u.model = model;
        u.fallback = k.choice.fallback;
        u.script = !k.script.empty();
        run.info->bundles.push_back(std::move(u));
        const auto add = [](std::vector<std::string>& list, const std::string& dir) {
            if (!dir.empty() && std::find(list.begin(), list.end(), dir) == list.end()) list.push_back(dir);
        };
        add(run.info->assetRoots.materials, k.bundle.materialsDir);
        add(run.info->assetRoots.models, k.bundle.modelsDir);
        add(run.info->assetRoots.sounds, k.bundle.soundsDir);
        add(run.info->assetRoots.effects, k.bundle.effectsDir);
    }
    return k;
}

// Text of the map with prefabs expanded (recursive). `depth` guards against prefabs that include themselves.
// A map without bundle placements gives the text this function has always given.
std::string flattenText(const MapFile& in, const std::string& mapPath, FlattenRun& run, int depth, LevelResult& up) {
    std::string out = in.header;
    const std::string eol = in.eol;
    // Placement ids of this level: the first use of an id keeps it; "a<N>" fills in where one is needed and missing.
    std::set<std::string> declared, taken;
    for (const Entity& e : in.entities)
        if (const auto inst = prefabInstance(e); inst && !inst->pid.empty()) declared.insert(inst->pid);
    int autoId = 0;
    // Bare prefabs placed more than once: the linked names their placements share.
    struct BareUse {
        size_t count = 0;
        std::set<std::string> linked;
    };
    std::map<std::string, BareUse> bare;
    std::vector<std::string> bareOrder;
    for (const Entity& e : in.entities) {
        const auto inst = prefabInstance(e);
        if (!inst) {
            out += e.text();
            continue;
        }
        const PrefabRef ref = resolvePrefabReference(mapPath, inst->model, run.roots);
        BundleKind* kind = nullptr;
        std::string path = ref.mapFile;
        if (ref.kind == PrefabRef::Kind::Bundle) {
            kind = &bundleKind(run, ref.path, inst->model);
            if (!kind->ok) continue;  // the reason is in the warnings
            path = kind->choice.mapFile;
        }
        std::string text;
        if (path.empty() || !readFile(path, text)) {
            run.warn("prefab '" + inst->model + "' not found; left out of the build");
            continue;
        }
        Id next = 1;
        ParseResult pr = parseMap(text, next);
        if (!pr.map) {
            run.warn("prefab '" + inst->model + "' does not parse; left out of the build");
            continue;
        }
        MapFile prefab = std::move(*pr.map);
        LevelResult inner;
        if (depth < 8) {
            bool nested = false;
            for (const auto& pe : prefab.entities) nested |= prefabInstance(pe).has_value();
            if (nested) {
                Id n2 = 1;
                ParseResult innerMap = parseMap(flattenText(prefab, path, run, depth + 1, inner), n2);
                if (innerMap.map) prefab = std::move(*innerMap.map);
            }
        } else {
            run.warn("prefab '" + inst->model + "' nests deeper than 8 levels; inner prefabs left out");
        }
        if (kind && !run.support.features.count(kPrefabFeatureScriptableInstances)) {
            // The build cannot write scriptable instances yet: they are left out and counted.
            const size_t before = prefab.entities.size();
            prefab.entities.erase(std::remove_if(prefab.entities.begin(), prefab.entities.end(),
                                                 [](const Entity& pe) { return pe.classname() == "scriptable"; }),
                                  prefab.entities.end());
            if (run.info) run.info->bundles[kind->use].scriptablesLeftOut += before - prefab.entities.size();
        }
        PrefabContents c = instantiatePrefab(prefab, *inst, eol);

        // The placement id: a bundle always has one, a bare prefab only when something inside it needs one.
        std::string pid;
        if (kind || !inner.scoped.empty() || !inner.starts.empty()) {
            if (!inst->pid.empty() && taken.insert(inst->pid).second) {
                pid = inst->pid;
            } else {
                do pid = "a" + std::to_string(++autoId);
                while (declared.count(pid) || taken.count(pid));
                taken.insert(pid);
                run.warn("prefab '" + inst->model + "' at " + vecText(inst->origin) +
                         (inst->pid.empty() ? std::string(" has no ") + kPrefabPlacementIdKey
                                            : " repeats the " + std::string(kPrefabPlacementIdKey) + " '" + inst->pid + "'") +
                         "; its names are built with '" + pid + "' (give it its own " + kPrefabPlacementIdKey + " to keep them fixed)");
            }
        }
        if (!pid.empty()) {
            PrefabNameScope scope = kind ? prefabNameScope(prefab, kind->bundle.manifest, pid) : PrefabNameScope{pid, {}, prefabNameKeys()};
            scope.local.insert(inner.scoped.begin(), inner.scoped.end());  // names of inner placements chain: p7_p2_door
            for (auto& ent : c.entities) scopePrefabNames(ent.first, scope);
            for (const std::string& n : scope.local) up.scoped.insert(scopedPrefabName(pid, n));
            const Placement pl = placementOf(*inst);
            const Mat3 rot = anglesToMatrix(inst->angles);
            for (const ScriptStart& s : inner.starts)
                up.starts.push_back({s.folder, scopedPrefabName(pid, s.pid), pl.point(s.origin), matrixToAngles(rot * anglesToMatrix(s.angles))});
            if (kind && !kind->script.empty()) up.starts.push_back({ref.path, pid, inst->origin, inst->angles});
        }
        if (kind) {
            if (run.info) {
                ++run.info->bundlePlacements;
                ++run.info->bundles[kind->use].placements;
            }
        } else {
            BareUse& b = bare[path];
            if (b.count++ == 0) {
                bareOrder.push_back(path);
                b.linked = prefabLinkedNames(prefab);
                for (const std::string& n : inner.scoped) b.linked.erase(n);
            }
        }
        if (!c.worldPrims.empty()) {
            out += "// prefab " + inst->model + eol + "{" + eol + "\"classname\" \"func_group\"" + eol;
            for (const Primitive& p : c.worldPrims) out += p.text();
            out += "}" + eol;
        }
        for (const auto& [kvs, prims] : c.entities) {
            out += "{" + eol;
            for (const auto& [k, v] : kvs) out += "\"" + k + "\" \"" + quoteValue(v) + "\"" + eol;
            for (const Primitive& p : prims) out += p.text();
            out += "}" + eol;
        }
    }
    for (const std::string& path : bareOrder) {
        const BareUse& b = bare[path];
        if (b.count < 2 || b.linked.empty()) continue;
        std::string names;
        size_t shown = 0;
        for (const std::string& n : b.linked)
            if (shown++ < 4) names += (names.empty() ? "'" : ", '") + n + "'";
        if (b.linked.size() > 4) names += ", ...";
        run.warn("prefab '" + fs::path(path).filename().string() + "' is placed " + std::to_string(b.count) +
                 " times and its placements share the linked name(s) " + names +
                 ": a trigger of one placement also drives the others (save it as a prefab bundle, " + kPrefabBundleSuffix +
                 ", to give each placement its own names)");
    }
    return out + in.trailer;
}

}  // namespace

bool flattenPrefabBundles(const MapFile& in, const std::string& mapPath, const std::vector<std::string>& roots,
                          const PrefabBuildSupport& support, MapFile& out, PrefabFlattenInfo& info,
                          std::vector<std::string>* warnings, std::string* err) {
    info = PrefabFlattenInfo{};
    FlattenRun run{roots, support, warnings, &info, {}, {}, {}};
    LevelResult top;
    Id next = 1;
    ParseResult pr = parseMap(flattenText(in, mapPath, run, 0, top), next);
    if (!run.fatal.empty()) {
        if (err) *err = run.fatal;
        return false;
    }
    if (!pr.map) {
        if (err) *err = "flattened prefabs do not parse" + (pr.error ? ": line " + std::to_string(pr.error->line) + " " + pr.error->message : std::string());
        return false;
    }
    out = std::move(*pr.map);
    // The scripts: one entry per bundle kind in order of first use, one placement per start.
    for (const PrefabBundleUse& u : info.bundles) {
        const BundleKind& k = run.kinds[u.folder];
        if (k.script.empty()) continue;
        MapScriptExtras::PrefabScript ps;
        ps.id = u.id;
        ps.source = fs::path(u.folder).filename().string();
        ps.text = k.script;
        ps.prefix = k.bundle.manifest.scriptPrefix;
        ps.entry = k.bundle.manifest.scriptEntry;
        ps.once = k.bundle.manifest.scriptOnce;
        for (const ScriptStart& s : top.starts)
            if (s.folder == u.folder) ps.placements.push_back({s.pid, s.origin, s.angles});
        info.scripts.push_back(std::move(ps));
    }
    // One line per bundle kind for the build log, then what it needs and the build does not have.
    for (const PrefabBundleUse& u : info.bundles) {
        const BundleKind& k = run.kinds[u.folder];
        const std::string who = "prefab bundle '" + u.id + "' (" + fs::path(u.folder).filename().string() + ")";
        std::string line = who + ": " + std::to_string(u.placements) + " placement(s), " + (u.fallback ? "fallback" : "main") + " contents";
        if (u.script) {
            size_t starts = 0;
            for (const ScriptStart& s : top.starts) starts += s.folder == u.folder;
            line += "; its script is included once and started " + std::to_string(starts) + " time(s)";
        }
        std::string folders;
        for (const auto& [name, dir] : {std::pair<const char*, const std::string*>{"materials", &k.bundle.materialsDir},
                                        std::pair<const char*, const std::string*>{"models", &k.bundle.modelsDir},
                                        std::pair<const char*, const std::string*>{"sounds", &k.bundle.soundsDir},
                                        std::pair<const char*, const std::string*>{"effects", &k.bundle.effectsDir}})
            if (!dir->empty()) folders += (folders.empty() ? "" : ", ") + std::string(name);
        if (!folders.empty()) line += "; its own asset folders are used: " + folders;
        run.warn(line);
        if (!k.choice.reason.empty()) run.warn(who + ": " + k.choice.reason);
        if (u.scriptablesLeftOut)
            run.warn(who + ": " + std::to_string(u.scriptablesLeftOut) +
                     " scriptable object(s) left out (scriptables inside a prefab are not built yet)");
        for (const std::string& a : k.choice.missingAssets)
            run.warn(who + " needs the " + a + ", which is not in the stock catalogue");
    }
    return true;
}

bool flattenPrefabs(const MapFile& in, const std::string& mapPath, const std::vector<std::string>& roots, MapFile& out,
                    std::vector<std::string>* warnings, std::string* err) {
    PrefabFlattenInfo info;
    return flattenPrefabBundles(in, mapPath, roots, PrefabBuildSupport{}, out, info, warnings, err);
}

bool explodePrefab(Document& doc, Id prefabEntity, const MapFile& prefab, std::string* err, const PrefabNameScope* scope) {
    auto fail = [&](const char* msg) {
        if (err) *err = msg;
        return false;
    };
    const Entity* pe = doc.findEntity(prefabEntity);
    if (!pe) return fail("no such entity");
    const auto inst = prefabInstance(*pe);
    if (!inst) return fail("entity is not a misc_prefab with a model");
    for (const auto& it : pe->items)
        if (it.isPrimitive()) return fail("misc_prefab must be a point entity");
    const Entity* world = doc.worldspawn();
    if (!world) return fail("map has no worldspawn");
    const Id worldId = world->id;

    PrefabContents c = instantiatePrefab(prefab, *inst, doc.eol());
    if (scope)
        for (auto& ent : c.entities) scopePrefabNames(ent.first, *scope);

    // One undo step for the whole explode (Document::beginGroup). A failure part way rolls it back.
    const size_t before = doc.undoPosition();
    doc.beginGroup("Explode Prefab");
    auto abort = [&](const char* msg) {
        doc.endGroup();
        if (doc.undoPosition() > before) doc.undo();
        return fail(msg);
    };
    std::set<SelRef> created;
    if (!c.worldPrims.empty()) {
        std::vector<Document::PrimEdit> edits;
        for (auto& p : c.worldPrims) edits.push_back({worldId, kNoId, std::move(p)});
        if (!doc.editPrimitives(edits, "Explode Prefab")) return abort("could not insert prefab primitives");
        created.insert(doc.selection().begin(), doc.selection().end());
    }
    std::vector<Id> entities;
    for (auto& [kvs, prims] : c.entities) entities.push_back(doc.addEntity(kvs, std::move(prims), "Explode Prefab"));
    // The misc_prefab entity itself (a selected point entity is removed by deleteSelection).
    doc.deselectAll();
    doc.select({prefabEntity, kNoId}, false);
    if (!doc.deleteSelection()) return abort("could not remove the misc_prefab entity");
    doc.endGroup();
    // Radiant leaves the exploded contents selected.
    doc.deselectAll();
    for (const SelRef& r : created) doc.select(r, true);
    for (Id e : entities)
        if (e != kNoId)
            for (const SelRef& r : doc.selectionUnit({e, kNoId})) doc.select(r, true);
    return true;
}

std::string selectionToPrefabText(const Document& doc, const Vec3& origin) {
    const std::string eol = doc.eol();
    MapFile out;
    out.eol = eol;
    out.header = "iwmap 4" + eol;

    Entity world;
    world.id = 1;
    world.prefix = "// entity 0" + eol;
    world.open = {"{", eol};
    world.close = {"}", eol};
    world.items.push_back(kvItem("classname", "worldspawn", eol, 2));
    Id nextItem = 3;
    out.entities.push_back(world);  // index 0 is the worldspawn; never hold a reference into out.entities

    // World primitives go to the prefab's worldspawn. The primitives of a brush entity stay in a copy of that entity
    // with its keys (a selected script_brushmodel door is saved as a door, not as wall brushes).
    std::map<Id, size_t> brushEntities;  // source entity -> index in out.entities
    std::set<std::pair<Id, Id>> added;
    auto addPrim = [&](const Entity& owner, const EntityItem& item) {
        const Primitive* src = item.prim();
        if (!src || !added.insert({owner.id, item.id}).second) return;
        size_t index = 0;
        if (!owner.isWorldspawn()) {
            const auto found = brushEntities.find(owner.id);
            if (found != brushEntities.end()) {
                index = found->second;
            } else {
                Entity copy = owner;
                copy.prefix = "// entity " + std::to_string(out.entities.size()) + eol;
                copy.items.clear();
                for (const auto& it : owner.items) {
                    if (it.isPrimitive()) continue;
                    copy.items.push_back(it);
                    if (KeyValueLine* kv = copy.items.back().kv(); kv && kv->name() == "origin")
                        if (auto o = parseVec3(kv->value)) kv->set(vecText(*o - origin));
                }
                index = out.entities.size();
                out.entities.push_back(std::move(copy));
                brushEntities[owner.id] = index;
            }
        }
        Primitive p = *src;
        p.translate(-origin);
        out.entities[index].items.push_back(primItem(std::move(p), nextItem++));
    };

    for (const SelRef& r : doc.selection()) {
        const Entity* e = doc.findEntity(r.entity);
        if (!e) continue;
        if (r.item != kNoId) {
            const EntityItem* it = doc.findItem(r.entity, r.item);
            if (it && it->prim()) addPrim(*e, *it);
            continue;
        }
        if (e->isWorldspawn()) continue;
        bool hasPrim = false;
        for (const auto& it : e->items)
            if (it.prim()) {
                hasPrim = true;
                addPrim(*e, it);
            }
        if (hasPrim) continue;

        // Point entity: its origin becomes (old origin - origin).
        Entity copy = *e;
        copy.prefix = "// entity " + std::to_string(out.entities.size()) + eol;
        const Vec3 o = e->origin().value_or(Vec3{}) - origin;
        bool haveOrigin = false;
        for (auto& it : copy.items)
            if (KeyValueLine* kv = it.kv(); kv && kv->name() == "origin") {
                kv->set(vecText(o));
                haveOrigin = true;
            }
        if (!haveOrigin) copy.items.push_back(kvItem("origin", vecText(o), eol, nextItem++));
        out.entities.push_back(std::move(copy));
    }
    return out.text();
}

}  // namespace iw8r
