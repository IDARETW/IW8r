#include "nav_debug.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <sstream>

#include "json_lite.h"

namespace iw8r {
namespace {

using json::Value;

constexpr double kNoFloor = -99999.0;       // writer's sentinel for an empty column
constexpr double kNodeCross = 12.0;         // half length of the node cross and tick, world units
constexpr long long kMaxGridCells = 1LL << 24;

bool fail(std::string* err, const std::string& message) {
    if (err) *err = message;
    return false;
}

bool readNumber(const Value* v, double& out) {
    if (!v) return false;
    const auto n = v->number();
    if (!n || !std::isfinite(*n)) return false;
    out = *n;
    return true;
}

bool readInt(const Value* v, long long& out) {
    double d = 0;
    if (!readNumber(v, d)) return false;
    if (d != std::floor(d) || std::fabs(d) > 2147483647.0) return false;
    out = static_cast<long long>(d);
    return true;
}

// [x, y, z, ...] with at least three finite numbers.
bool readVec3(const Value& v, Vec3& out) {
    if (v.type != Value::Type::Array || v.arr.size() < 3) return false;
    double x = 0, y = 0, z = 0;
    if (!readNumber(v.at(0), x) || !readNumber(v.at(1), y) || !readNumber(v.at(2), z)) return false;
    out = Vec3{x, y, z};
    return true;
}

std::string fmt(double value) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(1) << value;
    return os.str();
}

std::string fmtVec(const Vec3& p) {
    return "(" + fmt(p.x) + ", " + fmt(p.y) + ", " + fmt(p.z) + ")";
}

}  // namespace

bool readNavDebug(const std::string& jsonText, NavDebug& out, std::string* err) {
    out = NavDebug{};
    std::string parseErr;
    const auto root = json::parse(jsonText, &parseErr);
    if (!root) return fail(err, "not JSON: " + parseErr);
    if (root->type != Value::Type::Object) return fail(err, "top level is not an object");

    NavDebug d;
    if (!readNumber(root->get("minX"), d.minX) || !readNumber(root->get("minY"), d.minY) ||
        !readNumber(root->get("cell"), d.cell))
        return fail(err, "minX, minY or cell missing or not a number");
    if (!(d.cell > 0)) return fail(err, "cell must be greater than 0");

    long long nx = 0, ny = 0;
    if (!readInt(root->get("nx"), nx) || !readInt(root->get("ny"), ny) || nx < 0 || ny < 0)
        return fail(err, "nx or ny missing or not a non-negative integer");
    if (nx * ny > kMaxGridCells) return fail(err, "height grid too large");
    d.nx = static_cast<int>(nx);
    d.ny = static_cast<int>(ny);

    const Value* top = root->get("top");
    if (!top || top->type != Value::Type::Array || static_cast<long long>(top->arr.size()) != nx * ny)
        return fail(err, "top must be an array of nx * ny heights");
    d.top.reserve(top->arr.size());
    for (const Value& height : top->arr) {
        double h = 0;
        if (!readNumber(&height, h)) return fail(err, "top contains a non-numeric height");
        d.top.push_back(h);
    }

    const Value* areas = root->get("areas");
    if (!areas || areas->type != Value::Type::Array) return fail(err, "areas missing or not an array");
    for (const Value& area : areas->arr) {
        if (area.type != Value::Type::Array || area.arr.size() < 3)
            return fail(err, "an area must have at least 3 corners");
        std::vector<Vec3> poly;
        std::vector<int> adjacent;
        poly.reserve(area.arr.size());
        adjacent.reserve(area.arr.size());
        for (const Value& corner : area.arr) {
            Vec3 p;
            long long adj = 0;
            if (corner.type != Value::Type::Array || corner.arr.size() != 4 || !readVec3(corner, p) ||
                !readInt(corner.at(3), adj))
                return fail(err, "an area corner must be [x, y, z, adjacent]");
            if (adj < -1 || adj >= static_cast<long long>(areas->arr.size()))
                return fail(err, "area adjacency is out of range");
            poly.push_back(p);
            adjacent.push_back(static_cast<int>(adj));
        }
        d.areaPolys.push_back(std::move(poly));
        d.areaAdjacent.push_back(std::move(adjacent));
    }

    const Value* nodes = root->get("nodes");
    if (!nodes || nodes->type != Value::Type::Array) return fail(err, "nodes missing or not an array");
    std::vector<std::vector<long long>> linkTargets;
    linkTargets.reserve(nodes->arr.size());
    for (const Value& node : nodes->arr) {
        if (node.type != Value::Type::Array || node.arr.size() != 5)
            return fail(err, "a node must be [x, y, z, zone, [links]]");
        Vec3 p;
        long long zone = 0;
        const Value& links = node.arr[4];
        if (!readVec3(node, p) || !readInt(node.at(3), zone) || zone < 0)
            return fail(err, "node position or zone is invalid");
        if (links.type != Value::Type::Array) return fail(err, "node links must be an array");
        std::vector<long long> targets;
        for (const Value& target : links.arr) {
            long long index = 0;
            if (!readInt(&target, index)) return fail(err, "node link index is not an integer");
            targets.push_back(index);
        }
        d.nodes.push_back(p);
        d.nodeZones.push_back(static_cast<unsigned>(zone));
        linkTargets.push_back(std::move(targets));
    }
    const long long nodeCount = static_cast<long long>(d.nodes.size());
    for (std::size_t from = 0; from < linkTargets.size(); ++from) {
        for (const long long to : linkTargets[from]) {
            if (to < 0 || to >= nodeCount) return fail(err, "node link index is out of range");
            d.links.emplace_back(d.nodes[from], d.nodes[static_cast<std::size_t>(to)]);
        }
    }

    const Value* spawns = root->get("spawns");
    if (!spawns || spawns->type != Value::Type::Array) return fail(err, "spawns missing or not an array");
    for (const Value& spawn : spawns->arr) {
        Vec3 p;
        if (!readVec3(spawn, p)) return fail(err, "a spawn must be [x, y, z]");
        d.spawns.push_back(p);
    }

    long long fixed = 0, zones = 0;
    if (!readInt(root->get("fixed"), fixed) || !readInt(root->get("zones"), zones) || fixed < 0 || zones < 0)
        return fail(err, "fixed or zones missing or not a non-negative integer");
    d.fixedCount = static_cast<unsigned>(fixed);
    d.zoneCount = static_cast<unsigned>(zones);

    out = std::move(d);
    return true;
}

std::vector<std::pair<Vec3, Vec3>> navDebugLines(const NavDebug& dump, double zLift) {
    std::vector<std::pair<Vec3, Vec3>> lines;
    const auto up = [zLift](Vec3 p) {
        p.z += zLift;
        return p;
    };
    for (const auto& poly : dump.areaPolys) {
        const std::size_t n = poly.size();
        for (std::size_t k = 0; k < n; ++k) lines.emplace_back(up(poly[k]), up(poly[(k + 1) % n]));
    }
    for (const Vec3& node : dump.nodes) {
        const Vec3 c = up(node);
        lines.emplace_back(c - Vec3{kNodeCross, 0, 0}, c + Vec3{kNodeCross, 0, 0});
        lines.emplace_back(c - Vec3{0, kNodeCross, 0}, c + Vec3{0, kNodeCross, 0});
        lines.emplace_back(c, c + Vec3{0, 0, kNodeCross});
    }
    for (const auto& [from, to] : dump.links) lines.emplace_back(up(from), up(to));
    return lines;
}

std::string navDebugSummary(const NavDebug& dump) {
    std::size_t corners = 0;
    bool any = false;
    Vec3 lo, hi;
    const auto grow = [&](const Vec3& p) {
        if (!any) {
            lo = hi = p;
            any = true;
            return;
        }
        for (int axis = 0; axis < 3; ++axis) {
            lo[axis] = std::min(lo[axis], p[axis]);
            hi[axis] = std::max(hi[axis], p[axis]);
        }
    };
    for (const auto& poly : dump.areaPolys) {
        corners += poly.size();
        for (const Vec3& p : poly) grow(p);
    }
    for (const Vec3& node : dump.nodes) grow(node);

    std::size_t walkable = 0;
    for (const double h : dump.top)
        if (h > kNoFloor) ++walkable;

    std::ostringstream os;
    os << "nav debug: " << dump.areaPolys.size() << " areas (" << corners << " corners), "
       << dump.nodes.size() << " nodes in " << dump.zoneCount << " zones (" << dump.fixedCount
       << " fixed), " << dump.links.size() << " links, " << dump.spawns.size() << " spawns; grid "
       << dump.nx << "x" << dump.ny << " cell " << fmt(dump.cell) << ", walkable columns " << walkable
       << "/" << dump.top.size();
    if (any) os << "; bounds " << fmtVec(lo) << " to " << fmtVec(hi);
    return os.str();
}

}  // namespace iw8r
