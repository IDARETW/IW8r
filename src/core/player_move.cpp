#include "player_move.h"

#include <algorithm>
#include <cmath>

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
    const Vec3 down = c.s.origin - Vec3{0, 0, 0.25};
    const auto tr = c.w.traceCapsule(c.s.origin, down, c.p.radius, c.maxs.z);
    if (tr.fraction == 1.0 || tr.allSolid) {
        c.s.onGround = false;
        return;
    }
    if (c.s.velocity.z > 0 && dot(c.s.velocity, tr.plane.n) > 10) {  // moving up and away: a jump just started
        c.s.onGround = false;
        return;
    }
    c.s.groundNormal = tr.plane.n;
    c.s.onGround = tr.plane.n.z >= c.p.minWalkNormal;
}

// Quake 3 PM_SlideMove: up to four bumps, clipping the velocity against every plane touched.
bool slideMove(Ctx& c, double dt, bool gravity) {
    Vec3 planes[6];
    int numPlanes = 0;
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
        // A plane hit twice: nudge the velocity out so the box does not stick.
        bool repeat = false;
        for (int i = 0; i < numPlanes; ++i)
            if (dot(tr.plane.n, planes[i]) > 0.99) {
                c.s.velocity = c.s.velocity + tr.plane.n;
                repeat = true;
                break;
            }
        if (repeat) continue;
        planes[numPlanes++] = tr.plane.n;
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

// Quake 3 PM_StepSlideMove. Stepping up is allowed only from the ground (Replay: allowStep = grounded).
void stepSlideMove(Ctx& c, double dt, bool gravity, bool allowStep) {
    const Vec3 startO = c.s.origin, startV = c.s.velocity;
    if (!slideMove(c, dt, gravity)) return;  // moved the whole way
    if (!allowStep) return;
    const Vec3 down = startO - Vec3{0, 0, c.p.stepSize};
    const auto downTr = c.w.traceCapsule(startO, down, c.p.radius, c.maxs.z);
    if (startV.z > 0 && (downTr.fraction == 1.0 || dot(downTr.plane.n, Vec3{0, 0, 1}) < 0.7)) return;
    const Vec3 slideO = c.s.origin, slideV = c.s.velocity;
    const Vec3 up = startO + Vec3{0, 0, c.p.stepSize};
    const auto upTr = c.w.traceCapsule(startO, up, c.p.radius, c.maxs.z);
    if (upTr.allSolid) return;
    const double stepHeight = upTr.end.z - startO.z;
    c.s.origin = upTr.end;
    c.s.velocity = startV;
    slideMove(c, dt, gravity);
    const Vec3 dn = c.s.origin - Vec3{0, 0, stepHeight};
    const auto dnTr = c.w.traceCapsule(c.s.origin, dn, c.p.radius, c.maxs.z);
    if (!dnTr.allSolid) c.s.origin = dnTr.end;
    if (dnTr.fraction < 1) c.s.velocity = clipVelocity(c.s.velocity, dnTr.plane.n, kOverbounce);
    // Keep the step only if it got further horizontally than the plain slide.
    const auto horiz = [](const Vec3& a, const Vec3& b) { return std::hypot(a.x - b.x, a.y - b.y); };
    if (horiz(c.s.origin, startO) + 0.01 < horiz(slideO, startO)) {
        c.s.origin = slideO;
        c.s.velocity = slideV;
    }
}

// Ground friction (walk_friction.cpp Replay120ApplyFriction): drop = friction * max(speed, stop) * scale * dt.
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

void accelerate(Ctx& c, const Vec3& wishDir, double wishSpeed, double accel, double dt) {
    const double current = dot(c.s.velocity, wishDir);
    const double add = wishSpeed - current;
    if (add <= 0) return;
    const double accelSpeed = std::min(accel * dt * wishSpeed, add);
    c.s.velocity = c.s.velocity + wishDir * accelSpeed;
}

Vec3 stanceMaxs(const MoveParams& p, const PlayerState& s) {
    return Vec3{p.radius, p.radius, s.prone ? p.proneHeight : s.crouched ? p.crouchHeight : p.standHeight};
}

// Landing friction (walk_friction.cpp Replay120JumpReduceFriction): 2.0 for the first 100 ms of the window,
// then 1 + t/900 down to 1 when the window ends.
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

// Eye height: lerps to the stance height over the Replay lerp times (200 ms stand/crouch down, 400 ms up/prone).
void updateViewHeight(const MoveParams& p, PlayerState& s, double dt) {
    const double target = s.prone ? p.proneEye : s.crouched ? p.crouchEye : p.standEye;
    if (target != s.viewTarget) {
        s.viewFrom = s.viewHeight;
        s.viewTarget = target;
        s.viewLerpT = 0;
        if (target == p.standEye) s.viewLerpDur = p.standLerp;
        else if (target == p.proneEye) s.viewLerpDur = p.proneLerp;
        else s.viewLerpDur = s.viewFrom > target ? p.crouchDownLerp : p.crouchUpLerp;
    }
    if (s.viewLerpT < s.viewLerpDur) {
        s.viewLerpT = std::min(s.viewLerpDur, s.viewLerpT + dt);
        const double u = s.viewLerpDur > 0 ? s.viewLerpT / s.viewLerpDur : 1.0;
        const double e = u * u * (3.0 - 2.0 * u);  // ease in and out (the engine uses a sampled table)
        s.viewHeight = s.viewFrom + (target - s.viewFrom) * e;
    } else {
        s.viewHeight = target;
    }
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
    s.sprinting = false;
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

// Keeps PlayerState::jumpHeld / crouchHeld current on every exit path of playerMove.
struct InputLatch {
    PlayerState& s;
    const MoveInput& in;
    ~InputLatch() {
        s.jumpHeld = in.jump;
        s.crouchHeld = in.crouch;
    }
};

}  // namespace

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
        // Fly along the view: no collision, no gravity.
        const Vec3 fwd{std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
        const Vec3 right{std::sin(yaw), -std::cos(yaw), 0};
        Vec3 wish = fwd * in.forward + right * in.right + Vec3{0, 0, in.up};
        const double speed = p.noclipSpeed * (in.sprint ? 2.0 : 1.0);
        s.velocity = length(wish) > 1e-9 ? normalize(wish) * speed : Vec3{};
        s.origin = s.origin + s.velocity * dt;
        s.onGround = false;
        s.sprinting = s.sliding = s.mantling = false;
        return;
    }
    s.sinceJump += dt;
    s.sinceSlideEnd += dt;
    s.sinceSprintStart += dt;
    if (s.mantling) {  // a mantle owns the frame: no stance, friction, jump or wish movement
        advanceMantle(s, dt);
        updateViewHeight(p, s, dt);
        return;
    }

    // Stance: prone > crouch > stand; rising only when there is room. A slide keeps the crouched stance.
    auto fits = [&](double h) { return !world.traceCapsule(s.origin, s.origin, p.radius, h).startSolid; };
    if (in.prone) {
        s.prone = true;
        s.crouched = false;
        s.sliding = false;
    } else if (s.prone && fits(in.crouch ? p.crouchHeight : p.standHeight)) {
        s.prone = false;
        s.crouched = in.crouch;
    }
    if (!s.prone) {
        if (s.sliding || in.crouch) s.crouched = true;
        else if (s.crouched && fits(p.standHeight)) s.crouched = false;
    }
    Ctx c{world, p, s, playerMins(p), stanceMaxs(p, s)};
    groundTrace(c);
    const bool wasGround = s.onGround;
    // Landing friction starts on the first grounded frame after a jump (Jump_ApplySlowdown with pm_time 0).
    if (s.jumpingState && s.onGround && s.landingMs <= 0) s.landingMs = p.jumpLandingSeconds * 1000.0;

    // Slide: starts from a crouch press while sprinting on walkable ground; ends on duration, jump, prone, low
    // speed or leaving the ground.
    const double hspeed = std::hypot(s.velocity.x, s.velocity.y);
    if (s.sinceSlideEnd > p.jumpSubsequentWindow) s.slideSubsequent = 0;  // Slide_Update's subsequent reset
    if (s.sliding) {
        s.slideElapsed += dt;
        const bool endNow = (in.jump && !s.jumpHeld) || in.prone || !s.onGround ||
                            hspeed < p.slideMinContinueSpeed || s.slideElapsed >= p.slideSeconds;
        if (endNow) {
            s.sliding = false;
            s.sinceSlideEnd = 0;
        }
    } else if (in.crouch && !s.crouchHeld && s.onGround && s.sprinting && !s.prone && !s.mantling &&
               hspeed >= p.slideMinGroundSpeed && s.sinceSprintStart >= p.slideStartAfterSprint &&
               s.groundNormal.z >= p.minWalkNormal && s.slideSubsequent < p.slideMaxSubsequent) {
        if (s.sinceSlideEnd < p.jumpSubsequentWindow) ++s.slideSubsequent;
        s.sliding = true;
        s.slideElapsed = 0;
        s.sprinting = false;
        s.sprintButtonUpRequired = true;
        s.crouched = true;
    }

    // Sprint: forward only (the forward minimum), held Shift, grounded and standing; a 2 s meter.
    const bool fwdOk = in.forward >= p.sprintMinForward;
    if (s.sprinting) {
        s.sprintMeter = std::max(0.0, s.sprintMeter - dt);
        // A jump press does not end the sprint here: the jump below uses it (sprint leap) and then clears it.
        const bool endNow = !in.sprint || in.crouch || in.prone || !fwdOk || !s.onGround ||
                            s.crouched || s.sprintMeter <= 0;
        if (endNow) {
            s.sprinting = false;
            s.sprintButtonUpRequired = in.sprint || s.sprintMeter <= 0;
        }
    } else {
        s.sprintMeter = std::min(p.sprintSeconds, s.sprintMeter + dt);
    }
    if (s.sprintButtonUpRequired && !in.sprint) s.sprintButtonUpRequired = false;
    if (!s.sprinting && in.sprint && !s.sprintButtonUpRequired && s.onGround && !s.crouched && !s.prone &&
        !s.sliding && !s.mantling && fwdOk && s.sprintMeter >= p.sprintMinActivation) {
        s.sprinting = true;
        s.sinceSprintStart = 0;
    }

    const Vec3 fwd{std::cos(yaw), std::sin(yaw), 0}, right{std::sin(yaw), -std::cos(yaw), 0};

    // Mantle: a fresh jump press (or the airborne high-mantle button) with a push into a ledge in front.
    if (p.mantleEnabled && !s.prone && !s.sliding && !in.crouch) {
        const bool trigger = (in.jump && !s.jumpHeld) || (!s.onGround && in.highMantle);
        const double pushLen = std::min(1.0, std::hypot(in.forward, in.right));
        if (trigger && pushLen >= kMantlePushMin) {
            const Vec3 wishDir = fwd * in.forward + right * in.right;
            const MantleCandidate mc = findMantleCandidate(world, s.origin, wishDir, p.radius, p.standHeight,
                                                           p.crouchHeight, s.crouched ? p.crouchHeight : p.standHeight);
            if (mc.valid) {
                startMantle(s, mc);
                updateViewHeight(p, s, dt);
                return;
            }
        }
    }

    // Ladder: a ladder brush face just in front (or the one being climbed) takes over the frame.
    if (!s.prone && !s.sliding) {
        const Vec3 probeDir = s.onLadder ? s.ladderNormal * -1.0 : fwd;
        const auto tr = world.traceCapsule(s.origin, s.origin + probeDir * 4.0, p.radius, c.maxs.z);
        const bool ladderAhead = tr.fraction < 1 && !tr.startSolid && tr.solid >= 0 &&
                                 world.solids()[size_t(tr.solid)].ladder && std::fabs(tr.plane.n.z) < 0.3;
        const bool wantsOn = s.onLadder || (in.forward > 0.1 && dot(fwd, tr.plane.n) < -0.5);
        if (ladderAhead && wantsOn && !(s.onLadder && s.onGround && in.forward < -0.1)) {
            s.onLadder = true;
            s.ladderNormal = flat(tr.plane.n);
            s.sprinting = false;
            s.jumpingState = false;
            if (in.jump && !s.jumpHeld) {  // push off the ladder
                s.onLadder = false;
                s.velocity = s.ladderNormal * p.ladderPushOff;
                stepSlideMove(c, dt, true, false);
                groundTrace(c);
                updateViewHeight(p, s, dt);
                return;
            }
            const double speed = p.walkSpeed * p.ladderClimbScale;
            const double upDown = s.pitch < p.ladderLookDownPitch ? -in.forward : in.forward;
            const Vec3 along = normalize(cross(Vec3{0, 0, 1}, s.ladderNormal));  // across the ladder face
            s.velocity = Vec3{0, 0, upDown * speed} + along * (dot(right, along) * in.right * speed * 0.5) -
                         s.ladderNormal * 10.0;  // keep in contact with the face
            stepSlideMove(c, dt, false, false);
            groundTrace(c);
            if (s.onGround && upDown < 0) s.onLadder = false;  // climbed down to the floor
            updateViewHeight(p, s, dt);
            return;
        }
        // Off the top (or side): the ladder face is gone; keep a small hop so the feet clear the top edge.
        if (s.onLadder) {
            s.onLadder = false;
            if (in.forward > 0.1 && !s.onGround) s.velocity = fwd * (p.walkSpeed * 0.5) + Vec3{0, 0, 120};
        }
    }

    // Jump: a fresh press on the ground, standing (or late out of a slide), after the minimum interval.
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
            if (s.sprinting && p.sprintLeapEnabled) {
                // Sprint leap (Leap_ShouldSprintLeap / Jump_Check): the sprint's age sets the height floor and a
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
            s.sprinting = false;
            s.sprintButtonUpRequired = false;
            s.sliding = false;
            jumped = true;
        }
    }

    // Wish velocity (walk_movement.cpp ReplayWishVelocity). Inputs are -1..1; the engine's max/len factor keeps
    // diagonals at the same speed as the axis moves they combine.
    const double len = std::hypot(in.forward, in.right);
    const bool jogOk = !s.sprinting && !s.crouched && !s.prone && !s.sliding;
    Vec3 wish{};
    if (len > 1e-9) {
        const double k = std::max(std::fabs(in.forward), std::fabs(in.right)) / len;
        if (s.onGround) {
            double speedScale = p.walkSpeed * k * p.weaponMoveScale;
            if (s.sprinting || s.sliding) speedScale *= p.sprintScale;
            if (s.prone) speedScale *= p.proneScale;
            else if (s.crouched) speedScale *= p.crouchScale;
            if (in.tacticalSprint && p.tacticalSprintEnabled && s.sprinting) speedScale *= p.tacticalSprintScale;
            double fwdCmd = in.forward;
            if (s.sliding && s.slideElapsed < p.slideInSeconds) fwdCmd = 1.0;  // slide-in: forced forward
            const double forwardScale = fwdCmd > 0 ? (jogOk ? p.jogForwardScale : 1.0)
                                                    : fwdCmd < 0 ? p.backScale : 1.0;
            const double strafeCmd = in.right * (s.sprinting ? p.sprintStrafeScale : 1.0);
            wish = fwd * (fwdCmd * speedScale * forwardScale) + right * (strafeCmd * speedScale * p.strafeScale);
        } else {
            wish = (fwd * in.forward + right * in.right) * (p.walkSpeed * k);  // air: no weapon, stance or sprint
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
        // Along the ground plane at the same speed (Quake 3 PM_WalkMove: "don't decrease velocity when going up or down a
        // slope"). The vertical part was clamped to 0 here, so a ramp was climbed by step-sliding into it: about 4x
        // slower than flat ground (map-agent report 2026-11-14, a 192 x 128 ramp in 7 s).
        const double speed0 = length(s.velocity);
        s.velocity = clipVelocity(s.velocity, s.groundNormal, kOverbounce);
        if (const double l = length(s.velocity); l > 1e-9) s.velocity = s.velocity * (speed0 / l);
        stepSlideMove(c, dt, false, true);
    } else {
        stepSlideMove(c, dt, true, false);
    }
    groundTrace(c);

    // Walking down a stair or off a short ledge: stay on the ground when a walkable surface is within stepSize.
    if (wasGround && !s.onGround && !jumped && s.velocity.z <= 0) {
        const Vec3 down = s.origin - Vec3{0, 0, p.stepSize};
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
    s.sprinting = s.sliding = s.mantling = false;
    s.mantleProgress = 0;
    s.jumpingState = false;
    s.landingMs = 0;
    s.subsequentJumps = 0;
    s.slideSubsequent = 0;
    s.sinceJump = s.sinceSlideEnd = s.sinceSprintStart = kLongAgo;
    s.slideElapsed = 0;
    s.sprintMeter = p.sprintSeconds;
    s.sprintButtonUpRequired = false;
    s.crouchHeld = false;
    s.viewHeight = s.viewFrom = s.viewTarget = p.standEye;
    s.viewLerpT = s.viewLerpDur = 0;

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

}  // namespace iw8r
