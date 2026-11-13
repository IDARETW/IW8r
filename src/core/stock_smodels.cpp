#include "stock_smodels.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string_view>
#include <system_error>

#include "json_lite.h"
#include "map_text.h"
#include "prefab.h"

namespace iw8r {
namespace {

using json::Value;
namespace fs = std::filesystem;

constexpr double kTranslationUnit = 1.0 / 4096.0;  // int32 translation, 1/4096 inch (docs/research/48 section 4)
constexpr size_t kCollectionBytes = 16;            // GfxStaticModelCollection
constexpr size_t kInstanceBytes = 24;              // GfxSModelInstanceData
constexpr size_t kIndirectionBytes = 8;            // GfxModelStaticIndirection (docs/research/105 section 1.1)
constexpr double kPi = 3.14159265358979323846;

bool unhex(std::string_view hex, std::vector<uint8_t>& out) {
    if (hex.size() % 2) return false;
    out.resize(hex.size() / 2);
    const auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < out.size(); ++i) {
        const int hi = nib(hex[2 * i]), lo = nib(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = uint8_t(hi << 4 | lo);
    }
    return true;
}

uint32_t u32At(const std::vector<uint8_t>& b, size_t off) {
    return uint32_t(b[off]) | uint32_t(b[off + 1]) << 8 | uint32_t(b[off + 2]) << 16 | uint32_t(b[off + 3]) << 24;
}

uint16_t u16At(const std::vector<uint8_t>& b, size_t off) { return uint16_t(b[off] | b[off + 1] << 8); }

int32_t i32At(const std::vector<uint8_t>& b, size_t off) {
    const uint32_t u = u32At(b, off);
    int32_t v;
    std::memcpy(&v, &u, 4);
    return v;
}

float f32At(const std::vector<uint8_t>& b, size_t off) {
    const uint32_t u = u32At(b, off);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

// Orientation component: unsigned 16-bit mapped to [-1, 1] (0x8000 is zero, 0xffff is one).
double orientComponent(uint16_t u) { return double(u) / 32767.5 - 1.0; }

double comp(const Vec3& v, int i) { return i == 0 ? v.x : i == 1 ? v.y : v.z; }

// The file's text, memory-mapped (json::FileText): the 422 MB mp_donetsk export costs no private memory.
bool readWholeFile(const fs::path& p, json::FileText& out, std::string* err) {
    if (!out.open(p)) {
        if (err) *err = "cannot read " + p.string();
        return false;
    }
    return true;
}

// The decoded text of a string value: a view of the export when it has no escape (hex never has), else a decoded
// copy in `scratch`. False when `v` is not a string.
bool stringOf(const json::View& v, std::string& scratch, std::string_view& out) {
    if (v.rawString(out)) return true;
    if (!v.string(scratch)) return false;
    out = scratch;
    return true;
}

// Every element of an array view parsed on its own (a small DOM at a time).
void forEachRecord(const json::View& arr, const std::function<void(const Value&)>& fn) {
    arr.forEachElement([&](json::View e) {
        const auto v = e.parse();
        if (!v) return false;
        fn(*v);
        return true;
    });
}

std::vector<fs::path> jsonFilesUnder(const fs::path& dir) {
    std::vector<fs::path> out;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return out;
    for (const auto& e : fs::recursive_directory_iterator(dir, ec))
        if (e.is_regular_file(ec) && e.path().extension() == ".json") out.push_back(e.path());
    std::sort(out.begin(), out.end());
    return out;
}

// The gfx_map JSON: the path itself when it is a file, else the single JSON under <dir>/assets/gfx_map.
bool findGfxMapJson(const std::string& exportPathOrDir, fs::path& file, std::string* err) {
    std::error_code ec;
    const fs::path p(exportPathOrDir);
    if (fs::is_regular_file(p, ec)) {
        file = p;
        return true;
    }
    const fs::path dir = p / "assets" / "gfx_map";
    const auto files = jsonFilesUnder(dir);
    if (files.size() != 1) {
        if (err) *err = "expected one gfx_map JSON under " + dir.string() + ", found " + std::to_string(files.size());
        return false;
    }
    file = files.front();
    return true;
}

// Quaternion (x, y, z, w) to the three model axes (columns of the rotation matrix). The quaternion is normalised.
std::array<Vec3, 3> quatColumns(double x, double y, double z, double w) {
    const double n = std::sqrt(x * x + y * y + z * z + w * w);
    if (n > 1e-12) {
        x /= n;
        y /= n;
        z /= n;
        w /= n;
    } else {
        x = y = z = 0;
        w = 1;
    }
    return {Vec3{1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w)},
            Vec3{2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w)},
            Vec3{2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)}};
}

// Rounds to 1e-6 and formats; -0 is written as 0.
std::string fmt6(double v) {
    const double r = std::round(v * 1e6) / 1e6;
    return formatNumber(r + 0.0);
}

}  // namespace

bool readStockModelInstances(const std::string& exportPathOrDir, std::vector<StockModelInstance>& out, std::string* err) {
    out.clear();
    fs::path file;
    if (!findGfxMapJson(exportPathOrDir, file, err)) return false;

    // Only fields.smodels is read, record by record (no DOM of the whole export: 422 MB of JSON on mp_donetsk).
    json::FileText text;
    std::string jerr;
    if (!readWholeFile(file, text, err)) return false;
    if (!json::validate(text.view(), &jerr)) {
        if (err) *err = "gfx_map JSON: " + jerr;
        return false;
    }

    const json::View fields = json::View(text.view()).get("asset").get("fields");
    const json::View sm = fields.get("smodels");
    if (!sm.valid()) {
        if (err) *err = "gfx_map has no fields.smodels";
        return false;
    }
    const json::View models = sm.get("models").get("values");
    const json::View collNode = sm.get("collections");
    const json::View instNode = sm.get("smodelInstanceData");
    const json::View collBytes = collNode.get("bytes");
    const json::View instBytes = instNode.get("bytes");
    const json::View collValues = collNode.get("values");
    const json::View instValues = instNode.get("values");
    if (models.type() != Value::Type::Array) {
        if (err) *err = "smodels has no models.values";
        return false;
    }

    std::vector<uint8_t> coll, inst;
    auto put32 = [](std::vector<uint8_t>& v, uint64_t x) {
        for (int k = 0; k < 4; ++k) v.push_back(uint8_t((x >> (8 * k)) & 0xff));
    };
    auto num = [](const Value* n) { return n && n->number() ? *n->number() : 0.0; };
    std::string scratch;
    std::string_view hex;
    if (collBytes.type() == Value::Type::String && instBytes.type() == Value::Type::String) {
        if (!stringOf(collBytes, scratch, hex) || !unhex(hex, coll) || coll.size() % kCollectionBytes != 0) {
            if (err) *err = "smodels.collections bytes are not hex or not a multiple of 16";
            return false;
        }
        if (!stringOf(instBytes, scratch, hex) || !unhex(hex, inst) || inst.size() % kInstanceBytes != 0) {
            if (err) *err = "smodels.smodelInstanceData bytes are not hex or not a multiple of 24";
            return false;
        }
    } else if (collValues.type() == Value::Type::Array && instValues.type() == Value::Type::Array) {
        // Some exports (mp_m_king, mp_m_speedball) expand the arrays into objects: rebuild the same byte layout.
        forEachRecord(collValues, [&](const Value& c) {
            put32(coll, uint64_t(num(c.get("firstInstance"))));
            put32(coll, uint64_t(num(c.get("instanceCount"))));
            put32(coll, uint64_t(num(c.get("smodelIndex"))));
            put32(coll, 0);
        });
        forEachRecord(instValues, [&](const Value& i) {
            const Value* t = i.get("translation");
            for (int k = 0; k < 3; ++k)
                put32(inst, uint64_t(uint32_t(int32_t(t && size_t(k) < t->arr.size() ? num(&t->arr[size_t(k)]) : 0.0))));
            const Value* o = i.get("orientation");
            const Value* ov = o ? o->get("v") : nullptr;  // two u32: (x | y << 16), (z | w << 16)
            for (int k = 0; k < 2; ++k) put32(inst, uint64_t(ov && size_t(k) < ov->arr.size() ? num(&ov->arr[size_t(k)]) : 0.0));
            float sc = float(num(i.get("scale")));
            uint32_t bits;
            std::memcpy(&bits, &sc, 4);
            put32(inst, bits);
        });
    } else {
        if (err) *err = "smodels has no collections / smodelInstanceData bytes or values";
        return false;
    }
    const uint64_t instanceTotal = inst.size() / kInstanceBytes;

    // Lighting indirection, one 8-byte record per instance row (optional: older exports may lack it; then the
    // instances keep their defaults). A malformed array is ignored the same way.
    std::vector<uint8_t> indir;
    if (const json::View ind = sm.get("modelStaticIndirection"); ind.valid()) {
        const json::View ib = ind.get("bytes");
        const json::View iv = ind.get("values");
        if (stringOf(ib, scratch, hex)) {
            if (!unhex(hex, indir) || indir.size() % kIndirectionBytes != 0) indir.clear();
        } else if (iv.type() == Value::Type::Array) {
            forEachRecord(iv, [&](const Value& r) {
                const double li = num(r.get("lightingInstanceIndirection"));
                put32(indir, li < 0 ? uint64_t(uint32_t(int64_t(li))) : uint64_t(li));
                const uint32_t lo = uint32_t(num(r.get("splinedModelInstanceIndex"))) & 0xFFFF;
                const uint32_t hi = uint32_t(num(r.get("worldTransient"))) & 0xFFFF;
                put32(indir, uint64_t(lo | hi << 16));
            });
        }
    }
    const uint64_t indirTotal = indir.size() / kIndirectionBytes;

    std::vector<std::string> names;
    std::vector<uint32_t> modelFlags;
    forEachRecord(models, [&](const Value& m) {
        const Value* mm = m.get("model");
        const std::string* n = mm && mm->get("name") ? mm->get("name")->string() : nullptr;
        names.push_back(n ? *n : std::string());
        const Value* fl = m.get("flags");  // {"value": n, "name": "..."} or a plain number
        const Value* fv = fl && fl->type == Value::Type::Object ? fl->get("value") : fl;
        modelFlags.push_back(uint32_t(num(fv)));
    });
    text.close();  // everything needed is decoded

    const size_t collectionCount = coll.size() / kCollectionBytes;
    {
        // The instances drawn, reserved at once (1,096,414 on mp_donetsk: no regrowth copies of the vector).
        uint64_t total = 0;
        for (size_t c = 0; c < collectionCount; ++c) total += u32At(coll, c * kCollectionBytes + 4);
        if (total <= instanceTotal * 4 + 1024) out.reserve(size_t(total));
    }
    for (size_t c = 0; c < collectionCount; ++c) {
        const uint64_t first = u32At(coll, c * kCollectionBytes);
        const uint64_t count = u32At(coll, c * kCollectionBytes + 4);
        const uint64_t model = u32At(coll, c * kCollectionBytes + 8);
        if (model >= names.size()) {
            if (err) *err = "collection " + std::to_string(c) + " names model " + std::to_string(model) + " of " + std::to_string(names.size());
            out.clear();
            return false;
        }
        if (first + count > instanceTotal) {
            if (err) *err = "collection " + std::to_string(c) + " has instances past the export's " + std::to_string(instanceTotal);
            out.clear();
            return false;
        }
        for (uint64_t k = first; k < first + count; ++k) {
            const size_t o = size_t(k) * kInstanceBytes;
            StockModelInstance s;
            s.model = names[size_t(model)];
            s.origin = Vec3{i32At(inst, o) * kTranslationUnit, i32At(inst, o + 4) * kTranslationUnit,
                            i32At(inst, o + 8) * kTranslationUnit};
            s.axis = quatColumns(orientComponent(u16At(inst, o + 12)), orientComponent(u16At(inst, o + 14)),
                                 orientComponent(u16At(inst, o + 16)), orientComponent(u16At(inst, o + 18)));
            s.scale = f32At(inst, o + 20);
            s.index = uint32_t(k);
            s.modelFlags = modelFlags[size_t(model)];
            if (k < indirTotal) {
                s.lightingIndex = u32At(indir, size_t(k) * kIndirectionBytes);
                s.worldTransient = u16At(indir, size_t(k) * kIndirectionBytes + 6);
            }
            out.push_back(std::move(s));
        }
    }
    return true;
}

std::map<std::string, size_t> stockModelUsage(const std::vector<StockModelInstance>& instances) {
    std::map<std::string, size_t> use;
    for (const StockModelInstance& s : instances) ++use[s.model];
    return use;
}

std::vector<std::pair<std::string, std::string>> stockModelInstancesAsEntities(const StockModelInstance& inst) {
    const Vec3 ang = axisToAngles(inst.axis);
    const double scale = std::isfinite(inst.scale) && inst.scale > 0 ? inst.scale : 1.0;
    const std::string origin = fmt6(inst.origin.x) + " " + fmt6(inst.origin.y) + " " + fmt6(inst.origin.z);
    const std::string angles = fmt6(ang.x) + " " + fmt6(ang.y) + " " + fmt6(ang.z);
    return {{"classname", "misc_model"},
            {"model", inst.model},
            {"origin", origin},
            {"angles", angles},
            {"modelscale", fmt6(scale)}};
}

Vec3 axisToAngles(const std::array<Vec3, 3>& axis) {
    // m[r][c] = component r of model axis c (a rotation matrix with the axes as columns).
    double m[3][3];
    for (int c = 0; c < 3; ++c) {
        const double len = length(axis[c]);
        const Vec3 unit = len > 1e-12 ? axis[c] * (1.0 / len) : Vec3{double(c == 0), double(c == 1), double(c == 2)};
        for (int r = 0; r < 3; ++r) m[r][c] = comp(unit, r);
    }
    // Inverse of anglesToMatrix (R = Rz(yaw) * Ry(pitch) * Rx(roll)), as in matrixToAngles of prefab.cpp.
    const double sp = std::clamp(-m[2][0], -1.0, 1.0);
    const double pitch = std::asin(sp);
    double yaw, roll;
    if (std::fabs(sp) > 1 - 1e-12) {  // gimbal lock: roll is folded into yaw
        roll = 0;
        yaw = std::atan2(-m[0][1], m[1][1]);
    } else {
        roll = std::atan2(m[2][1], m[2][2]);
        yaw = std::atan2(m[1][0], m[0][0]);
    }
    return {pitch * 180.0 / kPi + 0.0, yaw * 180.0 / kPi + 0.0, roll * 180.0 / kPi + 0.0};
}

std::array<Vec3, 3> anglesToAxis(const Vec3& pitchYawRoll) {
    const Mat3 r = anglesToMatrix(pitchYawRoll);
    std::array<Vec3, 3> axis;
    for (int c = 0; c < 3; ++c) axis[c] = Vec3{r.m[0][c], r.m[1][c], r.m[2][c]};
    return axis;
}

}  // namespace iw8r
