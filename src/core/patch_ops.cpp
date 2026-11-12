#include "patch_ops.h"
#include "patch_tess.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>
#include <set>

#include "brush_ops.h"

namespace iw8r {

namespace {

constexpr double kTexRepeat = 512.0;  // map units per texture repeat (Radiant natural / fit texturing)
constexpr double kVertEps = 1e-4;     // corner matching and planarity tolerance, map units
constexpr double kNoArea = 1e-12;
constexpr size_t kNone = static_cast<size_t>(-1);

bool isWs(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; }

struct Span {
    size_t a, b;
};

// Token spans of a line, split the same way as the map reader.
std::vector<Span> tokenSpans(const std::string& s) {
    std::vector<Span> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && isWs(s[i])) ++i;
        if (i >= s.size()) break;
        const size_t a = i;
        while (i < s.size() && !isWs(s[i])) ++i;
        out.push_back({a, i});
    }
    return out;
}

bool parseDouble(const std::string& tok, double& out) {
    if (tok.empty()) return false;
    const char* first = tok.data();
    const char* last = tok.data() + tok.size();
    if (*first == '+') ++first;
    auto r = std::from_chars(first, last, out);
    return r.ec == std::errc() && r.ptr == last;
}

// Row-major vertex indices (into p.verts) of a complete grid. Fails on gaps, duplicates or empty input.
bool gridOrder(const Primitive& p, int& rows, int& cols, std::vector<size_t>& order) {
    rows = cols = 0;
    order.clear();
    if (p.verts.empty()) return false;
    for (const auto& v : p.verts) {
        if (v.row < 0 || v.col < 0) return false;
        rows = std::max(rows, v.row + 1);
        cols = std::max(cols, v.col + 1);
    }
    if (static_cast<size_t>(rows) * static_cast<size_t>(cols) != p.verts.size()) return false;
    order.assign(p.verts.size(), kNone);
    for (size_t i = 0; i < p.verts.size(); ++i) {
        const size_t idx = static_cast<size_t>(p.verts[i].row) * static_cast<size_t>(cols) +
                           static_cast<size_t>(p.verts[i].col);
        if (order[idx] != kNone) return false;
        order[idx] = i;
    }
    return true;
}

// Texture pair of the vertex whose "v" token is at index `vtoken`: the "t s t ls lt" group after the xyz.
// A "c r g b a" colour block in between is skipped. Missing or unparsable values give (0, 0).
std::array<double, 2> readSt(const std::string& line, size_t vtoken, std::array<double, 2>* lm = nullptr) {
    const auto sp = tokenSpans(line);
    auto tok = [&](size_t i) { return line.substr(sp[i].a, sp[i].b - sp[i].a); };
    std::array<double, 2> st{0.0, 0.0};
    for (size_t i = vtoken + 4; i < sp.size();) {
        const std::string t = tok(i);
        if (t == "v") break;
        if (t == "c") {
            i += 5;
            continue;
        }
        if (t == "t") {
            double a = 0, b = 0;
            if (i + 2 < sp.size() && parseDouble(tok(i + 1), a) && parseDouble(tok(i + 2), b)) st = {a, b};
            double c = 0, d = 0;
            if (lm && i + 4 < sp.size() && parseDouble(tok(i + 3), c) && parseDouble(tok(i + 4), d)) *lm = {c, d};
            break;
        }
        ++i;
    }
    return st;
}

PatchGrid makeGrid(int rows, int cols, const std::string& keyword, const std::string& material) {
    PatchGrid g;
    g.rows = rows;
    g.cols = cols;
    g.keyword = keyword;
    g.material = material;
    g.pos.assign(static_cast<size_t>(rows) * static_cast<size_t>(cols), Vec3{});
    g.st.assign(g.pos.size(), {0.0, 0.0});
    return g;
}

PatchGrid emptyGrid() {
    PatchGrid g;
    g.rows = 0;
    g.cols = 0;
    return g;
}

// Copy with st sized to pos (missing texture coordinates are zero).
PatchGrid normalized(PatchGrid g) {
    g.st.resize(g.pos.size(), {0.0, 0.0});
    return g;
}

bool validGrid(const PatchGrid& g) {
    return g.rows >= 1 && g.cols >= 1 && g.pos.size() == static_cast<size_t>(g.rows) * static_cast<size_t>(g.cols);
}

size_t idx(const PatchGrid& g, int r, int c) {
    return static_cast<size_t>(r) * static_cast<size_t>(g.cols) + static_cast<size_t>(c);
}

Vec3 midpoint(const Vec3& a, const Vec3& b) { return (a + b) * 0.5; }

std::array<double, 2> midSt(const std::array<double, 2>& a, const std::array<double, 2>& b) {
    return {0.5 * (a[0] + b[0]), 0.5 * (a[1] + b[1])};
}

// Row insertion/removal on the row axis only. Columns use the transpose of these.
PatchGrid insertRowOnly(const PatchGrid& in, int k) {
    const PatchGrid g = normalized(in);
    const int n = g.rows, m = g.cols;
    if (!validGrid(g) || n < 3 || k < 1 || k > n - 2) return g;
    PatchGrid out = makeGrid(n + 2, m, g.keyword, g.material);
    for (int ri = 0; ri < n + 2; ++ri) {
        for (int c = 0; c < m; ++c) {
            const size_t dst = idx(out, ri, c);
            if (ri == k) {
                out.pos[dst] = midpoint(g.pos[idx(g, k - 1, c)], g.pos[idx(g, k, c)]);
                out.st[dst] = midSt(g.st[idx(g, k - 1, c)], g.st[idx(g, k, c)]);
            } else if (ri == k + 2) {
                out.pos[dst] = midpoint(g.pos[idx(g, k, c)], g.pos[idx(g, k + 1, c)]);
                out.st[dst] = midSt(g.st[idx(g, k, c)], g.st[idx(g, k + 1, c)]);
            } else {
                const int old = ri < k ? ri : (ri == k + 1 ? k : ri - 2);
                out.pos[dst] = g.pos[idx(g, old, c)];
                out.st[dst] = g.st[idx(g, old, c)];
            }
        }
    }
    return out;
}

PatchGrid removeRowOnly(const PatchGrid& in, int k) {
    const PatchGrid g = normalized(in);
    const int n = g.rows, m = g.cols;
    if (!validGrid(g) || n < 5 || k < 1 || k > n - 2) return g;
    PatchGrid out = makeGrid(n - 2, m, g.keyword, g.material);
    for (int ri = 0; ri < n - 2; ++ri) {
        const int old = ri <= k - 2 ? ri : (ri == k - 1 ? k : ri + 2);
        for (int c = 0; c < m; ++c) {
            out.pos[idx(out, ri, c)] = g.pos[idx(g, old, c)];
            out.st[idx(out, ri, c)] = g.st[idx(g, old, c)];
        }
    }
    return out;
}

// Ring offsets in units of the box half extents. Cylinder: 9 columns, quarter arcs with the odd columns on the
// box corners (the tangent intersections). Endcap: the same pattern over the half ring y >= 0 in 5 columns.
constexpr int kCylX[9] = {1, 1, 0, -1, -1, -1, 0, 1, 1};
constexpr int kCylY[9] = {0, 1, 1, 1, 0, -1, -1, -1, 0};
constexpr int kEndX[5] = {1, 1, 0, -1, -1};
constexpr int kEndY[5] = {0, 1, 1, 1, 0};

bool containsNear(const std::vector<Vec3>& pts, const Vec3& p, double eps) {
    for (const Vec3& q : pts)
        if (length(q - p) <= eps) return true;
    return false;
}

std::vector<Vec3> uniquePoints(const std::vector<Vec3>& pts) {
    std::vector<Vec3> out;
    for (const Vec3& p : pts)
        if (!containsNear(out, p, kVertEps)) out.push_back(p);
    return out;
}

}  // namespace

std::optional<PatchGrid> patchGrid(const Primitive& p) {
    if (p.kind != PrimKind::Patch) return std::nullopt;
    int rows = 0, cols = 0;
    std::vector<size_t> order;
    if (!gridOrder(p, rows, cols, order)) return std::nullopt;
    PatchGrid g = makeGrid(rows, cols, p.patchKeyword, p.patchMaterial);
    std::vector<std::array<double, 2>> lm(order.size(), {0.0, 0.0});
    for (size_t k = 0; k < order.size(); ++k) {
        const PatchVertex& v = p.verts[order[k]];
        g.pos[k] = v.pos;
        g.st[k] = readSt(p.lines[v.line].text, v.token, &lm[k]);
    }
    // The file stores texture coordinates x 1024 (kPatchStUnits); PatchGrid::st is in texture repeats. Two older
    // editor conventions are recognised and kept looking as they did: repeats written directly (tiny rate of change),
    // and world units with a lightmap pair of 1 per 64 units (tools/make_dev_showcase.py before 2026-11-10), read as
    // a 128-unit repeat like the planar projection those patches were drawn with.
    PatchGrid lmGrid = g;
    lmGrid.st = lm;
    const double rate = patchStRate(g), lmRate = patchStRate(lmGrid);
    const bool repeats = rate > 0 && rate < 0.05;
    const bool worldUnits = std::fabs(rate - 1.0) < 0.05 && std::fabs(lmRate - 1.0 / 64.0) < 0.002;
    const double div = repeats ? 1.0 : worldUnits ? 128.0 : kPatchStUnits;
    for (auto& st : g.st) st = {st[0] / div, st[1] / div};
    return g;
}

double patchStRate(const PatchGrid& g) {
    // Median |d st| / |d position| between neighbouring control points: CoD / IW maps measure about 1024 / repeat size
    // (2 to 128 per unit in mp_backlot_geo); editor-legacy repeats are about 1 / 512.
    std::vector<double> rates;
    for (int r = 0; r < g.rows; ++r)
        for (int c = 0; c + 1 < g.cols; ++c) {
            const size_t a = size_t(r) * size_t(g.cols) + size_t(c), b = a + 1;
            const double d = length(g.pos[b] - g.pos[a]);
            if (d < 1) continue;
            rates.push_back(std::hypot(g.st[b][0] - g.st[a][0], g.st[b][1] - g.st[a][1]) / d);
        }
    for (int r = 0; r + 1 < g.rows; ++r)
        for (int c = 0; c < g.cols; ++c) {
            const size_t a = size_t(r) * size_t(g.cols) + size_t(c), b = a + size_t(g.cols);
            const double d = length(g.pos[b] - g.pos[a]);
            if (d < 1) continue;
            rates.push_back(std::hypot(g.st[b][0] - g.st[a][0], g.st[b][1] - g.st[a][1]) / d);
        }
    if (rates.empty()) return 0;
    std::nth_element(rates.begin(), rates.begin() + long(rates.size() / 2), rates.end());
    return rates[rates.size() / 2];
}

bool patchStIsLegacy(const PatchGrid& g) {
    const double med = patchStRate(g);
    return med > 0 && med < 0.05;
}

std::vector<std::array<double, 2>> tessellatePatchUVs(const Primitive& p, int subdiv) {
    const auto g = patchGrid(p);
    if (!g) return {};
    bool any = false;
    std::vector<Vec3> ctrl(g->st.size());
    for (size_t k = 0; k < ctrl.size(); ++k) {
        ctrl[k] = Vec3{g->st[k][0], g->st[k][1], 0};
        any |= g->st[k][0] != 0 || g->st[k][1] != 0;
    }
    if (!any) return {};  // no texture coordinates written: callers keep their planar projection
    const PatchMesh m = tessellatePatch(ctrl, g->rows, g->cols, p.patchKeyword, subdiv);
    std::vector<std::array<double, 2>> out;
    out.reserve(m.points.size());
    for (const Vec3& q : m.points) out.push_back({q.x, q.y});
    return out;
}

Primitive makePatchPrimitive(const PatchGrid& g, const std::string& eol, const std::string& comment) {
    Primitive prim;
    if (!validGrid(g)) return prim;  // no lines: the caller gets an empty, Unknown primitive
    const PatchGrid n = normalized(g);
    const std::string mat = n.material.empty() ? std::string("caulk") : n.material;
    const std::string kw = n.keyword.empty() ? std::string("curve") : n.keyword;
    auto add = [&](const std::string& t) { prim.lines.push_back({t, eol}); };

    if (!comment.empty()) add(comment);
    add("{");
    add(" " + kw);
    add(" {");
    add("  " + mat);
    add("  lightmap_gray");
    add("  " + std::to_string(n.rows) + " " + std::to_string(n.cols) + " 16 8");
    for (int r = 0; r < n.rows; ++r) {
        add("  (");
        for (int c = 0; c < n.cols; ++c) {
            const size_t i = idx(n, r, c);
            const Vec3& p = n.pos[i];
            const auto& st = n.st[i];
            add("\tv " + formatNumber(p.x) + " " + formatNumber(p.y) + " " + formatNumber(p.z) + " t " +
                formatNumber(st[0] * kPatchStUnits) + " " + formatNumber(st[1] * kPatchStUnits) + " 0 0");
        }
        add("  )");
    }
    add(" }");
    add("}");
    reparsePrimitive(prim);
    prim.dirty = true;
    return prim;
}

PatchGrid makeFlatPatch(const AABB& box, int axis, int rows, int cols, const std::string& material) {
    if (axis < 0 || axis > 2 || rows < 2 || cols < 2 || !box.valid()) return emptyGrid();
    PatchGrid g = makeGrid(rows, cols, "mesh", material);
    const int colAxis = (axis + 1) % 3;
    const int rowAxis = (axis + 2) % 3;
    const double sizeCol = box.maxs[colAxis] - box.mins[colAxis];
    const double sizeRow = box.maxs[rowAxis] - box.mins[rowAxis];
    const Vec3 c = box.center();
    for (int r = 0; r < rows; ++r) {
        for (int col = 0; col < cols; ++col) {
            const double fc = static_cast<double>(col) / (cols - 1);
            const double fr = static_cast<double>(r) / (rows - 1);
            Vec3 p;
            p[axis] = c[axis];
            p[colAxis] = box.mins[colAxis] + sizeCol * fc;
            p[rowAxis] = box.mins[rowAxis] + sizeRow * fr;
            g.pos[idx(g, r, col)] = p;
            g.st[idx(g, r, col)] = {fc * sizeCol / kTexRepeat, fr * sizeRow / kTexRepeat};
        }
    }
    return g;
}

PatchGrid makeCylinderPatch(const AABB& box, const std::string& material) {
    if (!box.valid()) return emptyGrid();
    PatchGrid g = makeGrid(3, 9, "curve", material);
    const Vec3 c = box.center();
    const double rx = (box.maxs.x - box.mins.x) * 0.5, ry = (box.maxs.y - box.mins.y) * 0.5;
    const double zs[3] = {box.mins.z, c.z, box.maxs.z};
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 9; ++k) g.pos[idx(g, r, k)] = {c.x + rx * kCylX[k], c.y + ry * kCylY[k], zs[r]};
    return naturalTexture(g);
}

PatchGrid makeBevelPatch(const AABB& box, const std::string& material) {
    if (!box.valid()) return emptyGrid();
    PatchGrid g = makeGrid(3, 3, "curve", material);
    const double ys[3] = {box.mins.y, box.center().y, box.maxs.y};
    // Columns in XZ: floor end, handle (the corner), wall end.
    const double xs[3] = {box.mins.x, box.maxs.x, box.maxs.x};
    const double zc[3] = {box.mins.z, box.mins.z, box.maxs.z};
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 3; ++k) g.pos[idx(g, r, k)] = {xs[k], ys[r], zc[k]};
    return naturalTexture(g);
}

PatchGrid makeEndcapPatch(const AABB& box, const std::string& material) {
    if (!box.valid()) return emptyGrid();
    PatchGrid g = makeGrid(3, 5, "curve", material);
    const Vec3 c = box.center();
    const double rx = (box.maxs.x - box.mins.x) * 0.5, ry = (box.maxs.y - box.mins.y) * 0.5;
    const double zs[3] = {box.mins.z, c.z, box.maxs.z};
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 5; ++k) g.pos[idx(g, r, k)] = {c.x + rx * kEndX[k], c.y + ry * kEndY[k], zs[r]};
    return naturalTexture(g);
}

PatchGrid invertPatch(const PatchGrid& in) {
    const PatchGrid g = normalized(in);
    if (!validGrid(g)) return g;
    PatchGrid out = g;
    for (int r = 0; r < g.rows; ++r)
        for (int c = 0; c < g.cols; ++c) {
            out.pos[idx(out, r, c)] = g.pos[idx(g, r, g.cols - 1 - c)];
            out.st[idx(out, r, c)] = g.st[idx(g, r, g.cols - 1 - c)];
        }
    return out;
}

PatchGrid transposePatch(const PatchGrid& in) {
    const PatchGrid g = normalized(in);
    if (!validGrid(g)) return g;
    PatchGrid out = g;
    out.rows = g.cols;
    out.cols = g.rows;
    for (int r = 0; r < out.rows; ++r)
        for (int c = 0; c < out.cols; ++c) {
            out.pos[idx(out, r, c)] = g.pos[idx(g, c, r)];
            out.st[idx(out, r, c)] = g.st[idx(g, c, r)];
        }
    return out;
}

PatchGrid naturalTexture(const PatchGrid& in) {
    PatchGrid g = normalized(in);
    if (!validGrid(g)) return g;
    for (int r = 0; r < g.rows; ++r) {
        double acc = 0;
        for (int c = 0; c < g.cols; ++c) {
            if (c > 0) acc += length(g.pos[idx(g, r, c)] - g.pos[idx(g, r, c - 1)]);
            g.st[idx(g, r, c)][0] = acc / kTexRepeat;
        }
    }
    for (int c = 0; c < g.cols; ++c) {
        double acc = 0;
        for (int r = 0; r < g.rows; ++r) {
            if (r > 0) acc += length(g.pos[idx(g, r, c)] - g.pos[idx(g, r - 1, c)]);
            g.st[idx(g, r, c)][1] = acc / kTexRepeat;
        }
    }
    return g;
}

PatchGrid insertRowCol(const PatchGrid& g, bool row, int at) {
    if (row) return insertRowOnly(g, at);
    return transposePatch(insertRowOnly(transposePatch(g), at));
}

PatchGrid removeRowCol(const PatchGrid& g, bool row, int at) {
    if (row) return removeRowOnly(g, at);
    return transposePatch(removeRowOnly(transposePatch(g), at));
}

PatchGrid thickenPatch(const PatchGrid& in, double amount, std::vector<PatchGrid>* sides) {
    if (sides) sides->clear();
    const PatchGrid g = normalized(in);
    if (!validGrid(g)) return g;
    const int n = g.rows, m = g.cols;

    // Control-grid normals: the tessellator's two triangles per cell, summed per point.
    std::vector<Vec3> nrm(g.pos.size(), Vec3{});
    for (int i = 0; i + 1 < n; ++i)
        for (int j = 0; j + 1 < m; ++j) {
            const size_t a = idx(g, i, j), b = idx(g, i, j + 1), c = idx(g, i + 1, j), d = idx(g, i + 1, j + 1);
            const Vec3 n1 = cross(g.pos[b] - g.pos[a], g.pos[c] - g.pos[a]);
            const Vec3 n2 = cross(g.pos[d] - g.pos[b], g.pos[c] - g.pos[b]);
            nrm[a] += n1;
            nrm[b] += n1 + n2;
            nrm[c] += n1 + n2;
            nrm[d] += n2;
        }
    for (auto& v : nrm) v = length(v) > 0 ? normalize(v) : Vec3{0, 0, 1};

    PatchGrid copy = g;
    for (size_t k = 0; k < g.pos.size(); ++k) copy.pos[k] = g.pos[k] + nrm[k] * amount;

    if (sides) {
        auto seam = [&](const std::vector<size_t>& edge) {
            const size_t len = edge.size();
            PatchGrid s = makeGrid(2, static_cast<int>(len), "mesh", g.material);
            for (size_t k = 0; k < len; ++k) {
                s.pos[k] = g.pos[edge[k]];
                s.pos[len + k] = copy.pos[edge[k]];
            }
            sides->push_back(naturalTexture(s));
        };
        std::vector<size_t> e;
        for (int c = 0; c < m; ++c) e.push_back(idx(g, 0, c));
        seam(e);
        e.clear();
        for (int c = 0; c < m; ++c) e.push_back(idx(g, n - 1, c));
        seam(e);
        e.clear();
        for (int r = 0; r < n; ++r) e.push_back(idx(g, r, 0));
        seam(e);
        e.clear();
        for (int r = 0; r < n; ++r) e.push_back(idx(g, r, m - 1));
        seam(e);
    }
    return copy;
}

bool movePatchVerts(Primitive& p, const std::vector<size_t>& indices, const Vec3& delta) {
    if (p.kind != PrimKind::Patch) return false;
    int rows = 0, cols = 0;
    std::vector<size_t> order;
    if (!gridOrder(p, rows, cols, order)) return false;
    for (size_t i : indices)
        if (i >= order.size()) return false;
    if (delta == Vec3{}) return true;

    // line -> (token index of "v" -> new position). Edits run right to left so earlier spans stay valid.
    std::map<size_t, std::map<size_t, Vec3>> byLine;
    const std::set<size_t> moving(indices.begin(), indices.end());
    for (size_t i : moving) {
        const PatchVertex& v = p.verts[order[i]];
        byLine[v.line][v.token] = v.pos + delta;
    }
    for (const auto& [li, edits] : byLine) {
        std::string text = p.lines[li].text;
        const auto sp = tokenSpans(text);
        for (auto it = edits.rbegin(); it != edits.rend(); ++it) {
            const size_t t = it->first;
            const Vec3& np = it->second;
            for (int k = 3; k >= 1; --k) {
                const Span s = sp[t + static_cast<size_t>(k)];
                text.replace(s.a, s.b - s.a, formatNumber(np[k - 1]));
            }
        }
        p.lines[li].text = text;
    }
    reparsePrimitive(p);
    p.dirty = true;
    return true;
}

std::vector<Vec3> brushVertices(const Primitive& brush) {
    if (brush.kind != PrimKind::Brush) return {};
    std::vector<Vec3> all;
    for (const Winding& w : brushWindings(brushFromPrimitive(brush)))
        for (const Vec3& p : w) all.push_back(p);
    return uniquePoints(all);
}

std::optional<Primitive> moveBrushVerts(const Primitive& brush, const std::vector<Vec3>& from, const Vec3& delta,
                                        const std::string& eol) {
    if (brush.kind != PrimKind::Brush) return std::nullopt;
    const std::vector<Vec3> verts = brushVertices(brush);
    if (verts.empty()) return std::nullopt;
    for (const Vec3& f : from)
        if (!containsNear(verts, f, kVertEps)) return std::nullopt;
    if (delta == Vec3{}) return brush;

    auto mapV = [&](const Vec3& v) { return containsNear(from, v, kVertEps) ? v + delta : v; };

    const ConvexBrush b = brushFromPrimitive(brush);
    const auto ws = brushWindings(b);
    if (ws.size() != b.size()) return std::nullopt;

    // Each contributing face: the plane through its moved corners, on the same outward side as before.
    ConvexBrush out;
    for (size_t i = 0; i < b.size(); ++i) {
        if (ws[i].empty()) continue;
        std::vector<Vec3> mw;
        for (const Vec3& p : ws[i]) mw.push_back(mapV(p));
        Vec3 nn;
        for (size_t k = 0; k < mw.size(); ++k) nn += cross(mw[k], mw[(k + 1) % mw.size()]);
        if (length(nn) <= kNoArea) return std::nullopt;
        const Vec3 n = normalize(nn);
        if (dot(n, b[i].plane.n) <= 0) return std::nullopt;  // face flipped
        Vec3 c;
        for (const Vec3& p : mw) c += p;
        c = c * (1.0 / static_cast<double>(mw.size()));
        const double d = dot(n, c);
        for (const Vec3& p : mw)
            if (std::fabs(dot(n, p) - d) > kVertEps) return std::nullopt;  // non-planar face
        out.push_back({Plane{n, d}, b[i].suffix});
    }
    if (out.empty()) return std::nullopt;

    // Every corner, moved or not, must lie inside every new plane.
    std::vector<Vec3> movedVerts;
    for (const Vec3& v : verts) movedVerts.push_back(mapV(v));
    movedVerts = uniquePoints(movedVerts);
    for (const Vec3& v : movedVerts)
        for (const auto& f : out)
            if (dot(f.plane.n, v) - f.plane.d > kVertEps) return std::nullopt;

    // Keep a leading "//" comment line (e.g. "// brush N") verbatim.
    std::string comment;
    if (!brush.lines.empty()) {
        const std::string& t = brush.lines[0].text;
        const size_t s = t.find_first_not_of(" \t");
        if (s != std::string::npos && t.compare(s, 2, "//") == 0) comment = t;
    }
    std::optional<Primitive> prim = makeBrushPrimitive(out, eol, comment);
    if (!prim) return std::nullopt;

    // The rebuilt brush must have exactly the moved corner set.
    const std::vector<Vec3> rebuilt = brushVertices(*prim);
    if (rebuilt.size() != movedVerts.size()) return std::nullopt;
    for (const Vec3& v : movedVerts)
        if (!containsNear(rebuilt, v, 1e-3)) return std::nullopt;
    return prim;
}

bool setPatchSt(Primitive& p, const std::vector<std::array<double, 2>>& st) {
    // st is row-major like PatchGrid (repeats); every control point's "t s t" pair is rewritten in file units (x 1024),
    // so a patch in an older convention is converted whole. Other tokens (colour block, lightmap pair) stay as written.
    int rows = 0, cols = 0;
    std::vector<size_t> order;
    if (p.kind != PrimKind::Patch || !gridOrder(p, rows, cols, order) || st.size() != order.size()) return false;
    std::map<size_t, std::map<size_t, std::array<double, 2>>> byLine;  // line -> (token of "t" -> new pair)
    for (size_t k = 0; k < order.size(); ++k) {
        const PatchVertex& v = p.verts[order[k]];
        const std::string& text = p.lines[v.line].text;
        const auto sp = tokenSpans(text);
        for (size_t i = v.token + 4; i + 2 < sp.size(); ++i) {
            const std::string t = text.substr(sp[i].a, sp[i].b - sp[i].a);
            if (t == "v") break;
            if (t == "t") {
                byLine[v.line][i] = {st[k][0] * kPatchStUnits, st[k][1] * kPatchStUnits};
                break;
            }
        }
    }
    for (const auto& [li, edits] : byLine) {
        std::string text = p.lines[li].text;
        const auto sp = tokenSpans(text);
        for (auto it = edits.rbegin(); it != edits.rend(); ++it)
            for (int k = 2; k >= 1; --k) {
                const Span s = sp[it->first + size_t(k)];
                text.replace(s.a, s.b - s.a, formatNumber(it->second[size_t(k - 1)]));
            }
        p.lines[li].text = text;
    }
    reparsePrimitive(p);
    p.dirty = true;
    return true;
}

}  // namespace iw8r
