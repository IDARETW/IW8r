// glTF binary reader (src/core/glb_mesh.h): tiny GLB files built in memory, read back.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "glb_mesh.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

void put32(std::string& s, uint32_t v) {
    for (int i = 0; i < 4; ++i) s += char((v >> (8 * i)) & 0xff);
}
void putF(std::string& s, float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    put32(s, u);
}
void pad4(std::string& s, char fill) {
    while (s.size() % 4) s += fill;
}

// Standard GLB container: JSON chunk (padded with spaces) and BIN chunk (padded with zeros).
std::string makeGlb(const std::string& json, const std::string& bin) {
    std::string j = json, b = bin;
    pad4(j, ' ');
    pad4(b, '\0');
    std::string out = "glTF";
    put32(out, 2);
    put32(out, uint32_t(12 + 8 + j.size() + 8 + b.size()));
    put32(out, uint32_t(j.size()));
    put32(out, 0x4E4F534A);  // JSON
    out += j;
    put32(out, uint32_t(b.size()));
    put32(out, 0x004E4942);  // BIN
    out += b;
    return out;
}

// A unit-ish quad (0,0,0)-(1,2,0) as two triangles. Positions, normals (0,0,1), uv, then the indices.
// indexType: 5121 (u8), 5123 (u16) or 5125 (u32). withNormals false drops NORMAL from the primitive.
std::string quadGlb(const std::string& nodeJson, int indexType, bool withNormals = true) {
    std::string bin;
    const float pos[] = {0, 0, 0, 1, 0, 0, 1, 2, 0, 0, 2, 0};
    for (float f : pos) putF(bin, f);
    for (int i = 0; i < 4; ++i) {
        putF(bin, 0);
        putF(bin, 0);
        putF(bin, 1);
    }
    const float uv[] = {0, 0, 1, 0, 1, 1, 0, 1};
    for (float f : uv) putF(bin, f);
    const uint32_t idx[] = {0, 1, 2, 0, 2, 3};
    for (uint32_t v : idx) {
        if (indexType == 5121) bin += char(v);
        else if (indexType == 5123) {
            bin += char(v & 0xff);
            bin += char(v >> 8);
        } else put32(bin, v);
    }
    const size_t idxBytes = indexType == 5121 ? 6 : indexType == 5123 ? 12 : 24;
    const std::string normals = withNormals ? ",\"NORMAL\":1" : "";
    const std::string json =
        std::string("{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],") +
        "\"nodes\":[" + nodeJson + "],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0" + normals + ",\"TEXCOORD_0\":2},\"indices\":3}]}],"
        "\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},"
        "{\"bufferView\":1,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},"
        "{\"bufferView\":2,\"componentType\":5126,\"count\":4,\"type\":\"VEC2\"},"
        "{\"bufferView\":3,\"componentType\":" + std::to_string(indexType) + ",\"count\":6,\"type\":\"SCALAR\"}],"
        "\"bufferViews\":["
        "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":48},"
        "{\"buffer\":0,\"byteOffset\":48,\"byteLength\":48},"
        "{\"buffer\":0,\"byteOffset\":96,\"byteLength\":32},"
        "{\"buffer\":0,\"byteOffset\":128,\"byteLength\":" + std::to_string(idxBytes) + "}],"
        "\"buffers\":[{\"byteLength\":" + std::to_string(128 + idxBytes) + "}]}";
    return makeGlb(json, bin);
}

// ACTS node: rotation -90 degrees about X (x, y, z) -> (x, z, -y), scale 0.0254 (inches to metres).
const char* kActsNode = "{\"mesh\":0,\"rotation\":[-0.7071067811865476,0.0,0.0,0.7071067811865476],\"scale\":[0.0254,0.0254,0.0254]}";

bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }
bool nearV(const Vec3& a, const Vec3& b, double eps = 1e-6) { return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps); }

}  // namespace

TEST(glb_node_transform_and_scale) {
    const std::string bytes = quadGlb(kActsNode, 5123);
    ImportedModel m;
    std::string err;
    CHECK(parseGlb(bytes, m, &err));
    CHECK(err.empty());
    CHECK(m.meshes.size() == 1);
    if (m.meshes.size() != 1) return;
    const ImportedMesh& mesh = m.meshes[0];
    CHECK(mesh.pos.size() == 4);
    CHECK(mesh.idx.size() == 6);
    // Raw (1,2,0) becomes glTF Y-up metres: (0.0254, 0, -0.0508).
    CHECK(nearV(mesh.pos[2], Vec3{0.0254, 0, -0.0508}));
    CHECK(nearV(mesh.normal[0], Vec3{0, 1, 0}));  // raw +Z normal becomes +Y
    CHECK(near(mesh.uv[2][0], 1) && near(mesh.uv[2][1], 1));
    CHECK(m.bounds.valid());
    CHECK(near(m.bounds.maxs.x, 0.0254) && near(m.bounds.mins.z, -0.0508));
}

TEST(glb_scale_option_gives_inches_and_upright_z_up_round_trip) {
    // scale 1/0.0254 undoes the node scale: positions are Y-up inches, and (x, -z, y) returns the raw Z-up inches.
    const std::string bytes = quadGlb(kActsNode, 5123);
    ImportedModel m;
    GlbReadOptions opt;
    opt.scale = 1.0 / 0.0254;
    CHECK(parseGlb(bytes, m, nullptr, opt));
    if (m.meshes.empty()) return;
    const Vec3 yUp = m.meshes[0].pos[2];
    CHECK(nearV(yUp, Vec3{1, 0, -2}, 1e-9));
    const Vec3 zUp{yUp.x, -yUp.z, yUp.y};  // the editor's buildModelGeom mapping
    CHECK(nearV(zUp, Vec3{1, 2, 0}, 1e-9));
}

TEST(glb_index_types_u8_u16_u32) {
    for (int t : {5121, 5123, 5125}) {
        ImportedModel m;
        std::string err;
        const std::string bytes = quadGlb(kActsNode, t);
        CHECK(parseGlb(bytes, m, &err));
        if (m.meshes.empty()) {
            std::printf("    index type %d: %s\n", t, err.c_str());
            continue;
        }
        CHECK(m.meshes[0].idx.size() == 6);
        CHECK(m.meshes[0].idx[3] == 0 && m.meshes[0].idx[4] == 2 && m.meshes[0].idx[5] == 3);
    }
}

TEST(glb_raw_option_keeps_stored_coordinates) {
    const std::string bytes = quadGlb(kActsNode, 5123);
    ImportedModel m;
    GlbReadOptions opt;
    opt.applyNodeTransforms = false;
    CHECK(parseGlb(bytes, m, nullptr, opt));
    if (m.meshes.empty()) return;
    CHECK(nearV(m.meshes[0].pos[2], Vec3{1, 2, 0}));
}

TEST(glb_mirrored_node_reverses_winding) {
    // scale (-1, 1, 1) mirrors the mesh, so the triangles are swapped to keep them facing outward.
    const std::string mirror = "{\"mesh\":0,\"scale\":[-1,1,1]}";
    ImportedModel plain, mirrored;
    CHECK(parseGlb(quadGlb("{\"mesh\":0}", 5123), plain));
    CHECK(parseGlb(quadGlb(mirror, 5123), mirrored));
    if (plain.meshes.empty() || mirrored.meshes.empty()) return;
    CHECK(mirrored.meshes[0].idx[1] == plain.meshes[0].idx[2]);
    CHECK(mirrored.meshes[0].idx[2] == plain.meshes[0].idx[1]);
    CHECK(near(mirrored.meshes[0].pos[1].x, -1));
}

TEST(glb_missing_normals_and_uv_warn) {
    ImportedModel m;
    CHECK(parseGlb(quadGlb("{\"mesh\":0}", 5123, false), m));
    if (m.meshes.empty()) return;
    CHECK(nearV(m.meshes[0].normal[0], Vec3{0, 0, 1}));
    CHECK(!m.warnings.empty());
}

TEST(glb_rejects_bad_input) {
    ImportedModel m;
    std::string err;
    std::string bad = quadGlb("{\"mesh\":0}", 5123);
    bad[0] = 'X';
    CHECK(!parseGlb(bad, m, &err));
    CHECK(err.find("magic") != std::string::npos);

    CHECK(!parseGlb(std::string(8, '\0'), m, &err));

    std::string shortFile = quadGlb("{\"mesh\":0}", 5123);
    shortFile.pop_back();
    CHECK(!parseGlb(shortFile, m, &err));

    CHECK(!readGlb("does/not/exist.glb", m, &err));
}

TEST(glb_reads_file_and_names_it) {
    const std::string bytes = quadGlb(kActsNode, 5123);
    const std::filesystem::path dir = std::filesystem::path(test::tmpDir()) / "glb_mesh";
    std::filesystem::create_directories(dir);
    const std::string path = (dir / "sample_model.0.shared.bin.geometry.glb").string();
    if (FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fwrite(bytes.data(), 1, bytes.size(), f);
        std::fclose(f);
    }
    ImportedModel m;
    std::string err;
    CHECK(readGlb(path, m, &err));
    CHECK(m.name == "sample_model");
    CHECK(m.meshes.size() == 1);
}

// The ACTS fork's _SELFVIS attribute (research 105 section 4): the vertex's self-visibility word as four bytes. The
// direction turns with the node like the normals; amount and cone come out as stored.
TEST(glb_reads_self_visibility) {
    // decode: x, y bits 255 -> +Z (about), amount 127 -> 1, cone 127 -> 1; x, y bits 0 -> the folded -Z, amount 64.
    const uint32_t up = 255u | 255u << 9 | 127u << 18 | 127u << 25, down = 0u | 0u << 9 | 64u << 18 | 0u << 25;
    const auto du = decodeSelfVisibility(up), dd = decodeSelfVisibility(down);
    CHECK(near(du[2], 1.0, 1e-4) && near(du[3], 1.0, 1e-6));
    CHECK(near(dd[0], 0, 1e-6) && near(dd[1], 0, 1e-6) && near(dd[2], -64.0 / 127.0, 1e-6) && near(dd[3], 0, 1e-6));

    std::string bin;
    const float pos[] = {0, 0, 0, 1, 0, 0, 1, 2, 0, 0, 2, 0};
    for (float f : pos) putF(bin, f);
    for (int i = 0; i < 4; ++i) putF(bin, 0), putF(bin, 0), putF(bin, 1);
    const float uv[] = {0, 0, 1, 0, 1, 1, 0, 1};
    for (float f : uv) putF(bin, f);
    for (uint32_t v : {0u, 1u, 2u, 0u, 2u, 3u}) bin += char(v), bin += char(0);
    for (uint32_t w : {up, down, up, down}) put32(bin, w);  // at 140
    for (uint32_t k = 0; k < 4; ++k) put32(bin, (16384u * k) | ((65535u - 16384u * k) << 16));  // _LMAPUV at 156
    const std::string json =
        std::string("{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[") + kActsNode + "],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2,\"_SELFVIS\":4,\"_LMAPUV\":5},\"indices\":3,"
        "\"extras\":{\"sourceSurface\":0,\"sourceFlags\":64}}]}],"
        "\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},"
        "{\"bufferView\":1,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},"
        "{\"bufferView\":2,\"componentType\":5126,\"count\":4,\"type\":\"VEC2\"},"
        "{\"bufferView\":3,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"},"
        "{\"bufferView\":4,\"componentType\":5121,\"count\":4,\"type\":\"VEC4\"},"
        "{\"bufferView\":5,\"componentType\":5123,\"count\":4,\"type\":\"VEC2\",\"normalized\":true}],"
        "\"bufferViews\":["
        "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":48},{\"buffer\":0,\"byteOffset\":48,\"byteLength\":48},"
        "{\"buffer\":0,\"byteOffset\":96,\"byteLength\":32},{\"buffer\":0,\"byteOffset\":128,\"byteLength\":12},"
        "{\"buffer\":0,\"byteOffset\":140,\"byteLength\":16},{\"buffer\":0,\"byteOffset\":156,\"byteLength\":16}],"
        "\"buffers\":[{\"byteLength\":172}]}";
    ImportedModel m;
    std::string err;
    CHECK(parseGlb(makeGlb(json, bin), m, &err));
    CHECK(m.meshes.size() == 1);
    if (m.meshes.size() != 1) return;
    const ImportedMesh& mesh = m.meshes[0];
    CHECK(mesh.sourceFlags == 64);
    CHECK(mesh.selfVis.size() == 4);
    if (mesh.selfVis.size() != 4) return;
    // Raw +Z becomes +Y with the ACTS node (as the normal); the folded -Z becomes -Y at 64/127.
    CHECK(near(mesh.selfVis[0][1], 1.0, 1e-4) && near(mesh.selfVis[0][3], 1.0, 1e-6));
    CHECK(near(mesh.selfVis[1][1], -64.0 / 127.0, 1e-5) && near(mesh.selfVis[1][0], 0, 1e-5) && near(mesh.selfVis[1][2], 0, 1e-5));
    CHECK(mesh.lmapUv.size() == 4);
    if (mesh.lmapUv.size() == 4) CHECK(near(mesh.lmapUv[1][0], 16384.0 / 65535.0, 1e-6) && near(mesh.lmapUv[1][1], 49151.0 / 65535.0, 1e-6));
    // Without the attribute: empty.
    ImportedModel plain;
    CHECK(parseGlb(quadGlb(kActsNode, 5123), plain, nullptr));
    CHECK(!plain.meshes.empty() && plain.meshes[0].selfVis.empty() && plain.meshes[0].sourceFlags == 0);
}
