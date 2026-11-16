// Play preview movement against the Replay 1.20 pmove (docs/research/140_movement_parity.md): the verified speeds,
// acceleration and friction, sprint / tactical sprint, the stance view-height curves, the ladder, the mantle height
// limit, the mouse-look constants and the game's default controls (PlayControls).
#include <cmath>
#include <cstdio>

#include "collision_world.h"
#include "document.h"
#include "player_move.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

std::string mpBox(double x0, double y0, double z0, double x1, double y1, double z1, const char* mat = "floor") {
    auto pt = [](double x, double y, double z) {
        return "( " + formatNumber(x) + " " + formatNumber(y) + " " + formatNumber(z) + " )";
    };
    const std::string s = std::string(" ") + mat + " 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n";
    std::string t = "{\n";
    t += " " + pt(x1, y1, z0) + " " + pt(x0, y1, z0) + " " + pt(x0, y0, z0) + s;
    t += " " + pt(x0, y0, z1) + " " + pt(x0, y1, z1) + " " + pt(x1, y1, z1) + s;
    t += " " + pt(x0, y0, z1) + " " + pt(x1, y0, z1) + " " + pt(x1, y0, z0) + s;
    t += " " + pt(x1, y1, z1) + " " + pt(x0, y1, z1) + " " + pt(x0, y1, z0) + s;
    t += " " + pt(x1, y0, z1) + " " + pt(x1, y1, z1) + " " + pt(x1, y1, z0) + s;
    t += " " + pt(x0, y1, z1) + " " + pt(x0, y0, z1) + " " + pt(x0, y0, z0) + s;
    return t + "}\n";
}

CollisionWorld mpWorld(const std::string& extra = {}) {
    std::string t = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n" + mpBox(-2048, -2048, -16, 2048, 2048, 0) + extra + "}\n";
    Id next = 1;
    CollisionWorld w;
    w.build(*parseMap(t, next).map);
    return w;
}

double hs(const PlayerState& s) { return std::hypot(s.velocity.x, s.velocity.y); }

void steps(const CollisionWorld& w, PlayerState& s, const MoveInput& in, const MoveParams& p, int n) {
    for (int i = 0; i < n; ++i) playerMove(w, s, in, p);
}

PlayerState placed(const CollisionWorld& w, const MoveParams& p, Vec3 at = {-1500, 0, 50}) {
    PlayerState s;
    placePlayer(w, s, at, p);
    s.yaw = 0;
    return s;
}

}  // namespace

// PM_CmdScale (0x14111EF60): g_speed 190 x weapon 0.86 x {jog 1.15 forward, back 0.88, strafe 0.8} x stance
// {crouch 0.45, prone 0.2} x sprint 1.4 x tactical 1.2. The acceleration out-runs the friction at every stance, so the
// speed settles exactly on the wish speed.
TEST(parity_steady_speeds_match_cmdscale) {
    const CollisionWorld w = mpWorld();
    const MoveParams p;
    struct Case {
        const char* name;
        double fwd, right;
        int stance;  // 0 stand, 1 crouch, 2 prone
        int sprint;  // 0 none, 1 sprint, 2 tactical
        double expect;
    };
    const double k = 190 * 0.86;
    const Case cases[] = {
        {"walk forward", 1, 0, 0, 0, k * 1.15},
        {"walk back", -1, 0, 0, 0, k * 0.88},
        {"strafe", 0, 1, 0, 0, k * 0.8},
        {"diagonal", 1, 1, 0, 0, k / std::sqrt(2.0) * std::hypot(1.15, 0.8)},
        {"crouch", 1, 0, 1, 0, k * 0.45},
        {"prone", 1, 0, 2, 0, k * 0.2},
        {"sprint", 1, 0, 0, 1, k * 1.4},
        {"tactical sprint", 1, 0, 0, 2, k * 1.4 * 1.2},
        // Sprinting halves the side command before the scale (max/len of (1, 0.5)).
        {"sprint diagonal", 1, 1, 0, 1, k * 1.4 / std::hypot(1.0, 0.5) * std::hypot(1.0, 0.5 * 0.8)},
    };
    for (const Case& c : cases) {
        PlayerState s = placed(w, p);
        MoveInput in;
        in.forward = c.fwd;
        in.right = c.right;
        in.crouch = c.stance == 1;
        in.prone = c.stance == 2;
        in.sprint = c.sprint > 0;
        steps(w, s, in, p, 60);
        in.tacticalSprint = c.sprint == 2;
        steps(w, s, in, p, 90);
        const bool ok = std::fabs(hs(s) - c.expect) < 0.05;
        if (!ok) std::printf("  %s: %.3f, expected %.3f\n", c.name, hs(s), c.expect);
        CHECK(ok);
    }
}

// PM_Accelerate (0x14111C7D0): accelspeed = accel * dt * max(100, wishspeed); PM_Friction (0x141121160):
// drop = 5.5 * max(100, speed) * dt.
TEST(parity_acceleration_floor_and_friction) {
    const CollisionWorld w = mpWorld();
    const MoveParams p;
    const double dt = 1.0 / 60.0;
    // Standing, from rest: one step adds 9 x dt x 187.9 (the wish speed is above the floor).
    PlayerState s = placed(w, p);
    MoveInput fwd;
    fwd.forward = 1;
    playerMove(w, s, fwd, p);
    CHECK(std::fabs(hs(s) - 9 * dt * 190 * 0.86 * 1.15) < 1e-6);
    // Prone, from rest: 19 x dt x 100 (the floor), not 19 x dt x 32.7.
    PlayerState pr = placed(w, p);
    MoveInput prone;
    prone.prone = true;
    steps(w, pr, prone, p, 60);  // settle on the ground and the eye
    CHECK(pr.prone && hs(pr) < 1e-9);
    prone.forward = 1;
    playerMove(w, pr, prone, p);
    CHECK(std::fabs(hs(pr) - 19 * dt * 100) < 1e-6);
    // Friction from the walk speed: one step without input drops 5.5 x 187.9 x dt; under 100 the floor applies.
    steps(w, s, fwd, p, 60);
    const double v0 = hs(s);
    playerMove(w, s, MoveInput{}, p);
    CHECK(std::fabs(hs(s) - (v0 - 5.5 * v0 * dt)) < 1e-6);
    steps(w, s, MoveInput{}, p, 60);
    CHECK(hs(s) == 0);
}

// Tactical sprint (PM_GetSprintLeft 0x141122140, PM_ComputeSprintMeterDelta 0x141122270, PM_DoesSprintMeterTestPass
// 0x14111F730): a 2 s meter drained only by the tactical sprint, refilled 1 s per s while not sprinting, 40 % to
// start; the plain sprint carries on without a meter.
TEST(parity_tactical_sprint_meter) {
    const CollisionWorld w = mpWorld();
    const MoveParams p;
    PlayerState s = placed(w, p);
    MoveInput in;
    in.forward = 1;
    in.sprint = true;
    steps(w, s, in, p, 30);
    CHECK(s.sprinting && !s.tacticalSprinting && std::fabs(s.sprintMeter - 2.0) < 1e-9);
    in.sprint = false;  // toggle sprint: the key can go up
    in.tacticalSprint = true;
    playerMove(w, s, in, p);
    in.tacticalSprint = false;
    CHECK(s.tacticalSprinting);
    steps(w, s, in, p, 60);
    CHECK(s.tacticalSprinting && std::fabs(hs(s) - 190 * 0.86 * 1.4 * 1.2) < 0.05);
    CHECK(std::fabs(s.sprintMeter - 1.0) < 1e-6);  // drained from the step after it started
    steps(w, s, in, p, 70);  // the meter runs out at 2 s: back to the plain sprint
    CHECK(s.sprinting && !s.tacticalSprinting && s.sprintMeter == 0);
    steps(w, s, in, p, 30);
    CHECK(std::fabs(hs(s) - 190 * 0.86 * 1.4) < 0.05);
    // With the meter empty, a request does nothing; the plain sprint lasts (no meter of its own).
    in.tacticalSprint = true;
    steps(w, s, in, p, 120);
    CHECK(s.sprinting && !s.tacticalSprinting);
    in.tacticalSprint = false;
    // Stop: the meter refills at 1 s per s; 0.5 s is not enough for the 40 % (0.8 s), 0.9 s is.
    MoveInput stop;
    steps(w, s, stop, p, 30);
    CHECK(!s.sprinting && std::fabs(s.sprintMeter - 0.5) < 1e-6);
    MoveInput go = in;
    go.sprint = true;
    steps(w, s, go, p, 2);
    go.sprint = false;
    go.tacticalSprint = true;
    playerMove(w, s, go, p);
    CHECK(s.sprinting && !s.tacticalSprinting);
    steps(w, s, stop, p, 60);
    CHECK(s.sprintMeter > 0.8);
    go.tacticalSprint = false;
    go.sprint = true;
    steps(w, s, go, p, 2);
    go.sprint = false;
    go.tacticalSprint = true;
    playerMove(w, s, go, p);
    CHECK(s.tacticalSprinting);
}

// Toggle sprint (profile holdSprint 0): the press starts it, it lasts after the key goes up and ends when forward
// drops. Ended by a jump it comes back on landing while forward stays held (PM_SetSprintRestore), and the jump is a
// sprint leap.
TEST(parity_toggle_sprint_and_restore) {
    const CollisionWorld w = mpWorld();
    const MoveParams p;
    PlayerState s = placed(w, p);
    MoveInput in;
    in.forward = 1;
    in.sprint = true;
    playerMove(w, s, in, p);
    CHECK(s.sprinting);
    in.sprint = false;
    steps(w, s, in, p, 60);
    CHECK(s.sprinting);
    // Jump: sprint ends with the jump (a leap after 0.3 s of sprint), comes back after landing.
    MoveInput jump = in;
    jump.jump = true;
    playerMove(w, s, jump, p);
    CHECK(!s.sprinting && !s.onGround && s.sprintRestore);
    double apex = s.origin.z;
    bool landed = false;
    for (int i = 0; i < 120 && !landed; ++i) {
        playerMove(w, s, in, p);
        apex = std::max(apex, s.origin.z);
        landed = s.onGround;
    }
    CHECK(landed && std::fabs(apex - p.sprintLeapHeight) < 2.0);
    playerMove(w, s, in, p);
    CHECK(s.sprinting);
    // Forward released: the sprint ends and does not come back without a new press.
    steps(w, s, MoveInput{}, p, 5);
    CHECK(!s.sprinting && !s.sprintRestore);
    steps(w, s, in, p, 30);
    CHECK(!s.sprinting);
}

// The stance view-height curves (PM_ViewHeightTableLerp tables, PM_GetStanceViewLerpTime): stand -> crouch 200 ms,
// crouch -> prone 400 ms (with its overshoot), prone -> crouch 400 ms, crouch -> stand 200 ms, one stance at a time.
TEST(parity_view_height_curves) {
    CHECK(std::fabs(viewCurveValue(ViewCurve::CrouchProne, 0.5) - 0.862) < 1e-9);       // past prone, then back
    CHECK(std::fabs(viewCurveValue(ViewCurve::ProneCrouch, 0.05) + 0.034) < 1e-9);      // dips first
    CHECK(std::fabs(viewCurveValue(ViewCurve::StandCrouch, 0.55) - 0.5) < 1e-9);
    CHECK(std::fabs(viewCurveValue(ViewCurve::CrouchStand, 0.45) - 0.5) < 1e-9);
    const CollisionWorld w = mpWorld();
    const MoveParams p;
    // Half way through stand -> crouch (0.1 s of 0.2 s): 60 - 20 x curve(0.5) = 60 - 20 x 0.44 = 51.2.
    PlayerState s = placed(w, p);
    MoveInput c;
    c.crouch = true;
    c.dt = 0.025;
    steps(w, s, c, p, 4);
    CHECK(std::fabs(eyeHeight(p, s) - (60 - 20 * 0.44)) < 0.3);  // (the lerp steps in whole percent)
    // Stand -> prone in one go: through crouch (200 ms) then down to prone (400 ms), settled at 11.
    PlayerState d = placed(w, p);
    MoveInput pr;
    pr.prone = true;
    pr.dt = 0.025;
    steps(w, d, pr, p, 8);  // 0.2 s: the crouch height
    CHECK(std::fabs(eyeHeight(p, d) - 40) < 0.5);
    steps(w, d, pr, p, 10);  // 0.25 s into crouch -> prone: past the overshoot, on the way back to 11
    const double mid = eyeHeight(p, d);
    CHECK(mid < 40 - 29 * 0.75 && mid > 40 - 29 * 0.87);
    steps(w, d, pr, p, 10);
    CHECK(std::fabs(eyeHeight(p, d) - 11) < 1e-6);
}

// Mantle_GetMaxClimbHeight (0x141106C40): 58 units in MP. A standing jump press mantles onto 56, not onto 64.
TEST(parity_mantle_height_limit) {
    const MoveParams p;
    for (const double h : {56.0, 64.0}) {
        const CollisionWorld w = mpWorld(mpBox(512, -200, 0, 600, 200, h));
        PlayerState s = placed(w, p, {490, 0, 50});
        MoveInput in;
        in.forward = 1;
        in.jump = true;
        bool started = false;
        for (int i = 0; i < 120 && !started; ++i) {
            playerMove(w, s, in, p);
            in.jump = false;
            started = s.mantling;
        }
        CHECK(started == (h < 58));
    }
}

// Noclip: 3 x g_speed (PM_CmdScale for pm_type noclip). Mouse look: 0.022 x sensitivity 12 x 0.3 degrees per count.
TEST(parity_noclip_and_mouse_constants) {
    const CollisionWorld w = mpWorld();
    const MoveParams p;
    PlayerState s = placed(w, p);
    s.noclip = true;
    MoveInput in;
    in.forward = 1;
    playerMove(w, s, in, p);
    CHECK(std::fabs(length(s.velocity) - 570) < 1e-9);
    CHECK(std::fabs(mouseDegreesPerCount() - 0.0792) < 1e-12);
    CHECK(std::fabs(mouseDegreesPerCount(5) - 0.033) < 1e-12);
    CHECK(p.pitchLimit == 85);
}

// The game's default controls (default_keys.cfg, profile defaults crouchType / proneType 1 = toggle,
// tapToSlideEnabledKeyboard 1, holdSprint 0, stanceUpJump 1).
TEST(parity_play_controls_default_binds) {
    PlayControls pc;
    PlayerState s;
    s.onGround = true;
    bool held[int(PlayKey::Count)] = {};
    const auto tap = [&](PlayKey k) {  // the step with the key down, then a step with it up
        held[int(k)] = true;
        MoveInput in = pc.input(held, s, 1.0 / 60);
        held[int(k)] = false;
        pc.input(held, s, 1.0 / 60);
        return in;
    };
    const auto idle = [&]() { return pc.input(held, s, 1.0 / 60); };
    // C toggles crouch; Ctrl toggles prone.
    CHECK(tap(PlayKey::Crouch).crouch);
    CHECK(idle().crouch);
    CHECK(!tap(PlayKey::Crouch).crouch);
    CHECK(tap(PlayKey::Prone).prone);
    CHECK(!tap(PlayKey::Prone).prone && !idle().prone);
    // Space: from prone it crouches, from crouch it stands, standing it jumps.
    tap(PlayKey::Prone);
    MoveInput in = tap(PlayKey::Jump);
    CHECK(!in.jump && in.crouch && !in.prone);
    in = tap(PlayKey::Jump);
    CHECK(!in.jump && !in.crouch);
    held[int(PlayKey::Jump)] = true;
    CHECK(idle().jump);  // a jump while held
    held[int(PlayKey::Jump)] = false;
    CHECK(!idle().jump);
    // Shift while crouched stands up (to sprint); a second press while sprinting asks for tactical sprint.
    tap(PlayKey::Crouch);
    in = tap(PlayKey::Sprint);
    CHECK(in.sprint && !in.crouch);
    s.sprinting = true;
    in = tap(PlayKey::Sprint);
    CHECK(in.tacticalSprint);
    s.tacticalSprinting = true;
    idle();                         // sent until the tactical sprint has started ...
    CHECK(!idle().tacticalSprint);  // ... then no more
    s.tacticalSprinting = false;
    // C while sprinting: a slide request (and the crouch if the slide cannot start).
    in = tap(PlayKey::Crouch);
    CHECK(in.slide && in.crouch);
    CHECK(!idle().slide);
    // During the slide the crouch bit is off; a C press cancels into a crouch.
    s.sprinting = false;
    s.sliding = true;
    s.crouched = true;
    CHECK(!idle().crouch);
    CHECK(tap(PlayKey::Crouch).crouch);
    // The slide ended crouched: the client keeps the crouch.
    s.sliding = false;
    CHECK(idle().crouch);
    // H autoforward; W ends it.
    tap(PlayKey::Crouch);
    CHECK(tap(PlayKey::AutoForward).forward == 1);
    CHECK(idle().forward == 1);
    CHECK(tap(PlayKey::Forward).forward == 1);  // W is held this step
    CHECK(idle().forward == 0);
    // A press shorter than a step still counts (press() from the key event).
    pc.press(PlayKey::Crouch);
    CHECK(idle().crouch);
}

// End to end with the controls: sprint, tap C to slide, the slide ends in a crouch, Space stands up.
TEST(parity_controls_slide_end_to_end) {
    const CollisionWorld w = mpWorld();
    const MoveParams p;
    PlayerState s = placed(w, p);
    PlayControls pc;
    bool held[int(PlayKey::Count)] = {};
    const auto step = [&](int n) {
        for (int i = 0; i < n; ++i) playerMove(w, s, pc.input(held, s, 1.0 / 60), p);
    };
    held[int(PlayKey::Forward)] = true;
    held[int(PlayKey::Sprint)] = true;
    step(2);
    held[int(PlayKey::Sprint)] = false;
    step(40);
    CHECK(s.sprinting);
    pc.press(PlayKey::Crouch);
    step(1);
    CHECK(s.sliding && s.crouched);
    step(40);
    CHECK(!s.sliding && s.crouched && pc.stance == PlayStance::Crouch);
    step(30);
    CHECK(s.crouched && !s.sprinting);  // forward still held: crouched walk, not a sprint
    pc.press(PlayKey::Jump);
    step(30);
    CHECK(!s.crouched && s.onGround && pc.stance == PlayStance::Stand);
}

// A ladder volume behind something that blocks (the compact showcase's tower: the ladder model's collision stands
// ~19 units in front of the prefab's ladder brush). BG_GetLadderInfo finds a ladder 30 units out
// (ladderLineQueryDistance), so the player climbs from where the model stops it, up to the top and onto the tower.
TEST(parity_ladder_behind_a_model_collision) {
    const CollisionWorld w = mpWorld(mpBox(100, -12, 0, 104, 12, 160, "ladder") +   // the ladder volume
                                     mpBox(82, -14, 0, 92, 14, 150) +                  // a model's collision in front
                                     mpBox(104, -64, 0, 200, 64, 160));                // the tower
    const MoveParams p;
    PlayerState s = placed(w, p, {0, 0, 50});
    MoveInput in;
    in.forward = 1;
    double top = 0;
    bool climbed = false;
    for (int i = 0; i < 60 * 5 && !(climbed && s.onGround && s.origin.x > 110); ++i) {  // until standing on the tower
        playerMove(w, s, in, p);
        climbed |= s.onLadder;
        top = std::max(top, s.origin.z);
    }
    std::printf("  ladder behind a model: top %.1f, end %.1f %.1f %.1f ground %d ladder %d\n", top, s.origin.x, s.origin.y, s.origin.z,
                int(s.onGround), int(s.onLadder));
    CHECK(climbed);
    CHECK(s.onGround && std::fabs(s.origin.z - 160) < 0.5 && s.origin.x > 104);  // walked off the top onto the tower
    CHECK(top < 175);
}

// Mantle_CheckActivationConditions (0x141108A90): the jump button held while pushing into a ledge mantles (also in the
// air, after the jump it made); in the air after a jump, more than 36 units up, pushing into a ledge mantles by itself.
TEST(parity_mantle_activation_held_and_airborne) {
    const MoveParams p;
    {  // Space held from 120 units out: the jump goes first, the mantle follows in the air.
        const CollisionWorld w = mpWorld(mpBox(512, -200, 0, 600, 200, 48));
        PlayerState s = placed(w, p, {377, 0, 50});
        MoveInput in;
        in.forward = 1;
        in.jump = true;
        bool mantled = false;
        for (int i = 0; i < 240 && !(mantled && !s.mantling); ++i) {
            playerMove(w, s, in, p);
            mantled |= s.mantling;
        }
        CHECK(mantled && std::fabs(s.origin.z - 48) < 0.5);
    }
    {  // One frame of Space, then only forward: at the top of the jump the 80 wall (41 above the feet) mantles by itself.
        const CollisionWorld w = mpWorld(mpBox(512, -200, 0, 600, 200, 80));
        PlayerState s = placed(w, p, {300, 0, 50});
        MoveInput in;
        in.forward = 1;
        for (int i = 0; i < 120 && s.origin.x < 440; ++i) playerMove(w, s, in, p);  // at walk speed, 57 units out
        in.jump = true;
        playerMove(w, s, in, p);
        in.jump = false;
        CHECK(!s.onGround && !s.mantling);
        bool mantled = false;
        for (int i = 0; i < 240 && !(mantled && !s.mantling); ++i) {
            playerMove(w, s, in, p);
            mantled |= s.mantling;
        }
        std::printf("  airborne auto mantle: mantled %d, end %.1f %.1f ground %d\n", int(mantled), s.origin.x, s.origin.z, int(s.onGround));
        CHECK(mantled && std::fabs(s.origin.z - 80) < 0.5);
    }
    {  // Crouched: no mantle, Space does nothing at the ledge.
        const CollisionWorld w = mpWorld(mpBox(512, -200, 0, 600, 200, 40));
        PlayerState s = placed(w, p, {490, 0, 50});
        MoveInput in;
        in.forward = 1;
        in.crouch = true;
        steps(w, s, in, p, 30);
        in.jump = true;
        steps(w, s, in, p, 60);
        CHECK(!s.mantling && s.origin.z < 1);
    }
    // A Space tap shorter than a step still jumps (the press is latched for one step).
    PlayControls pc;
    PlayerState st;
    st.onGround = true;
    bool held[int(PlayKey::Count)] = {};
    pc.press(PlayKey::Jump);
    CHECK(pc.input(held, st, 1.0 / 60).jump);
    CHECK(!pc.input(held, st, 1.0 / 60).jump);
}

// Hideout's play preview froze the player in the air: stock model physics pieces without caps became convex solids
// reaching the +-262144 winding box, slabs through the whole map. Such open hulls are not added.
TEST(parity_open_hull_is_not_a_solid) {
    CollisionWorld w;
    // An octagonal prism with no end caps: open along z.
    std::vector<Plane> open;
    for (int k = 0; k < 8; ++k) {
        const double a = k * 3.14159265358979323846 / 4;
        open.push_back({Vec3{std::cos(a), std::sin(a), 0}, 8.0});
    }
    CHECK(!w.addConvex(open));
    std::vector<Plane> capped = open;
    capped.push_back({Vec3{0, 0, 1}, 64});
    capped.push_back({Vec3{0, 0, -1}, 0});
    CHECK(w.addConvex(capped));
    CHECK(w.solids().size() == 1);
}

namespace {

// A stock map's world in the play preview: triangle facets 2 units thick (CollisionWorld::addTriangle), outward
// normals. A box as 12 facets.
void facetQuad(CollisionWorld& w, Vec3 a, Vec3 b, Vec3 c, Vec3 d, const Vec3& outward) {
    if (dot(cross(b - a, c - a), outward) < 0) std::swap(b, d);
    w.addTriangle(a, b, c, 2.0);
    w.addTriangle(a, c, d, 2.0);
}
void facetBox(CollisionWorld& w, Vec3 lo, Vec3 hi) {
    const Vec3 v[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
                       {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
    facetQuad(w, v[4], v[5], v[6], v[7], {0, 0, 1});
    facetQuad(w, v[0], v[3], v[2], v[1], {0, 0, -1});
    facetQuad(w, v[0], v[1], v[5], v[4], {0, -1, 0});
    facetQuad(w, v[2], v[3], v[7], v[6], {0, 1, 0});
    facetQuad(w, v[1], v[2], v[6], v[5], {1, 0, 0});
    facetQuad(w, v[3], v[0], v[4], v[7], {-1, 0, 0});
}
CollisionWorld facetWorld() {
    CollisionWorld w;
    facetQuad(w, {-2048, -2048, 0}, {2048, -2048, 0}, {2048, 2048, 0}, {-2048, 2048, 0}, {0, 0, 1});  // the floor
    return w;
}

}  // namespace

// Mantle, step-up, walking down and the ladder-free walk work against facets as against brushes.
TEST(parity_movement_on_triangle_facets) {
    const MoveParams p;
    {  // Mantle onto a 48-unit facet box with Space held.
        CollisionWorld w = facetWorld();
        facetBox(w, {512, -200, 0}, {600, 200, 48});
        PlayerState s = placed(w, p, {470, 0, 50});
        CHECK(s.onGround && std::fabs(s.origin.z) < 0.5);
        MoveInput in;
        in.forward = 1;
        in.jump = true;
        bool mantled = false;
        for (int i = 0; i < 240 && !(mantled && !s.mantling); ++i) {
            playerMove(w, s, in, p);
            mantled |= s.mantling;
        }
        CHECK(mantled && std::fabs(s.origin.z - 48) < 0.5);
    }
    {  // A 16-unit facet step is walked up, a 12-unit one walked down without leaving the ground.
        CollisionWorld w = facetWorld();
        facetBox(w, {200, -200, 0}, {600, 200, 16});
        PlayerState s = placed(w, p, {0, 0, 50});
        MoveInput in;
        in.forward = 1;
        steps(w, s, in, p, 90);
        CHECK(s.onGround && s.origin.x > 260 && std::fabs(s.origin.z - 16) < 0.5);
        CollisionWorld w2 = facetWorld();
        facetBox(w2, {-600, -200, 0}, {128, 200, 12});
        PlayerState d = placed(w2, p, {0, 0, 50});
        CHECK(std::fabs(d.origin.z - 12) < 0.5);
        int airborne = 0;
        for (int i = 0; i < 120 && d.origin.x < 220; ++i) {
            playerMove(w2, d, in, p);
            if (!d.onGround) ++airborne;
        }
        CHECK(d.origin.x >= 220 && airborne == 0 && std::fabs(d.origin.z) < 0.5);
    }
}

// Low stock props with steep faces (Hideout: debris piles, cinderblocks, broken wall caps): walking into them bobbed
// the player (stepped onto a face too steep to stand on, slid back, stepped again). With PM_ClipSlideVelocity's
// steep-face-as-wall clip and PM_StepSlideMove's landing rule the player stops at the face or slides along it; z stays
// steady.
TEST(parity_no_bobbing_against_a_low_steep_wedge) {
    const MoveParams p;
    struct Case {
        double degrees, height, yaw;
    };
    for (const Case cs : {Case{60, 20, 0}, Case{75, 24, 0}, Case{60, 24, 30}, Case{50, 22, 0}, Case{80, 16, 0}}) {
        // A prism along y: a front face rising at `degrees` from level to the ridge, the same back face.
        const double a = cs.degrees * 3.14159265358979323846 / 180.0;
        const double run = cs.height / std::tan(a);
        const double x0 = 200;
        CollisionWorld w = mpWorld();
        std::vector<Plane> wedge = {{Vec3{0, 0, -1}, 0},
                                    {Vec3{-std::sin(a), 0, std::cos(a)}, -std::sin(a) * x0},
                                    {Vec3{std::sin(a), 0, std::cos(a)}, std::sin(a) * (x0 + 2 * run)},
                                    {Vec3{0, 1, 0}, 400},
                                    {Vec3{0, -1, 0}, 400}};
        CHECK(w.addConvex(wedge));
        PlayerState s = placed(w, p, {0, 0, 50});
        s.yaw = cs.yaw;
        MoveInput in;
        in.forward = 1;
        double zMin = 1e9, zMax = -1e9;
        int reversals = 0;
        double lastZ = s.origin.z, lastDir = 0;
        for (int i = 0; i < 60 * 3; ++i) {
            playerMove(w, s, in, p);
            const double dz = s.origin.z - lastZ;
            if (std::fabs(dz) > 0.05) {
                const double dir = dz > 0 ? 1 : -1;
                if (lastDir != 0 && dir != lastDir) ++reversals;
                lastDir = dir;
            }
            lastZ = s.origin.z;
            if (i >= 120) zMin = std::min(zMin, s.origin.z), zMax = std::max(zMax, s.origin.z);  // the last second
        }
        // At most one climb and one descent (stopped at the face, slid along it, or walked over it once), then still.
        const bool steady = reversals <= 2 && zMax - zMin < 1.0;
        if (!steady || !(s.origin.z < cs.height + 1))
            std::printf("  wedge %.0f deg %.0f high yaw %.0f: z %.2f..%.2f, %d reversals, end %.1f %.1f %.1f\n", cs.degrees,
                        cs.height, cs.yaw, zMin, zMax, reversals, s.origin.x, s.origin.y, s.origin.z);
        CHECK(steady);
    }
}

// The steep-landing rules must still let the player step onto a low box whose top edge is chamfered steeply (stock
// crates and curbs): 16 high, a 60 degree chamfer 4 units tall on the front edge.
TEST(parity_steps_onto_a_chamfered_box) {
    const MoveParams p;
    for (const double deg : {45.0, 60.0, 75.0}) {
        const double a = deg * 3.14159265358979323846 / 180.0;
        CollisionWorld w = mpWorld();
        const Vec3 n{-std::sin(a), 0, std::cos(a)};
        std::vector<Plane> box = {{Vec3{0, 0, -1}, 0}, {Vec3{0, 0, 1}, 16},  {Vec3{-1, 0, 0}, -200}, {Vec3{1, 0, 0}, 400},
                                  {Vec3{0, 1, 0}, 300}, {Vec3{0, -1, 0}, 300}, {n, dot(n, Vec3{200, 0, 12})}};
        CHECK(w.addConvex(box));
        PlayerState s = placed(w, p, {0, 0, 50});
        MoveInput in;
        in.forward = 1;
        for (int i = 0; i < 120; ++i) playerMove(w, s, in, p);
        const bool on = s.onGround && std::fabs(s.origin.z - 16) < 0.5 && s.origin.x > 220;
        if (!on) std::printf("  chamfer %.0f: end %.1f %.1f ground %d\n", deg, s.origin.x, s.origin.z, int(s.onGround));
        CHECK(on);
    }
}
