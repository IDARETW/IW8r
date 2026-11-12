#include "patch_tess.h"

#include <algorithm>

namespace iw8r {

namespace {

constexpr int kMinSubdiv = 1;
constexpr int kMaxSubdiv = 32;

// Two triangles per grid cell: (a, b, c) and (b, d, c), with a = (i, j), b = (i, j+1), c = (i+1, j), d = (i+1, j+1).
// For a flat grid with columns along +x and rows along +y this winds counter-clockwise seen from +z.
void addGridTriangles(PatchMesh& m) {
    const int w = m.width, h = m.height;
    for (int i = 0; i + 1 < h; ++i) {
        for (int j = 0; j + 1 < w; ++j) {
            const uint32_t a = static_cast<uint32_t>(i * w + j);
            const uint32_t b = a + 1;
            const uint32_t c = a + static_cast<uint32_t>(w);
            const uint32_t d = c + 1;
            m.indices.insert(m.indices.end(), {a, b, c, b, d, c});
        }
    }
}

// Area-weighted face normals averaged per point, then normalized. Points not used by any triangle get +Z.
void computeNormals(PatchMesh& m) {
    m.normals.assign(m.points.size(), Vec3{});
    for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        const uint32_t ia = m.indices[t], ib = m.indices[t + 1], ic = m.indices[t + 2];
        const Vec3 n = cross(m.points[ib] - m.points[ia], m.points[ic] - m.points[ia]);
        m.normals[ia] += n;
        m.normals[ib] += n;
        m.normals[ic] += n;
    }
    for (auto& n : m.normals) {
        n = length(n) > 0 ? normalize(n) : Vec3{0, 0, 1};
    }
}

PatchMesh triangulateControlGrid(const std::vector<Vec3>& ctrl, int rows, int cols) {
    PatchMesh m;
    m.width = cols;
    m.height = rows;
    m.points = ctrl;
    addGridTriangles(m);
    computeNormals(m);
    return m;
}

// Biquadratic Bezier patch evaluated at (u, v) in [0,1]^2. p[a][b]: a indexes rows (u), b indexes columns (v).
// The endpoint weights are exactly 0 or 1, so corner points come out bit-exact.
Vec3 evalBiquadratic(const Vec3 p[3][3], double u, double v) {
    const double bu[3] = {(1.0 - u) * (1.0 - u), 2.0 * u * (1.0 - u), u * u};
    const double bv[3] = {(1.0 - v) * (1.0 - v), 2.0 * v * (1.0 - v), v * v};
    Vec3 r{};
    for (int a = 0; a < 3; ++a)
        for (int b = 0; b < 3; ++b) r += p[a][b] * (bu[a] * bv[b]);
    return r;
}

PatchMesh tessellateCurve(const std::vector<Vec3>& ctrl, int rows, int cols, int subdiv) {
    const int seg = std::clamp(subdiv, kMinSubdiv, kMaxSubdiv);
    const int subRows = (rows - 1) / 2;
    const int subCols = (cols - 1) / 2;

    PatchMesh m;
    m.width = subCols * seg + 1;
    m.height = subRows * seg + 1;
    m.points.resize(static_cast<size_t>(m.width) * m.height);

    // Each global grid point is evaluated exactly once, in the sub-patch that owns it (the lower-index one on a
    // shared edge), so seam vertices are never duplicated and are identical on both sides.
    for (int i = 0; i < m.height; ++i) {
        const int si = std::min(i / seg, subRows - 1);
        const double u = static_cast<double>(i - si * seg) / seg;
        for (int j = 0; j < m.width; ++j) {
            const int sj = std::min(j / seg, subCols - 1);
            const double v = static_cast<double>(j - sj * seg) / seg;
            Vec3 p[3][3];
            for (int a = 0; a < 3; ++a)
                for (int b = 0; b < 3; ++b) p[a][b] = ctrl[static_cast<size_t>(2 * si + a) * cols + (2 * sj + b)];
            m.points[static_cast<size_t>(i) * m.width + j] = evalBiquadratic(p, u, v);
        }
    }
    addGridTriangles(m);
    computeNormals(m);
    return m;
}

bool validCurveDims(int rows, int cols) {
    return rows >= 3 && cols >= 3 && (rows % 2) == 1 && (cols % 2) == 1;
}

}  // namespace

PatchMesh tessellatePatch(const std::vector<Vec3>& ctrl, int rows, int cols, const std::string& keyword, int subdiv) {
    if (rows < 1 || cols < 1 || ctrl.size() != static_cast<size_t>(rows) * static_cast<size_t>(cols)) return {};
    if (keyword == "curve" && validCurveDims(rows, cols)) return tessellateCurve(ctrl, rows, cols, subdiv);
    return triangulateControlGrid(ctrl, rows, cols);
}

PatchMesh tessellatePrimitive(const Primitive& p, int subdiv) {
    if (p.kind != PrimKind::Patch || p.verts.empty()) return {};

    int rows = 0, cols = 0;
    for (const auto& v : p.verts) {
        if (v.row < 0 || v.col < 0) return {};
        rows = std::max(rows, v.row + 1);
        cols = std::max(cols, v.col + 1);
    }
    if (static_cast<size_t>(rows) * static_cast<size_t>(cols) != p.verts.size()) return {};

    std::vector<Vec3> ctrl(p.verts.size());
    std::vector<bool> seen(p.verts.size(), false);
    for (const auto& v : p.verts) {
        const size_t idx = static_cast<size_t>(v.row) * cols + static_cast<size_t>(v.col);
        if (seen[idx]) return {};  // duplicate cell: not a complete grid
        seen[idx] = true;
        ctrl[idx] = v.pos;
    }
    return tessellatePatch(ctrl, rows, cols, p.patchKeyword, subdiv);
}

}  // namespace iw8r
