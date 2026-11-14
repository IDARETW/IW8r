#include "umbra_view.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace iw8r {

namespace {

void setBit(std::vector<uint32_t>& bits, uint32_t i) {
    if ((i >> 5) >= bits.size()) bits.resize((i >> 5) + 1, 0u);
    bits[i >> 5] |= 1u << (i & 31u);
}

void boxLines(const float mn[3], const float mx[3], uint32_t rgba, std::vector<UmbraLine>& out) {
    const auto corner = [&](int i) { return Vec3{i & 1 ? mx[0] : mn[0], i & 2 ? mx[1] : mn[1], i & 4 ? mx[2] : mn[2]}; };
    static const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto& e : edges) out.push_back({corner(e[0]), corner(e[1]), rgba});
}

}  // namespace

UmbraTomeIndex umbraTomeIndex(const UmbraTome& tome) {
    UmbraTomeIndex ix;
    const uint32_t total = tome.userIdTotal();
    for (uint32_t k = 0; k < total; ++k) {
        const uint32_t id = tome.userId(k);
        const unsigned cls = umbraUserIdClass(id);
        ++ix.idsByClass[cls];
        if (cls == kUmbraSurface) setBit(ix.knownSurfaces, umbraUserIdIndex(id));
        else if (cls == kUmbraStaticModel) setBit(ix.knownModels, umbraUserIdIndex(id));
    }
    return ix;
}

UmbraViewResult umbraViewVisibility(const UmbraTome& tome, const UmbraTomeIndex& index, const Vec3& pos, const Vec3& forward,
                                    const Vec3& up, double tanHalfX, double tanHalfY, const UmbraQueryOptions& opt) {
    UmbraViewResult r;
    const auto t0 = std::chrono::steady_clock::now();
    const float p[3] = {float(pos.x), float(pos.y), float(pos.z)};
    const float f[3] = {float(forward.x), float(forward.y), float(forward.z)};
    const float u[3] = {float(up.x), float(up.y), float(up.z)};
    UmbraVisibility vis = tome.queryView(p, f, u, float(tanHalfX), float(tanHalfY), opt);
    r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    r.status = vis.status;
    r.truncated = vis.truncated;
    r.tile = vis.tile;
    r.cells = uint32_t(vis.cells.size());
    for (unsigned c = 0; c < kUmbraClassCount; ++c) r.visibleByClass[c] = vis.userIdsByClass[c];
    r.startCells = std::move(vis.startCells);
    r.visibleCells = std::move(vis.cells);
    r.culls = vis.status == UmbraQueryStatus::Ok && !r.startCells.empty();
    if (!r.culls) return r;
    // Visible, or not listed by the tome at all: drawn. Listed and not visible: culled.
    r.surfaceBits.assign(index.knownSurfaces.size(), 0u);
    r.modelBits.assign(index.knownModels.size(), 0u);
    for (size_t i = 0; i < r.surfaceBits.size(); ++i) r.surfaceBits[i] = ~index.knownSurfaces[i];
    for (size_t i = 0; i < r.modelBits.size(); ++i) r.modelBits[i] = ~index.knownModels[i];
    for (const uint32_t id : vis.userIds) {
        const unsigned cls = umbraUserIdClass(id);
        const uint32_t i = umbraUserIdIndex(id);
        if (cls == kUmbraSurface && (i >> 5) < r.surfaceBits.size()) r.surfaceBits[i >> 5] |= 1u << (i & 31u);
        else if (cls == kUmbraStaticModel && (i >> 5) < r.modelBits.size()) r.modelBits[i >> 5] |= 1u << (i & 31u);
    }
    return r;
}

const char* umbraStatusText(UmbraQueryStatus s) {
    switch (s) {
    case UmbraQueryStatus::Ok: return "in a cell";
    case UmbraQueryStatus::OutsideTome: return "outside the tome";
    case UmbraQueryStatus::NoTile: return "in space the tome does not cover";
    case UmbraQueryStatus::InSolid: return "inside solid";
    }
    return "";
}

std::vector<UmbraLine> umbraOverlayLines(const UmbraTome& tome, const UmbraViewResult& view, const Vec3& eye, double radius,
                                         size_t maxCells) {
    std::vector<UmbraLine> out;
    const uint32_t cellCount = uint32_t(tome.cells().size());
    // Visible cells near the eye, nearest first, dim blue.
    struct Near {
        double d;
        uint32_t cell;
    };
    std::vector<Near> near;
    for (const uint32_t c : view.visibleCells) {
        if (c >= cellCount) continue;
        if (std::find(view.startCells.begin(), view.startCells.end(), c) != view.startCells.end()) continue;
        float mn[3], mx[3];
        tome.cellBounds(c, mn, mx);
        const Vec3 mid{(mn[0] + mx[0]) * 0.5, (mn[1] + mx[1]) * 0.5, (mn[2] + mx[2]) * 0.5};
        const double d = length(mid - eye);
        if (d <= radius) near.push_back({d, c});
    }
    std::sort(near.begin(), near.end(), [](const Near& a, const Near& b) { return a.d < b.d; });
    if (near.size() > maxCells) near.resize(maxCells);
    for (const Near& n : near) {
        float mn[3], mx[3];
        tome.cellBounds(n.cell, mn, mx);
        boxLines(mn, mx, 0xffa06030u, out);
    }
    // The camera's own cells (cyan) and their portals (yellow; gates magenta boxes).
    for (const uint32_t c : view.startCells) {
        if (c >= cellCount) continue;
        float mn[3], mx[3];
        tome.cellBounds(c, mn, mx);
        boxLines(mn, mx, 0xffffe040u, out);
        const UmbraCell& cell = tome.cells()[c];
        for (uint32_t p = cell.portalStart; p < cell.portalStart + cell.portalCount && p < tome.portals().size(); ++p) {
            float q[4][3];
            if (tome.portalQuad(p, q)) {
                for (int k = 0; k < 4; ++k)
                    out.push_back({Vec3{q[k][0], q[k][1], q[k][2]}, Vec3{q[(k + 1) % 4][0], q[(k + 1) % 4][1], q[(k + 1) % 4][2]}, 0xff30d8ffu});
            } else if (tome.gateBounds(p, mn, mx)) {
                boxLines(mn, mx, 0xffff40ffu, out);
            }
        }
    }
    return out;
}

}  // namespace iw8r
