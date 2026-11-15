#include "glb_mesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <vector>

#include "json_lite.h"

namespace iw8r {
namespace {

constexpr uint32_t kMagic = 0x46546C67;      // "glTF"
constexpr uint32_t kChunkJson = 0x4E4F534A;  // "JSON"
constexpr uint32_t kChunkBin = 0x004E4942;   // "BIN" + NUL

uint32_t rd32(const unsigned char* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

// Column-major 4x4, as glTF stores matrices.
struct Mat4 {
    std::array<double, 16> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    Mat4 operator*(const Mat4& b) const {
        Mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int row = 0; row < 4; ++row) {
                double s = 0;
                for (int k = 0; k < 4; ++k) s += m[k * 4 + row] * b.m[c * 4 + k];
                r.m[c * 4 + row] = s;
            }
        return r;
    }
    Vec3 point(const Vec3& p) const {
        return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12], m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
                m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
    }
    double det3() const {
        return m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2]) + m[8] * (m[1] * m[6] - m[5] * m[2]);
    }
    // Normal transform: inverse-transpose of the upper 3x3 (the cofactor matrix over det), renormalised.
    Vec3 normal(const Vec3& n) const {
        // Row-major A = [[a b c],[d e f],[g h i]] taken from the column-major storage.
        const double a = m[0], b = m[4], c = m[8];
        const double d = m[1], e = m[5], f = m[9];
        const double g = m[2], h = m[6], i = m[10];
        const double c00 = e * i - f * h, c01 = -(d * i - f * g), c02 = d * h - e * g;
        const double c10 = -(b * i - c * h), c11 = a * i - c * g, c12 = -(a * h - b * g);
        const double c20 = b * f - c * e, c21 = -(a * f - c * d), c22 = a * e - b * d;
        Vec3 r{c00 * n.x + c01 * n.y + c02 * n.z, c10 * n.x + c11 * n.y + c12 * n.z, c20 * n.x + c21 * n.y + c22 * n.z};
        const double len = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
        if (len > 0) r = r * (1.0 / len);
        return det3() < 0 ? -r : r;
    }
};

Mat4 nodeMatrix(const json::Value& node) {
    if (const json::Value* mat = node.get("matrix"); mat && mat->type == json::Value::Type::Array && mat->arr.size() == 16) {
        Mat4 out;
        for (size_t i = 0; i < 16; ++i) out.m[i] = mat->arr[i].num;
        return out;
    }
    double t[3] = {0, 0, 0}, q[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1};
    const auto read = [&node](const char* key, double* dst, size_t n) {
        const json::Value* v = node.get(key);
        if (v && v->type == json::Value::Type::Array && v->arr.size() == n)
            for (size_t i = 0; i < n; ++i) dst[i] = v->arr[i].num;
    };
    read("translation", t, 3);
    read("rotation", q, 4);
    read("scale", s, 3);
    // Rotation from a unit quaternion (x, y, z, w), then scale, then translation: T * R * S.
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    Mat4 r;
    r.m = {1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 0,
           2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 0,
           2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y), 0,
           0, 0, 0, 1};
    Mat4 sc;
    sc.m = {s[0], 0, 0, 0, 0, s[1], 0, 0, 0, 0, s[2], 0, 0, 0, 0, 1};
    Mat4 out = r * sc;
    out.m[12] = t[0];
    out.m[13] = t[1];
    out.m[14] = t[2];
    return out;
}

double numOr(const json::Value* v, double def) {
    return v && v->type == json::Value::Type::Number ? v->num : def;
}

class Reader {
public:
    Reader(const json::Value& doc, const std::string& bin, const GlbReadOptions& opt, ImportedModel& out, std::string* err)
        : doc_(doc), bin_(bin), opt_(opt), out_(out), err_(err) {}

    bool run() {
        const json::Value* nodes = doc_.get("nodes");
        if (!nodes || nodes->type != json::Value::Type::Array || nodes->arr.empty()) {
            // No scene graph: every mesh once, untransformed.
            if (const json::Value* ms = doc_.get("meshes"); ms && ms->type == json::Value::Type::Array)
                for (size_t i = 0; i < ms->arr.size(); ++i)
                    if (!emitMesh(int(i), Mat4{}, false)) return false;
        } else {
            for (int r : sceneRoots(nodes->arr.size()))
                if (!visit(r, Mat4{}, 0)) return false;
        }
        if (out_.meshes.empty()) return fail("no triangle primitives in the file");
        for (const ImportedMesh& m : out_.meshes)
            for (const Vec3& p : m.pos) out_.bounds.add(p);
        return true;
    }

private:
    bool fail(const std::string& msg) {
        if (err_) *err_ = msg;
        return false;
    }
    void warn(const std::string& msg) {
        if (std::find(out_.warnings.begin(), out_.warnings.end(), msg) == out_.warnings.end()) out_.warnings.push_back(msg);
    }

    std::vector<int> sceneRoots(size_t nodeCount) const {
        std::vector<int> roots;
        const json::Value* scenes = doc_.get("scenes");
        if (scenes && scenes->type == json::Value::Type::Array && !scenes->arr.empty()) {
            const size_t sceneIdx = size_t(std::max(0.0, numOr(doc_.get("scene"), 0)));
            const json::Value* scene = scenes->at(sceneIdx);
            if (const json::Value* list = scene ? scene->get("nodes") : nullptr; list && list->type == json::Value::Type::Array)
                for (const json::Value& v : list->arr) roots.push_back(int(v.num));
            return roots;
        }
        // No scenes: the nodes that no other node lists as a child.
        std::vector<bool> child(nodeCount, false);
        for (const json::Value& n : doc_.get("nodes")->arr)
            if (const json::Value* ch = n.get("children"); ch && ch->type == json::Value::Type::Array)
                for (const json::Value& c : ch->arr)
                    if (c.num >= 0 && size_t(c.num) < nodeCount) child[size_t(c.num)] = true;
        for (size_t i = 0; i < nodeCount; ++i)
            if (!child[i]) roots.push_back(int(i));
        return roots;
    }

    bool visit(int nodeIdx, const Mat4& parent, int depth) {
        if (depth > 64) return fail("node hierarchy deeper than 64 levels");
        const json::Value* nodes = doc_.get("nodes");
        const json::Value* node = nodes && nodeIdx >= 0 ? nodes->at(size_t(nodeIdx)) : nullptr;
        if (!node) return fail("node index out of range: " + std::to_string(nodeIdx));
        const Mat4 world = parent * (opt_.applyNodeTransforms ? nodeMatrix(*node) : Mat4{});
        if (const json::Value* mesh = node->get("mesh"); mesh && mesh->type == json::Value::Type::Number)
            if (!emitMesh(int(mesh->num), world, world.det3() < 0)) return false;
        if (const json::Value* ch = node->get("children"); ch && ch->type == json::Value::Type::Array)
            for (const json::Value& c : ch->arr)
                if (!visit(int(c.num), world, depth + 1)) return false;
        return true;
    }

    // Accessor elements as doubles. Normalised integers are mapped to [0,1] (unsigned) or [-1,1] (signed).
    bool readAccessor(int idx, size_t components, std::vector<double>& out, size_t& count) {
        const json::Value* accs = doc_.get("accessors");
        const json::Value* acc = accs && idx >= 0 ? accs->at(size_t(idx)) : nullptr;
        if (!acc) return fail("accessor index out of range: " + std::to_string(idx));
        const std::string who = "accessor " + std::to_string(idx);
        if (acc->get("sparse")) return fail(who + ": sparse accessors are not supported");
        const int ctype = int(numOr(acc->get("componentType"), 0));
        const json::Value* typeV = acc->get("type");
        const std::string type = typeV && typeV->string() ? *typeV->string() : "";
        const size_t ncomp = type == "SCALAR" ? 1 : type == "VEC2" ? 2 : type == "VEC3" ? 3 : type == "VEC4" ? 4 : 0;
        if (ncomp == 0 || ncomp != components) return fail(who + " has type " + type + ", expected " + std::to_string(components) + " components");
        size_t csize = 0;
        switch (ctype) {
            case 5120: case 5121: csize = 1; break;
            case 5122: case 5123: csize = 2; break;
            case 5125: case 5126: csize = 4; break;
            default: return fail(who + " has an unknown componentType " + std::to_string(ctype));
        }
        const bool normalized = acc->get("normalized") && acc->get("normalized")->b;
        count = size_t(numOr(acc->get("count"), 0));
        const size_t accOffset = size_t(numOr(acc->get("byteOffset"), 0));

        const json::Value* views = doc_.get("bufferViews");
        const json::Value* view = views ? views->at(size_t(std::max(0.0, numOr(acc->get("bufferView"), -1)))) : nullptr;
        if (!view) return fail(who + " has no bufferView");
        const size_t viewOffset = size_t(numOr(view->get("byteOffset"), 0));
        const size_t viewLength = size_t(numOr(view->get("byteLength"), 0));
        const size_t elem = csize * ncomp;
        const size_t stride = size_t(numOr(view->get("byteStride"), double(elem)));
        if (stride < elem) return fail(who + ": stride is smaller than its element");
        if (viewOffset + viewLength > bin_.size()) return fail(who + ": buffer view runs past the BIN chunk");
        const size_t start = viewOffset + accOffset;
        if (count > 0 && start + (count - 1) * stride + elem > viewOffset + viewLength)
            return fail(who + " reads past its buffer view");

        out.resize(count * ncomp);
        const auto* base = reinterpret_cast<const unsigned char*>(bin_.data());
        for (size_t i = 0; i < count; ++i)
            for (size_t c = 0; c < ncomp; ++c) {
                const unsigned char* p = base + start + i * stride + c * csize;
                double v = 0;
                switch (ctype) {
                    case 5126: {
                        const uint32_t u = rd32(p);
                        float f;
                        std::memcpy(&f, &u, 4);
                        v = f;
                        break;
                    }
                    case 5125: v = double(rd32(p)); break;
                    case 5123: v = double(uint16_t(p[0] | p[1] << 8)); break;
                    case 5121: v = double(p[0]); break;
                    case 5122: v = double(int16_t(uint16_t(p[0] | p[1] << 8))); break;
                    case 5120: v = double(int8_t(p[0])); break;
                }
                if (normalized && ctype != 5126) {
                    const bool isSigned = ctype == 5120 || ctype == 5122;
                    const double maxv = (ctype == 5121) ? 255.0 : (ctype == 5120) ? 127.0 : (ctype == 5123) ? 65535.0 : 32767.0;
                    v = isSigned ? std::max(-1.0, v / maxv) : v / maxv;
                }
                out[i * ncomp + c] = v;
            }
        return true;
    }

    bool emitMesh(int meshIdx, const Mat4& world, bool mirrored) {
        const json::Value* mesh = meshIdx >= 0 && doc_.get("meshes") ? doc_.get("meshes")->at(size_t(meshIdx)) : nullptr;
        if (!mesh) return fail("mesh index out of range: " + std::to_string(meshIdx));
        const json::Value* prims = mesh->get("primitives");
        if (!prims || prims->type != json::Value::Type::Array) return true;
        for (const json::Value& prim : prims->arr) {
            if (numOr(prim.get("mode"), 4) != 4) {
                warn("skipped primitives that are not TRIANGLES");
                continue;
            }
            const json::Value* attrs = prim.get("attributes");
            const json::Value* pos = attrs ? attrs->get("POSITION") : nullptr;
            if (!pos) return fail("primitive without POSITION");

            ImportedMesh m;
            std::vector<double> v;
            size_t n = 0;
            if (!readAccessor(int(pos->num), 3, v, n)) return false;
            m.pos.resize(n);
            for (size_t i = 0; i < n; ++i) m.pos[i] = world.point(Vec3{v[i * 3], v[i * 3 + 1], v[i * 3 + 2]}) * opt_.scale;

            if (const json::Value* nr = attrs->get("NORMAL")) {
                size_t nn = 0;
                if (!readAccessor(int(nr->num), 3, v, nn)) return false;
                if (nn != n) return fail("NORMAL count differs from POSITION");
                m.normal.resize(n);
                for (size_t i = 0; i < n; ++i) m.normal[i] = world.normal(Vec3{v[i * 3], v[i * 3 + 1], v[i * 3 + 2]});
            } else {
                warn("no NORMAL attribute: normals set to (0,0,1)");
                m.normal.assign(n, Vec3{0, 0, 1});
            }
            if (const json::Value* tc = attrs->get("TEXCOORD_0")) {
                size_t nt = 0;
                if (!readAccessor(int(tc->num), 2, v, nt)) return false;
                if (nt != n) return fail("TEXCOORD_0 count differs from POSITION");
                m.uv.resize(n);
                for (size_t i = 0; i < n; ++i) m.uv[i] = {v[i * 2], v[i * 2 + 1]};
            } else {
                warn("no TEXCOORD_0 attribute: uv set to (0,0)");
                m.uv.assign(n, std::array<double, 2>{0, 0});
            }

            if (const json::Value* col = attrs->get("COLOR_0")) {
                // VEC4 or VEC3 (alpha 1), float or normalised bytes / shorts.
                const json::Value* accs = doc_.get("accessors");
                const json::Value* acc = accs ? accs->at(size_t(std::max(0.0, col->num))) : nullptr;
                const json::Value* ty = acc ? acc->get("type") : nullptr;
                const size_t comps = ty && ty->string() && *ty->string() == "VEC3" ? 3 : 4;
                size_t nc = 0;
                if (!readAccessor(int(col->num), comps, v, nc)) return false;
                if (nc != n) return fail("COLOR_0 count differs from POSITION");
                m.color.resize(n);
                for (size_t i = 0; i < n; ++i)
                    for (size_t c = 0; c < 4; ++c) m.color[i][c] = c < comps ? float(v[i * comps + c]) : 1.0f;
            }
            if (const json::Value* sv = attrs->get("_SELFVIS")) {
                // ACTS: the vertex's u32 as VEC4 UNSIGNED_BYTE, not normalised (research 105 section 4).
                size_t ns = 0;
                if (!readAccessor(int(sv->num), 4, v, ns)) return false;
                if (ns != n) return fail("_SELFVIS count differs from POSITION");
                m.selfVis.resize(n);
                for (size_t i = 0; i < n; ++i) {
                    const uint32_t word = uint32_t(v[i * 4]) | uint32_t(v[i * 4 + 1]) << 8 | uint32_t(v[i * 4 + 2]) << 16 |
                                          uint32_t(v[i * 4 + 3]) << 24;
                    const std::array<float, 4> d = decodeSelfVisibility(word);
                    const double amount = std::sqrt(double(d[0]) * d[0] + double(d[1]) * d[1] + double(d[2]) * d[2]);
                    const Vec3 dir = amount > 0 ? world.normal(Vec3{d[0] / amount, d[1] / amount, d[2] / amount}) : Vec3{0, 0, 0};
                    m.selfVis[i] = {float(dir.x * amount), float(dir.y * amount), float(dir.z * amount), d[3]};
                }
            }
            if (const json::Value* ex = prim.get("extras"))
                if (const json::Value* sf = ex->get("sourceFlags")) m.sourceFlags = uint32_t(std::max(0.0, sf->num));
            if (const json::Value* ind = prim.get("indices")) {
                size_t ni = 0;
                std::vector<double> iv;
                if (!readAccessor(int(ind->num), 1, iv, ni)) return false;
                m.idx.resize(ni);
                for (size_t i = 0; i < ni; ++i) m.idx[i] = uint32_t(iv[i]);
            } else {
                m.idx.resize(n);
                for (size_t i = 0; i < n; ++i) m.idx[i] = uint32_t(i);
            }
            if (m.idx.size() % 3 != 0) return fail("index count is not a multiple of 3");
            for (uint32_t i : m.idx)
                if (i >= n) return fail("index " + std::to_string(i) + " is past the vertex count " + std::to_string(n));
            if (mirrored)
                for (size_t t = 0; t + 2 < m.idx.size(); t += 3) std::swap(m.idx[t + 1], m.idx[t + 2]);
            out_.meshes.push_back(std::move(m));
        }
        return true;
    }

    const json::Value& doc_;
    const std::string& bin_;
    const GlbReadOptions& opt_;
    ImportedModel& out_;
    std::string* err_;
};

}  // namespace

std::array<float, 4> decodeSelfVisibility(uint32_t word) {
    double x = double(word & 511u) / 511.0 * 2.0 - 1.0, y = double((word >> 9) & 511u) / 511.0 * 2.0 - 1.0;
    double z = 1.0 - std::fabs(x) - std::fabs(y);
    if (z < 0) {  // the lower hemisphere, folded
        const double fx = (1.0 - std::fabs(y)) * (x < 0 ? -1.0 : 1.0), fy = (1.0 - std::fabs(x)) * (y < 0 ? -1.0 : 1.0);
        x = fx, y = fy;
    }
    const double len = std::sqrt(x * x + y * y + z * z);
    const double amount = double((word >> 18) & 127u) / 127.0;
    const double k = len > 0 ? amount / len : 0.0;
    return {float(x * k), float(y * k), float(z * k), float(double((word >> 25) & 127u) / 127.0)};
}

bool parseGlb(const std::string& bytes, ImportedModel& out, std::string* err, const GlbReadOptions& opt) {
    const auto fail = [err](const std::string& msg) {
        if (err) *err = msg;
        return false;
    };
    if (bytes.size() < 20) return fail("file too small for a GLB header");
    const auto* p = reinterpret_cast<const unsigned char*>(bytes.data());
    if (rd32(p) != kMagic) return fail("not a GLB file (bad magic)");
    if (rd32(p + 4) != 2) return fail("unsupported glTF version " + std::to_string(rd32(p + 4)));
    const size_t length = rd32(p + 8);
    if (length != bytes.size())
        return fail("GLB length field " + std::to_string(length) + " != file size " + std::to_string(bytes.size()));

    std::string jsonText, bin;
    size_t off = 12;
    while (off + 8 <= length) {
        const size_t clen = rd32(p + off);
        const uint32_t ctype = rd32(p + off + 4);
        off += 8;
        if (clen > length - off) return fail("chunk runs past the end of the file");
        if (ctype == kChunkJson && jsonText.empty()) jsonText.assign(bytes.data() + off, clen);
        else if (ctype == kChunkBin && bin.empty()) bin.assign(bytes.data() + off, clen);
        off += clen;
    }
    if (jsonText.empty()) return fail("GLB has no JSON chunk");

    std::string perr;
    const std::optional<json::Value> doc = json::parse(jsonText, &perr);
    if (!doc) return fail("GLB JSON: " + perr);
    if (const json::Value* buffers = doc->get("buffers"); buffers && buffers->arr.size() > 1)
        return fail("only one buffer (the GLB BIN chunk) is supported");
    if (const json::Value* buffers = doc->get("buffers"); buffers && !buffers->arr.empty() && buffers->arr[0].get("uri"))
        return fail("external buffers are not supported");

    out.bounds = AABB{};
    out.meshes.clear();
    out.warnings.clear();
    Reader reader(*doc, bin, opt, out, err);
    return reader.run();
}

bool readGlb(const std::string& path, ImportedModel& out, std::string* err, const GlbReadOptions& opt) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (err) *err = "cannot open " + path;
        return false;
    }
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string stem = path;
    if (const size_t slash = stem.find_last_of("/\\"); slash != std::string::npos) stem = stem.substr(slash + 1);
    out.name = stem.substr(0, stem.find('.'));
    return parseGlb(bytes, out, err, opt);
}

}  // namespace iw8r
