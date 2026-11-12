// Shape models (Primitives > Shape Models): every shape imports as a closed-ish mesh with outward winding.
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

#include "model_import.h"
#include "shape_models.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

TEST(shape_models_import_and_wind_outward) {
    const fs::path dir = fs::temp_directory_path() / "iw8r_shape_test";
    fs::create_directories(dir);
    for (const auto& s : shapeKinds()) {
        const std::string name = shapeModelName(s.kind, "tm2co/me_curb_stone_01");
        CHECK(name.rfind(std::string("iw8r_shape_") + s.id + "_", 0) == 0);
        const fs::path p = dir / (name + ".obj");
        {
            std::ofstream f(p, std::ios::binary);
            f << shapeObj(s.kind, "tm2co/me_curb_stone_01");
        }
        ImportedModel m;
        std::string err;
        CHECK(importObj(p.string(), m, &err));
        if (m.meshes.empty()) continue;
        const auto& mesh = m.meshes[0];
        CHECK(mesh.material == "tm2co/me_curb_stone_01");
        // Winding vs the authored normals (OBJ Y-up): almost every triangle agrees, and the shape is 64 across.
        size_t agree = 0, tris = 0;
        double minY = 1e9, maxX = -1e9;
        for (size_t i = 0; i + 2 < mesh.idx.size(); i += 3) {
            const Vec3 a = mesh.pos[mesh.idx[i]], b = mesh.pos[mesh.idx[i + 1]], c = mesh.pos[mesh.idx[i + 2]];
            const Vec3 n = cross(b - a, c - a);
            Vec3 vn;
            if (mesh.normal.size() == mesh.pos.size())
                vn = mesh.normal[mesh.idx[i]] + mesh.normal[mesh.idx[i + 1]] + mesh.normal[mesh.idx[i + 2]];
            agree += dot(n, vn) > 0;
            ++tris;
        }
        for (const Vec3& p : mesh.pos) minY = std::min(minY, p.y), maxX = std::max(maxX, p.x);
        CHECK(tris > 0 && agree * 10 >= tris * 9);
        CHECK(std::fabs(minY) < 1e-3 && maxX > 15.99 && maxX < 32.01);  // base on the floor (OBJ y = up), at most 64 across (capsule 32)
    }
}
