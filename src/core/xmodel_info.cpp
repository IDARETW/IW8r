#include "xmodel_info.h"

#include "json_lite.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace iw8r {
namespace {

using json::Value;
namespace fs = std::filesystem;

constexpr double kQuatScale = 1.0 / 32767.0;  // quats are int16 components of a unit quaternion
constexpr int kScriptStringsWalkUp = 3;       // <zone>/assets/xmodel/<file>: the table is at most three folders up

struct Quat {
    double x = 0, y = 0, z = 0, w = 1;
};

void setErr(std::string* err, const std::string& msg) {
    if (err) *err = msg;
}

const Value* child(const Value* v, const char* key) {
    return v && v->type == Value::Type::Object ? v->get(key) : nullptr;
}

double numberOr(const Value* v, double fallback) {
    const auto n = v ? v->number() : std::nullopt;
    return n ? *n : fallback;
}

// The string of a node, or empty.
std::string stringOr(const Value* v) {
    const std::string* s = v ? v->string() : nullptr;
    return s ? *s : std::string();
}

// Reads the first n numbers of {"v": [...]} (the $union_views vector form). Returns false when absent or short.
bool readVec(const Value* node, size_t n, double* out) {
    const Value* v = child(node, "v");
    if (!v || v->type != Value::Type::Array || v->arr.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        const auto x = v->arr[i].number();
        if (!x) return false;
        out[i] = *x;
    }
    return true;
}

bool decodeHex(const std::string& hex, std::vector<uint8_t>& out) {
    if (hex.size() % 2 != 0) return false;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    out.clear();
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        const int hi = nibble(hex[i]), lo = nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(static_cast<uint8_t>(hi * 16 + lo));
    }
    return true;
}

Quat qmul(const Quat& a, const Quat& b) {
    return Quat{a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

Quat normalized(const Quat& q) {
    const double len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (len <= 0) return Quat{};
    return Quat{q.x / len, q.y / len, q.z / len, q.w / len};
}

// Rotates v by the unit quaternion q (q * (v,0) * conj(q)).
void rotate(const Quat& q, const double* v, double* out) {
    const Quat t = qmul(qmul(q, Quat{v[0], v[1], v[2], 0.0}), Quat{-q.x, -q.y, -q.z, q.w});
    out[0] = t.x;
    out[1] = t.y;
    out[2] = t.z;
}

// Parses the export. When nameIds is set it receives the scr_string_t id of each bone (-1 when absent).
bool parseImpl(const std::string& jsonText, XModelInfo& out, std::vector<int>* nameIds, std::string* err) {
    std::string parseErr;
    const auto root = json::parse(jsonText, &parseErr);
    if (!root) {
        setErr(err, "not JSON: " + parseErr);
        return false;
    }
    if (root->type != Value::Type::Object) {
        setErr(err, "root is not an object");
        return false;
    }
    const Value* format = root->get("format");
    if (!format || !format->string() || format->string()->rfind("mw19-asset-json", 0) != 0) {
        setErr(err, "not an mw19 asset-json export");
        return false;
    }
    const Value* rootType = root->get("root_type");
    if (!rootType || !rootType->string() || *rootType->string() != "XModel") {
        setErr(err, "root_type is not XModel");
        return false;
    }
    const Value* fields = child(root->get("asset"), "fields");
    if (!fields || fields->type != Value::Type::Object) {
        setErr(err, "asset.fields is missing");
        return false;
    }
    const Value* nameField = child(fields, "name");
    if (!child(nameField, "string") || !child(nameField, "string")->string()) {
        setErr(err, "fields.name.string is missing");
        return false;
    }

    XModelInfo info;
    info.name = stringOr(child(nameField, "string"));

    // LODs: lodInfo has numLods live slots followed by unused slots.
    const Value* lodArr = child(fields, "lodInfo");
    const size_t lodSlots = lodArr && lodArr->type == Value::Type::Array ? lodArr->arr.size() : 0;
    const double numLodsRaw = numberOr(child(fields, "numLods"), static_cast<double>(lodSlots));
    const size_t numLods = numLodsRaw <= 0 ? 0 : std::min(static_cast<size_t>(numLodsRaw), lodSlots);
    for (size_t i = 0; i < numLods; ++i) {
        const Value& slot = lodArr->arr[i];
        XModelLod lod;
        lod.distance = numberOr(child(&slot, "dist"), 0.0);
        lod.surfCount = static_cast<int>(numberOr(child(&slot, "numsurfs"), 0.0));
        lod.surfs = stringOr(child(child(&slot, "modelSurfsStaging"), "name"));
        lod.surfIndex = static_cast<int>(numberOr(child(&slot, "surfIndex"), 0.0));
        if (!info.lods.empty() && lod.distance < info.lods.back().distance) {
            setErr(err, "LOD distances are not ascending");
            return false;
        }
        info.lods.push_back(lod);
    }
    const int rawColl = static_cast<int>(numberOr(child(fields, "collLod"), -1.0));
    info.collLod = rawColl >= 0 && rawColl < static_cast<int>(info.lods.size()) ? rawColl : -1;

    info.radius = numberOr(child(fields, "radius"), 0.0);
    double mid[3] = {0, 0, 0}, half[3] = {0, 0, 0};
    const Value* bounds = child(fields, "bounds");
    readVec(child(bounds, "midPoint"), 3, mid);
    readVec(child(bounds, "halfSize"), 3, half);
    for (int i = 0; i < 3; ++i) {
        info.bounds[i] = mid[i] - half[i];
        info.bounds[i + 3] = mid[i] + half[i];
    }

    // Materials: the leading ',' is the shared-name marker.
    const Value* mats = child(child(fields, "materialHandles"), "values");
    std::vector<std::string> handles;
    if (mats && mats->type == Value::Type::Array) {
        for (const auto& m : mats->arr) {
            std::string s = stringOr(child(&m, "name"));
            if (!s.empty() && s[0] == ',') s.erase(0, 1);
            handles.push_back(s);
            if (!s.empty() && std::find(info.materials.begin(), info.materials.end(), s) == info.materials.end())
                info.materials.push_back(s);
        }
    }
    // Each LOD's surface i uses materialHandles[surfIndex + i].
    for (XModelLod& lod : info.lods)
        for (int i = 0; i < lod.surfCount; ++i) {
            const size_t h = static_cast<size_t>(std::max(0, lod.surfIndex)) + static_cast<size_t>(i);
            lod.materials.push_back(h < handles.size() ? handles[h] : std::string());
        }

    // Bones: boneNames lists skeleton bones, then client bones. Non-root bones are the trailing parentList entries.
    const Value* nameVals = child(child(fields, "boneNames"), "values");
    const size_t boneCount = nameVals && nameVals->type == Value::Type::Array ? nameVals->arr.size() : 0;

    std::vector<uint8_t> parentBytes;
    const Value* parentList = child(fields, "parentList");
    if (parentList && parentList->type == Value::Type::Object) {
        if (!decodeHex(stringOr(child(parentList, "bytes")), parentBytes)) {
            setErr(err, "parentList.bytes is not hex");
            return false;
        }
    }
    if (parentBytes.size() > boneCount) {
        setErr(err, "parentList is longer than boneNames");
        return false;
    }
    const size_t numRoots = boneCount - parentBytes.size();

    const Value* transVals = child(child(fields, "trans"), "values");
    const Value* quatVals = child(child(fields, "quats"), "values");
    const size_t nonRoot = parentBytes.size();
    if (transVals && (transVals->type != Value::Type::Array || transVals->arr.size() < 3 * nonRoot)) {
        setErr(err, "trans has fewer values than the non-root bones");
        return false;
    }
    if (quatVals && (quatVals->type != Value::Type::Array || quatVals->arr.size() < 4 * nonRoot)) {
        setErr(err, "quats has fewer values than the non-root bones");
        return false;
    }

    std::vector<int> parents(boneCount, -1);
    for (size_t c = numRoots; c < boneCount; ++c) {
        const size_t off = parentBytes[c - numRoots];
        if (off == 0 || off > c) {
            setErr(err, "bone " + std::to_string(c) + " has an invalid parent offset");
            return false;
        }
        parents[c] = static_cast<int>(c - off);
    }

    // Model-space positions: baseMat when it has one entry per bone, else the parent-chain composition of the locals.
    std::vector<std::array<double, 3>> pos(boneCount, std::array<double, 3>{0, 0, 0});
    const Value* baseMat = child(child(fields, "baseMat"), "values");
    bool haveBaseMat = baseMat && baseMat->type == Value::Type::Array && baseMat->arr.size() >= boneCount;
    for (size_t c = 0; haveBaseMat && c < boneCount; ++c) {
        if (!readVec(child(&baseMat->arr[c], "trans"), 3, pos[c].data())) haveBaseMat = false;
    }
    if (!haveBaseMat) {
        std::vector<Quat> rot(boneCount);
        for (size_t c = 0; c < boneCount; ++c) {
            if (c < numRoots) continue;  // roots: no local transform exported, so the origin
            const size_t k = c - numRoots;
            double lt[3] = {0, 0, 0};
            Quat lq;
            if (transVals) {
                for (int i = 0; i < 3; ++i) lt[i] = numberOr(&transVals->arr[3 * k + i], 0.0);
            }
            if (quatVals) {
                lq = normalized(Quat{numberOr(&quatVals->arr[4 * k], 0) * kQuatScale,
                                     numberOr(&quatVals->arr[4 * k + 1], 0) * kQuatScale,
                                     numberOr(&quatVals->arr[4 * k + 2], 0) * kQuatScale,
                                     numberOr(&quatVals->arr[4 * k + 3], 1) * kQuatScale});
            }
            const size_t p = static_cast<size_t>(parents[c]);
            double rt[3];
            rotate(rot[p], lt, rt);
            for (int i = 0; i < 3; ++i) pos[c][i] = pos[p][i] + rt[i];
            rot[c] = normalized(qmul(rot[p], lq));
        }
    }

    info.bones.resize(boneCount);
    if (nameIds) nameIds->assign(boneCount, -1);
    for (size_t c = 0; c < boneCount; ++c) {
        const int id = static_cast<int>(numberOr(child(&nameVals->arr[c], "value"), -1.0));
        XModelBone& bone = info.bones[c];
        bone.name = id >= 0 ? "#" + std::to_string(id) : std::string();
        bone.parent = parents[c];
        bone.pos[0] = pos[c][0];
        bone.pos[1] = pos[c][1];
        bone.pos[2] = pos[c][2];
        if (nameIds) (*nameIds)[c] = id;
    }

    out = std::move(info);
    return true;
}

// Replaces "#<id>" bone names with the script string table of the zone (script_strings.json, a JSON array with
// null at index 0), searched for from the export's folder up to kScriptStringsWalkUp folders above it.
void resolveBoneNames(const std::string& exportPath, const std::vector<int>& ids, std::vector<XModelBone>& bones) {
    std::error_code ec;
    fs::path dir = fs::path(exportPath).parent_path();
    fs::path table;
    for (int up = 0; up <= kScriptStringsWalkUp && !dir.empty(); ++up, dir = dir.parent_path()) {
        if (fs::exists(dir / "script_strings.json", ec)) {
            table = dir / "script_strings.json";
            break;
        }
    }
    if (table.empty()) return;

    std::ifstream in(table, std::ios::binary);
    if (!in) return;
    std::ostringstream text;
    text << in.rdbuf();
    const auto strings = json::parse(text.str(), nullptr);
    if (!strings || strings->type != Value::Type::Array) return;

    for (size_t c = 0; c < bones.size() && c < ids.size(); ++c) {
        const int id = ids[c];
        if (id < 0 || static_cast<size_t>(id) >= strings->arr.size()) continue;
        const std::string* s = strings->arr[static_cast<size_t>(id)].string();
        if (s && !s->empty()) bones[c].name = *s;
    }
}

}  // namespace

bool parseXModelExport(const std::string& jsonText, XModelInfo& out, std::string* err) {
    return parseImpl(jsonText, out, nullptr, err);
}

bool readXModelExport(const std::string& path, XModelInfo& out, std::string* err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        setErr(err, "cannot open " + path);
        return false;
    }
    std::ostringstream text;
    text << in.rdbuf();

    XModelInfo info;
    std::vector<int> ids;
    if (!parseImpl(text.str(), info, &ids, err)) return false;
    resolveBoneNames(path, ids, info.bones);
    out = std::move(info);
    return true;
}

int previewLodFor(const XModelInfo& m, double distance, double lodScale) {
    return lodForDistance(m, distance / (lodScale > 0 ? lodScale : 1.0));
}

int lodForDistance(const XModelInfo& m, double distance) {
    // Each LOD is drawn up to its distance (0 = no limit), as the IW engines' XModelGetLodForDist does: the first LOD
    // whose distance is still ahead of the camera. Past the last one the model is not drawn (-1).
    for (size_t i = 0; i < m.lods.size(); ++i)
        if (m.lods[i].distance <= 0 || distance < m.lods[i].distance) return static_cast<int>(i);
    return m.lods.empty() ? 0 : -1;
}

bool isTag(const XModelBone& bone) {
    return bone.name.rfind("tag_", 0) == 0;
}

}  // namespace iw8r
