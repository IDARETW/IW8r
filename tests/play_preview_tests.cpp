// Play preview: box traces against brushes and player movement (walk, wall, step, jump, crouch, noclip).
#include <cmath>
#include <cstdio>

#include "collision_world.h"
#include "document.h"
#include "player_move.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

std::string box(double x0, double y0, double z0, double x1, double y1, double z1, const char* mat = "floor") {
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

// Floor 0..1024 x -512..512 at z <= 0, a wall at x 512..528, a 16-unit step at x 128..1024 for y in 200..400,
// a 24-unit ledge for y in -400..-200, and a trigger brush entity that must not block.
MapFile testMap() {
    std::string t = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n";
    t += box(-512, -512, -16, 1024, 512, 0);
    t += box(512, -512, 0, 528, 0, 256);           // wall for y < 0
    t += box(128, 200, 0, 1024, 400, 16);          // 16-unit step (walkable)
    t += box(128, -400, 0, 400, -200, 24);         // 24-unit ledge (too high)
    t += box(-200, -100, 0, -150, 100, 128, "clip_weapon");  // weapon clip: players pass
    t += "}\n{\n\"classname\" \"trigger_multiple\"\n" + box(-100, -100, 0, -60, 100, 128, "trigger") + "}\n";
    Id next = 1;
    return *parseMap(t, next).map;
}

void run(const CollisionWorld& w, PlayerState& s, MoveInput in, const MoveParams& p, double seconds) {
    for (double t = 0; t < seconds; t += in.dt) playerMove(w, s, in, p);
}

}  // namespace

TEST(collision_traces_against_brushes) {
    CollisionWorld w;
    w.build(testMap());
    CHECK(w.solids().size() == 4);  // the weapon clip and the trigger do not block players
    // A ray down onto the floor stops at z = 0 (within the trace epsilon).
    auto tr = w.traceRay({0, 0, 100}, {0, 0, -100});
    CHECK(tr.fraction < 1 && std::fabs(tr.end.z) < 0.1 && tr.plane.n.z > 0.99);
    // A 30x30 box moving +x into the wall stops at x = 512 - 15.
    tr = w.traceBox({400, -100, 10}, {700, -100, 10}, {-15, -15, 0}, {15, 15, 70});
    CHECK(tr.fraction < 1 && std::fabs(tr.end.x - 497) < 0.1);
    // Above the wall (y > 0) the move is free.
    tr = w.traceBox({400, 100, 10}, {450, 100, 10}, {-15, -15, 0}, {15, 15, 70});
    CHECK(tr.fraction == 1.0);
    // Inside the floor: start solid.
    tr = w.traceBox({0, 0, -8}, {0, 0, -8}, {-15, -15, 0}, {15, 15, 70});
    CHECK(tr.startSolid);
}

TEST(player_walks_steps_and_is_blocked) {
    CollisionWorld w;
    w.build(testMap());
    MoveParams p;
    PlayerState s;
    CHECK(placePlayer(w, s, {0, -100, 50}, p));
    CHECK(s.onGround && std::fabs(s.origin.z) < 0.2);
    // Walk +x for 3 s: blocked by the wall at x = 497, still on the floor.
    s.yaw = 0;
    MoveInput in;
    in.forward = 1;
    run(w, s, in, p, 4.0);
    CHECK(std::fabs(s.origin.x - 497) < 1.0 && std::fabs(s.origin.z) < 0.2);
    // Forward walk on open ground: 190 x 1.15 (jog forward, walk_profile.cpp playerJogForwardSpeedScale) x 0.86 (M4).
    // The earlier 163 expectation left the jog scale out; the reference applies it (walk_movement.cpp ReplayWishVelocity).
    PlayerState r;
    placePlayer(w, r, {-400, 0, 50}, p);
    run(w, r, in, p, 1.0);
    CHECK(std::hypot(r.velocity.x, r.velocity.y) > 185 && std::hypot(r.velocity.x, r.velocity.y) < 190);
    // The 16-unit step is climbed; the 24-unit ledge is not.
    PlayerState up;
    placePlayer(w, up, {0, 300, 50}, p);
    run(w, up, in, p, 1.5);
    CHECK(up.origin.x > 200 && std::fabs(up.origin.z - 16) < 0.5);
    PlayerState ledge;
    placePlayer(w, ledge, {0, -300, 50}, p);
    run(w, ledge, in, p, 1.5);
    CHECK(ledge.origin.x < 128 && std::fabs(ledge.origin.z) < 0.5);
    // Weapon clip and triggers do not block players.
    PlayerState through;
    placePlayer(w, through, {-300, 0, 50}, p);
    run(w, through, in, p, 2.0);
    CHECK(through.origin.x > 0);
}

TEST(player_jumps_crouches_and_noclips) {
    CollisionWorld w;
    w.build(testMap());
    MoveParams p;
    PlayerState s;
    placePlayer(w, s, {0, 0, 50}, p);
    MoveInput in;
    in.jump = true;
    double apex = 0;
    for (int i = 0; i < 120; ++i) {
        playerMove(w, s, in, p);
        apex = std::max(apex, s.origin.z);
    }
    CHECK(std::fabs(apex - p.jumpHeight) < 2.0);           // a standing jump reaches jumpHeight
    CHECK(s.onGround && std::fabs(s.origin.z) < 0.2);      // and lands again (holding jump does not re-jump)
    // Climb the 24 ledge with a jump while walking.
    PlayerState j;
    placePlayer(w, j, {0, -300, 50}, p);
    MoveInput go;
    go.forward = 1;
    for (int i = 0; i < 30; ++i) playerMove(w, j, go, p);
    go.jump = true;
    for (int i = 0; i < 90; ++i) playerMove(w, j, go, p);
    CHECK(j.origin.x > 128 && std::fabs(j.origin.z - 24) < 0.5);
    // Crouch lowers the eye over the 200 ms stand-to-crouch lerp (not instantly); noclip passes through the wall.
    MoveInput c;
    c.crouch = true;
    playerMove(w, s, c, p);
    CHECK(s.crouched && eyeHeight(p, s) < p.standEye && eyeHeight(p, s) > p.crouchEye);
    run(w, s, c, p, 0.25);
    CHECK(s.crouched && eyeHeight(p, s) == p.crouchEye);
    s.noclip = true;
    s.crouched = false;
    s.origin = {400, -100, 40};
    s.yaw = 0;
    s.pitch = 0;
    MoveInput fly;
    fly.forward = 1;
    for (int i = 0; i < 60; ++i) playerMove(w, s, fly, p);
    CHECK(s.origin.x > 600);
}

TEST(capsule_trace_rounds_the_corners) {
    CollisionWorld w;
    w.build(testMap());
    // Straight into the wall face both stop at the same x (497).
    const auto f = w.traceCapsule({400, -100, 10}, {700, -100, 10}, 15, 70);
    CHECK(f.fraction < 1 && std::fabs(f.end.x - 497) < 0.1);
    // Standing on the floor: the capsule bottom sphere rests at z = 0.
    const auto down = w.traceCapsule({0, 0, 50}, {0, 0, -50}, 15, 70);
    CHECK(down.fraction < 1 && std::fabs(down.end.z) < 0.1);
}

namespace {

MapFile worldOf(const std::string& solids) {
    std::string t = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n" + solids + "}\n";
    Id next = 1;
    return *parseMap(t, next).map;
}

std::string floorOnly() { return box(-512, -512, -16, 1024, 512, 0); }

double hspeed(const PlayerState& s) { return std::hypot(s.velocity.x, s.velocity.y); }

// Jumps at a ledge (jump on the first frame, then forward held). Returns whether a mantle started and ended.
bool tryMantle(const CollisionWorld& w, PlayerState& s, const MoveParams& p, double seconds, bool* endedOnTop) {
    MoveInput in;
    in.forward = 1;
    in.jump = true;
    bool started = false;
    for (double t = 0; t < seconds; t += in.dt) {
        playerMove(w, s, in, p);
        in.jump = false;
        if (s.mantling) started = true;
        if (started && !s.mantling) break;
    }
    if (endedOnTop) *endedOnTop = started && !s.mantling;
    return started;
}

}  // namespace

TEST(mantle_onto_40_unit_box_ends_on_top) {
    CollisionWorld w;
    w.build(worldOf(floorOnly() + box(512, -200, 0, 600, 200, 40)));
    MoveParams p;
    PlayerState s;
    CHECK(placePlayer(w, s, {490, 0, 50}, p));
    s.yaw = 0;
    bool onTop = false;
    CHECK(tryMantle(w, s, p, 4.0, &onTop));
    CHECK(onTop && s.mantleProgress == 1.0);
    CHECK(std::fabs(s.origin.z - 40) < 0.5);                 // ends on the 40-unit top
    CHECK(s.origin.x > 512 && s.origin.x < 530);             // just past the edge
    CHECK(std::fabs(s.mantleLedgeHeight - 40) < 0.5 && !s.mantleHigh);
    CHECK(std::fabs(s.mantleEnd.z - 40) < 0.5 && s.mantleStart.x < 500);
    // Then the player is free on top: walking carries on along the box (the top ends at x = 600).
    MoveInput go;
    go.forward = 1;
    run(w, s, go, p, 0.3);
    CHECK(s.onGround && std::fabs(s.origin.z - 40) < 0.5 && s.origin.x > 540 && s.origin.x < 600);
}

TEST(mantle_fails_on_100_unit_wall) {
    CollisionWorld w;
    w.build(worldOf(floorOnly() + box(512, -200, 0, 600, 200, 100)));
    MoveParams p;
    PlayerState s;
    placePlayer(w, s, {490, 0, 50}, p);
    s.yaw = 0;
    CHECK(!tryMantle(w, s, p, 3.0, nullptr));
    CHECK(s.origin.x < 512);  // the wall stops the player; nothing carries it over
}

TEST(mantle_fails_under_low_ceiling) {
    CollisionWorld w;
    // The crouched body needs 40 + 50 = 90 above the floor on top of the ledge; the ceiling at 80 blocks it.
    w.build(worldOf(floorOnly() + box(512, -200, 0, 600, 200, 40) + box(480, -200, 80, 640, 200, 100)));
    MoveParams p;
    PlayerState s;
    placePlayer(w, s, {490, 0, 1}, p);  // from low down: a start under the ceiling would be lifted onto it
    s.yaw = 0;
    CHECK(!tryMantle(w, s, p, 3.0, nullptr));
    CHECK(s.origin.x < 512);
}

TEST(mantle_under_tight_ceiling_ends_crouched) {
    CollisionWorld w;
    // The crouched body fits below the ceiling at 95 (needs 90), the standing body does not (needs 110).
    w.build(worldOf(floorOnly() + box(512, -200, 0, 600, 200, 40) + box(480, -200, 95, 640, 200, 130)));
    MoveParams p;
    PlayerState s;
    placePlayer(w, s, {490, 0, 1}, p);
    s.yaw = 0;
    bool onTop = false;
    CHECK(tryMantle(w, s, p, 4.0, &onTop));
    CHECK(onTop && std::fabs(s.origin.z - 40) < 0.5);
    CHECK(s.crouched);
}

TEST(mantle_duration_follows_reference_branches) {
    // Standing still, 40-unit ledge: 0.4 + (0.9 - 0.4) * (40 - 24) / 34 = 0.635 s (mode scale 1 at speed 0).
    const double still = mantleDurationSeconds(40, 100, 0);
    CHECK(std::fabs(still - 0.635) < 0.01);
    // Full speed: the mode scale 0.75 shortens it.
    CHECK(mantleDurationSeconds(40, 100, 250) < still);
    // High branch (above 58): factors 0.9..1.0, so a 66-unit ledge takes longer than a 40-unit one.
    CHECK(mantleDurationSeconds(66, 100, 0) > still);
}

TEST(slide_covers_distance_and_ends_crouched) {
    CollisionWorld w;
    w.build(worldOf(floorOnly()));
    MoveParams p;
    PlayerState s;
    placePlayer(w, s, {-400, 0, 50}, p);
    s.yaw = 0;
    MoveInput sprint;
    sprint.forward = 1;
    sprint.sprint = true;
    run(w, s, sprint, p, 1.5);
    CHECK(s.sprinting && !s.sliding);
    const double before = hspeed(s);
    // A crouch press while sprinting starts the slide; crouch stays held through it.
    MoveInput slide = sprint;
    slide.crouch = true;
    playerMove(w, s, slide, p);
    CHECK(s.sliding && s.crouched);
    const double x0 = s.origin.x;
    run(w, s, slide, p, 0.5);
    CHECK(!s.sliding);                                   // a standard slide lasts 0.4 s
    const double distance = s.origin.x - x0;
    CHECK(distance > 60 && distance < 120);              // well past a walking step
    CHECK(hspeed(s) < before);                           // slide friction bleeds speed
    CHECK(s.crouched);                                   // the slide ends in crouch while crouch is held
    // Releasing crouch stands the player back up once there is room.
    MoveInput stand;
    run(w, s, stand, p, 0.5);
    CHECK(!s.crouched);
}

TEST(sprint_is_faster_only_forward) {
    CollisionWorld w;
    w.build(worldOf(floorOnly()));
    MoveParams p;
    PlayerState walk;
    placePlayer(w, walk, {-400, 0, 50}, p);
    walk.yaw = 0;
    MoveInput fwd;
    fwd.forward = 1;
    run(w, walk, fwd, p, 1.5);
    const double vWalk = hspeed(walk);
    PlayerState sp;
    placePlayer(w, sp, {-400, 0, 50}, p);
    sp.yaw = 0;
    MoveInput sprint = fwd;
    sprint.sprint = true;
    run(w, sp, sprint, p, 1.5);
    CHECK(sp.sprinting);
    const double vSprint = hspeed(sp);
    CHECK(vSprint > vWalk + 30 && vSprint > 215 && vSprint < 235);  // 190 x 1.4 x 0.86
    // Sprint does not start for strafing, and backwards is never a sprint.
    PlayerState side;
    placePlayer(w, side, {-400, 0, 50}, p);
    side.yaw = 0;
    MoveInput strafe;
    strafe.right = 1;
    strafe.sprint = true;
    run(w, side, strafe, p, 1.5);
    CHECK(!side.sprinting && hspeed(side) < 140);  // 190 x 0.86 x 0.8 = 131
    PlayerState back;
    placePlayer(w, back, {0, 0, 50}, p);  // room to back off (300 units) on the floor
    back.yaw = 0;
    MoveInput rev;
    rev.forward = -1;
    rev.sprint = true;
    run(w, back, rev, p, 1.5);
    CHECK(!back.sprinting && hspeed(back) < vWalk);
}

TEST(landing_friction_slows_right_after_landing) {
    CollisionWorld w;
    w.build(worldOf(floorOnly()));
    MoveParams p;
    PlayerState s;
    placePlayer(w, s, {-400, 0, 50}, p);
    s.yaw = 0;
    MoveInput fwd;
    fwd.forward = 1;
    run(w, s, fwd, p, 1.5);
    const double before = hspeed(s);
    MoveInput jump = fwd;
    jump.jump = true;
    playerMove(w, s, jump, p);
    bool landed = false;
    for (int i = 0; i < 180 && !landed; ++i) {  // airborne first, then the landing
        playerMove(w, s, fwd, p);
        landed = s.onGround && s.origin.z < 1;
    }
    CHECK(landed);
    // Landing friction (2x falling to 1x over the next second) bleeds the speed the player carried.
    double lowest = before;
    for (int i = 0; i < 60; ++i) {
        playerMove(w, s, fwd, p);
        lowest = std::min(lowest, hspeed(s));
    }
    CHECK(lowest < before - 10);
}

TEST(jump_has_minimum_interval_and_no_bunny_hop_gain) {
    CollisionWorld w;
    w.build(worldOf(floorOnly()));
    MoveParams p;
    PlayerState s;
    placePlayer(w, s, {0, 0, 50}, p);
    MoveInput in;
    in.jump = true;
    double apex1 = 0;
    bool airborne = false;
    for (int i = 0; i < 180; ++i) {  // one jump: up, then back down onto the floor
        playerMove(w, s, in, p);
        in.jump = false;             // a single press: holding would not repeat it either way
        apex1 = std::max(apex1, s.origin.z);
        airborne = airborne || !s.onGround;
        if (airborne && s.onGround) break;
    }
    CHECK(std::fabs(apex1 - p.jumpHeight) < 2.0);
    // A press right after landing is ~0.6 s after the first jump start: allowed (past the 0.5 s debounce) and
    // inside the 1 s window, so it is the second jump of the sequence: 0.75 x the height.
    CHECK(s.onGround);
    in.jump = true;
    double apex2 = 0;
    for (int i = 0; i < 120; ++i) {
        playerMove(w, s, in, p);
        in.jump = false;
        apex2 = std::max(apex2, s.origin.z);
    }
    CHECK(apex2 < apex1 - 5);
    CHECK(std::fabs(apex2 - 0.75 * p.jumpHeight) < 2.0);
}

TEST(sprint_jump_leaps_higher_after_0_3_seconds) {
    CollisionWorld w;
    w.build(worldOf(floorOnly()));
    MoveParams p;
    PlayerState s;
    placePlayer(w, s, {-400, 0, 50}, p);
    s.yaw = 0;
    MoveInput sprint;
    sprint.forward = 1;
    sprint.sprint = true;
    run(w, s, sprint, p, 0.6);  // sprint for 0.6 s: the leap is at full strength
    CHECK(s.sprinting);
    MoveInput jump = sprint;
    jump.jump = true;
    playerMove(w, s, jump, p);
    CHECK(!s.sprinting && !s.onGround);  // the jump takes the sprint with it
    double apex = s.origin.z;
    jump.jump = false;
    for (int i = 0; i < 120; ++i) {
        playerMove(w, s, jump, p);
        apex = std::max(apex, s.origin.z);
    }
    CHECK(std::fabs(apex - p.sprintLeapHeight) < 2.0);  // 50, not the standing 39
}

TEST(view_height_lerps_between_stances) {
    CollisionWorld w;
    w.build(worldOf(floorOnly()));
    MoveParams p;
    PlayerState s;
    placePlayer(w, s, {0, 0, 50}, p);
    CHECK(eyeHeight(p, s) == p.standEye);
    MoveInput c;
    c.crouch = true;
    playerMove(w, s, c, p);
    const double early = eyeHeight(p, s);
    CHECK(early < p.standEye && early > p.crouchEye + 5);
    run(w, s, c, p, 0.1);
    const double mid = eyeHeight(p, s);
    CHECK(mid > p.crouchEye && mid < early);
    run(w, s, c, p, 0.2);
    CHECK(eyeHeight(p, s) == p.crouchEye);
    // Standing back up: the stand lerp is 200 ms, so 0.1 s in the eye is still between the two heights.
    MoveInput up;
    run(w, s, up, p, 0.1);
    CHECK(!s.crouched && eyeHeight(p, s) > p.crouchEye && eyeHeight(p, s) < p.standEye);
    run(w, s, up, p, 0.25);
    CHECK(eyeHeight(p, s) == p.standEye);
}

TEST(walking_down_a_step_keeps_ground_contact) {
    CollisionWorld w;
    w.build(testMap());
    MoveParams p;
    PlayerState s;
    CHECK(placePlayer(w, s, {300, 300, 50}, p));
    CHECK(std::fabs(s.origin.z - 16) < 0.5);  // on the 16-unit step
    s.yaw = 180;                              // walk -x, off the step edge at x = 128
    MoveInput in;
    in.forward = 1;
    int airborne = 0;
    for (int i = 0; i < 240 && s.origin.x > 60; ++i) {
        playerMove(w, s, in, p);
        if (!s.onGround) ++airborne;
    }
    CHECK(s.origin.x < 60);
    CHECK(airborne == 0);                     // the step-down snaps to the floor, no free fall
    CHECK(std::fabs(s.origin.z) < 0.5);
}

// Ladders (6.8): a brush with the ladder tool material is climbable; looking down climbs down; jump pushes off.
TEST(play_ladder_climb_descend_pushoff) {
    std::string t = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n";
    t += box(-512, -512, -16, 512, 512, 0);
    t += box(100, -24, 0, 108, 24, 256, "ladder");  // thin ladder slab facing -X
    t += "}\n";
    Id next = 1;
    const MapFile map = *parseMap(t, next).map;
    CollisionWorld w;
    w.build(map);
    CHECK(w.solids().size() == 2 && w.solids()[1].ladder);
    MoveParams p;
    PlayerState s;
    CHECK(placePlayer(w, s, {40, 0, 40}, p));
    MoveInput in;
    in.forward = 1;
    run(w, s, in, p, 0.5);  // walk into the face
    CHECK(s.onLadder);
    const double z0 = s.origin.z;
    run(w, s, in, p, 1.0);
    CHECK(s.onLadder && s.origin.z > z0 + 80);  // ~104 units/s up
    CHECK(s.origin.x < 100 - p.radius + 1);     // stays in front of the face
    // Looking down, forward climbs down.
    const double z1 = s.origin.z;
    s.pitch = -45;
    run(w, s, in, p, 0.5);
    CHECK(s.origin.z < z1 - 30);
    s.pitch = 0;
    // Jump: push off, away from the face, then fall.
    MoveInput jump;
    jump.jump = true;
    playerMove(w, s, jump, p);
    CHECK(!s.onLadder && s.velocity.x < -50);
    run(w, s, MoveInput{}, p, 2.0);
    CHECK(s.onGround && s.origin.x < 80);
    // A plain (non-ladder) wall is not climbable.
    std::string t2 = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n" + box(-512, -512, -16, 512, 512, 0) + box(100, -24, 0, 108, 24, 256) + "}\n";
    CollisionWorld w2;
    w2.build(*parseMap(t2, next).map);
    PlayerState s2;
    CHECK(placePlayer(w2, s2, {40, 0, 40}, p));
    run(w2, s2, in, p, 1.5);
    CHECK(!s2.onLadder && s2.origin.z < 1);
}

// Map-agent report 2026-11-14: a 192-unit ramp rising 128 took about 7 s in Play Preview (flat: about 1.5 s). A walkable
// ramp is climbed at the walk speed along the slope.
TEST(player_climbs_a_ramp_at_walk_speed) {
    // Floor, then a ramp from x 0 (z 0) to x 192 (z 128), then a top platform; the ramp as a wedge brush.
    std::string t = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n";
    t += box(-512, -256, -16, 0, 256, 0);
    t += box(192, -256, 0, 512, 256, 128);
    const std::string s = " floor 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n";
    // The box helper's face order and winding, its top face tilted from z 0 at x 0 to z 128 at x 192.
    t += "{\n ( 192 256 -16 ) ( 0 256 -16 ) ( 0 -256 -16 )" + s +       // bottom
         " ( 0 -256 0 ) ( 0 256 0 ) ( 192 256 128 )" + s +              // the slope
         " ( 0 -256 0 ) ( 192 -256 128 ) ( 192 -256 -16 )" + s +        // y = -256
         " ( 192 256 128 ) ( 0 256 0 ) ( 0 256 -16 )" + s +             // y = 256
         " ( 192 -256 128 ) ( 192 256 128 ) ( 192 256 -16 )" + s +      // x = 192
         " ( 0 256 0 ) ( 0 -256 0 ) ( 0 -256 -16 )" + s + "}\n}\n";     // x = 0
    Id next = 1;
    const auto map = parseMap(t, next).map;
    CHECK(map.has_value());
    if (!map) return;
    CollisionWorld w;
    w.build(*map);
    MoveParams p;
    PlayerState st;
    CHECK(placePlayer(w, st, {-64, 0, 40}, p));
    st.yaw = 0;
    MoveInput in;
    in.forward = 1;
    double t2 = 0;
    for (; t2 < 6.0 && st.origin.x < 230; t2 += in.dt) playerMove(w, st, in, p);
    // The ramp's length along the slope is 231 units; at about 186 u/s that is about 1.3 s, plus 64 + 38 flat units.
    std::printf("  ramp: x %.1f z %.1f after %.2f s (ground %d, n.z %.3f)\n", st.origin.x, st.origin.z, t2, int(st.onGround), st.groundNormal.z);
    CHECK(st.origin.x >= 230 && std::fabs(st.origin.z - 128) < 2.0 && t2 < 2.6);
}
