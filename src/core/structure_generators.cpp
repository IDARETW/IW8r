// IW8 Radiant Suite - procedural stair and simple building generators (see structure_generators.h).
#include "structure_generators.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace iw8r {
namespace {

constexpr double kMinFootprint = 128;  // smallest building footprint, X and Y
constexpr double kStairSpan = 96;      // width of the inner staircase across the building (X)
constexpr double kMinStairSpan = 48;   // narrower than this and the staircase is refused
constexpr double kMinWallHeight = 32;  // wall height left above a floor slab
constexpr int kMaxFloors = 64;

std::vector<StructureBox> fail(std::string* err, const std::string& msg) {
    if (err) *err = msg;
    return {};
}

bool finiteAABB(const AABB& b) {
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(b.mins[i]) || !std::isfinite(b.maxs[i])) return false;
    }
    return true;
}

AABB makeBox(const Vec3& mn, const Vec3& mx) {
    AABB b;
    b.mins = mn;
    b.maxs = mx;
    return b;
}

// A rectangle in a wall's (u, v) plane, or in the XY plane for slabs.
struct Rect2 {
    double u0, u1, v0, v1;
};

// Removes `hole` from every rectangle in `in`. Each result has positive area.
std::vector<Rect2> subtractRect(const std::vector<Rect2>& in, const Rect2& hole) {
    std::vector<Rect2> out;
    for (const Rect2& r : in) {
        const double ix0 = std::max(r.u0, hole.u0), ix1 = std::min(r.u1, hole.u1);
        const double iy0 = std::max(r.v0, hole.v0), iy1 = std::min(r.v1, hole.v1);
        if (!(ix1 > ix0 && iy1 > iy0)) {
            out.push_back(r);
            continue;
        }
        if (iy0 > r.v0) out.push_back({r.u0, r.u1, r.v0, iy0});  // below the hole
        if (iy1 < r.v1) out.push_back({r.u0, r.u1, iy1, r.v1});  // above the hole
        if (ix0 > r.u0) out.push_back({r.u0, ix0, iy0, iy1});    // left of the hole
        if (ix1 < r.u1) out.push_back({ix1, r.u1, iy0, iy1});    // right of the hole
    }
    return out;
}

}  // namespace

std::vector<StructureBox> generateStairs(const AABB& box, const StairOptions& opt, std::string* err) {
    if (err) err->clear();
    if (opt.axis != 0 && opt.axis != 1) return fail(err, "stairs: axis must be 0 (X) or 1 (Y)");
    if (!box.valid() || !finiteAABB(box)) return fail(err, "stairs: box is invalid");
    if (!std::isfinite(opt.maxRise) || opt.maxRise < 1) return fail(err, "stairs: maxRise must be at least 1");
    if (!std::isfinite(opt.minRun) || opt.minRun <= 0) return fail(err, "stairs: minRun must be positive");
    if (!opt.solidUnderneath && !(opt.treadThickness > 0)) return fail(err, "stairs: treadThickness must be positive");

    const int a = opt.axis;      // climb axis
    const int o = 1 - a;         // the other horizontal axis (the stair's width)

    // Whole units: the height and run are floored, and every interior edge is rounded onto the grid.
    const double height = std::floor(box.maxs.z - box.mins.z);
    const double run = std::floor(box.maxs[a] - box.mins[a]);
    if (height < 1) return fail(err, "stairs: box is less than 1 unit high");

    // Rises are whole units no larger than floor(maxRise). The step count is ceil(height / maxRise).
    const double maxRise = std::floor(opt.maxRise);
    const int n = std::max(1, static_cast<int>(std::ceil(height / maxRise)));

    std::vector<double> top(n + 1), edge(n + 1);
    for (int i = 0; i <= n; ++i) {
        top[i] = std::round(height * i / n);
        edge[i] = std::round(run * i / n);
    }
    top[n] = height;
    edge[n] = run;

    double minDepth = std::numeric_limits<double>::infinity();
    for (int i = 0; i < n; ++i) minDepth = std::min(minDepth, edge[i + 1] - edge[i]);
    if (minDepth < opt.minRun || minDepth < 1) {
        return fail(err, "stairs: run is too short for " + std::to_string(n) + " steps at minRun " +
                             std::to_string(opt.minRun));
    }

    std::vector<StructureBox> out;
    out.reserve(n);
    const double base = box.mins.z;
    for (int i = 0; i < n; ++i) {
        // Forward steps start at the low end; reversed steps start at the high end and run toward -axis.
        const double s0 = opt.reverse ? box.maxs[a] - edge[i + 1] : box.mins[a] + edge[i];
        const double s1 = opt.reverse ? box.maxs[a] - edge[i] : box.mins[a] + edge[i + 1];
        const double zTop = base + top[i + 1];
        const double zBot = opt.solidUnderneath ? base : std::max(base, zTop - opt.treadThickness);

        Vec3 mn, mx;
        mn[a] = s0;
        mx[a] = s1;
        mn[o] = box.mins[o];
        mx[o] = box.maxs[o];
        mn.z = zBot;
        mx.z = zTop;
        out.push_back({makeBox(mn, mx), StructurePart::Stair});
    }
    if (err) err->clear();
    return out;
}

std::vector<StructureBox> generateBuilding(const AABB& box, const BuildingOptions& opt, std::string* err) {
    if (err) err->clear();
    if (!box.valid() || !finiteAABB(box)) return fail(err, "building: box is invalid");

    const double x0 = box.mins.x, y0 = box.mins.y, z0 = box.mins.z;
    const double x1 = box.maxs.x, y1 = box.maxs.y;
    const double fx = x1 - x0, fy = y1 - y0;
    if (fx < kMinFootprint || fy < kMinFootprint) return fail(err, "building: footprint is smaller than 128 x 128");

    if (opt.floors < 1 || opt.floors > kMaxFloors) return fail(err, "building: floors must be 1 to 64");
    const double H = opt.storeyHeight, t = opt.wallThickness, ft = opt.floorThickness;
    const double all[] = {H, t, ft, opt.doorWidth, opt.doorHeight, opt.windowWidth, opt.windowHeight, opt.windowSill};
    for (double v : all) {
        if (!std::isfinite(v)) return fail(err, "building: options must be finite");
    }
    if (!(t > 0) || !(ft > 0)) return fail(err, "building: wallThickness and floorThickness must be positive");
    if (2 * t + 32 > std::min(fx, fy)) return fail(err, "building: walls are too thick for the footprint");
    const double wallH = H - ft;  // wall height in every storey
    if (wallH < kMinWallHeight) {
        return fail(err, "building: storeyHeight must leave at least 32 units of wall above the floor slab");
    }
    if (opt.doorWidth < 0 || opt.doorHeight < 0 || opt.windowWidth < 0 || opt.windowHeight < 0 || opt.windowSill < 0) {
        return fail(err, "building: openings cannot be negative");
    }
    const bool hasDoor = opt.doorWidth > 0;
    if (hasDoor) {
        if (!(opt.doorHeight > 0 && opt.doorHeight <= wallH)) {
            return fail(err, "building: doorHeight must be positive and fit in the storey");
        }
        if (opt.doorWidth > fx - 2 * t) return fail(err, "building: door is wider than the wall");
    }
    if ((opt.windowWidth > 0) != (opt.windowHeight > 0)) {
        return fail(err, "building: windowWidth and windowHeight must both be set or both be 0");
    }
    const bool hasWindows = opt.windowWidth > 0;
    if (hasWindows) {
        if (opt.windowSill + opt.windowHeight > wallH) return fail(err, "building: window does not fit in the storey");
        if (opt.windowWidth > std::min(fx, fy) - 2 * t) return fail(err, "building: window is wider than the wall");
    }

    // The inner staircase hugs the +X wall and climbs along +Y from the -Y side. It only exists with two or more floors.
    const bool stairs = opt.stairs && opt.floors >= 2;
    const double stairSpan = stairs ? std::min(kStairSpan, (fx - 2 * t) - 16) : 0;
    if (stairs && stairSpan < kMinStairSpan) {
        return fail(err, "building: footprint is too narrow for the inner stairs");
    }
    const Rect2 stairHole{x1 - t - stairSpan, x1 - t, y0 + t, y1 - t};

    // Height of the floor surface of storey k (k = 0 is the ground floor) and the bottom of slab k.
    auto walk = [&](int k) { return z0 + k * H + ft; };
    auto slabBottom = [&](int k) { return z0 + k * H; };

    std::vector<StructureBox> out;

    // Floor slabs. The first upper slab has an opening over the staircase.
    for (int k = 0; k < opt.floors; ++k) {
        std::vector<Rect2> pieces{{x0, x1, y0, y1}};
        if (stairs && k == 1) pieces = subtractRect(pieces, stairHole);
        for (const Rect2& p : pieces) {
            out.push_back({makeBox(Vec3(p.u0, p.v0, slabBottom(k)), Vec3(p.u1, p.v1, slabBottom(k) + ft)),
                           StructurePart::Floor});
        }
    }

    if (stairs) {
        const AABB shaft = makeBox(Vec3(stairHole.u0, stairHole.v0, walk(0)), Vec3(stairHole.u1, stairHole.v1, walk(1)));
        StairOptions so;
        so.axis = 1;
        std::string sub;
        std::vector<StructureBox> flight = generateStairs(shaft, so, &sub);
        if (flight.empty()) return fail(err, "building: stairs: " + sub);
        out.insert(out.end(), flight.begin(), flight.end());
    }

    // Walls. -Y and +Y span the full X width, so the corners belong to them; -X and +X fill the space between.
    enum class Side { NegY, PosY, NegX, PosX };
    auto wallBox = [&](Side s, const Rect2& r) {
        switch (s) {
            case Side::NegY: return makeBox(Vec3(r.u0, y0, r.v0), Vec3(r.u1, y0 + t, r.v1));
            case Side::PosY: return makeBox(Vec3(r.u0, y1 - t, r.v0), Vec3(r.u1, y1, r.v1));
            case Side::NegX: return makeBox(Vec3(x0, r.u0, r.v0), Vec3(x0 + t, r.u1, r.v1));
            case Side::PosX: return makeBox(Vec3(x1 - t, r.u0, r.v0), Vec3(x1, r.u1, r.v1));
        }
        return AABB{};
    };
    auto emitWall = [&](Side s, const Rect2& extent, const std::vector<Rect2>& holes) {
        std::vector<Rect2> pieces{extent};
        for (const Rect2& h : holes) pieces = subtractRect(pieces, h);
        for (const Rect2& p : pieces) out.push_back({wallBox(s, p), StructurePart::Wall});
    };

    const double cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
    for (int k = 0; k < opt.floors; ++k) {
        const double wz0 = walk(k), wz1 = slabBottom(k + 1);
        std::vector<Rect2> doorHoles;
        if (hasDoor && k == 0) {
            doorHoles.push_back({cx - opt.doorWidth / 2, cx + opt.doorWidth / 2, wz0, wz0 + opt.doorHeight});
        }
        // A window centred on a wall at horizontal position `centre` (u), sill above this storey's floor.
        auto windowAt = [&](double centre) {
            const double half = opt.windowWidth / 2;
            return Rect2{centre - half, centre + half, wz0 + opt.windowSill, wz0 + opt.windowSill + opt.windowHeight};
        };

        emitWall(Side::NegY, {x0, x1, wz0, wz1}, doorHoles);
        if (hasWindows) {
            emitWall(Side::PosY, {x0, x1, wz0, wz1}, {windowAt(cx)});
            emitWall(Side::NegX, {y0 + t, y1 - t, wz0, wz1}, {windowAt(cy)});
            emitWall(Side::PosX, {y0 + t, y1 - t, wz0, wz1}, {windowAt(cy)});
        } else {
            emitWall(Side::PosY, {x0, x1, wz0, wz1}, {});
            emitWall(Side::NegX, {y0 + t, y1 - t, wz0, wz1}, {});
            emitWall(Side::PosX, {y0 + t, y1 - t, wz0, wz1}, {});
        }
    }

    if (opt.roof) {
        const double rz = z0 + opt.floors * H;
        out.push_back({makeBox(Vec3(x0, y0, rz), Vec3(x1, y1, rz + ft)), StructurePart::Roof});
    }
    return out;
}

const char* structurePartName(StructurePart p) {
    switch (p) {
        case StructurePart::Floor: return "floor";
        case StructurePart::Wall: return "wall";
        case StructurePart::Ceiling: return "ceiling";
        case StructurePart::Roof: return "roof";
        case StructurePart::Stair: return "stair";
        case StructurePart::Landing: return "landing";
        case StructurePart::Trim: return "trim";
    }
    return "unknown";
}

}  // namespace iw8r
