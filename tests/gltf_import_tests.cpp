// glTF import (importGltf): .gltf with data: URIs or external .bin files, .glb containers, node transforms,
// materials, generated normals, UV flip, LOD and collision nodes, the Y-up export path, and error messages.
// The input files are built in code and written to the temp folder.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "json_lite.h"
#include "model_import.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path dir = fs::path(test::tmpDir()) / "gltf_import" / name;
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

void writeBytes(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::optional<json::Value> parseFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return json::parse(text, nullptr);
}

bool near(double a, double b, double eps = 1e-4) { return std::fabs(a - b) <= eps; }

bool nearVec(const Vec3& a, const Vec3& b) { return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z); }

bool hasWarning(const ImportedModel& m, const std::string& needle) {
    for (const std::string& w : m.warnings)
        if (w.find(needle) != std::string::npos) return true;
    return false;
}

void put32(std::string& s, uint32_t v) {
    for (int i = 0; i < 4; ++i) s += static_cast<char>((v >> (8 * i)) & 0xff);
}

std::string base64(const std::string& in) {
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < in.size(); i += 3) {
        const bool has1 = i + 1 < in.size(), has2 = i + 2 < in.size();
        uint32_t v = static_cast<uint32_t>(static_cast<uint8_t>(in[i])) << 16;
        if (has1) v |= static_cast<uint32_t>(static_cast<uint8_t>(in[i + 1])) << 8;
        if (has2) v |= static_cast<uint32_t>(static_cast<uint8_t>(in[i + 2]));
        out += table[(v >> 18) & 63];
        out += table[(v >> 12) & 63];
        out += has1 ? table[(v >> 6) & 63] : '=';
        out += has2 ? table[v & 63] : '=';
    }
    return out;
}

std::string joined(const std::vector<std::string>& items) {
    std::string s = "[";
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i) s += ",";
        s += items[i];
    }
    return s + "]";
}

std::string intList(const std::vector<int>& values) {
    std::vector<std::string> items;
    for (const int v : values) items.push_back(std::to_string(v));
    return joined(items);
}

// A glTF document built piece by piece. Every accessor points into the one binary buffer `bin`.
struct Asset {
    std::string bin;
    std::vector<std::string> views, accessors, materials, meshes, nodes;
};

int addView(Asset& a, const std::string& data) {
    while (a.bin.size() % 4) a.bin.push_back('\0');
    const std::size_t off = a.bin.size();
    a.bin += data;
    a.views.push_back("{\"buffer\":0,\"byteOffset\":" + std::to_string(off) +
                      ",\"byteLength\":" + std::to_string(data.size()) + "}");
    return static_cast<int>(a.views.size()) - 1;
}

// Float VEC3 (ncomp 3) or VEC2 (ncomp 2) accessor.
int addFloats(Asset& a, const std::vector<float>& values, int ncomp) {
    std::string data;
    for (const float f : values) {
        uint32_t u;
        std::memcpy(&u, &f, 4);
        put32(data, u);
    }
    const int view = addView(a, data);
    a.accessors.push_back("{\"bufferView\":" + std::to_string(view) + ",\"componentType\":5126,\"count\":" +
                          std::to_string(values.size() / static_cast<std::size_t>(ncomp)) +
                          ",\"type\":\"" + (ncomp == 3 ? "VEC3" : "VEC2") + "\"}");
    return static_cast<int>(a.accessors.size()) - 1;
}

int addIndices(Asset& a, const std::vector<uint16_t>& values) {
    std::string data;
    for (const uint16_t v : values) {
        data += static_cast<char>(v & 0xff);
        data += static_cast<char>(v >> 8);
    }
    const int view = addView(a, data);
    a.accessors.push_back("{\"bufferView\":" + std::to_string(view) + ",\"componentType\":5123,\"count\":" +
                          std::to_string(values.size()) + ",\"type\":\"SCALAR\"}");
    return static_cast<int>(a.accessors.size()) - 1;
}

int addMaterial(Asset& a, const std::string& name) {
    a.materials.push_back("{\"name\":\"" + name + "\"}");
    return static_cast<int>(a.materials.size()) - 1;
}

// One primitive. An empty nrm, uv or idx leaves that attribute (or the index accessor) out. material -1: none.
std::string primitive(Asset& a, const std::vector<float>& pos, const std::vector<float>& nrm,
                      const std::vector<float>& uv, const std::vector<uint16_t>& idx, int material, int mode = 4) {
    std::string attrs = "\"POSITION\":" + std::to_string(addFloats(a, pos, 3));
    if (!nrm.empty()) attrs += ",\"NORMAL\":" + std::to_string(addFloats(a, nrm, 3));
    if (!uv.empty()) attrs += ",\"TEXCOORD_0\":" + std::to_string(addFloats(a, uv, 2));
    std::string s = "{\"attributes\":{" + attrs + "}";
    if (!idx.empty()) s += ",\"indices\":" + std::to_string(addIndices(a, idx));
    if (material >= 0) s += ",\"material\":" + std::to_string(material);
    return s + ",\"mode\":" + std::to_string(mode) + "}";
}

int addMesh(Asset& a, const std::vector<std::string>& primitives) {
    a.meshes.push_back("{\"primitives\":" + joined(primitives) + "}");
    return static_cast<int>(a.meshes.size()) - 1;
}

// A node. extra holds raw members such as "\"scale\":[-1,1,1]"; children are node indices (already added).
int addNode(Asset& a, const std::string& name, int mesh, const std::string& extra = "",
            const std::vector<int>& children = {}) {
    std::string s = "{\"name\":\"" + name + "\"";
    if (mesh >= 0) s += ",\"mesh\":" + std::to_string(mesh);
    if (!extra.empty()) s += "," + extra;
    if (!children.empty()) s += ",\"children\":" + intList(children);
    a.nodes.push_back(s + "}");
    return static_cast<int>(a.nodes.size()) - 1;
}

// The document text. bufferUri "" means no uri (buffer 0 is the GLB BIN chunk). extra: raw top-level members.
std::string documentJson(const Asset& a, const std::string& bufferUri, const std::vector<int>& roots,
                         const std::string& extra = "") {
    std::string s = "{\"asset\":{\"version\":\"2.0\"}";
    if (!extra.empty()) s += "," + extra;
    s += ",\"buffers\":[{\"byteLength\":" + std::to_string(a.bin.size());
    if (!bufferUri.empty()) s += ",\"uri\":\"" + bufferUri + "\"";
    s += "}],\"bufferViews\":" + joined(a.views) + ",\"accessors\":" + joined(a.accessors) +
         ",\"materials\":" + joined(a.materials) + ",\"meshes\":" + joined(a.meshes) +
         ",\"nodes\":" + joined(a.nodes) + ",\"scenes\":[{\"nodes\":" + intList(roots) + "}],\"scene\":0}";
    return s;
}

std::string dataUri(const Asset& a) { return "data:application/octet-stream;base64," + base64(a.bin); }

// GLB container: JSON chunk padded with spaces, BIN chunk padded with zeros.
std::string makeGlb(const std::string& json, const std::string& bin) {
    std::string j = json, b = bin;
    while (j.size() % 4) j += ' ';
    while (b.size() % 4) b += '\0';
    std::string out = "glTF";
    put32(out, 2);
    put32(out, static_cast<uint32_t>(12 + 8 + j.size() + 8 + b.size()));
    put32(out, static_cast<uint32_t>(j.size()));
    put32(out, 0x4E4F534A);  // JSON
    out += j;
    put32(out, static_cast<uint32_t>(b.size()));
    put32(out, 0x004E4942);  // BIN
    out += b;
    return out;
}

// Vertex `i` of the first surface of LOD 0 in an exported replay file, or nullptr.
const json::Value* replayVertex(const json::Value& geo, std::size_t i) {
    const json::Value* lods = geo.get("lods");
    const json::Value* lod0 = lods ? lods->at(0) : nullptr;
    const json::Value* surfaces = lod0 ? lod0->get("surfaces") : nullptr;
    const json::Value* surface = surfaces ? surfaces->at(0) : nullptr;
    const json::Value* verts = surface ? surface->get("vertices") : nullptr;
    return verts ? verts->at(i) : nullptr;
}

// Component `i` of a JSON number array, or a value that fails any near() check when absent.
double numAt(const json::Value* arr, std::size_t i) {
    const json::Value* e = arr ? arr->at(i) : nullptr;
    return e && e->number() ? *e->number() : 1e30;
}

const std::vector<float> kUp = {0, 0, 1, 0, 0, 1, 0, 0, 1};  // three +Z normals

}  // namespace

// ---------------------------------------------------------------------------------------------------------------

// Two materials, a second primitive per material (merged into one mesh), UVs (v flipped), and a strip that is
// skipped with a warning.
TEST(gltf_materials_uv_flip_and_skipped_strip) {
    const fs::path dir = scratch("materials");
    Asset a;
    const int wood = addMaterial(a, "wood");
    const int metal = addMaterial(a, "metal");
    const std::vector<float> uvs = {0, 0, 1, 0, 0, 1};
    const std::string p1 = primitive(a, {0, 0, 0, 1, 0, 0, 0, 1, 0}, kUp, uvs, {}, wood);
    const std::string p2 = primitive(a, {0, 0, 1, 1, 0, 1, 0, 1, 1}, kUp, uvs, {}, wood);
    const std::string p3 = primitive(a, {2, 0, 0, 3, 0, 0, 2, 1, 0}, kUp, uvs, {}, metal);
    const std::string strip = primitive(a, {5, 0, 0, 6, 0, 0, 5, 1, 0}, kUp, {}, {}, metal, 5);
    addNode(a, "block", addMesh(a, {p1, p2, p3, strip}));
    const fs::path file = dir / "block.gltf";
    writeBytes(file, documentJson(a, dataUri(a), {0}));

    ImportedModel m;
    std::string err;
    CHECK(importGltf(file.string(), m, &err));
    if (m.meshes.size() != 2) {
        CHECK(false);
        return;
    }
    CHECK(m.name == "block");
    CHECK(m.meshes[0].material == "wood");
    CHECK(m.meshes[1].material == "metal");
    CHECK(m.meshes[0].pos.size() == 6 && m.meshes[0].idx.size() == 6);
    CHECK(m.meshes[1].pos.size() == 3 && m.meshes[1].idx.size() == 3);
    CHECK(m.meshes[0].idx == (std::vector<uint32_t>{0, 1, 2, 3, 4, 5}));
    CHECK(hasWarning(m, "mode 5"));
    // glTF v = 0 is the top; ImportedMesh has v = 0 at the bottom. (1,0) -> (1,1); (0,1) -> (0,0).
    CHECK(near(m.meshes[0].uv[1][0], 1.0) && near(m.meshes[0].uv[1][1], 1.0));
    CHECK(near(m.meshes[0].uv[2][0], 0.0) && near(m.meshes[0].uv[2][1], 0.0));
    CHECK(nearVec(m.meshes[0].pos[3], Vec3(0, 0, 1)));
    CHECK(nearVec(m.meshes[1].pos[2], Vec3(2, 1, 0)));
    CHECK(nearVec(m.meshes[0].normal[0], Vec3(0, 0, 1)));
    CHECK(m.bounds.valid());
    CHECK(m.lods.empty());
    CHECK(!m.collision);
}

// Node TRS (translation, rotation about Z by 90 degrees, uniform scale 2) and a child under a translated group with
// a matrix. Positions are the glTF values after the node transforms; the axis change is left to the exporter.
TEST(gltf_node_transforms_trs_matrix_and_hierarchy) {
    const fs::path dir = scratch("transforms");
    Asset a;
    const std::string tri1 = primitive(a, {1, 0, 0, 0, 1, 0, 0, 0, 1}, kUp, {}, {}, -1);
    const int m1 = addMesh(a, {tri1});
    const int piece = addNode(a, "piece", m1,
                              "\"translation\":[10,0,0],\"rotation\":[0,0,0.70710678,0.70710678],\"scale\":[2,2,2]");
    const std::string tri2 = primitive(a, {0, 0, 0, 1, 0, 0, 0, 1, 0}, kUp, {}, {}, -1);
    const int m2 = addMesh(a, {tri2});
    const int leaf = addNode(a, "leaf", m2, "\"matrix\":[1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,5,1]");
    const int group = addNode(a, "group", -1, "\"translation\":[1,0,0]", {leaf});
    const fs::path file = dir / "transforms.gltf";
    writeBytes(file, documentJson(a, dataUri(a), {piece, group}));

    ImportedModel m;
    std::string err;
    CHECK(importGltf(file.string(), m, &err));
    if (m.meshes.size() != 1 || m.meshes[0].pos.size() != 6) {
        CHECK(false);
        return;
    }
    const auto& pos = m.meshes[0].pos;
    // piece: (1,0,0) -> scale (2,0,0) -> rotate (0,2,0) -> translate (10,2,0); (0,1,0) -> (8,0,0); (0,0,1) -> (10,0,2)
    CHECK(nearVec(pos[0], Vec3(10, 2, 0)));
    CHECK(nearVec(pos[1], Vec3(8, 0, 0)));
    CHECK(nearVec(pos[2], Vec3(10, 0, 2)));
    // leaf: group translation (1,0,0) then the matrix translation (0,0,5)
    CHECK(nearVec(pos[3], Vec3(1, 0, 5)));
    CHECK(nearVec(pos[4], Vec3(2, 0, 5)));
    CHECK(nearVec(pos[5], Vec3(1, 1, 5)));
    // The normal (0,0,1) keeps its direction under the rotation and the uniform scale.
    CHECK(nearVec(m.meshes[0].normal[0], Vec3(0, 0, 1)));
    (void)leaf;
}

// A node with a negative scale mirrors the geometry: the triangle keeps facing outward (corners swapped) and its
// normal, computed with the inverse transpose, still points along +Z.
TEST(gltf_mirrored_node_keeps_triangles_outward) {
    const fs::path dir = scratch("mirror");
    Asset a;
    const std::string tri = primitive(a, {0, 0, 0, 1, 0, 0, 0, 1, 0}, kUp, {}, {}, -1);
    const int mesh = addMesh(a, {tri});
    addNode(a, "mirror", mesh, "\"scale\":[-1,1,1]");
    const fs::path file = dir / "mirror.gltf";
    writeBytes(file, documentJson(a, dataUri(a), {0}));

    ImportedModel m;
    std::string err;
    CHECK(importGltf(file.string(), m, &err));
    if (m.meshes.size() != 1 || m.meshes[0].idx.size() != 3) {
        CHECK(false);
        return;
    }
    const auto& mesh0 = m.meshes[0];
    CHECK(mesh0.idx == (std::vector<uint32_t>{0, 2, 1}));
    CHECK(nearVec(mesh0.pos[1], Vec3(-1, 0, 0)));
    const Vec3 face = cross(mesh0.pos[mesh0.idx[1]] - mesh0.pos[mesh0.idx[0]], mesh0.pos[mesh0.idx[2]] - mesh0.pos[mesh0.idx[0]]);
    CHECK(face.z > 0);
    CHECK(nearVec(mesh0.normal[0], Vec3(0, 0, 1)));
}

// No NORMAL attribute: area-weighted vertex normals. Vertex 0 is shared by a large triangle (normal +Z) and a small
// one (normal +X): the large one dominates.
TEST(gltf_missing_normals_are_area_weighted) {
    const fs::path dir = scratch("normals");
    Asset a;
    const std::string p = primitive(a, {0, 0, 0, 10, 0, 0, 0, 10, 0, 0, 0.1f, 0, 0, 0, 0.1f}, {}, {},
                                    {0, 1, 2, 0, 3, 4}, -1);
    addNode(a, "shared", addMesh(a, {p}));
    const fs::path file = dir / "normals.gltf";
    writeBytes(file, documentJson(a, dataUri(a), {0}));

    ImportedModel m;
    std::string err;
    CHECK(importGltf(file.string(), m, &err));
    if (m.meshes.size() != 1 || m.meshes[0].normal.size() != 5) {
        CHECK(false);
        return;
    }
    const auto& n = m.meshes[0].normal;
    CHECK(hasWarning(m, "generated 5 vertex normal(s)"));
    CHECK(hasWarning(m, "without TEXCOORD_0"));
    CHECK(n[0].z > 0.999 && std::fabs(n[0].x) < 0.01);
    CHECK(nearVec(n[1], Vec3(0, 0, 1)));
    CHECK(nearVec(n[3], Vec3(1, 0, 0)));
    CHECK(near(length(n[3]), 1.0));
}

// Nodes named *_lod1, *_LOD2, collision* and UCX_* go to lods and collision; the rest is LOD 0. A child of a
// collision group inherits the tag.
TEST(gltf_lod_and_collision_nodes_are_split) {
    const fs::path dir = scratch("lods");
    Asset a;
    const auto tri = [&a](float x) { return primitive(a, {x, 0, 0, x + 1, 0, 0, x, 1, 0}, kUp, {}, {}, -1); };
    addNode(a, "crate", addMesh(a, {tri(0)}));                       // 0: LOD 0
    addNode(a, "crate_lod1", addMesh(a, {tri(100)}));                // 1: LOD 1
    addNode(a, "CRATE_LOD2", addMesh(a, {tri(200)}));                // 2: LOD 2 (case-insensitive)
    addNode(a, "collision_crate", addMesh(a, {tri(300)}));           // 3: collision
    addNode(a, "UCX_crate_01", addMesh(a, {tri(400)}));              // 4: collision
    const int hull = addNode(a, "hull_part", addMesh(a, {tri(500)}));  // 5: inherits collision from 6
    addNode(a, "collision_group", -1, "", {hull});                   // 6: collision group, no mesh
    const fs::path file = dir / "crate.gltf";
    writeBytes(file, documentJson(a, dataUri(a), {0, 1, 2, 3, 4, 6}));

    ImportedModel m;
    std::string err;
    CHECK(importGltf(file.string(), m, &err));
    CHECK(m.name == "crate");
    CHECK(m.meshes.size() == 1 && m.meshes[0].idx.size() == 3);
    if (!m.meshes.empty() && !m.meshes[0].pos.empty()) CHECK(nearVec(m.meshes[0].pos[0], Vec3(0, 0, 0)));
    if (m.lods.size() != 2) {
        CHECK(false);
    } else {
        CHECK(m.lods[0].name == "crate" && m.lods[1].name == "crate");
        CHECK(m.lods[0].meshes.size() == 1 && m.lods[0].meshes[0].pos.size() == 3);
        CHECK(!m.lods[0].meshes.empty() && near(m.lods[0].meshes[0].pos[0].x, 100));
        CHECK(!m.lods[1].meshes.empty() && near(m.lods[1].meshes[0].pos[0].x, 200));
        CHECK(m.lods[0].lods.empty() && !m.lods[0].collision);
    }
    CHECK(m.collision != nullptr);
    if (m.collision) {
        CHECK(m.collision->meshes.size() == 1 && m.collision->meshes[0].idx.size() == 9);
        CHECK(near(m.collision->bounds.mins.x, 300) && near(m.collision->bounds.maxs.x, 501));
        CHECK(m.collision->lods.empty());
    }
    // The LOD and collision geometry is not in the render mesh.
    CHECK(near(m.bounds.maxs.x, 1));
}

TEST(gltf_lod_gap_is_warned_and_kept_in_order) {
    const fs::path dir = scratch("lod_gap");
    Asset a;
    const auto tri = [&a](float x) { return primitive(a, {x, 0, 0, x + 1, 0, 0, x, 1, 0}, kUp, {}, {}, -1); };
    addNode(a, "wall", addMesh(a, {tri(0)}));
    addNode(a, "wall_lod2", addMesh(a, {tri(200)}));
    const fs::path file = dir / "wall.gltf";
    writeBytes(file, documentJson(a, dataUri(a), {0, 1}));

    ImportedModel m;
    std::string err;
    CHECK(importGltf(file.string(), m, &err));
    CHECK(hasWarning(m, "no *_lod1 node before *_lod2"));
    CHECK(m.lods.size() == 1);
    CHECK(!m.lods.empty() && !m.lods[0].meshes.empty() && near(m.lods[0].meshes[0].pos[0].x, 200));
}

// .glb (BIN chunk as buffer 0, material name, TRIANGLES) and .gltf with an external, percent-encoded .bin file.
TEST(gltf_glb_container_and_external_bin) {
    const fs::path dir = scratch("containers");
    Asset a;
    const int steel = addMaterial(a, "steel");
    const std::string p = primitive(a, {0, 0, 0, 1, 0, 0, 0, 1, 0}, kUp, {}, {0, 1, 2}, steel);
    addNode(a, "slab", addMesh(a, {p}));

    const fs::path glb = dir / "slab.glb";
    writeBytes(glb, makeGlb(documentJson(a, "", {0}), a.bin));
    ImportedModel m;
    std::string err;
    CHECK(importGltf(glb.string(), m, &err));
    CHECK(m.name == "slab");
    CHECK(m.meshes.size() == 1 && m.meshes[0].material == "steel" && m.meshes[0].idx.size() == 3);
    if (!m.meshes.empty() && m.meshes[0].pos.size() == 3) CHECK(nearVec(m.meshes[0].pos[1], Vec3(1, 0, 0)));

    writeBytes(dir / "slab data.bin", a.bin);
    writeBytes(dir / "external.gltf", documentJson(a, "slab%20data.bin", {0}));
    ImportedModel e;
    CHECK(importGltf((dir / "external.gltf").string(), e, &err));
    CHECK(e.meshes.size() == 1 && e.meshes[0].pos.size() == 3);
    if (!e.meshes.empty() && e.meshes[0].pos.size() == 3) CHECK(nearVec(e.meshes[0].pos[2], Vec3(0, 1, 0)));
}

// The exporter (writeExtractionModel, default options) maps the glTF Y-up position (0,1,0) to Radiant Z-up (0,0,1),
// the normal (0,0,1) to (0,-1,0), and a glTF uv (0.25, 0.75) comes back as (0.25, 0.75) in the replay file.
TEST(gltf_export_maps_y_up_and_keeps_glTF_uv) {
    const fs::path dir = scratch("export");
    Asset a;
    const std::string p = primitive(a, {0, 0, 0, 1, 0, 0, 0, 1, 0}, kUp, {0.25f, 0.75f, 0.5f, 0.75f, 0.25f, 0.5f}, {}, -1);
    addNode(a, "tri_export", addMesh(a, {p}));
    writeBytes(dir / "tri_export.gltf", documentJson(a, dataUri(a), {0}));

    ImportedModel m;
    std::string err;
    CHECK(importGltf((dir / "tri_export.gltf").string(), m, &err));
    CHECK(writeExtractionModel(m, (dir / "out").string(), ModelExportOptions{}, &err));
    const auto geo = parseFile(dir / "out" / "xmodel" / "tri_export.replay.json");
    if (!geo) {
        CHECK(false);
        return;
    }
    const json::Value* v = replayVertex(*geo, 2);
    CHECK(v != nullptr);
    if (!v) return;
    CHECK(near(numAt(v->get("position"), 0), 0.0) && near(numAt(v->get("position"), 1), 0.0) &&
          near(numAt(v->get("position"), 2), 1.0));
    CHECK(near(numAt(v->get("normal"), 1), -1.0) && near(numAt(v->get("normal"), 2), 0.0));
    const json::Value* v0 = replayVertex(*geo, 0);
    CHECK(v0 && near(numAt(v0->get("uv"), 0), 0.25) && near(numAt(v0->get("uv"), 1), 0.75));
}

TEST(gltf_errors_name_the_file_and_the_problem) {
    const fs::path dir = scratch("errors");
    ImportedModel m;
    std::string err;

    CHECK(!importGltf((dir / "missing.glb").string(), m, &err));
    CHECK(err.find("OBJ") != std::string::npos);

    writeBytes(dir / "junk.gltf", "not json at all");
    err.clear();
    CHECK(!importGltf((dir / "junk.gltf").string(), m, &err));
    CHECK(err.find("junk.gltf") != std::string::npos);

    writeBytes(dir / "v1.gltf", R"({"asset":{"version":"1.0"},"nodes":[]})");
    err.clear();
    CHECK(!importGltf((dir / "v1.gltf").string(), m, &err));
    CHECK(err.find("unsupported glTF version 1.0") != std::string::npos);

    Asset tri;
    const std::string p = primitive(tri, {0, 0, 0, 1, 0, 0, 0, 1, 0}, {}, {}, {}, -1);
    addNode(tri, "t", addMesh(tri, {p}));

    writeBytes(dir / "draco.gltf",
               documentJson(tri, dataUri(tri), {0}, "\"extensionsRequired\":[\"KHR_draco_mesh_compression\"]"));
    err.clear();
    CHECK(!importGltf((dir / "draco.gltf").string(), m, &err));
    CHECK(err.find("KHR_draco_mesh_compression") != std::string::npos);

    writeBytes(dir / "b64.gltf", documentJson(tri, "data:application/octet-stream;base64,@@@", {0}));
    err.clear();
    CHECK(!importGltf((dir / "b64.gltf").string(), m, &err));
    CHECK(err.find("base64") != std::string::npos);

    writeBytes(dir / "gone.gltf", documentJson(tri, "gone.bin", {0}));
    err.clear();
    CHECK(!importGltf((dir / "gone.gltf").string(), m, &err));
    CHECK(err.find("gone.bin") != std::string::npos);

    Asset badAccessor;
    addNode(badAccessor, "x", addMesh(badAccessor, {"{\"attributes\":{\"POSITION\":7},\"mode\":4}"}));
    writeBytes(dir / "accessor.gltf", documentJson(badAccessor, dataUri(badAccessor), {0}));
    err.clear();
    CHECK(!importGltf((dir / "accessor.gltf").string(), m, &err));
    CHECK(err.find("accessor index") != std::string::npos);

    Asset badIndex;
    const std::string bi = primitive(badIndex, {0, 0, 0, 1, 0, 0, 0, 1, 0}, {}, {}, {0, 1, 5}, -1);
    addNode(badIndex, "bad", addMesh(badIndex, {bi}));
    writeBytes(dir / "index.gltf", documentJson(badIndex, dataUri(badIndex), {0}));
    err.clear();
    CHECK(!importGltf((dir / "index.gltf").string(), m, &err));
    CHECK(err.find("past the vertex count") != std::string::npos);

    Asset lines;
    const std::string ln = primitive(lines, {0, 0, 0, 1, 0, 0, 0, 1, 0}, {}, {}, {}, -1, 1);
    addNode(lines, "wire", addMesh(lines, {ln}));
    writeBytes(dir / "lines.gltf", documentJson(lines, dataUri(lines), {0}));
    err.clear();
    CHECK(!importGltf((dir / "lines.gltf").string(), m, &err));
    CHECK(err.find("no TRIANGLES") != std::string::npos);

    Asset lodOnly;
    addNode(lodOnly, "x_lod1", addMesh(lodOnly, {primitive(lodOnly, {0, 0, 0, 1, 0, 0, 0, 1, 0}, {}, {}, {}, -1)}));
    writeBytes(dir / "lod_only.gltf", documentJson(lodOnly, dataUri(lodOnly), {0}));
    err.clear();
    CHECK(!importGltf((dir / "lod_only.gltf").string(), m, &err));
    CHECK(err.find("no LOD 0 geometry") != std::string::npos);

    Asset lod6;
    const std::string six = primitive(lod6, {0, 0, 0, 1, 0, 0, 0, 1, 0}, {}, {}, {}, -1);
    addNode(lod6, "x", addMesh(lod6, {six}));
    addNode(lod6, "y_lod6", addMesh(lod6, {six}));
    writeBytes(dir / "lod6.gltf", documentJson(lod6, dataUri(lod6), {0, 1}));
    err.clear();
    CHECK(!importGltf((dir / "lod6.gltf").string(), m, &err));
    CHECK(err.find("limit is LOD 5") != std::string::npos);

    const fs::path glb = dir / "trunc.glb";
    const std::string full = makeGlb(documentJson(tri, "", {0}), tri.bin);
    writeBytes(glb, full.substr(0, full.size() - 10));
    err.clear();
    CHECK(!importGltf(glb.string(), m, &err));
    CHECK(err.find("does not fit") != std::string::npos);

    writeBytes(dir / "empty.gltf", R"({"asset":{"version":"2.0"}})");
    err.clear();
    CHECK(!importGltf((dir / "empty.gltf").string(), m, &err));
    CHECK(!err.empty());
}
