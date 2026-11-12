#include "stock_world_export.h"

#include <cctype>
#include <cstdio>
#include <unordered_map>

namespace iw8r {
namespace {

// Sky dome materials: the name without a leading "tw/" starts with "sky" (tw/sky_mp_shipment).
bool isSkyMaterial(const std::string& name) {
    std::string n = name;
    if (n.size() >= 3 && (n[0] == 't' || n[0] == 'T') && (n[1] == 'w' || n[1] == 'W') && n[2] == '/') n.erase(0, 3);
    if (n.size() < 3) return false;
    return std::tolower(static_cast<unsigned char>(n[0])) == 's' && std::tolower(static_cast<unsigned char>(n[1])) == 'k' &&
           std::tolower(static_cast<unsigned char>(n[2])) == 'y';
}

// OBJ group and material names cannot hold whitespace; an empty name becomes "default".
std::string objName(const std::string& s) {
    if (s.empty()) return "default";
    std::string o = s;
    for (char& c : o)
        if (std::isspace(static_cast<unsigned char>(c))) c = '_';
    return o;
}

}  // namespace

std::string stockWorldToObj(const StockWorld& w, const AABB& region, bool skipSky, size_t* triangles,
                            std::vector<std::string>* materials, Vec3* offset) {
    const Vec3 centre = region.valid() ? region.center() : Vec3{};
    if (offset) *offset = centre;
    if (triangles) *triangles = 0;
    if (materials) materials->clear();

    std::string out;
    out += "# IW8 Radiant stock world export (Replay 1.20 gfx_map geometry, Z-up inches)\n";
    char line[256];
    std::snprintf(line, sizeof line, "# origin: region centre at (%.6f %.6f %.6f) in stock coordinates\n", centre.x,
                  centre.y, centre.z);
    out += line;
    out += "o stock_world\n";
    if (!region.valid()) return out;

    size_t kept = 0;
    uint32_t nextIndex = 1;  // OBJ indices are 1-based and shared by v, vt and vn (one entry per emitted vertex)
    for (const ImportedMesh& m : w.meshes) {
        if (skipSky && isSkyMaterial(m.material)) continue;
        const size_t vcount = m.pos.size();

        // Triangles with at least one vertex inside the region.
        std::vector<size_t> faces;  // start index into m.idx
        for (size_t i = 0; i + 2 < m.idx.size(); i += 3) {
            if (m.idx[i] >= vcount || m.idx[i + 1] >= vcount || m.idx[i + 2] >= vcount) continue;
            for (int c = 0; c < 3; ++c) {
                if (region.contains(m.pos[m.idx[i + c]])) {
                    faces.push_back(i);
                    break;
                }
            }
        }
        if (faces.empty()) continue;

        const bool hasUv = m.uv.size() == vcount;
        const bool hasNormal = m.normal.size() == vcount;
        const std::string name = objName(m.material);
        std::string verts, body;
        std::unordered_map<uint32_t, uint32_t> obj;  // mesh vertex -> OBJ index in this group
        const auto emit = [&](uint32_t v) -> uint32_t {
            if (auto it = obj.find(v); it != obj.end()) return it->second;
            const uint32_t id = nextIndex++;
            obj.emplace(v, id);
            const Vec3 p = m.pos[v] - centre;
            std::snprintf(line, sizeof line, "v %.6f %.6f %.6f\n", p.x, p.y, p.z);
            verts += line;
            if (hasUv) {
                std::snprintf(line, sizeof line, "vt %.6f %.6f\n", m.uv[v][0], m.uv[v][1]);
            } else {
                std::snprintf(line, sizeof line, "vt 0 0\n");
            }
            verts += line;
            if (hasNormal) {
                const Vec3 n = m.normal[v];
                std::snprintf(line, sizeof line, "vn %.6f %.6f %.6f\n", n.x, n.y, n.z);
            } else {
                std::snprintf(line, sizeof line, "vn 0 0 1\n");
            }
            verts += line;
            return id;
        };
        for (size_t start : faces) {
            uint32_t id[3];
            for (int c = 0; c < 3; ++c) id[c] = emit(m.idx[start + c]);
            std::snprintf(line, sizeof line, "f %u/%u/%u %u/%u/%u %u/%u/%u\n", id[0], id[0], id[0], id[1], id[1], id[1],
                          id[2], id[2], id[2]);
            body += line;
        }

        out += "g " + name + "\n";
        out += "usemtl " + name + "\n";
        out += verts;
        out += body;
        kept += faces.size();
        if (materials) materials->push_back(name);
    }
    if (triangles) *triangles = kept;
    return out;
}

}  // namespace iw8r
