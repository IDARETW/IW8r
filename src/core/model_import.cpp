#include "model_import.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include "json_lite.h"

namespace iw8r {
namespace fs = std::filesystem;

namespace {

constexpr std::size_t kMaxSurfacesPerModel = 255;  // Replay static models index at most 255 surfaces over all LODs
constexpr std::size_t kMaxLods = 6;
constexpr std::size_t kMaxSurfaceVerts = 65535;
constexpr std::size_t kMaxSurfaceTris = 65535;

void setErr(std::string* err, const std::string& message) {
    if (err) *err = message;
}

std::string trimmed(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

// Lower-case [a-z0-9_-]; anything else becomes '_'. Used for model names taken from file stems.
std::string sanitizeName(const std::string& raw) {
    std::string s;
    for (const char c : raw) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalnum(u))
            s.push_back(static_cast<char>(std::tolower(u)));
        else
            s.push_back(c == '-' ? '-' : '_');
    }
    return s.empty() ? std::string("model") : s;
}

bool isSafeModelName(const std::string& s) {
    if (s.empty()) return false;
    return std::all_of(s.begin(), s.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
    });
}

// Material names are file paths under materials/ (e.g. "iw8r/grey"): safe characters, no absolute or ".." parts.
bool isSafeMaterialName(const std::string& s) {
    if (s.empty() || s.front() == '/' || s.find("..") != std::string::npos || s.find('\\') != std::string::npos ||
        s.find(':') != std::string::npos)
        return false;
    return std::all_of(s.begin(), s.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '/' || c == '.';
    });
}

// Maps a usemtl name to the characters a materials/ path may use (lower case, others become '_').
std::string materialFileName(const std::string& raw) {
    std::string s;
    for (const char c : raw) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalnum(u))
            s.push_back(static_cast<char>(std::tolower(u)));
        else
            s.push_back((c == '_' || c == '-' || c == '/' || c == '.') ? c : '_');
    }
    return s;
}

// ---------------------------------------------------------------------------------------------------------------
// OBJ parsing
// ---------------------------------------------------------------------------------------------------------------

struct Corner {
    long v = -1, vt = -1, vn = -1;  // 0-based; -1 when the face corner omits the index
};

struct Triangle {
    Corner c[3];
    std::size_t group = 0;
};

struct ObjData {
    std::vector<Vec3> pos, nrm;
    std::vector<std::array<double, 2>> uv;
    std::vector<Triangle> tris;
    std::vector<std::string> groups;  // material names, in order of first use
    std::map<std::string, std::size_t> groupIndex;
    std::size_t current = SIZE_MAX;   // current group index (SIZE_MAX before the first usemtl or face)
    std::size_t skippedFaces = 0;
};

std::size_t groupOf(ObjData& d, const std::string& name) {
    const auto found = d.groupIndex.find(name);
    if (found != d.groupIndex.end()) return found->second;
    d.groups.push_back(name);
    d.groupIndex.emplace(name, d.groups.size() - 1);
    return d.groups.size() - 1;
}

// Resolves a 1-based (or negative, relative to the end) OBJ index into a 0-based index of `count` elements.
bool resolveIndex(const std::string& token, std::size_t count, long& out) {
    long value = 0;
    try {
        std::size_t used = 0;
        value = std::stol(token, &used);
        if (used != token.size()) return false;
    } catch (...) {
        return false;
    }
    const long n = static_cast<long>(count);
    if (value > 0 && value <= n)
        out = value - 1;
    else if (value < 0 && -value <= n)
        out = n + value;
    else
        return false;
    return true;
}

bool parseFaceToken(const std::string& token, const ObjData& d, Corner& corner) {
    const auto s1 = token.find('/');
    const std::string pv = token.substr(0, s1);
    std::string pt, pn;
    if (s1 != std::string::npos) {
        const auto s2 = token.find('/', s1 + 1);
        pt = token.substr(s1 + 1, s2 == std::string::npos ? std::string::npos : s2 - s1 - 1);
        if (s2 != std::string::npos) pn = token.substr(s2 + 1);
    }
    if (!resolveIndex(pv, d.pos.size(), corner.v)) return false;
    if (!pt.empty() && !resolveIndex(pt, d.uv.size(), corner.vt)) return false;
    if (!pn.empty() && !resolveIndex(pn, d.nrm.size(), corner.vn)) return false;
    return true;
}

// map_Kd lines of the mtllib files, keyed by material name. Textures are hints only.
std::map<std::string, std::string> readMtlHints(const fs::path& objDir, const std::string& names,
                                                std::vector<std::string>& warnings) {
    std::map<std::string, std::string> hints;
    std::istringstream list(names);
    std::string file;
    while (list >> file) {
        std::ifstream in(objDir / file);
        if (!in) {
            warnings.push_back("mtllib '" + file + "' not found; no texture hints");
            continue;
        }
        std::string line, current;
        while (std::getline(in, line)) {
            std::istringstream row(trimmed(line));
            std::string key;
            row >> key;
            if (key == "newmtl") {
                std::getline(row, current);
                current = trimmed(current);
            } else if (key == "map_Kd" && !current.empty()) {
                std::string texture;
                std::string word;
                while (row >> word)
                    if (word.front() != '-') texture = word;  // skips options such as -s 1 1 1
                if (!texture.empty()) hints.emplace(current, texture);
            }
        }
    }
    return hints;
}

}  // namespace

bool importObj(const std::string& path, ImportedModel& out, std::string* err) {
    std::ifstream in(path);
    if (!in) {
        setErr(err, "cannot open OBJ file: " + path);
        return false;
    }
    const fs::path objPath(path);
    ObjData d;
    std::vector<std::string> mtlFiles;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(in, line)) {
        ++lineNumber;
        const auto hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        std::istringstream row(trimmed(line));
        std::string key;
        if (!(row >> key)) continue;
        const auto fail = [&](const std::string& why) {
            setErr(err, path + ":" + std::to_string(lineNumber) + ": " + why);
            return false;
        };
        if (key == "v") {
            double x, y, z;
            if (!(row >> x >> y >> z)) return fail("vertex needs three coordinates");
            d.pos.emplace_back(x, y, z);
        } else if (key == "vn") {
            double x, y, z;
            if (!(row >> x >> y >> z)) return fail("normal needs three components");
            d.nrm.push_back(normalize(Vec3(x, y, z)));
        } else if (key == "vt") {
            double u = 0, v = 0;
            if (!(row >> u >> v)) return fail("texture coordinate needs two components");
            d.uv.push_back({u, v});
        } else if (key == "usemtl") {
            std::string name;
            std::getline(row, name);
            d.current = groupOf(d, trimmed(name));
        } else if (key == "mtllib") {
            std::string names;
            std::getline(row, names);
            mtlFiles.push_back(trimmed(names));
        } else if (key == "f") {
            std::vector<Corner> corners;
            std::string token;
            while (row >> token) {
                Corner corner;
                if (!parseFaceToken(token, d, corner)) return fail("bad face index '" + token + "'");
                corners.push_back(corner);
            }
            if (corners.size() < 3) {
                ++d.skippedFaces;
                continue;
            }
            if (d.current == SIZE_MAX) d.current = groupOf(d, "");
            for (std::size_t i = 1; i + 1 < corners.size(); ++i)
                d.tris.push_back({{corners[0], corners[i], corners[i + 1]}, d.current});
        }
        // o, g, s, l, p and other keywords carry nothing this importer needs.
    }
    if (d.tris.empty()) {
        setErr(err, path + ": no faces");
        return false;
    }

    out = {};
    out.name = sanitizeName(objPath.stem().string());
    if (d.skippedFaces)
        out.warnings.push_back(std::to_string(d.skippedFaces) + " face(s) with fewer than 3 corners skipped");

    // Hints from the material libraries.
    std::map<std::string, std::string> hints;
    for (const auto& names : mtlFiles) {
        auto found = readMtlHints(objPath.parent_path(), names, out.warnings);
        hints.insert(found.begin(), found.end());
    }
    for (const auto& name : d.groups) {
        const auto hint = hints.find(name);
        if (hint != hints.end())
            out.warnings.push_back("material '" + name + "' names texture '" + hint->second +
                                   "' (map_Kd): textures are not imported; a grey material is written");
    }

    // Generated normals: area-weighted average of the triangles that touch each position, used for corners without vn.
    bool anyGenerated = false;
    for (const auto& tri : d.tris)
        for (const auto& corner : tri.c)
            if (corner.vn < 0) anyGenerated = true;
    std::vector<Vec3> generated;
    if (anyGenerated) {
        std::vector<Vec3> accum(d.pos.size());
        for (const auto& tri : d.tris) {
            const Vec3 a = d.pos[tri.c[0].v], b = d.pos[tri.c[1].v], c = d.pos[tri.c[2].v];
            const Vec3 face = cross(b - a, c - a);  // not normalised: area weighting
            for (const auto& corner : tri.c) accum[corner.v] += face;
        }
        generated.reserve(accum.size());
        for (const auto& sum : accum) generated.push_back(length(sum) > 1e-12 ? normalize(sum) : Vec3(0, 0, 1));
        std::size_t count = 0;
        for (const auto& tri : d.tris)
            for (const auto& corner : tri.c)
                if (corner.vn < 0) ++count;
        out.warnings.push_back("generated " + std::to_string(count) +
                               " vertex normal(s) (the file has no vn for them)");
    }
    std::size_t missingUv = 0;
    for (const auto& tri : d.tris)
        for (const auto& corner : tri.c)
            if (corner.vt < 0) ++missingUv;
    if (missingUv) out.warnings.push_back(std::to_string(missingUv) + " corner(s) without vt: uv set to (0,0)");

    // One mesh per material group (in first-use order); vertices deduplicated per (group, v, vt, vn).
    std::vector<ImportedMesh> meshes(d.groups.size());
    std::map<std::array<long, 4>, uint32_t> vertexIndex;
    for (std::size_t g = 0; g < d.groups.size(); ++g) meshes[g].material = d.groups[g];
    for (const auto& tri : d.tris) {
        for (const auto& corner : tri.c) {
            const std::array<long, 4> key{static_cast<long>(tri.group), corner.v, corner.vt, corner.vn};
            auto found = vertexIndex.find(key);
            if (found == vertexIndex.end()) {
                ImportedMesh& mesh = meshes[tri.group];
                const uint32_t index = static_cast<uint32_t>(mesh.pos.size());
                mesh.pos.push_back(d.pos[corner.v]);
                mesh.normal.push_back(corner.vn >= 0 ? d.nrm[corner.vn] : generated[corner.v]);
                mesh.uv.push_back(corner.vt >= 0 ? d.uv[corner.vt] : std::array<double, 2>{0, 0});
                out.bounds.add(d.pos[corner.v]);
                found = vertexIndex.emplace(key, index).first;
            }
            meshes[tri.group].idx.push_back(found->second);
        }
    }
    for (auto& mesh : meshes)
        if (!mesh.idx.empty()) out.meshes.push_back(std::move(mesh));
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// glTF 2.0 import
// ---------------------------------------------------------------------------------------------------------------

namespace {

constexpr uint32_t kGlbChunkJson = 0x4E4F534A;  // "JSON"
constexpr uint32_t kGlbChunkBin = 0x004E4942;   // "BIN\0"
constexpr int kCollisionTag = -1;               // node name collision* / UCX_*
constexpr int kNoTag = -2;                      // node name says nothing: inherit the parent's tag
constexpr int kMaxRenderLod = 5;                // LOD 0 .. 5, six in total
constexpr int kMaxNodeDepth = 64;

uint32_t le32(const unsigned char* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

std::string lowered(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

double numberOr(const json::Value* v, double def) {
    return v && v->type == json::Value::Type::Number ? v->num : def;
}

// Non-negative integer member; the default when absent or not a plausible index or size.
std::size_t sizeOr(const json::Value* v, std::size_t def) {
    if (!v || v->type != json::Value::Type::Number || !(v->num >= 0) || v->num > 9e15) return def;
    return static_cast<std::size_t>(v->num);
}

bool indexOf(const json::Value* v, std::size_t& out) {
    if (!v || v->type != json::Value::Type::Number || !(v->num >= 0) || v->num > 1e9) return false;
    out = static_cast<std::size_t>(v->num);
    return true;
}

// Column-major 4x4, as glTF stores matrices.
struct Affine {
    std::array<double, 16> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    Affine operator*(const Affine& b) const {
        Affine r;
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
    // Normals follow the surface: the cofactor matrix (det * inverse transpose) times n, negated when det < 0.
    Vec3 normal(const Vec3& n) const {
        const double a = m[0], b = m[4], c = m[8];
        const double d = m[1], e = m[5], f = m[9];
        const double g = m[2], h = m[6], i = m[10];
        const Vec3 r{(e * i - f * h) * n.x - (d * i - f * g) * n.y + (d * h - e * g) * n.z,
                     -(b * i - c * h) * n.x + (a * i - c * g) * n.y - (a * h - b * g) * n.z,
                     (b * f - c * e) * n.x - (a * f - c * d) * n.y + (a * e - b * d) * n.z};
        return normalize(det3() < 0 ? -r : r);
    }
};

// Node transform: its matrix, or T * R * S from translation, rotation quaternion (x, y, z, w) and scale.
Affine nodeTransform(const json::Value& node) {
    const auto numbers = [&node](const char* key, std::size_t n, double* dst) {
        const json::Value* v = node.get(key);
        if (!v || v->type != json::Value::Type::Array || v->arr.size() != n) return false;
        for (std::size_t i = 0; i < n; ++i) {
            if (v->arr[i].type != json::Value::Type::Number) return false;
            dst[i] = v->arr[i].num;
        }
        return true;
    };
    Affine out;
    double matrix[16];
    if (numbers("matrix", 16, matrix)) {
        std::copy(matrix, matrix + 16, out.m.begin());
        return out;
    }
    double t[3] = {0, 0, 0}, q[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1};
    numbers("translation", 3, t);
    numbers("rotation", 4, q);
    numbers("scale", 3, s);
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    Affine rot;
    rot.m = {1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 0,
             2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 0,
             2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y), 0,
             0, 0, 0, 1};
    Affine scale;
    scale.m = {s[0], 0, 0, 0, 0, s[1], 0, 0, 0, 0, s[2], 0, 0, 0, 0, 1};
    out = rot * scale;
    out.m[12] = t[0];
    out.m[13] = t[1];
    out.m[14] = t[2];
    return out;
}

// LOD and collision tag from a node name: collision* and UCX_* (case-insensitive) -> kCollisionTag; *_lod<N> -> N;
// anything else -> kNoTag.
int nodeTag(const std::string& name) {
    const std::string lower = lowered(name);
    if (lower.rfind("collision", 0) == 0 || lower.rfind("ucx_", 0) == 0) return kCollisionTag;
    const std::size_t at = lower.rfind("_lod");
    if (at == std::string::npos || at + 4 >= lower.size()) return kNoTag;
    long value = 0;
    for (std::size_t i = at + 4; i < lower.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(lower[i]))) return kNoTag;
        if (value < 1000) value = value * 10 + (lower[i] - '0');
    }
    return static_cast<int>(value);
}

// Area-weighted vertex normals from the triangles (idx), summed per vertex and normalised; (0,0,1) when none.
std::vector<Vec3> areaWeightedNormals(const std::vector<Vec3>& pos, const std::vector<uint32_t>& idx) {
    std::vector<Vec3> accum(pos.size());
    for (std::size_t t = 0; t + 2 < idx.size(); t += 3) {
        const Vec3 a = pos[idx[t]], b = pos[idx[t + 1]], c = pos[idx[t + 2]];
        const Vec3 face = cross(b - a, c - a);  // length is twice the area: the area weighting
        accum[idx[t]] += face;
        accum[idx[t + 1]] += face;
        accum[idx[t + 2]] += face;
    }
    for (Vec3& s : accum) s = length(s) > 1e-12 ? normalize(s) : Vec3(0, 0, 1);
    return accum;
}

bool base64Decode(const std::string& in, std::string& out) {
    int bits = -8;  // bits of `val` not yet written, minus 8
    unsigned val = 0;
    for (const char c : in) {
        int d = 0;
        if (c >= 'A' && c <= 'Z')
            d = c - 'A';
        else if (c >= 'a' && c <= 'z')
            d = c - 'a' + 26;
        else if (c >= '0' && c <= '9')
            d = c - '0' + 52;
        else if (c == '+')
            d = 62;
        else if (c == '/')
            d = 63;
        else if (c == '=')
            break;
        else if (std::isspace(static_cast<unsigned char>(c)))
            continue;
        else
            return false;
        val = ((val << 6) | static_cast<unsigned>(d)) & 0xFFFFFFu;
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<char>((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return true;
}

// %XX escapes in a relative buffer URI.
std::string percentDecode(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            out.push_back(static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16)));
            i += 2;
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

// Splits a GLB container into its JSON chunk and BIN chunk (the BIN chunk is empty when absent).
bool splitGlb(const std::string& bytes, std::string& jsonText, std::string& bin, std::string& err) {
    if (bytes.size() < 20) {
        err = "file too small for a GLB header";
        return false;
    }
    const auto* p = reinterpret_cast<const unsigned char*>(bytes.data());
    if (le32(p + 4) != 2) {
        err = "unsupported glTF version " + std::to_string(le32(p + 4)) + " in GLB header";
        return false;
    }
    const std::size_t length = le32(p + 8);
    if (length < 20 || length > bytes.size()) {
        err = "GLB length field " + std::to_string(length) + " does not fit the file (" + std::to_string(bytes.size()) +
              " bytes)";
        return false;
    }
    std::size_t off = 12;
    while (off + 8 <= length) {
        const std::size_t clen = le32(p + off);
        const uint32_t ctype = le32(p + off + 4);
        off += 8;
        if (clen > length - off) {
            err = "GLB chunk runs past the end of the file";
            return false;
        }
        if (ctype == kGlbChunkJson && jsonText.empty())
            jsonText.assign(bytes.data() + off, clen);
        else if (ctype == kGlbChunkBin && bin.empty())
            bin.assign(bytes.data() + off, clen);
        off += clen;
    }
    if (jsonText.empty()) {
        err = "GLB has no JSON chunk";
        return false;
    }
    return true;
}

// Decodes every buffer: base64 data: URIs, external files next to the glTF, or the GLB BIN chunk (buffer 0).
bool loadBuffers(const json::Value& doc, const fs::path& dir, const std::string* glbBin, std::vector<std::string>& out,
                 std::string& err) {
    const json::Value* buffers = doc.get("buffers");
    if (!buffers) return true;
    if (buffers->type != json::Value::Type::Array) {
        err = "buffers is not an array";
        return false;
    }
    for (std::size_t i = 0; i < buffers->arr.size(); ++i) {
        const json::Value& b = buffers->arr[i];
        const std::string who = "buffer " + std::to_string(i);
        const std::size_t want = sizeOr(b.get("byteLength"), 0);
        const json::Value* uriV = b.get("uri");
        std::string data;
        if (!uriV) {
            if (i != 0 || !glbBin) {
                err = who + " has no uri";
                return false;
            }
            data = *glbBin;
        } else if (!uriV->string()) {
            err = who + ": uri is not a string";
            return false;
        } else {
            const std::string& uri = *uriV->string();
            if (uri.compare(0, 5, "data:") == 0) {
                const std::size_t comma = uri.find(',');
                if (comma == std::string::npos || uri.substr(0, comma).find(";base64") == std::string::npos) {
                    err = who + ": only base64 data: URIs are supported";
                    return false;
                }
                if (!base64Decode(uri.substr(comma + 1), data)) {
                    err = who + ": the base64 data URI is malformed";
                    return false;
                }
            } else {
                const fs::path file = dir / percentDecode(uri);
                std::ifstream in(file, std::ios::binary);
                if (!in) {
                    err = who + ": cannot open " + file.string();
                    return false;
                }
                data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            }
        }
        if (data.size() < want) {
            err = who + " holds fewer bytes than its byteLength";
            return false;
        }
        out.push_back(std::move(data));
    }
    return true;
}

struct GltfBucket {
    std::map<std::string, std::size_t> byMaterial;  // material name -> index into meshes
    std::vector<ImportedMesh> meshes;
};

bool hasTriangles(const GltfBucket& b) {
    for (const ImportedMesh& m : b.meshes)
        if (m.idx.size() >= 3) return true;
    return false;
}

// Walks the default scene and collects the TRIANGLES primitives into one bucket per tag (0 = LOD 0, 1..5 = LODs,
// kCollisionTag = collision), one mesh per material name inside each bucket.
class GltfImporter {
public:
    explicit GltfImporter(const json::Value& doc, std::vector<std::string> buffers)
        : doc_(doc), buffers_(std::move(buffers)) {}

    const std::string& error() const { return err_; }

    bool readScene() {
        const json::Value* nodes = doc_.get("nodes");
        if (!nodes || nodes->type != json::Value::Type::Array || nodes->arr.empty())
            return fail("the file has no nodes, so there is no geometry to import");
        std::vector<std::size_t> roots;
        const json::Value* scenes = doc_.get("scenes");
        if (scenes && scenes->type == json::Value::Type::Array && !scenes->arr.empty()) {
            std::size_t sceneIndex = 0;
            if (doc_.get("scene") && !indexOf(doc_.get("scene"), sceneIndex)) return fail("the scene index is malformed");
            const json::Value* scene = scenes->at(sceneIndex);
            if (!scene) return fail("the scene index " + std::to_string(sceneIndex) + " does not exist");
            if (const json::Value* list = scene->get("nodes"); list && list->type == json::Value::Type::Array)
                for (const json::Value& n : list->arr) {
                    std::size_t ni = 0;
                    if (!indexOf(&n, ni)) return fail("a scene root node index is malformed");
                    roots.push_back(ni);
                }
        } else {
            // No scenes: every node that no other node lists as a child.
            std::vector<bool> child(nodes->arr.size(), false);
            for (const json::Value& n : nodes->arr)
                if (const json::Value* ch = n.get("children"); ch && ch->type == json::Value::Type::Array)
                    for (const json::Value& c : ch->arr) {
                        std::size_t ci = 0;
                        if (indexOf(&c, ci) && ci < child.size()) child[ci] = true;
                    }
            for (std::size_t i = 0; i < child.size(); ++i)
                if (!child[i]) roots.push_back(i);
        }
        for (const std::size_t r : roots)
            if (!visit(r, Affine{}, 0, 0)) return false;
        return true;
    }

    // Fills out from the buckets. The buckets are consumed. Fails when there is no triangle or no LOD 0.
    bool build(const std::string& name, ImportedModel& out, std::string& err) {
        bool any = false;
        for (const auto& entry : buckets_)
            if (hasTriangles(entry.second)) any = true;
        if (!any) {
            err = "the file has no TRIANGLES primitives to import";
            return false;
        }
        const auto render0 = buckets_.find(0);
        if (render0 == buckets_.end() || !hasTriangles(render0->second)) {
            err = "the file has no LOD 0 geometry: every mesh node is named *_lodN or collision*";
            return false;
        }

        ImportedModel model;
        model.name = name;
        model.meshes = takeMeshes(render0->second);
        model.bounds = boundsOf(model.meshes);

        bool previous = true;  // LOD 0 exists
        for (int lod = 1; lod <= kMaxRenderLod; ++lod) {
            const auto it = buckets_.find(lod);
            if (it == buckets_.end() || !hasTriangles(it->second)) {
                previous = false;
                continue;
            }
            if (!previous)
                warnings_.push_back("no *_lod" + std::to_string(lod - 1) + " node before *_lod" + std::to_string(lod) +
                                    ": the LOD levels are kept in order");
            ImportedModel lodModel;
            lodModel.name = name;
            lodModel.meshes = takeMeshes(it->second);
            lodModel.bounds = boundsOf(lodModel.meshes);
            model.lods.push_back(std::move(lodModel));
            previous = true;
        }

        if (const auto col = buckets_.find(kCollisionTag); col != buckets_.end() && hasTriangles(col->second)) {
            auto collision = std::make_shared<ImportedModel>();
            collision->name = name;
            collision->meshes = takeMeshes(col->second);
            collision->bounds = boundsOf(collision->meshes);
            model.collision = collision;
        }

        for (const auto& [mode, count] : skippedModes_)
            warnings_.push_back("skipped " + std::to_string(count) + " primitive(s) with mode " + std::to_string(mode) +
                                " (only TRIANGLES, mode 4, is imported)");
        if (generatedNormals_)
            warnings_.push_back("generated " + std::to_string(generatedNormals_) +
                                " vertex normal(s) (the file has no NORMAL for them)");
        if (missingUv_)
            warnings_.push_back(std::to_string(missingUv_) + " vertex(es) without TEXCOORD_0: uv set to (0,0)");
        model.warnings = warnings_;
        out = std::move(model);
        return true;
    }

private:
    bool fail(std::string message) {
        err_ = std::move(message);
        return false;
    }

    // Accessor elements as doubles (normalised integers mapped to [0,1] or [-1,1]). integerOnly rejects floats.
    bool readAccessor(const json::Value* ref, std::size_t ncomp, bool integerOnly, std::vector<double>& out,
                      std::size_t& count, const std::string& what) {
        std::size_t index = 0;
        const json::Value* accessors = doc_.get("accessors");
        const json::Value* acc = indexOf(ref, index) && accessors ? accessors->at(index) : nullptr;
        if (!acc) return fail(what + ": the accessor index is missing or out of range");
        if (acc->get("sparse")) return fail(what + ": sparse accessors are not supported");
        const json::Value* typeV = acc->get("type");
        const std::string type = typeV && typeV->string() ? *typeV->string() : std::string();
        const std::size_t have = type == "SCALAR" ? 1 : type == "VEC2" ? 2 : type == "VEC3" ? 3 : type == "VEC4" ? 4 : 0;
        if (have != ncomp) return fail(what + ": accessor type " + type + " does not fit this attribute");
        const int ctype = static_cast<int>(numberOr(acc->get("componentType"), 0));
        std::size_t csize = 0;
        switch (ctype) {
            case 5120: case 5121: csize = 1; break;
            case 5122: case 5123: csize = 2; break;
            case 5125: case 5126: csize = 4; break;
            default: return fail(what + ": unknown componentType " + std::to_string(ctype));
        }
        if (integerOnly && ctype == 5126) return fail(what + ": must be an integer type");
        const json::Value* normV = acc->get("normalized");
        const bool normalized = normV && normV->type == json::Value::Type::Bool && normV->b;
        count = sizeOr(acc->get("count"), 0);
        if (count > 100000000) return fail(what + ": accessor count is too large");

        std::size_t viewIndex = 0;
        const json::Value* views = doc_.get("bufferViews");
        const json::Value* view = indexOf(acc->get("bufferView"), viewIndex) && views ? views->at(viewIndex) : nullptr;
        if (!view) return fail(what + ": accessor without a valid bufferView (zero-filled accessors are not supported)");
        std::size_t bufferIndex = 0;
        if (view->get("buffer") && !indexOf(view->get("buffer"), bufferIndex))
            return fail(what + ": bufferView buffer index is malformed");
        if (bufferIndex >= buffers_.size()) return fail(what + ": bufferView names a buffer that does not exist");
        const std::string& buf = buffers_[bufferIndex];
        const std::size_t viewOffset = sizeOr(view->get("byteOffset"), 0);
        const std::size_t viewLength = sizeOr(view->get("byteLength"), 0);
        if (viewOffset > buf.size() || viewLength > buf.size() - viewOffset)
            return fail(what + ": bufferView runs past its buffer");

        const std::size_t elem = csize * ncomp;
        const std::size_t stride = sizeOr(view->get("byteStride"), elem);
        const std::size_t accOffset = sizeOr(acc->get("byteOffset"), 0);
        if (stride < elem) return fail(what + ": byteStride is smaller than one element");
        if (count > 0) {
            if (stride > viewLength) return fail(what + ": byteStride is larger than its bufferView");
            const std::size_t last = accOffset + (count - 1) * stride;
            if (last > viewLength || viewLength - last < elem) return fail(what + ": accessor reads past its bufferView");
        }

        out.assign(count * ncomp, 0.0);
        const auto* base = reinterpret_cast<const unsigned char*>(buf.data()) + viewOffset + accOffset;
        const bool isSigned = ctype == 5120 || ctype == 5122;
        for (std::size_t i = 0; i < count; ++i) {
            const unsigned char* elemPtr = base + i * stride;
            for (std::size_t c = 0; c < ncomp; ++c) {
                const unsigned char* p = elemPtr + c * csize;
                double v = 0;
                switch (ctype) {
                    case 5126: {
                        const uint32_t u = le32(p);
                        float f;
                        std::memcpy(&f, &u, 4);
                        v = f;
                        break;
                    }
                    case 5125: v = static_cast<double>(le32(p)); break;
                    case 5123: v = static_cast<double>(static_cast<uint16_t>(p[0] | p[1] << 8)); break;
                    case 5122: v = static_cast<double>(static_cast<int16_t>(static_cast<uint16_t>(p[0] | p[1] << 8))); break;
                    case 5121: v = static_cast<double>(p[0]); break;
                    default: v = static_cast<double>(static_cast<int8_t>(p[0])); break;
                }
                if (normalized && ctype != 5126) {
                    const double maxv = ctype == 5121 ? 255.0 : ctype == 5120 ? 127.0 : ctype == 5123 ? 65535.0 : 32767.0;
                    v = isSigned ? std::max(-1.0, v / maxv) : v / maxv;
                }
                out[i * ncomp + c] = v;
            }
        }
        return true;
    }

    // One primitive of a node: world-space positions, mirrored winding fixed, normals, uv (flipped), then the bucket.
    bool emitPrimitive(const json::Value& prim, const Affine& world, int tag) {
        const int mode = static_cast<int>(numberOr(prim.get("mode"), 4));
        if (mode != 4) {
            ++skippedModes_[mode];
            return true;
        }
        const json::Value* attrs = prim.get("attributes");
        if (!attrs || attrs->type != json::Value::Type::Object) return fail("a primitive has no attributes");
        const json::Value* posRef = attrs->get("POSITION");
        if (!posRef) return fail("a primitive has no POSITION attribute");

        std::vector<double> v;
        std::size_t n = 0;
        if (!readAccessor(posRef, 3, false, v, n, "POSITION")) return false;
        std::vector<Vec3> pos(n);
        for (std::size_t i = 0; i < n; ++i) pos[i] = world.point(Vec3{v[i * 3], v[i * 3 + 1], v[i * 3 + 2]});

        std::vector<uint32_t> idx;
        if (const json::Value* indRef = prim.get("indices")) {
            std::size_t ni = 0;
            std::vector<double> iv;
            if (!readAccessor(indRef, 1, true, iv, ni, "indices")) return false;
            idx.resize(ni);
            for (std::size_t i = 0; i < ni; ++i) {
                if (!(iv[i] >= 0) || iv[i] >= static_cast<double>(n))
                    return fail("index " + std::to_string(static_cast<long long>(iv[i])) + " is past the vertex count " +
                                std::to_string(n));
                idx[i] = static_cast<uint32_t>(iv[i]);
            }
        } else {
            idx.resize(n);
            for (std::size_t i = 0; i < n; ++i) idx[i] = static_cast<uint32_t>(i);
        }
        if (idx.size() % 3 != 0) return fail("a primitive has " + std::to_string(idx.size()) + " indices, not a multiple of 3");
        // A mirroring node flips every triangle's front face: swap two corners so they still face outward.
        if (world.det3() < 0)
            for (std::size_t t = 0; t + 2 < idx.size(); t += 3) std::swap(idx[t + 1], idx[t + 2]);

        std::vector<Vec3> nrm(n);
        if (const json::Value* nRef = attrs->get("NORMAL")) {
            std::size_t nn = 0;
            if (!readAccessor(nRef, 3, false, v, nn, "NORMAL")) return false;
            if (nn != n) return fail("NORMAL count differs from POSITION count");
            for (std::size_t i = 0; i < n; ++i) nrm[i] = world.normal(Vec3{v[i * 3], v[i * 3 + 1], v[i * 3 + 2]});
        } else {
            nrm = areaWeightedNormals(pos, idx);
            generatedNormals_ += n;
        }

        // glTF v = 0 is the top of the image; ImportedMesh uses the OBJ convention (v = 0 at the bottom).
        std::vector<std::array<double, 2>> uv(n, std::array<double, 2>{0.0, 0.0});
        if (const json::Value* tRef = attrs->get("TEXCOORD_0")) {
            std::size_t nt = 0;
            if (!readAccessor(tRef, 2, false, v, nt, "TEXCOORD_0")) return false;
            if (nt != n) return fail("TEXCOORD_0 count differs from POSITION count");
            for (std::size_t i = 0; i < n; ++i) uv[i] = {v[i * 2], 1.0 - v[i * 2 + 1]};
        } else {
            missingUv_ += n;
        }

        std::string material;
        if (const json::Value* mRef = prim.get("material")) {
            std::size_t mi = 0;
            const json::Value* mats = doc_.get("materials");
            const json::Value* mat = indexOf(mRef, mi) && mats ? mats->at(mi) : nullptr;
            if (!mat) return fail("a primitive names a material that does not exist");
            if (const json::Value* nm = mat->get("name"); nm && nm->string()) material = *nm->string();
        }

        GltfBucket& bucket = buckets_[tag];
        auto found = bucket.byMaterial.find(material);
        if (found == bucket.byMaterial.end()) {
            ImportedMesh fresh;
            fresh.material = material;
            bucket.meshes.push_back(std::move(fresh));
            found = bucket.byMaterial.emplace(material, bucket.meshes.size() - 1).first;
        }
        ImportedMesh& mesh = bucket.meshes[found->second];
        const std::size_t base = mesh.pos.size();
        if (base + n > UINT32_MAX) return fail("a mesh would exceed 4 billion vertices");
        mesh.pos.insert(mesh.pos.end(), pos.begin(), pos.end());
        mesh.normal.insert(mesh.normal.end(), nrm.begin(), nrm.end());
        mesh.uv.insert(mesh.uv.end(), uv.begin(), uv.end());
        mesh.idx.reserve(mesh.idx.size() + idx.size());
        for (const uint32_t i : idx) mesh.idx.push_back(static_cast<uint32_t>(base + i));
        return true;
    }

    bool visit(std::size_t nodeIndex, const Affine& parent, int inheritedTag, int depth) {
        if (depth > kMaxNodeDepth) return fail("the node hierarchy is deeper than 64 levels (a cycle?)");
        const json::Value* nodes = doc_.get("nodes");
        const json::Value* node = nodes ? nodes->at(nodeIndex) : nullptr;
        if (!node || node->type != json::Value::Type::Object)
            return fail("node " + std::to_string(nodeIndex) + " does not exist");
        const Affine world = parent * nodeTransform(*node);

        int tag = inheritedTag;
        if (const json::Value* nameV = node->get("name"); nameV && nameV->string()) {
            const int own = nodeTag(*nameV->string());
            if (own != kNoTag) tag = own;
        }
        if (tag > kMaxRenderLod) return fail("a node name asks for LOD " + std::to_string(tag) + "; the limit is LOD 5");

        if (const json::Value* meshRef = node->get("mesh")) {
            std::size_t mi = 0;
            const json::Value* meshes = doc_.get("meshes");
            const json::Value* mesh = indexOf(meshRef, mi) && meshes ? meshes->at(mi) : nullptr;
            if (!mesh) return fail("node " + std::to_string(nodeIndex) + " names a mesh that does not exist");
            if (const json::Value* prims = mesh->get("primitives"); prims && prims->type == json::Value::Type::Array)
                for (const json::Value& prim : prims->arr)
                    if (!emitPrimitive(prim, world, tag)) return false;
        }
        if (const json::Value* ch = node->get("children"); ch && ch->type == json::Value::Type::Array)
            for (const json::Value& c : ch->arr) {
                std::size_t ci = 0;
                if (!indexOf(&c, ci)) return fail("a child node index is malformed");
                if (!visit(ci, world, tag, depth + 1)) return false;
            }
        return true;
    }

    static std::vector<ImportedMesh> takeMeshes(GltfBucket& b) {
        std::vector<ImportedMesh> out;
        for (ImportedMesh& m : b.meshes)
            if (m.idx.size() >= 3) out.push_back(std::move(m));
        return out;
    }

    static AABB boundsOf(const std::vector<ImportedMesh>& meshes) {
        AABB box;
        for (const ImportedMesh& m : meshes)
            for (const Vec3& p : m.pos) box.add(p);
        return box;
    }

    const json::Value& doc_;
    std::vector<std::string> buffers_;
    std::map<int, GltfBucket> buckets_;
    std::map<int, std::size_t> skippedModes_;  // primitive mode -> count
    std::size_t generatedNormals_ = 0;
    std::size_t missingUv_ = 0;
    std::vector<std::string> warnings_;
    std::string err_;
};

}  // namespace

bool importGltf(const std::string& path, ImportedModel& out, std::string* err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        setErr(err, "cannot open glTF file: " + path + " (OBJ files are read by importObj)");
        return false;
    }
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const fs::path file(path);
    const bool isGlb = bytes.size() >= 4 && std::memcmp(bytes.data(), "glTF", 4) == 0;

    std::string jsonText, bin, msg;
    if (isGlb) {
        if (!splitGlb(bytes, jsonText, bin, msg)) {
            setErr(err, path + ": " + msg);
            return false;
        }
    } else {
        jsonText = bytes;
    }

    std::string perr;
    const std::optional<json::Value> doc = json::parse(jsonText, &perr);
    if (!doc || doc->type != json::Value::Type::Object) {
        setErr(err, path + ": not a glTF JSON document" + (perr.empty() ? std::string() : ": " + perr));
        return false;
    }
    if (const json::Value* asset = doc->get("asset")) {
        const json::Value* version = asset->get("version");
        if (version && version->string() && version->string()->rfind("2.", 0) != 0) {
            setErr(err, path + ": unsupported glTF version " + *version->string() + " (2.x is read)");
            return false;
        }
    }
    if (const json::Value* req = doc->get("extensionsRequired"); req && req->type == json::Value::Type::Array)
        for (const json::Value& ext : req->arr)
            if (ext.string()) {
                setErr(err, path + ": requires glTF extension " + *ext.string() + ", which is not supported");
                return false;
            }

    std::vector<std::string> buffers;
    if (!loadBuffers(*doc, file.parent_path(), isGlb ? &bin : nullptr, buffers, msg)) {
        setErr(err, path + ": " + msg);
        return false;
    }

    GltfImporter importer(*doc, std::move(buffers));
    if (!importer.readScene()) {
        setErr(err, path + ": " + importer.error());
        return false;
    }
    ImportedModel model;
    std::string berr;
    if (!importer.build(sanitizeName(file.stem().string()), model, berr)) {
        setErr(err, path + ": " + berr);
        return false;
    }
    out = std::move(model);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------------------------------------------

namespace {

struct ExportVertex {
    Vec3 pos, nrm, tan;
    double sign = 1.0;
    double u = 0, v = 0;
};

struct ExportSurface {
    std::string material;
    std::vector<ExportVertex> verts;
    std::vector<uint32_t> idx;  // already clockwise seen from outside
};

// JSON number: finite doubles only, shortest form that keeps float precision.
bool num(double value, std::string& out) {
    if (!std::isfinite(value)) return false;
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.9g", value == 0 ? 0.0 : value);
    out += buf;
    return true;
}

bool vec(const Vec3& v, std::string& out) {
    out += "[";
    if (!num(v.x, out)) return false;
    out += ",";
    if (!num(v.y, out)) return false;
    out += ",";
    if (!num(v.z, out)) return false;
    out += "]";
    return true;
}

bool writeText(const fs::path& path, const std::string& text, std::string* err) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        setErr(err, "cannot write " + path.string());
        return false;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) {
        setErr(err, "cannot finish " + path.string());
        return false;
    }
    return true;
}

// Tangents from the UV gradient of each triangle (accumulated per vertex), Gram-Schmidt against the normal.
// The sign makes binormal = cross(normal, tangent) * sign match the UV bitangent.
void computeTangents(ExportSurface& s) {
    std::vector<Vec3> tan(s.verts.size()), bit(s.verts.size());
    for (std::size_t t = 0; t + 2 < s.idx.size(); t += 3) {
        const uint32_t i0 = s.idx[t], i1 = s.idx[t + 1], i2 = s.idx[t + 2];
        const ExportVertex &a = s.verts[i0], &b = s.verts[i1], &c = s.verts[i2];
        const Vec3 e1 = b.pos - a.pos, e2 = c.pos - a.pos;
        const double du1 = b.u - a.u, dv1 = b.v - a.v, du2 = c.u - a.u, dv2 = c.v - a.v;
        const double det = du1 * dv2 - du2 * dv1;
        if (std::fabs(det) < 1e-12) continue;
        const double r = 1.0 / det;
        const Vec3 tt = (e1 * dv2 - e2 * dv1) * r;
        const Vec3 bb = (e2 * du1 - e1 * du2) * r;
        for (const uint32_t i : {i0, i1, i2}) {
            tan[i] += tt;
            bit[i] += bb;
        }
    }
    for (std::size_t i = 0; i < s.verts.size(); ++i) {
        ExportVertex& v = s.verts[i];
        Vec3 t = tan[i] - v.nrm * dot(v.nrm, tan[i]);
        if (length(t) < 1e-9) {
            const Vec3 axis = std::fabs(v.nrm.x) < 0.9 ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
            t = cross(v.nrm, axis);
        }
        v.tan = normalize(t);
        v.sign = dot(cross(v.nrm, v.tan), bit[i]) < 0 ? -1.0 : 1.0;
    }
}

}  // namespace

bool writeExtractionModel(const ImportedModel& m, const std::string& extractedDir, const ModelExportOptions& opt,
                          std::string* err) {
    if (!isSafeModelName(m.name)) {
        setErr(err, "model name '" + m.name + "' must use only letters, digits, '_' and '-'");
        return false;
    }
    if (!(opt.scale > 0) || !std::isfinite(opt.scale)) {
        setErr(err, "scale must be a positive finite number");
        return false;
    }
    if (opt.lodDistances.empty() || opt.lodDistances.size() > kMaxLods) {
        setErr(err, "a model has 1 to 6 LODs");
        return false;
    }
    for (std::size_t i = 0; i < opt.lodDistances.size(); ++i) {
        if (!std::isfinite(opt.lodDistances[i]) || opt.lodDistances[i] < 0 ||
            (i > 0 && opt.lodDistances[i] < opt.lodDistances[i - 1])) {
            setErr(err, "LOD distances must be finite, non-negative and non-decreasing");
            return false;
        }
    }

    // Export surfaces: scale and axis change, clockwise winding, UV flip, tangents.
    std::vector<ExportSurface> surfaces;
    for (const ImportedMesh& mesh : m.meshes) {
        if (mesh.idx.size() < 3 || mesh.idx.size() % 3 != 0) continue;
        if (mesh.pos.size() != mesh.normal.size() || mesh.pos.size() != mesh.uv.size()) {
            setErr(err, "mesh '" + mesh.material + "' has mismatched pos/normal/uv counts");
            return false;
        }
        if (mesh.pos.size() > kMaxSurfaceVerts || mesh.idx.size() / 3 > kMaxSurfaceTris) {
            setErr(err, "surface '" + mesh.material + "' is over 65535 vertices or triangles; split the model");
            return false;
        }
        ExportSurface s;
        s.material = opt.materialPrefix + (mesh.material.empty() ? std::string("grey") : materialFileName(mesh.material));
        if (!isSafeMaterialName(s.material)) {
            setErr(err, "material name '" + s.material + "' is not usable as a materials/ path");
            return false;
        }
        s.verts.reserve(mesh.pos.size());
        for (std::size_t i = 0; i < mesh.pos.size(); ++i) {
            ExportVertex v;
            const Vec3& p = mesh.pos[i];
            const Vec3& n = mesh.normal[i];
            // Y-up to Z-up is a rotation about X (det +1), so winding is preserved.
            const Vec3 rp = opt.zUp ? Vec3(p.x, -p.z, p.y) : p;
            const Vec3 rn = opt.zUp ? Vec3(n.x, -n.z, n.y) : n;
            v.pos = rp * opt.scale;
            v.nrm = length(rn) > 1e-12 ? normalize(rn) : Vec3(0, 0, 1);
            v.u = mesh.uv[i][0];
            v.v = 1.0 - mesh.uv[i][1];
            s.verts.push_back(v);
        }
        s.idx.reserve(mesh.idx.size());
        for (std::size_t t = 0; t < mesh.idx.size(); t += 3) {
            // Swap the last two corners: OBJ counter-clockwise -> model clockwise seen from outside.
            for (const std::size_t corner : {0u, 2u, 1u}) {
                const uint32_t index = mesh.idx[t + corner];
                if (index >= mesh.pos.size()) {
                    setErr(err, "triangle references a missing vertex");
                    return false;
                }
                s.idx.push_back(index);
            }
        }
        computeTangents(s);
        surfaces.push_back(std::move(s));
    }
    if (surfaces.empty()) {
        setErr(err, "model '" + m.name + "' has no triangles");
        return false;
    }
    if (surfaces.size() * opt.lodDistances.size() > kMaxSurfacesPerModel) {
        setErr(err, "model has " + std::to_string(surfaces.size()) + " surfaces x " +
                        std::to_string(opt.lodDistances.size()) + " LODs; Replay allows 255 over all LODs");
        return false;
    }

    const fs::path root(extractedDir);

    // xmodel/<name>.json
    std::string table = R"({"collLod":0,"flags":0,"lods":[)";
    for (std::size_t i = 0; i < opt.lodDistances.size(); ++i) {
        if (i) table += ",";
        table += R"({"distance":)";
        if (!num(opt.lodDistances[i], table)) {
            setErr(err, "non-finite LOD distance");
            return false;
        }
        table += "}";
    }
    table += R"(],"rootBoneName":"","type":"rigid"})";
    if (!writeText(root / "xmodel" / (m.name + ".json"), table, err)) return false;

    // Surfaces text, shared by every LOD (each LOD repeats the same geometry for now).
    std::string surfacesText = "[";
    for (std::size_t s = 0; s < surfaces.size(); ++s) {
        const ExportSurface& surface = surfaces[s];
        if (s) surfacesText += ",";
        surfacesText += R"({"material":")" + surface.material +
                        R"(","reflection_probe":0,"deformed":false,"vertices":[)";
        for (std::size_t i = 0; i < surface.verts.size(); ++i) {
            const ExportVertex& v = surface.verts[i];
            if (i) surfacesText += ",";
            surfacesText += R"({"position":)";
            if (!vec(v.pos, surfacesText)) return false;
            surfacesText += R"(,"normal":)";
            if (!vec(v.nrm, surfacesText)) return false;
            surfacesText += R"(,"tangent":)";
            if (!vec(v.tan, surfacesText)) return false;
            surfacesText += v.sign < 0 ? R"(,"binormal_sign":-1)" : R"(,"binormal_sign":1)";
            surfacesText += R"(,"uv":[)";
            if (!num(v.u, surfacesText)) return false;
            surfacesText += ",";
            if (!num(v.v, surfacesText)) return false;
            surfacesText += R"(],"lightmap_uv":[0,0]})";
        }
        surfacesText += R"(],"indices":[)";
        for (std::size_t i = 0; i < surface.idx.size(); ++i) {
            if (i) surfacesText += ",";
            surfacesText += std::to_string(surface.idx[i]);
        }
        surfacesText += "]}";
    }
    surfacesText += "]";

    // xmodel/<name>.replay.json
    std::string geometry = R"({"schema":1,"name":")" + m.name + R"(","bone_count":1,"lods":[)";
    for (std::size_t i = 0; i < opt.lodDistances.size(); ++i) {
        if (i) geometry += ",";
        geometry += R"({"distance":)";
        if (!num(opt.lodDistances[i], geometry)) return false;
        geometry += R"(,"surfaces":)" + surfacesText + "}";
    }
    geometry += "]}";
    if (!writeText(root / "xmodel" / (m.name + ".replay.json"), geometry, err)) return false;

    // materials/<name>.json: the grey placeholder the stock grey material uses. An existing file is kept.
    static const char* kGreyMaterial =
        R"({"techniqueSet":"iw8r_grey","textures":[{"name":"colorMap","semantic":"colorMap","image":"$white"},)"
        R"({"name":"normalMap","semantic":"normalMap","image":"$identitynormalmap"}]})";
    for (const ExportSurface& surface : surfaces) {
        const fs::path file = root / "materials" / (surface.material + ".json");
        std::error_code ec;
        if (fs::exists(file, ec)) continue;
        if (!writeText(file, kGreyMaterial, err)) return false;
    }
    return true;
}

bool importModelFile(const std::string& path, ImportedModel& out, std::string* err) {
    std::string ext = std::filesystem::path(path).extension().string();
    for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".obj") return importObj(path, out, err);
    if (ext == ".glb" || ext == ".gltf") return importGltf(path, out, err);
    if (err) *err = "unsupported model file '" + path + "' (use .obj, .glb or .gltf)";
    return false;
}

}  // namespace iw8r
