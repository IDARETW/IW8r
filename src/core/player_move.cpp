#include "player_move.h"

#include <algorithm>
#include <cmath>

// Rules and constants: docs/research/140_movement_parity.md (the Replay 1.20 exe functions named there).

namespace iw8r {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kOverbounce = 1.001;
constexpr double kMantlePushMin = 85.0 / 127.0;  // Replay120MantleWantsToActivate: dot * wish > 85 (of 127)
constexpr double kLongAgo = 1e6;                 // "long ago" for the timers

Vec3 clipVelocity(const Vec3& v, const Vec3& n, double overbounce) {
    double backoff = dot(v, n);
    backoff = backoff < 0 ? backoff * overbounce : backoff / overbounce;
    return v - n * backoff;
}

Vec3 flat(const Vec3& v) { return {v.x, v.y, 0}; }

struct Ctx {
    const CollisionWorld& w;
    const MoveParams& p;
    PlayerState& s;
    Vec3 mins, maxs;
};

void groundTrace(Ctx& c) {
    const Vec3 down = c.s.origin - Vec3{0, 0, 0.25};  // playerCollisionTraceLength 0.25
    const auto tr = c.w.traceCapsule(c.s.origin, down, c.p.radius, c.maxs.z);
    if (tr.fraction == 1.0 || tr.allSolid) {
        c.s.onGround = false;
        return;
    }
    if (c.s.velocity.z > 0 && dot(c.s.velocity, tr.plane.n) > 10) {  // moving up and away: a jump just started
        c.s.onGround = false;
        return;
    }
    // The game's ground trace is a flat-bottomed cylinder reaching 16 units down (collision type 1): when the capsule's
    // round bottom rests on a steep face, the floor under the feet is what the player stands on.
    if (tr.plane.n.z < c.p.minWalkNormal) {
        const auto under = c.w.traceRay(c.s.origin + Vec3{0, 0, 1}, c.s.origin - Vec3{0, 0, c.p.stepDownSnap});
        if (under.fraction < 1 && !under.startSolid && under.plane.n.z >= c.p.minWalkNormal) {
            c.s.groundNormal = under.plane.n;
            c.s.onGround = true;
            return;
        }
    }
    // Ledge walk (Replay120GroundTrace / PM_GroundTrace_ProbeAndResolve 0x140CD90C0): a grounded player, not within
    // 0.5 s of a jump, keeps level support on a plane too steep to stand on for up to 0.3 s, so a low steep prop is
    // walked over instead of sliding the player back.
    if (tr.plane.n.z < c.p.minWalkNormal && c.s.onGround && !c.s.jumpingState && c.s.sinceJump >= c.p.jumpMinInterval &&
        c.s.steepTime <= c.p.steepSupportSeconds) {
        c.s.groundNormal = Vec3{0, 0, 1};
        c.s.steepContact = true;
        return;
    }
    c.s.groundNormal = tr.plane.n;
    c.s.onGround = tr.plane.n.z >= c.p.minWalkNormal;
}

// Quake 3 PM_SlideMove: up to four bumps, clipping the velocity against every plane touched. On the ground a face
// steeper than 0.7 against the ground plane is clipped as a wall standing on the ground (PM_ClipSlideVelocity
// 0x14111E9B0: the face normal minus its ground component), so walking into a steep low face does not ride up it.
bool slideMove(Ctx& c, double dt, bool gravity) {
    Vec3 planes[6];
    int numPlanes = 0;
    const bool grounded = c.s.onGround;
    const Vec3 g = c.s.groundNormal;
    const auto asWall = [&](const Vec3& n) {
        if (!grounded) return n;
        const double d = dot(g, n);
        if (d > 0.7 || std::fabs(d + 1.0) < 0.001) return n;
        const Vec3 w = n - g * d;
        if (length(w) < 1e-9) return n;
        const Vec3 wn = normalize(w);
        return dot(wn, c.s.velocity) > 0 ? n : wn;
    };
    Vec3 endVelocity = c.s.velocity;
    if (gravity) {
        endVelocity.z -= c.p.gravity * dt;
        c.s.velocity.z = (c.s.velocity.z + endVelocity.z) * 0.5;
        if (c.s.onGround) c.s.velocity = clipVelocity(c.s.velocity, c.s.groundNormal, kOverbounce);
    }
    if (c.s.onGround) planes[numPlanes++] = c.s.groundNormal;
    if (length(c.s.velocity) > 1e-9) planes[numPlanes++] = normalize(c.s.velocity);
    double timeLeft = dt;
    int bump = 0;
    for (; bump < 4; ++bump) {
        const Vec3 end = c.s.origin + c.s.velocity * timeLeft;
        const auto tr = c.w.traceCapsule(c.s.origin, end, c.p.radius, c.maxs.z);
        if (tr.allSolid) {
            c.s.velocity.z = 0;
            return true;
        }
        if (tr.fraction > 0) c.s.origin = tr.end;
        if (tr.fraction == 1) break;
        timeLeft -= timeLeft * tr.fraction;
        if (numPlanes >= 6) {
            c.s.velocity = Vec3{};
            return true;
        }
        const Vec3 hitN = asWall(tr.plane.n);
        // A plane hit twice: nudge the velocity out so the box does not stick.
        bool repeat = false;
        for (int i = 0; i < numPlanes; ++i)
            if (dot(hitN, planes[i]) > 0.99) {
                c.s.velocity = c.s.velocity + hitN;
                repeat = true;
                break;
            }
        if (repeat) continue;
        planes[numPlanes++] = hitN;
        for (int i = 0; i < numPlanes; ++i) {
            if (dot(c.s.velocity, planes[i]) >= 0.1) continue;
            Vec3 clip = clipVelocity(c.s.velocity, planes[i], kOverbounce);
            Vec3 endClip = clipVelocity(endVelocity, planes[i], kOverbounce);
            for (int j = 0; j < numPlanes; ++j) {
                if (j == i || dot(clip, planes[j]) >= 0.1) continue;
                clip = clipVelocity(clip, planes[j], kOverbounce);
                endClip = clipVelocity(endClip, planes[j], kOverbounce);
                if (dot(clip, planes[i]) >= 0) continue;
                // Two planes: slide along their crease.
                const Vec3 dir = normalize(cross(planes[i], planes[j]));
                clip = dir * dot(dir, c.s.velocity);
                endClip = dir * dot(dir, endVelocity);
                for (int k = 0; k < numPlanes; ++k) {
                    if (k == i || k == j || dot(clip, planes[k]) >= 0.1) continue;
                    c.s.velocity = Vec3{};  // three planes: stop dead
                    return true;
                }
            }
            c.s.velocity = clip;
            endVelocity = endClip;
            break;
        }
    }
    if (gravity) c.s.velocity = endVelocity;
    return bump != 0;
}

// PM_StepSlideMove (0x14112F370): a blocked move is tried again from up to 18 units higher (an up trace of 19, step =
// fraction * 19 - 1) and put back down; the step is kept only if it lands on a plane with normal z >= 0.3, has gained
// no more than 36 x that normal z, and got further than the plain slide; otherwise the plain slide stands (no
// stepping onto steep faces, which slid the player back down: bobbing against low stock props). Stepping up is allowed
// only from the ground. (Walking down is the ground trace's stick extension, below.)
void stepSlideMove(Ctx& c, double dt, bool gravity, bool allowStep) {
    const Vec3 startO = c.s.origin, startV = c.s.velocity;
    if (!slideMove(c, dt, gravity)) return;  // moved the whole way
    if (!allowStep) return;
    const Vec3 down = startO - Vec3{0, 0, c.p.stepSize};
    const auto downTr = c.w.traceCapsule(startO, down, c.p.radius, c.maxs.z);
    // Moving up (a jump, not the trace of a slope clip left in the velocity) with no ground below: no step.
    if (startV.z > 10 && (downTr.fraction == 1.0 || dot(downTr.plane.n, Vec3{0, 0, 1}) < 0.7)) return;
    const Vec3 slideO = c.s.origin, slideV = c.s.velocity;
    const auto upTr = c.w.traceCapsule(startO, startO + Vec3{0, 0, c.p.stepSize + 1}, c.p.radius, c.maxs.z);
    if (upTr.allSolid) return;
    const double stepHeight = (c.p.stepSize + 1) * upTr.fraction - 1;
    if (stepHeight < 1) return;
    c.s.origin = startO + Vec3{0, 0, stepHeight};
    c.s.velocity = startV;
    slideMove(c, dt, gravity);
    const Vec3 dn = c.s.origin - Vec3{0, 0, stepHeight};
    const auto dnTr = c.w.traceCapsule(c.s.origin, dn, c.p.radius, c.maxs.z);
    if (!dnTr.allSolid) c.s.origin = dnTr.end;
    const auto keepPlain = [&] {
        c.s.origin = slideO;
        c.s.velocity = slideV;
    };
    // Progress along the original horizontal direction of motion.
    const double hv = std::hypot(startV.x, startV.y);
    const auto along = [&](const Vec3& a) {
        return hv > 1e-9 ? ((a.x - startO.x) * startV.x + (a.y - startO.y) * startV.y) / hv : 0.0;
    };
    if (dnTr.fraction < 1) {
        const double gained = c.s.origin.z - std::max(slideO.z, startO.z);
        if (dnTr.plane.n.z < c.p.minWalkNormal && !dnTr.startSolid) {
            // A landing on a face too steep to stand on. Replay keeps it for normal z >= 0.3; its flat ground cylinder
            // then stands on whatever is under its rim. The editor's capsule has a round bottom, so it keeps such a step
            // only when the stepped move really got further (over a crate's chamfered edge), not when it only crept up
            // a steep face (the bobbing against low stock props).
            if (along(c.s.origin) - along(slideO) < c.p.steepStepMinProgress) return keepPlain();
            // Not clipped by the steep face: that would turn the walk into vertical speed and stall the next step.
        } else if (gained > dnTr.plane.n.z * 36.0) {
            return keepPlain();
        } else {
            c.s.velocity = clipVelocity(c.s.velocity, dnTr.plane.n, kOverbounce);
        }
    }
    // Keep the step only if it got further along the original velocity than the plain slide.
    if (along(c.s.origin) <= along(slideO) + 0.001) {
        c.s.origin = slideO;
        c.s.velocity = slideV;
    }
}

// PM_Friction (0x141121160): drop = friction * max(100, speed) * scale * dt, on the ground only.
void applyFriction(Ctx& c, double dt, double scale) {
    const double speed = std::hypot(c.s.velocity.x, c.s.velocity.y);
    if (speed < 1) {
        c.s.velocity.x = c.s.velocity.y = 0;
        return;
    }
    const double drop = std::max(speed, c.p.stopSpeed) * c.p.friction * scale * dt;
    const double newSpeed = std::max(0.0, speed - drop) / speed;
    c.s.velocity.x *= newSpeed;
    c.s.velocity.y *= newSpeed;
}

// PM_Accelerate (0x14111C7D0): add = wishspeed - v.wishdir; accelspeed = min(accel * dt * max(100, wishspeed), add).
void accelerate(Ctx& c, const Vec3& wishDir, double wishSpeed, double accel, double dt) {
    const double current = dot(c.s.velocity, wishDir);
    const double add = wishSpeed - current;
    if (add <= 0) return;
    const double accelSpeed = std::min(accel * dt * std::max(c.p.accelMinSpeed, wishSpeed), add);
    c.s.velocity = c.s.velocity + wishDir * accelSpeed;
}

Vec3 stanceMaxs(const MoveParams& p, const PlayerState& s) {
    return Vec3{p.radius, p.radius, s.prone ? p.proneHeight : s.crouched ? p.crouchHeight : p.standHeight};
}

// Landing friction (Jump_GetTimedSpeedScale 0x141101D10): 2.0 for the first 100 ms of the 1000 ms window, then
// 1 + t/900 down to 1 when the window ends.
double landingFrictionScale(double ms) { return ms > 900 ? 2.0 : 1.0 + ms / 900.0; }

// Slide friction by slope (walk_slide.cpp SlideFrictionScale): moving downhill is nearly frictionless.
double slideFrictionScale(const MoveParams& p, const PlayerState& s) {
    const Vec3 h = flat(s.velocity);
    const double speed = length(h);
    if (!s.onGround || speed < 0.001) return p.slideFrictionNormal;
    const double uphillDot = (h.x / speed) * s.groundNormal.x + (h.y / speed) * s.groundNormal.y;
    if (uphillDot > 0.2) return p.slideFrictionDownhill;
    if (uphillDot < -0.2) return p.slideFrictionUphill;
    return p.slideFrictionNormal;
}

// The stance view-height curves (time fraction, height fraction), read from the Replay exe.
struct CurvePoint {
    double t, v;
};
const CurvePoint kStandCrouch[] = {{0, 0}, {0.01, 0.025}, {0.04, 0.075}, {0.3, 0.2}, {0.8, 0.8}, {0.9, 0.925}, {0.95, 0.975}, {1, 1}};
const CurvePoint kStandCrouchDirect[] = {{0, 0}, {0.01, 0.025}, {0.04, 0.075}, {0.3, 0.2}, {0.8, 0.8}, {1, 1}};
const CurvePoint kCrouchProne[] = {{0, 0},      {0.11, 0.069}, {0.22, 0.241}, {0.34, 0.517}, {0.45, 0.828},
                                   {0.5, 0.862}, {0.55, 0.828}, {0.7, 0.759},  {0.9, 0.793},  {1, 1}};
const CurvePoint kCrouchProneDirect[] = {{0, 0}, {0.45, 0.828}, {0.5, 0.862}, {0.55, 0.828}, {0.7, 0.759}, {0.9, 0.793}, {1, 1}};
const CurvePoint kProneCrouch[] = {{0, 0}, {0.05, -0.034}, {0.3, 0.345}, {0.5, 0.483}, {0.67, 0.69}, {0.83, 0.793}, {1, 1}};
const CurvePoint kCrouchStand[] = {{0, 0}, {0.05, 0.025}, {0.1, 0.075}, {0.2, 0.2}, {0.7, 0.8}, {0.96, 0.925}, {0.99, 0.975}, {1, 1}};

template <size_t N>
double curveAt(const CurvePoint (&pts)[N], double t) {
    if (t <= 0) return pts[0].v;
    for (size_t i = 1; i < N; ++i)
        if (t <= pts[i].t) {
            const double u = (t - pts[i - 1].t) / (pts[i].t - pts[i - 1].t);
            return pts[i - 1].v + (pts[i].v - pts[i - 1].v) * u;
        }
    return pts[N - 1].v;
}

int stanceRank(const MoveParams& p, double eye) { return eye <= p.proneEye ? 2 : eye <= p.crouchEye ? 1 : 0; }
double rankEye(const MoveParams& p, int r) { return r == 2 ? p.proneEye : r == 1 ? p.crouchEye : p.standEye; }

// Starts the lerp segment from the settled stance height s.viewTarget one stance towards `goalRank`.
void startViewSegment(const MoveParams& p, PlayerState& s, int goalRank) {
    const int from = stanceRank(p, s.viewTarget);
    const int to = goalRank > from ? from + 1 : from - 1;
    s.viewFrom = rankEye(p, from);
    s.viewTarget = rankEye(p, to);
    s.viewLerpT = 0;
    if (from == 0) {  // stand -> crouch (direct when the drop goes on to prone)
        s.viewDirect = goalRank == 2;
        s.viewCurve = int(s.viewDirect ? ViewCurve::StandCrouchDirect : ViewCurve::StandCrouch);
        s.viewLerpDur = s.mantling ? p.mantleCrouchLerp : p.crouchDownLerp;
    } else if (from == 1 && to == 2) {
        s.viewCurve = int(s.viewDirect ? ViewCurve::CrouchProneDirect : ViewCurve::CrouchProne);
        s.viewLerpDur = p.proneLerp;
    } else if (from == 1) {
        s.viewDirect = false;
        s.viewCurve = int(ViewCurve::CrouchStand);
        s.viewLerpDur = p.standLerp;
    } else {
        s.viewDirect = false;
        s.viewCurve = int(ViewCurve::ProneCrouch);
        s.viewLerpDur = p.crouchUpLerp;
    }
}

ViewCurve reverseCurve(ViewCurve c) {
    switch (c) {
        case ViewCurve::StandCrouch:
        case ViewCurve::StandCrouchDirect: return ViewCurve::CrouchStand;
        case ViewCurve::CrouchProne:
        case ViewCurve::CrouchProneDirect: return ViewCurve::ProneCrouch;
        case ViewCurve::ProneCrouch: return ViewCurve::CrouchProne;
        case ViewCurve::CrouchStand: return ViewCurve::StandCrouch;
    }
    return c;
}

// PM_ViewHeightAdjust: the eye moves one stance at a time (stand -> crouch -> prone) along the game's curves over the
// PM_GetStanceViewLerpTime durations; a change of mind mid-way turns the segment around at the mirrored progress.
void updateViewHeight(const MoveParams& p, PlayerState& s, double dt) {
    const int goal = s.prone ? 2 : s.crouched ? 1 : 0;
    if (s.viewCurve < 0) {
        if (stanceRank(p, s.viewTarget) == goal) {
            s.viewHeight = s.viewTarget = rankEye(p, goal);
            s.viewDirect = false;
            return;
        }
        startViewSegment(p, s, goal);
    } else {
        const int from = stanceRank(p, s.viewFrom), to = stanceRank(p, s.viewTarget);
        const bool away = (to > from && goal <= from) || (to < from && goal >= from);
        if (away) {  // turn around: the new segment starts where the old one was, mirrored in time
            const double u = s.viewLerpDur > 0 ? std::clamp(s.viewLerpT / s.viewLerpDur, 0.0, 1.0) : 1.0;
            std::swap(s.viewFrom, s.viewTarget);
            const ViewCurve rc = reverseCurve(ViewCurve(s.viewCurve));
            s.viewCurve = int(rc);
            s.viewDirect = false;
            s.viewLerpDur = rc == ViewCurve::CrouchStand ? p.standLerp : rc == ViewCurve::StandCrouch ? p.crouchDownLerp
                            : rc == ViewCurve::CrouchProne ? p.proneLerp : p.crouchUpLerp;
            s.viewLerpT = (1.0 - u) * s.viewLerpDur;
        }
    }
    s.viewLerpT += dt;
    const double u = s.viewLerpDur > 0 ? std::min(1.0, s.viewLerpT / s.viewLerpDur) : 1.0;
    if (u >= 1.0) {
        s.viewHeight = s.viewTarget;
        s.viewCurve = -1;  // the next segment (if any) starts on the next step, as in the game
        return;
    }
    const double t = std::floor(u * 100.0) * 0.01;  // the game steps the lerp in whole percent
    const double v = viewCurveValue(ViewCurve(s.viewCurve), t);
    s.viewHeight = s.viewFrom + (s.viewTarget - s.viewFrom) * v;
}

// PM_CmdScaleForStance (0x14111ED90): crouch / prone scales, blended along the time of a crouch <-> prone lerp.
double stanceSpeedScale(const MoveParams& p, const PlayerState& s) {
    if (s.viewCurve >= 0 && s.viewLerpDur > 0) {
        const double f = std::clamp(s.viewLerpT / s.viewLerpDur, 0.0, 1.0);
        const ViewCurve c = ViewCurve(s.viewCurve);
        if (c == ViewCurve::CrouchProne || c == ViewCurve::CrouchProneDirect) return (1 - f) * p.crouchScale + f * p.proneScale;
        if (c == ViewCurve::ProneCrouch) return (1 - f) * p.proneScale + f * p.crouchScale;
    }
    return s.prone ? p.proneScale : s.crouched ? p.crouchScale : 1.0;
}

// The ladder volumes of a world (solids with the ladder tool material), listed once per world.
const std::vector<int>& ladderSolids(const CollisionWorld& w) {
    thread_local const CollisionWorld* cachedWorld = nullptr;
    thread_local size_t cachedCount = 0;
    thread_local std::vector<int> list;
    if (cachedWorld != &w || cachedCount != w.solids().size()) {
        list.clear();
        for (size_t i = 0; i < w.solids().size(); ++i)
            if (w.solids()[i].ladder) list.push_back(int(i));
        cachedWorld = &w;
        cachedCount = w.solids().size();
    }
    return list;
}

struct LadderContact {
    bool found = false;
    Vec3 normal;   // outward, flat: the face the player climbs
    double top = 0;
    double gap = 0;  // from the capsule to the face
};

// A ladder face in front of the player within `reach` of the capsule (BG_GetLadderInfo: ladderLineQueryDistance 30),
// looked up on the ladder volumes themselves: a ladder model's collision in front of its volume does not hide it. The
// face must turn towards `dir` (more than 60 degrees) and reach above the feet.
LadderContact findLadder(const CollisionWorld& w, const Vec3& origin, const Vec3& dir, double radius, double height, double reach) {
    LadderContact best;
    double bestDist = 1e300;
    for (int i : ladderSolids(w)) {
        const auto& sol = w.solids()[size_t(i)];
        if (sol.bounds.maxs.z <= origin.z + 1 || sol.bounds.mins.z >= origin.z + height) continue;
        const double pad = radius + reach;
        if (origin.x < sol.bounds.mins.x - pad || origin.x > sol.bounds.maxs.x + pad || origin.y < sol.bounds.mins.y - pad ||
            origin.y > sol.bounds.maxs.y + pad)
            continue;
        for (const Plane& pl : sol.planes) {
            if (std::fabs(pl.n.z) > 0.3) continue;
            const Vec3 n = normalize(flat(pl.n));
            if (dot(n, dir) > -0.5) continue;
            const double gap = dot(pl.n, origin) - pl.d - radius;  // from the capsule to the face plane
            if (gap < -radius || gap > reach) continue;
            // The player stands in front of the face, not beside the volume.
            const Vec3 onFace = origin - n * (gap + radius);
            const double side = 2.0;
            if (onFace.x < sol.bounds.mins.x - radius - side || onFace.x > sol.bounds.maxs.x + radius + side ||
                onFace.y < sol.bounds.mins.y - radius - side || onFace.y > sol.bounds.maxs.y + radius + side)
                continue;
            if (gap < bestDist) {
                bestDist = gap;
                best.found = true;
                best.normal = n;
                best.top = sol.bounds.maxs.z;
                best.gap = std::max(0.0, gap);
            }
        }
    }
    return best;
}

void startMantle(PlayerState& s, const MantleCandidate& mc) {
    const Vec3 first = mc.landing - s.origin;
    const MantlePath path = buildMantlePath(buildMantleWaypoints(s.origin, first));
    s.mantle.path = path;
    s.mantle.elapsed = 0;
    s.mantle.duration = std::max(0.05, mantleDurationSeconds(mc.ledgeHeight, path.arcLength,
                                                             std::hypot(s.velocity.x, s.velocity.y)));
    s.mantle.start = s.origin;
    s.mantle.end = mc.landing;
    s.mantling = true;
    s.mantleProgress = 0;
    s.mantleStart = s.origin;
    s.mantleEnd = mc.landing;
    s.mantleLedgeHeight = mc.ledgeHeight;
    s.mantleHigh = mc.ledgeHeight > 58.0;
    if (mc.forceCrouch) s.crouched = true;
    s.onGround = false;
    s.sprinting = s.tacticalSprinting = false;
    s.sliding = false;
    s.jumpingState = false;
    s.landingMs = 0;
}

// Mantle_Move (kinematic): the origin follows the curve over the mantle duration; velocity is the displacement rate.
void advanceMantle(PlayerState& s, double dt) {
    MantleControl& m = s.mantle;
    m.elapsed += dt;
    const double progress = std::clamp(m.elapsed / m.duration, 0.0, 1.0);
    const Vec3 cur = evaluateMantlePath(m.path, progress);
    s.velocity = (cur - s.origin) * (1.0 / dt);
    s.origin = cur;
    s.mantleProgress = progress;
    if (progress >= 1.0) {
        s.origin = m.end;
        s.velocity = mantleExitVelocity(m.start, m.end, m.duration);
        s.mantling = false;
        s.mantleProgress = 1.0;
        s.onGround = false;
    }
}

// Keeps PlayerState::jumpHeld / crouchHeld / slideHeld current on every exit path of playerMove.
struct InputLatch {
    PlayerState& s;
    const MoveInput& in;
    ~InputLatch() {
        s.jumpHeld = in.jump;
        s.crouchHeld = in.crouch;
        s.slideHeld = in.slide;
    }
};

}  // namespace

double viewCurveValue(ViewCurve c, double t) {
    switch (c) {
        case ViewCurve::StandCrouch: return curveAt(kStandCrouch, t);
        case ViewCurve::StandCrouchDirect: return curveAt(kStandCrouchDirect, t);
        case ViewCurve::CrouchProne: return curveAt(kCrouchProne, t);
        case ViewCurve::CrouchProneDirect: return curveAt(kCrouchProneDirect, t);
        case ViewCurve::ProneCrouch: return curveAt(kProneCrouch, t);
        case ViewCurve::CrouchStand: return curveAt(kCrouchStand, t);
    }
    return 1.0;
}

Vec3 playerMins(const MoveParams& p) { return Vec3{-p.radius, -p.radius, 0}; }
Vec3 playerMaxs(const MoveParams& p, bool crouched) {
    return Vec3{p.radius, p.radius, crouched ? p.crouchHeight : p.standHeight};
}
double eyeHeight(const MoveParams&, const PlayerState& s) { return s.viewHeight; }

void playerMove(const CollisionWorld& world, PlayerState& s, const MoveInput& inRaw, const MoveParams& p) {
    const double dt = std::clamp(inRaw.dt, 0.0, 0.05);
    if (dt <= 0) return;
    MoveInput in = inRaw;
    in.forward = std::clamp(in.forward, -1.0, 1.0);
    in.right = std::clamp(in.right, -1.0, 1.0);
    const double yaw = s.yaw * kPi / 180.0, pitch = s.pitch * kPi / 180.0;
    InputLatch latch{s, in};
    if (s.noclip) {
        // Fly along the view: no collision, no gravity; 3 x g_speed (PM_CmdScale for pm_type noclip), sprint doubles.
        const Vec3 fwd{std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
        const Vec3 right{std::sin(yaw), -std::cos(yaw), 0};
        Vec3 wish = fwd * in.forward + right * in.right + Vec3{0, 0, in.up};
        const double speed = p.noclipSpeed * (in.sprint ? 2.0 : 1.0);
        s.velocity = length(wish) > 1e-9 ? normalize(wish) * speed : Vec3{};
        s.origin = s.origin + s.velocity * dt;
        s.onGround = false;
        s.sprinting = s.tacticalSprinting = s.sliding = s.mantling = false;
        return;
    }
    s.sinceJump += dt;
    s.steepTime = s.steepContact ? s.steepTime + dt : 0.0;  // the ledge walk support of the last step
    s.steepContact = false;
    s.sinceSlideEnd += dt;
    s.sinceSprintStart += dt;
    if (s.mantling) {  // a mantle owns the frame: no stance, friction, jump or wish movement
        advanceMantle(s, dt);
        updateViewHeight(p, s, dt);
        return;
    }

    // Stance: prone > crouch > stand; rising only when there is room. A slide keeps the crouched stance; prone needs
    // the ground (PM_AirMove turns a prone request in the air into a crouch).
    auto fits = [&](double h) { return !world.traceCapsule(s.origin, s.origin, p.radius, h).startSolid; };
    const bool proneWanted = in.prone && (s.onGround || s.prone);
    if (proneWanted) {
        s.prone = true;
        s.crouched = false;
        s.sliding = false;
    } else if (s.prone && fits(in.crouch || in.prone ? p.crouchHeight : p.standHeight)) {
        s.prone = false;
        s.crouched = in.crouch || in.prone;
    }
    if (!s.prone) {
        if (s.sliding || in.crouch || in.prone) s.crouched = true;
        else if (s.crouched && fits(p.standHeight)) s.crouched = false;
    }
    Ctx c{world, p, s, playerMins(p), stanceMaxs(p, s)};
    groundTrace(c);
    const bool wasGround = s.onGround;
    // Landing friction starts on the first grounded frame after a jump (Jump_ApplySlowdown with pm_time 0).
    if (s.jumpingState && s.onGround && s.landingMs <= 0) s.landingMs = p.jumpLandingSeconds * 1000.0;

    // Slide (Slide_CanSlide 0x14112CE90 / Slide_EndCheck): a slide request (or a fresh crouch press) while sprinting
    // on walkable ground of at most ~37 degrees, sprinting for 0.2 s, at 140 units / s or more. It ends on time, a new
    // crouch press, prone, jump, below 50 units / s or off the ground.
    const double hspeed = std::hypot(s.velocity.x, s.velocity.y);
    const bool crouchPressed = in.crouch && !s.crouchHeld;
    const bool slidePressed = (in.slide && !s.slideHeld) || crouchPressed;
    if (s.sinceSlideEnd > p.jumpSubsequentWindow) s.slideSubsequent = 0;  // Slide_Update's subsequent reset
    if (s.sliding) {
        s.slideElapsed += dt;
        const bool endNow = (in.jump && !s.jumpHeld) || in.prone || !s.onGround || (crouchPressed && s.slideElapsed > 0) ||
                            length(s.velocity) < p.slideMinContinueSpeed || s.slideElapsed >= p.slideSeconds;
        if (endNow) {
            s.sliding = false;
            s.sinceSlideEnd = 0;
        }
    } else if (slidePressed && s.onGround && s.sprinting && !s.prone && !s.mantling &&
               length(s.velocity) >= p.slideMinGroundSpeed && s.sinceSprintStart >= p.slideStartAfterSprint &&
               s.groundNormal.z >= p.slideMinNormal && s.slideSubsequent < p.slideMaxSubsequent) {
        if (s.sinceSlideEnd < p.jumpSubsequentWindow) ++s.slideSubsequent;
        s.sliding = true;
        s.slideElapsed = 0;
        s.sprinting = s.tacticalSprinting = false;
        s.sprintRestore = false;
        s.sprintButtonUpRequired = in.sprint;
        s.crouched = true;
    }

    // Sprint (PM_UpdateSprint 0x141128160, PM_ShouldSprintEnd 0x141125A20, PM_CanStartSprint 0x14111D320): a sprint
    // press with forward 105 of 127, on the ground and standing. It lasts until forward drops, a crouch / prone / jump,
    // or a jump's landing window in the air; ended that way it comes back by itself while forward stays held (sprint
    // restore). With tactical sprint on (MP) the plain sprint has no meter; the tactical sprint drains a 2 s meter that
    // refills 1 s per s while not sprinting and needs 40 % to start.
    const bool fwdOk = in.forward >= p.sprintMinForward;
    const bool legacyMeter = !p.tacticalSprintEnabled;
    const bool slideDelay = s.sinceSlideEnd < p.slideSprintDelay;
    if (s.sprinting) {
        if (s.tacticalSprinting) {
            s.sprintMeter -= dt;
            if (s.sprintMeter <= 0) s.sprintMeter = 0, s.tacticalSprinting = false;  // back to the plain sprint
        } else if (legacyMeter) {
            s.sprintMeter = std::max(0.0, s.sprintMeter - dt);
        }
        const bool meterOut = legacyMeter && s.sprintMeter <= 0;
        const bool released = p.holdToSprint && !in.sprint;
        // A jump press does not end the sprint here: the jump below uses it (sprint leap) and then ends it.
        const bool endNow = meterOut || released || !fwdOk || in.crouch || in.prone || s.crouched || s.prone ||
                            (!s.onGround && s.jumpingState);
        if (endNow) {
            s.sprinting = s.tacticalSprinting = false;
            s.sprintRestore = !meterOut && !released;
            s.sprintButtonUpRequired = in.sprint;
        }
    }
    if (!s.sprinting) {
        const double cap = legacyMeter ? p.sprintSeconds : p.tacticalSprintSeconds;
        s.sprintMeter = std::min(cap, s.sprintMeter + dt * (legacyMeter ? 1.0 : p.tacticalSprintRecharge));
        if (s.sprintRestore && !fwdOk && !s.sliding && !slideDelay) s.sprintRestore = false;
    }
    if (s.sprintButtonUpRequired && !in.sprint) s.sprintButtonUpRequired = false;
    const bool wantsSprint = (in.sprint && !s.sprintButtonUpRequired) || s.sprintRestore;
    if (!s.sprinting && wantsSprint && s.onGround && !s.crouched && !s.prone && !in.crouch && !in.prone && !s.sliding &&
        !s.mantling && fwdOk && !slideDelay && (!legacyMeter || s.sprintMeter >= p.sprintMinActivation)) {
        s.sprinting = true;
        s.sprintRestore = false;
        s.sinceSprintStart = 0;
    }
    if (p.tacticalSprintEnabled && s.sprinting && !s.tacticalSprinting && in.tacticalSprint &&
        s.sprintMeter >= p.tacticalSprintMinCharge * p.tacticalSprintSeconds)
        s.tacticalSprinting = true;

    const Vec3 fwd{std::cos(yaw), std::sin(yaw), 0}, right{std::sin(yaw), -std::cos(yaw), 0};

    // Mantle (Mantle_CheckActivationConditions 0x141108A90): pushing into a ledge at most Mantle_GetMaxClimbHeight above
    // the feet, with the jump button held (on the ground or in the air), or by itself in the air after a jump when more
    // than 36 units above the ground (the auto trigger); never with the crouch / prone bits.
    if (p.mantleEnabled && !s.prone && !s.sliding && !in.crouch && !in.prone) {
        const bool autoAir = !s.onGround && s.jumpingState &&
                             world.traceRay(s.origin, s.origin - Vec3{0, 0, p.mantleAutoMinHeight}).fraction >= 1.0;
        const bool trigger = in.jump || in.highMantle || autoAir;
        const double pushLen = std::min(1.0, std::hypot(in.forward, in.right));
        if (trigger && pushLen >= kMantlePushMin) {
            const Vec3 wishDir = fwd * in.forward + right * in.right;
            const MantleCandidate mc = findMantleCandidate(world, s.origin, wishDir, p.radius, p.standHeight,
                                                           p.crouchHeight, s.crouched ? p.crouchHeight : p.standHeight);
            if (mc.valid && mc.ledgeHeight <= p.mantleMaxHeight + 0.5) {
                startMantle(s, mc);
                updateViewHeight(p, s, dt);
                return;
            }
        }
    }

    // Ladder (PM_LadderMove 0x140CC6BF0): forward climbs and back descends at 0.4 x the command speed (the view
    // pitch does not matter: useLegacyPCLadderControls is off), accelerating at 9; with no input the vertical speed
    // settles at the rate of gravity; crouch held slides down at 100; no strafing; jump pushes off backwards at 200.
    if (!s.prone && !s.sliding) {
        const Vec3 probeDir = s.onLadder ? s.ladderNormal * -1.0 : fwd;
        const LadderContact lc = findLadder(world, s.origin, probeDir, p.radius, c.maxs.z, p.ladderReach);
        const bool wantsOn = s.onLadder || (in.forward > 0.1 && dot(fwd, lc.normal) < -0.5);
        if (lc.found && wantsOn && !(s.onLadder && s.onGround && in.forward < -0.1)) {
            s.onLadder = true;
            s.ladderNormal = lc.normal;
            s.sprinting = s.tacticalSprinting = false;
            s.jumpingState = false;
            if (in.jump && !s.jumpHeld) {  // push off the ladder
                s.onLadder = false;
                s.velocity = normalize(s.ladderNormal + Vec3{0, 0, p.ladderJumpUpDesire}) * p.ladderJumpBack;
                stepSlideMove(c, dt, true, false);
                groundTrace(c);
                updateViewHeight(p, s, dt);
                return;
            }
            Vec3 wishVel{0, 0, p.ladderClimbScale * p.walkSpeed * in.forward};
            if (in.crouch) {
                double v = -p.ladderSlideSpeed;
                if (v >= s.velocity.z) v = s.velocity.z - p.ladderSlideAccel * dt;
                wishVel.z = v;
            }
            s.velocity = Vec3{0, 0, s.velocity.z};  // no ladder strafe; the face holds the player (magnetism)
            const double wishSpeed = length(wishVel);
            if (wishSpeed > 1e-9) accelerate(c, wishVel * (1.0 / wishSpeed), wishSpeed, p.ladderAccelerate, dt);
            if (in.forward == 0 && !in.crouch) {  // no input: the climb settles
                const double d = p.gravity * dt;
                s.velocity.z = s.velocity.z <= 0 ? std::min(s.velocity.z + d, 0.0) : std::max(s.velocity.z - d, 0.0);
            }
            // Magnetism (ladderMagnetismEnhancedSpeed 100 over ladderMagnetismScaleDistance 10): pulled onto the face,
            // or against whatever stands in front of it (a ladder model's collision).
            s.velocity = Vec3{0, 0, s.velocity.z} - s.ladderNormal * (p.ladderMagnetism * std::clamp(lc.gap / 10.0, 0.1, 1.0));
            stepSlideMove(c, dt, false, false);
            s.velocity = Vec3{0, 0, s.velocity.z};
            groundTrace(c);
            if (s.onGround && in.forward < 0) s.onLadder = false;  // climbed down to the floor
            updateViewHeight(p, s, dt);
            return;
        }
        // Off the top (or side): the ladder face is gone; keep a small hop so the feet clear the top edge.
        if (s.onLadder) {
            s.onLadder = false;
            if (in.forward > 0.1 && !s.onGround) s.velocity = fwd * (p.walkSpeed * 0.5) + Vec3{0, 0, 120};
        }
    }

    // Jump (Jump_Check 0x141101570): a fresh press on the ground, standing (or late out of a slide), 0.5 s after the
    // last jump start.
    bool jumped = false;
    if (in.jump && !s.jumpHeld && s.onGround && s.sinceJump >= p.jumpMinInterval) {
        const bool lateSlide = !s.sliding && s.sinceSlideEnd < p.slideLateJumpSeconds && fits(p.standHeight);
        if (!s.prone && (!s.crouched || lateSlide)) {
            if (lateSlide) s.crouched = false;
            if (s.sinceJump > p.jumpSubsequentWindow) s.subsequentJumps = 0;
            double height = std::max(p.jumpSubsequentMin * p.jumpHeight,
                                     p.jumpHeight * std::pow(p.jumpSubsequentScale, double(s.subsequentJumps)));
            if (s.subsequentJumps < 7) ++s.subsequentJumps;
            if (lateSlide) {
                s.velocity.x *= p.slideJumpSpeedScale;
                s.velocity.y *= p.slideJumpSpeedScale;
            }
            if ((s.sprinting || s.sprintRestore) && p.sprintLeapEnabled) {
                // Sprint leap (Leap_ShouldSprintLeap / Jump_Start): the sprint's age sets the height floor and a
                // forward speed bonus, both full after 0.3 s of sprint.
                const double fraction = std::min(1.0, s.sinceSprintStart / p.sprintLeapSeconds);
                height = std::max(height, p.sprintLeapHeight * fraction);
                const double hscale = 1.0 + fraction * p.sprintLeapForwardScale;
                s.velocity.x *= hscale;
                s.velocity.y *= hscale;
            }
            s.velocity.z = std::sqrt(2.0 * p.gravity * height);
            s.onGround = false;
            s.jumpingState = true;
            s.landingMs = 0;
            s.sinceJump = 0;
            if (s.sprinting) s.sprintRestore = true;  // the sprint comes back on landing while forward is held
            s.sprinting = s.tacticalSprinting = false;
            s.sprintButtonUpRequired = false;
            s.sliding = false;
            jumped = true;
        }
    }

    // Wish velocity (PM_WalkMove 0x14112A130 / PM_CmdScale 0x14111EF60; PM_AirMove 0x14111CBA0). Inputs are -1..1
    // (the command / 127); the max/len factor keeps diagonals at the speed of the axis moves they combine. While
    // sprinting the side command is halved first.
    double fwdCmd = in.forward, strafeCmd = in.right;
    if (s.onGround && s.sprinting) strafeCmd *= p.sprintStrafeScale;
    if (s.onGround && s.sliding && s.slideElapsed < p.slideInSeconds) fwdCmd = 1.0;  // slide-in: forced forward
    const double len = std::hypot(fwdCmd, strafeCmd);
    const bool jogOk = !s.sprinting && !s.crouched && !s.prone && !s.sliding;
    Vec3 wish{};
    if (len > 1e-9) {
        const double k = std::max(std::fabs(fwdCmd), std::fabs(strafeCmd)) / len;
        if (s.onGround) {
            double speedScale = p.walkSpeed * k * p.weaponMoveScale * stanceSpeedScale(p, s);
            if (s.sprinting || s.sliding) speedScale *= p.sprintScale;
            if (s.tacticalSprinting) speedScale *= p.tacticalSprintScale;
            const double forwardScale = fwdCmd > 0 ? (jogOk ? p.jogForwardScale : 1.0) : fwdCmd < 0 ? p.backScale : 1.0;
            wish = fwd * (fwdCmd * speedScale * forwardScale) + right * (strafeCmd * speedScale * p.strafeScale);
        } else {
            wish = (fwd * fwdCmd + right * strafeCmd) * (p.walkSpeed * k);  // air: no weapon, stance or sprint
        }
    }
    double wishSpeed = length(wish);
    Vec3 wishDir = wishSpeed > 1e-9 ? wish * (1.0 / wishSpeed) : Vec3{};
    if (s.onGround && wishSpeed > 0) {
        if (s.sliding && s.slideElapsed < p.slideInSeconds)
            wishSpeed *= p.slideInSpeedScale * std::pow(0.85, double(s.slideSubsequent));
        wishDir = normalize(wishDir - s.groundNormal * dot(wishDir, s.groundNormal));
    }

    // Friction (ground only): landing, slide and slide-out scales multiply.
    if (s.onGround) {
        double fs = 1.0;
        if (s.jumpingState && s.landingMs > 0) fs *= landingFrictionScale(s.landingMs);
        if (s.sliding) fs *= slideFrictionScale(p, s);
        if (s.sinceSlideEnd < p.slideOutSeconds) {
            const double t = s.sinceSlideEnd / p.slideOutSeconds;
            fs *= (1.0 - t) * p.slideOutFrictionStart + t;
        }
        applyFriction(c, dt, fs);
    }
    if (wishSpeed > 0) {
        double accel = s.onGround ? (s.prone ? p.proneAccelerate : s.crouched ? p.crouchAccelerate : p.accelerate)
                                  : p.airAccelerate;
        if (s.sliding && p.slideInAcceleration > 0) accel = p.slideInAcceleration;
        accelerate(c, wishDir, wishSpeed, accel, dt);
    }

    if (s.onGround) {
        // Along the ground plane at the same speed (the engine projects the velocity onto the walked plane keeping its
        // length: "don't decrease velocity when going up or down a slope").
        const double speed0 = length(s.velocity);
        s.velocity = clipVelocity(s.velocity, s.groundNormal, kOverbounce);
        if (const double l = length(s.velocity); l > 1e-9) s.velocity = s.velocity * (speed0 / l);
        stepSlideMove(c, dt, false, true);
    } else {
        stepSlideMove(c, dt, true, false);
    }
    groundTrace(c);

    // Walking down stairs or off a short ledge (Replay120GroundTrace, collision type 1): grounded and not within 0.5 s
    // of a jump, the ground trace reaches 16 units below the feet (playerCollisionStickExtension) and stands the player
    // on a walkable surface found there.
    if (wasGround && !s.onGround && !jumped && !s.jumpingState && s.sinceJump >= p.jumpMinInterval && s.velocity.z <= 0) {
        const Vec3 down = s.origin - Vec3{0, 0, p.stepDownSnap};
        const auto tr = world.traceCapsule(s.origin, down, p.radius, c.maxs.z);
        if (tr.fraction < 1 && !tr.startSolid && !tr.allSolid && tr.plane.n.z >= p.minWalkNormal) {
            s.origin = tr.end;
            s.onGround = true;
            s.groundNormal = tr.plane.n;
            s.velocity.z = 0;
        }
    }
    if (s.onGround && s.velocity.z < 0) s.velocity.z = 0;

    // Timers: landing friction counts down from 1000 ms; the jump state ends with it.
    if (s.landingMs > 0) {
        s.landingMs -= dt * 1000.0;
        if (s.landingMs <= 0) {
            s.landingMs = 0;
            s.jumpingState = false;
        }
    }
    updateViewHeight(p, s, dt);
}

bool placePlayer(const CollisionWorld& world, PlayerState& s, const Vec3& from, const MoveParams& p) {
    // A placed player starts in a clean state: no slide, sprint, mantle or jump timers.
    s.sprinting = s.tacticalSprinting = s.sliding = s.mantling = false;
    s.mantleProgress = 0;
    s.jumpingState = false;
    s.landingMs = 0;
    s.subsequentJumps = 0;
    s.slideSubsequent = 0;
    s.sinceJump = s.sinceSlideEnd = s.sinceSprintStart = kLongAgo;
    s.slideElapsed = 0;
    s.sprintMeter = p.tacticalSprintEnabled ? p.tacticalSprintSeconds : p.sprintSeconds;
    s.sprintButtonUpRequired = false;
    s.sprintRestore = false;
    s.steepContact = false;
    s.steepTime = 0;
    s.crouchHeld = s.slideHeld = false;
    s.viewHeight = s.viewFrom = s.viewTarget = p.standEye;
    s.viewLerpT = s.viewLerpDur = 0;
    s.viewCurve = -1;
    s.viewDirect = false;

    const Vec3 maxs = playerMaxs(p, false);
    Vec3 start = from;
    // Out of a solid: try a few heights above.
    for (int k = 0; k < 64 && world.traceCapsule(start, start, p.radius, maxs.z).startSolid; ++k) start.z += 8;
    const auto tr = world.traceCapsule(start, start - Vec3{0, 0, 8192}, p.radius, maxs.z);
    if (tr.fraction == 1.0 || tr.allSolid) {
        s.origin = start;
        s.velocity = Vec3{};
        s.onGround = false;
        return false;
    }
    s.origin = tr.end;
    s.velocity = Vec3{};
    s.onGround = true;
    s.groundNormal = tr.plane.n;
    s.crouched = false;
    s.prone = false;
    return true;
}

// ---- Client input (CL_Input_* of Replay 1.20 / the game-test client source; research 140 section 6) ----

void PlayControls::reset() { *this = PlayControls{}; }

void PlayControls::press(PlayKey k) {
    if (k != PlayKey::Count) pending_[int(k)] = true;
}

void PlayControls::release(PlayKey) {}

void PlayControls::onPress(PlayKey k, const PlayerState& s) {
    switch (k) {
        case PlayKey::Forward:
        case PlayKey::Back:
            autoForward = false;  // a move key ends autoforward
            break;
        case PlayKey::AutoForward:
            autoForward = !autoForward;
            break;
        case PlayKey::Jump:  // +gostand (CL_Input_RaiseStanceUp): crouch -> stand, prone -> crouch, standing -> jump
            if (s.noclip || s.sliding || s.onLadder || stance == PlayStance::Stand) jumpLatched = true;
            else stance = stance == PlayStance::Prone ? PlayStance::Crouch : PlayStance::Stand;
            break;
        case PlayKey::Crouch:  // toggle crouch (crouchType 1); tap to slide while sprinting (tapToSlideEnabledKeyboard)
            if (s.noclip) break;
            if (s.sliding) {
                slideCancel = true;  // a crouch press during the slide ends it in a crouch
                stance = PlayStance::Crouch;
            } else if (s.sprinting && s.onGround) {
                slideRequest = true;  // the slide, or a plain crouch when it cannot start
                stance = PlayStance::Crouch;
            } else {
                stance = stance == PlayStance::Crouch ? PlayStance::Stand : PlayStance::Crouch;
            }
            break;
        case PlayKey::Prone:  // toggle prone (proneType 1)
            if (!s.noclip) stance = stance == PlayStance::Prone ? PlayStance::Stand : PlayStance::Prone;
            break;
        case PlayKey::Sprint:  // +breath_sprint: a second press while sprinting is tactical sprint
            if (s.sprinting && !s.tacticalSprinting) tacticalTrigger = true;
            else if (stance == PlayStance::Crouch && !s.sliding) stance = PlayStance::Stand;  // sprinting stands up
            break;
        default:
            break;
    }
}

MoveInput PlayControls::input(const bool heldNow[int(PlayKey::Count)], const PlayerState& s, double dt) {
    // A slide that ended: the game forces the stance it ended in (EV_STANCE_FORCE_CROUCH, or standing after a jump).
    if (wasSliding && !s.sliding) {
        stance = s.prone ? PlayStance::Prone : s.crouched ? PlayStance::Crouch : PlayStance::Stand;
        slideCancel = false;
    }
    wasSliding = s.sliding;
    bool tapped[int(PlayKey::Count)] = {};  // pressed since the last step (a tap may already be up again)
    for (int i = 0; i < int(PlayKey::Count); ++i) {
        const bool now = heldNow[i];
        if (pending_[i] || (now && !prevHeld_[i])) {
            onPress(PlayKey(i), s);
            tapped[i] = true;
        }
        pending_[i] = false;
        prevHeld_[i] = now;
        held[i] = now;
    }
    const auto h = [&](PlayKey k) { return held[int(k)] || tapped[int(k)]; };
    MoveInput in;
    in.dt = dt;
    in.forward = (autoForward || h(PlayKey::Forward) ? 1.0 : 0.0) - (h(PlayKey::Back) && !autoForward ? 1.0 : 0.0);
    in.right = (h(PlayKey::Right) ? 1.0 : 0.0) - (h(PlayKey::Left) ? 1.0 : 0.0);
    in.sprint = h(PlayKey::Sprint);
    in.jump = jumpLatched && h(PlayKey::Jump);
    if (!held[int(PlayKey::Jump)]) jumpLatched = false;
    in.highMantle = in.jump && !s.onGround;
    if (s.noclip) {
        in.up = (h(PlayKey::Jump) ? 1.0 : 0.0) - (h(PlayKey::Crouch) ? 1.0 : 0.0);
        in.jump = false;
        return in;
    }
    // The stance bits: during a slide the crouch bit is off (the slide holds the crouch) unless it is being cancelled.
    in.crouch = stance == PlayStance::Crouch && (!s.sliding || slideCancel);
    in.prone = stance == PlayStance::Prone;
    in.slide = slideRequest;
    slideRequest = false;
    // Tactical sprint: the request goes out until the tactical sprint starts, or with the sprint's end.
    in.tacticalSprint = tacticalTrigger;
    if (!s.sprinting || s.tacticalSprinting) tacticalTrigger = false;
    return in;
}

}  // namespace iw8r
