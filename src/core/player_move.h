// IW8 Radiant Suite - play preview player movement over a CollisionWorld, following the Replay 1.20 walk simulator
// (E:\mw124\mw120rproxy\iw8-zonetool\src\zonetool\sim, a reference and not a verified 1:1 port): ground/air wish
// speeds per direction, friction with landing and slide scales, jump with subsequent-jump reduction, sprint with a
// 2 s meter, slide, mantling, stance eye-height lerps, step-up on ground only, and noclip.
#pragma once

#include "collision_world.h"
#include "player_mantle.h"

namespace iw8r {

struct MoveParams {
    double gravity = 800;           // units / s^2 (g_gravity)
    double walkSpeed = 190;         // units / s at full command (walkSpeed, MakeUnverifiedReplay120Profile)
    // Replay 1.20 standardMP values (walk_profile.cpp MakeUnverifiedReplay120Profile, lines 3-75).
    double sprintScale = 1.4;       // suitSprintSpeedScale
    double crouchScale = 0.45;      // suitCrouchSpeedScale
    double proneScale = 0.2;        // suitProneSpeedScale
    double weaponMoveScale = 0.86;  // weaponMoveSpeedScale (M4)
    double jogForwardScale = 1.15;  // playerJogForwardSpeedScale (walking forward, not sprinting or crouched)
    double backScale = 0.88;        // playerBackSpeedScale
    double strafeScale = 0.8;       // playerStrafeSpeedScale
    double sprintStrafeScale = 0.5; // playerSprintStrafeSpeedScale (applied to the strafe command while sprinting)
    double sprintMinForward = 105.0 / 127.0;  // playerSprintForwardMinimum: sprint needs this much forward
    double sprintSeconds = 2.0;     // maxSprintTimeMsec 2000, recharged at the same rate it drains
    double sprintMinActivation = 1.0;  // playerSprintMinActivationTimeSeconds
    bool tacticalSprintEnabled = false;  // superSprint; off by default (basic preview disables it too)
    double tacticalSprintScale = 1.2;    // superSprintSpeedScale
    double jumpHeight = 39;         // suitJumpHeight (apex of a standing jump)
    double jumpMinInterval = 0.5;   // jumpMinIntervalMsec: debounce between jump starts
    double jumpSubsequentScale = 0.75;   // each jump within the window is this much lower ...
    double jumpSubsequentMin = 0.5;      // ... down to this fraction of jumpHeight
    double jumpSubsequentWindow = 1.0;   // seconds after a jump start before the count resets
    double jumpLandingSeconds = 1.0;     // jumpLandingSlowdownMsec: landing friction window
    bool sprintLeapEnabled = true;       // sprintLeapEnabled (profile): a sprint jump is higher and carries more speed
    double sprintLeapHeight = 50;        // suitSprintLeapHeight
    double sprintLeapSeconds = 0.3;      // sprintLeapMinimumSprintMsec
    double sprintLeapForwardScale = 0.4; // suitSprintLeapForwardVelocityScale
    double stepSize = 18;           // stepHeight: highest ledge walked up without jumping
    double accelerate = 9, crouchAccelerate = 12, proneAccelerate = 19, airAccelerate = 1;
    double friction = 5.5, stopSpeed = 100;
    double radius = 15;             // half width of the player capsule
    double standHeight = 70, crouchHeight = 50, proneHeight = 30;  // capsule radius 15, half heights 35/25/15
    double standEye = 60, crouchEye = 40, proneEye = 11;
    double standLerp = 0.2, crouchDownLerp = 0.2, crouchUpLerp = 0.4, proneLerp = 0.4;  // viewHeight*LerpMilliseconds
    double minWalkNormal = 0.7;     // cos(45.573 deg): maxSlopeDegrees
    double noclipSpeed = 600;
    // Slide (walk_slide.cpp / SuitDef values above).
    double slideSeconds = 0.4;          // slideMaxTimeSeconds: a standard slide
    double slideInSeconds = 0.2;        // slideInTimeSeconds: forward push and speed boost
    double slideInSpeedScale = 1.2;     // slideInMaxSpeedScale (x0.85 per consecutive slide)
    double slideInAcceleration = 4;
    double slideFrictionNormal = 0.15, slideFrictionDownhill = 0.001, slideFrictionUphill = 0.45;
    double slideOutSeconds = 0.3;       // slideOutTimeSeconds: friction ramps from slideOutFrictionStart to 1
    double slideOutFrictionStart = 0.15;
    double slideMinGroundSpeed = 140, slideMinContinueSpeed = 50;
    double slideStartAfterSprint = 0.2; // sprint must have lasted this long before a slide
    double slideLateJumpSeconds = 0.3;  // slideLateJumpGraceMsec
    double slideJumpSpeedScale = 0.5;   // slideJumpSpeedScale
    int slideMaxSubsequent = 15;
    bool mantleEnabled = true;
    // Ladders: brushes with the ladder tool material. APPROXIMATION: Replay's climb speed is not in the reference
    // (the basic sim profile disables ladders); 0.55 x walkSpeed is the IW-era feel. Rungs are 12 units in Replay.
    double ladderClimbScale = 0.55;
    double ladderPushOff = 100;     // jump on a ladder: push away from the face (units / s)
    double ladderLookDownPitch = -20;  // looking further down than this, forward climbs down
};

struct PlayerState {
    Vec3 origin;                  // feet (bottom centre of the box)
    Vec3 velocity;
    double yaw = 0, pitch = 0;    // degrees
    bool onGround = false;
    Vec3 groundNormal{0, 0, 1};
    bool crouched = false;
    bool prone = false;
    bool noclip = false;
    bool jumpHeld = false;        // jumps need a new press
    bool onLadder = false;        // climbing a ladder brush face (ladder tool material)
    Vec3 ladderNormal;            // outward normal of the ladder face while onLadder

    // Movement modes (for the camera and HUD).
    bool sprinting = false;
    bool sliding = false;         // crouched slide: crouched stance, forced-forward and friction scales apply
    double sprintMeter = 2.0;     // seconds of sprint left (2 s, recharging while not sprinting)
    double viewHeight = 60;       // eye height above origin, lerped between stances: use eyeHeight()

    // Mantle (valid while mantling; the camera follows origin, the traversal is kinematic).
    bool mantling = false;
    double mantleProgress = 0;    // 0..1 along the mantle curve
    Vec3 mantleStart, mantleEnd;  // origin when the mantle started, and the ledge-top landing
    double mantleLedgeHeight = 0; // ledge top above the feet
    bool mantleHigh = false;      // ledge above 58 units: the high branch of the mantle duration
    MantleControl mantle;

    // Timers and bookkeeping (internal; kept here so the state can be copied between frames).
    bool crouchHeld = false;
    bool sprintButtonUpRequired = false;
    bool jumpingState = false;    // jump started and landing friction not finished
    double landingMs = 0;         // landing friction timer (pm_time analogue), 1000 .. 0
    int subsequentJumps = 0;
    int slideSubsequent = 0;
    double sinceJump = 1e6, sinceSlideEnd = 1e6, sinceSprintStart = 1e6;
    double slideElapsed = 0;
    double viewFrom = 60, viewTarget = 60, viewLerpT = 0, viewLerpDur = 0;
};

struct MoveInput {
    double forward = 0, right = 0, up = 0;  // -1..1 (forward + / right +)
    bool jump = false, crouch = false, sprint = false, prone = false;
    bool tacticalSprint = false;  // only with MoveParams::tacticalSprintEnabled
    bool highMantle = false;      // airborne high-mantle button (reference highMantleButtonPressed)
    double dt = 1.0 / 60.0;
};

Vec3 playerMins(const MoveParams& p);
Vec3 playerMaxs(const MoveParams& p, bool crouched);
// Current eye height above origin (PlayerState::viewHeight, lerped between stances).
double eyeHeight(const MoveParams& p, const PlayerState& s);

// One movement step (dt clamped to 50 ms; longer frames are split by the caller).
void playerMove(const CollisionWorld& world, PlayerState& s, const MoveInput& in, const MoveParams& p);
// Places the player on the floor below `from` (or lifts it out of a solid); returns false when there is no floor.
// Also resets the movement modes and timers.
bool placePlayer(const CollisionWorld& world, PlayerState& s, const Vec3& from, const MoveParams& p);

}  // namespace iw8r
