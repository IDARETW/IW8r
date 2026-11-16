#include "player_mantle.h"

#include <algorithm>
#include <cmath>

namespace iw8r {

namespace {

// walk_mantle.cpp constants (MakeStandardMpMantleProfile / Mantle_CalcEndPos / Mantle_FindMantleEdge).
constexpr double kMinLedgeHeight = 16.0;     // minimumCandidateHeight
constexpr double kMaxLedgeHeight = 72.0;     // maximumCandidateHeight
constexpr double kChestHeight = 29.0;        // searchCenterHeight
constexpr double kWallReach = 40.0;          // chest probe: body radius + 25 (the candidate search is short)
constexpr double kScanRange = 40.0;          // how far past the face the top is searched
constexpr double kLedgeProbePadding = 4.0;   // ledgeProbePadding: endpoint 4 units beyond the edge
constexpr double kClearanceAdd = 15.0;       // ordinary edge clearance: height + 15
constexpr double kWalkableNormal = 0.7;      // cos(45.57 deg)
constexpr double kSupportHeightTolerance = 2.0;
constexpr double kHighMantleHeight = 58.0;   // duration branch for ledges above this
constexpr double kEncodeScale = 190.0;       // mantleHeightScale
constexpr int kSamplesPerSegment = 64;       // samplesPerSegment

Vec3 flat(Vec3 v) { return {v.x, v.y, 0}; }

Vec3 bezier(const MantleBezier& s, double t) {
    const double u = 1.0 - t;
    return s.p0 * (u * u * u) + s.p1 * (3.0 * t * u * u) + s.p2 * (3.0 * t * t * u) + s.p3 * (t * t * t);
}

}  // namespace

MantlePath buildMantlePath(const std::vector<Vec3>& points) {
    MantlePath path;
    if (points.size() < 2) return path;
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        const Vec3 a = points[i], b = points[i + 1];
        const Vec3 p3 = i + 2 < points.size() ? a * (1.0 / 6.0) + b * (2.0 / 3.0) + points[i + 2] * (1.0 / 6.0) : b;
        MantleBezier seg{i == 0 ? a : path.segments.back().p3, a * (2.0 / 3.0) + b * (1.0 / 3.0),
                         a * (1.0 / 3.0) + b * (2.0 / 3.0), p3, 0.0};
        Vec3 prev = bezier(seg, 0.0);
        for (int k = 1; k <= kSamplesPerSegment; ++k) {
            const Vec3 cur = bezier(seg, double(k) / kSamplesPerSegment);
            seg.arcLength += length(cur - prev);
            prev = cur;
        }
        path.arcLength += seg.arcLength;
        path.segments.push_back(seg);
    }
    return path;
}

Vec3 evaluateMantlePath(const MantlePath& path, double normalizedArc) {
    if (path.segments.empty() || path.arcLength <= 0) return {};
    const double target = std::clamp(normalizedArc, 0.0, 1.0) * path.arcLength;
    double preceding = 0;
    for (size_t i = 0; i < path.segments.size(); ++i) {
        const auto& seg = path.segments[i];
        const double next = preceding + seg.arcLength;
        if (target <= next || i + 1 == path.segments.size()) {
            if (seg.arcLength <= 0) return seg.p3;
            return bezier(seg, std::clamp((target - preceding) / seg.arcLength, 0.0, 1.0));
        }
        preceding = next;
    }
    return path.segments.back().p3;
}

std::vector<Vec3> buildMantleWaypoints(Vec3 origin, Vec3 firstOffset) {
    // walk_mantle.cpp BuildMantleWaypoints, normal (non-over) branch.
    static constexpr double heights[6] = {24, 32, 40, 48, 56, 72};
    static constexpr double midpoints[6] = {0.4, 0.4, 0.5, 0.5, 0.55, 0.55};
    static constexpr double offsets[6] = {-10, -9, -7, -6, -5, -1};
    const double value = std::clamp(firstOffset.z, 24.0, 72.0);
    size_t upper = 1;
    while (upper + 1 < 6 && value > heights[upper]) ++upper;
    const double range = heights[upper] - heights[upper - 1];
    const double f = range > 1e-6 ? (value - heights[upper - 1]) / range : 0.0;
    const double midpoint = (1 - f) * midpoints[upper - 1] + f * midpoints[upper];
    const double verticalOffset = (1 - f) * offsets[upper - 1] + f * offsets[upper];
    const Vec3 first = origin + firstOffset;
    const Vec3 middle{origin.x + firstOffset.x * midpoint, origin.y + firstOffset.y * midpoint,
                      first.z + verticalOffset};
    return {origin, middle, first};
}

double mantleDurationSeconds(double ledgeHeight, double arcLength, double horizontalSpeed) {
    // Height pack: 8 bits over the 190-unit scale (EncodeMantleHeight8 / DecodeMantleHeight).
    const double clamped = std::clamp(ledgeHeight, -kEncodeScale, kEncodeScale);
    const double height = std::round(std::fabs(clamped) / kEncodeScale * 255.0) / 255.0 * kEncodeScale;
    (void)arcLength;  // only the over/ladder branches of the engine formula use the arc length
    double lowThreshold, highThreshold, lowFactor, highFactor;
    if (height <= kHighMantleHeight) {
        lowThreshold = 24;
        highThreshold = 58;
        lowFactor = 0.4;
        highFactor = 0.9;
    } else {
        lowThreshold = 58;
        highThreshold = 72;
        lowFactor = 0.9;
        highFactor = 1.0;
    }
    const double heightValue = std::max(height, lowThreshold);
    const double heightFraction = (std::min(heightValue, highThreshold) - lowThreshold) / (highThreshold - lowThreshold);
    const double speedFraction = (std::clamp(horizontalSpeed, 60.0, 250.0) - 60.0) / 190.0;
    const double modeScale[2] = {1.0, 0.75};  // normalDurationScale
    const double modeFactor = (1.0 - speedFraction) * modeScale[0] + speedFraction * modeScale[1];
    return modeFactor * ((1.0 - heightFraction) * lowFactor + heightFraction * highFactor);
}

Vec3 mantleExitVelocity(Vec3 origin, Vec3 landing, double durationSeconds) {
    // Replay120MantleExitVelocity, ordinary branch: the endpoint offset spread over the rest of the mantle, capped.
    const Vec3 offset = landing - origin;
    const double distance = length(offset);
    Vec3 v;
    if (distance > 0 && durationSeconds > 0)
        v = normalize(offset) * std::min((1.0 / durationSeconds) * distance, 300.0);
    if (v.z > 0) v.z = 0;  // Mantle_Finish removes positive world-up velocity
    return v;
}

MantleCandidate findMantleCandidate(const CollisionWorld& world, Vec3 origin, Vec3 dir, double radius,
                                    double standHeight, double crouchHeight, double currentHeight) {
    MantleCandidate out;
    dir = normalize(flat(dir));
    if (length(dir) < 0.5) return out;
    const Vec3 up{0, 0, 1};

    // 1. A face in front of the body. The game queries the map's precomputed ledge edges in a frustum ahead of the
    // player (Mantle_FindMantleEdge: EdgeOctreeQuery, half height mantle_check_height / 2 around the search origin +
    // that half, from the radius - mantle_edge_search_near_plane_offset out; research 140 section 8.8), so a ledge low
    // in that band is found too, in the air as well. The editor has no edge data: rays across the band (every 4 units
    // from the lowest ledge height up, the 29 search centre among them) find the nearest face instead of one ray at 29.
    double faceDist = 1e300;
    for (double hgt = kMinLedgeHeight + 1; hgt <= 2 * kChestHeight; hgt += 4) {
        const Vec3 at = origin + up * hgt;
        const auto wall = world.traceRay(at, at + dir * kWallReach);
        if (wall.startSolid || wall.allSolid || wall.fraction >= 1) continue;
        if (dot(wall.plane.n, dir) > -0.5 || std::fabs(wall.plane.n.z) >= kWalkableNormal) continue;
        faceDist = std::min(faceDist, dot(flat(wall.end - origin), dir));
    }
    if (faceDist > 1e299) return out;

    // 2. The first downward hit beyond the face, between the minimum and maximum ledge heights.
    double edgeDist = -1;
    Vec3 top;
    for (double d = faceDist + 0.5; d <= faceDist + kScanRange; d += 1.0) {
        const Vec3 xy = origin + dir * d;
        const auto hit = world.traceRay({xy.x, xy.y, origin.z + kMaxLedgeHeight}, {xy.x, xy.y, origin.z + kMinLedgeHeight});
        if (hit.startSolid || hit.allSolid || hit.fraction >= 1) continue;  // inside a wall, or nothing in range
        if (hit.plane.n.z < kWalkableNormal) continue;                       // a face, not a top
        top = hit.end;
        edgeDist = d;
        break;
    }
    if (edgeDist < 0) return out;
    const double ledge = top.z - origin.z;
    if (ledge < kMinLedgeHeight || ledge > kMaxLedgeHeight) return out;

    // 3. Head clearance for a ledge that is taller than the standing body.
    const double clearance = ledge + kClearanceAdd;
    if (clearance > currentHeight) {
        const auto head = world.traceRay(origin, origin + up * clearance);
        if (head.startSolid || head.allSolid || head.fraction < 1) return out;
    }

    // 4. The body box passes over the top and reaches the padded endpoint.
    const double reach = edgeDist + kLedgeProbePadding;
    const Vec3 probeStart = origin + up * clearance;
    const Vec3 probeEnd = probeStart + dir * reach;
    const auto body = world.traceBox(probeStart, probeEnd, {-radius, -radius, -radius}, {radius, radius, radius});
    if (body.startSolid || body.allSolid || body.fraction < 1) return out;

    // 5. The feet land on the top at the endpoint (a too-thin top drops the support below the ledge height).
    const Vec3 feet = origin + up * clearance + dir * reach;
    const auto support = world.traceCapsule(feet, {feet.x, feet.y, origin.z}, radius, 2 * radius);
    if (support.startSolid || support.allSolid || support.fraction >= 1 || support.plane.n.z < kWalkableNormal) return out;
    if (std::fabs(support.end.z - top.z) > kSupportHeightTolerance) return out;

    // 6. The crouched body must fit on the top; the standing body decides whether the mantle ends crouched.
    const Vec3 landing = support.end;
    if (world.traceCapsule(landing, landing, radius, crouchHeight).startSolid) return out;
    out.forceCrouch = world.traceCapsule(landing, landing, radius, standHeight).startSolid;
    out.valid = true;
    out.landing = landing;
    out.ledgeHeight = ledge;
    return out;
}

}  // namespace iw8r
