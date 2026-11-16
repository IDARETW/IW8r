// IW8 Radiant Suite - play preview player movement over a CollisionWorld, matched to the Replay 1.20 pmove. Every
// constant and rule, the function of the game exe it was read from and whether it is verified there, is in
// docs/research/140_movement_parity.md. Covered: PM_WalkMove / PM_AirMove wish speeds (PM_CmdScale with the stance,
// sprint, tactical sprint, jog, back and strafe scales), PM_Friction (5.5 with a 100 control floor), PM_Accelerate
// (the 100 wish-speed floor), Jump_Check / Jump_Start (39 units, 0.75 per repeated jump down to 0.5, sprint leap 50),
// sprint (forward 105 of 127, no meter) and tactical sprint (a 2 s meter), slide, the stance view-height curves,
// step-up 18 and the 16-unit ground stick, ladders, mantling (player_mantle) and noclip. PlayControls is the game's client
// side: the default keyboard binds (default_keys.cfg) and the profile defaults (toggle crouch / prone, tap to slide,
// toggle sprint with a second press for tactical sprint, Space stands up before it jumps), and the mouse look.
#pragma once

#include "collision_world.h"
#include "player_mantle.h"

namespace iw8r {

struct MoveParams {
    double gravity = 800;           // units / s^2 (g_gravity, BG_RegisterCommonDvars)
    double walkSpeed = 190;         // units / s at full command (g_speed, G_Main_RegisterDvars)
    // Speed scales (PM_CmdScale 0x14111EF60, PM_CmdScaleForStance 0x14111ED90; suit values: the void suit asset).
    double sprintScale = 1.4;       // player_sprintSpeedScale / suit sprint scale
    double crouchScale = 0.45;      // suit crouch scale (x player_crouchSpeedScale 1.0)
    double proneScale = 0.2;        // suit prone scale (x player_proneSpeedScale 1.0)
    double weaponMoveScale = 0.86;  // weapon moveSpeedScale (M4), off while aiming down sights
    double jogForwardScale = 1.15;  // forward command while jogging (not sprinting, crouched, prone or sliding)
    double backScale = 0.88;        // backward command
    double strafeScale = 0.8;       // side command (the suit's slide strafe scale 0.8 while sliding)
    double sprintStrafeScale = 0.5; // the side command is halved while sprinting (PM_WalkMove)
    double sprintMinForward = 105.0 / 127.0;  // PM_CmdHasMinimumSprintMovement: forwardmove >= 105
    bool holdToSprint = false;      // profile holdSprint 0: one press starts a sprint that lasts until it ends itself
    // Tactical sprint (super sprint, on in MP): a second sprint press while sprinting. Normal sprint has no meter then.
    bool tacticalSprintEnabled = true;
    double tacticalSprintScale = 1.2;        // PM_CmdScale
    double tacticalSprintSeconds = 2.0;      // bg_sprintTimeBase 2.0 x the weapon's sprint duration scale (1)
    double tacticalSprintMinCharge = 0.4;    // superSprintMinActivationChargePercent
    double tacticalSprintRecharge = 1.0;     // superSprintRechargeScale: refills 1 s per s while not sprinting
    // Without tactical sprint (tacticalSprintEnabled false) the plain sprint uses the legacy meter.
    double sprintSeconds = 2.0;
    double sprintMinActivation = 1.0;        // PM_DoesSprintMeterTestPass: 1 s of meter to start
    double jumpHeight = 39;         // suit jump height (jump_height 39)
    double jumpMinInterval = 0.5;   // Jump_Check: 500 ms between jump starts
    double jumpSubsequentScale = 0.75;   // Jump_GetJumpHeight: 0.75 ^ jumps ...
    double jumpSubsequentMin = 0.5;      // ... floored at 0.5, counted up to 7
    double jumpSubsequentWindow = 1.0;   // the count resets 1 s after a jump start
    double jumpLandingSeconds = 1.0;     // landing friction window (pm_time 1000)
    bool sprintLeapEnabled = true;       // Leap_ShouldSprintLeap (MP on)
    double sprintLeapHeight = 50;        // sprintLeap_height
    double sprintLeapSeconds = 0.3;      // sprintLeap_minSprintTimeMs 300
    double sprintLeapForwardScale = 0.4; // sprintLeap_forwardVelocityScale
    double stepSize = 18;           // the climb the step stand-in allows the feet (stepSlideMove; type 1: 17.3 .. 23)
    double stepDownSnap = 16;       // ground trace stick extension (playerCollisionStickExtension) while grounded
    // Player collision type 1 (research 140 section 8): the movement capsule's bottom is raised by the stick height,
    // stickHeight / 40 x 2 (half height - radius): 8 standing, 4 crouched, 0 prone (MP constant 8; SP dvar
    // bg_standStickHeightBase); the ground is found by a flat-bottomed 8-sided prism swept from stick + radius above
    // the feet; the feet stand groundSkin above what it hits.
    double stickHeight = 8;
    double groundSkin = 0.125;
    double groundJumpWindow = 0.5;  // the ground trace treats the first 500 ms after a jump start as jumping
    double accelerate = 9, crouchAccelerate = 12, proneAccelerate = 19, airAccelerate = 1;  // PM_WalkMove literals
    double accelMinSpeed = 100;     // PM_Accelerate: accel * dt * max(100, wishspeed)
    double friction = 5.5, stopSpeed = 100;  // PM_Friction: friction * max(100, speed) * dt
    double radius = 15;             // half width of the player capsule
    double standHeight = 70, crouchHeight = 50, proneHeight = 30;  // capsule radius 15, half heights 35/25/15
    double standEye = 60, crouchEye = 40, proneEye = 11;           // suit view heights
    // Stance view-height lerp times (PM_GetStanceViewLerpTime 0x141122500).
    double standLerp = 0.2, crouchDownLerp = 0.2, crouchUpLerp = 0.4, proneLerp = 0.4, mantleCrouchLerp = 0.1;
    double minWalkNormal = 0.7;     // PM_GroundTrace: cos(45.573 deg)
    double steepSupportSeconds = 0.3;  // ground trace ledge walk: level support on a too-steep plane for this long
    double pitchLimit = 85;         // PM_ClampViewPitch: +-85 degrees
    double noclipSpeed = 570;       // PM_CmdScale: 3 x g_speed for pm_type noclip
    // Slide (Slide_CanSlide / Slide_EndCheck; times and scales from the suit).
    double slideSeconds = 0.4;          // a standard slide
    double slideInSeconds = 0.2;        // slide-in: forced forward and the speed boost
    double slideInSpeedScale = 1.2;     // x 0.85 per consecutive slide
    double slideInAcceleration = 4;
    double slideFrictionNormal = 0.15, slideFrictionDownhill = 0.001, slideFrictionUphill = 0.45;
    double slideOutSeconds = 0.3;       // friction ramps from slideOutFrictionStart to 1
    double slideOutFrictionStart = 0.15;
    double slideMinGroundSpeed = 140, slideMinContinueSpeed = 50;  // slide_min_required_velocity / _continue_
    double slideStartAfterSprint = 0.2; // slide_min_sprint_time_ms 200
    double slideMinNormal = 0.8;        // slide_angle 0.6: up^2 >= 1 - 0.36
    double slideLateJumpSeconds = 0.3;  // slide_lateJumpGraceMs
    double slideJumpSpeedScale = 0.5;   // suit slide_jump_speed_scale (a jump out of a slide)
    double slideSprintDelay = 0.45;     // no sprint for this long after a slide
    int slideMaxSubsequent = 15;
    bool mantleEnabled = true;
    double mantleMaxHeight = 58;        // Mantle_GetMaxClimbHeight (MP; 72 in BR)
    double mantleAutoMinHeight = 36;    // Mantle_CloseToGround: the airborne auto mantle needs this much air below
    double ladderReach = 30;            // BG_GetLadderInfo ladderLineQueryDistance: a ladder face this far is grabbed
    double ladderMagnetism = 100;       // ladderMagnetismEnhancedSpeed: the pull onto the face
    // Ladders (PM_LadderMove): brushes with the ladder tool material.
    double ladderClimbScale = 0.4;      // player_ladderEnhancedVerticalSpeedScale: 76 units / s at full forward
    double ladderAccelerate = 9;
    double ladderSlideSpeed = 100, ladderSlideAccel = 300;  // crouch held: slide down
    double ladderJumpBack = 200;        // ladderJumpBackVelocity
    double ladderJumpUpDesire = 0.3;    // ladderJumpDefaultUpDesire
};

struct PlayerState {
    Vec3 origin;                  // feet (bottom centre of the box)
    Vec3 velocity;
    double yaw = 0, pitch = 0;    // degrees; pitch up is positive (the editor camera's convention)
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
    bool tacticalSprinting = false;
    bool sliding = false;         // crouched slide: crouched stance, forced-forward and friction scales apply
    double sprintMeter = 2.0;     // seconds left: the tactical sprint meter (or the legacy sprint meter)
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
    bool slideHeld = false;
    bool sprintButtonUpRequired = false;
    bool sprintRestore = false;   // PM_SetSprintRestore: the sprint comes back by itself (after a jump, a crouch)
    bool jumpingState = false;    // jump started and landing friction not finished
    bool groundPlane = false;     // the last ground trace hit a plane, walkable or not (PM_AirMove slides along it)
    bool steepContact = false;    // the ground trace stood the player on a too-steep plane (ledge walk support)
    double steepTime = 0;         // how long that support has lasted
    double landingMs = 0;         // landing friction timer (pm_time analogue), 1000 .. 0
    int subsequentJumps = 0;
    int slideSubsequent = 0;
    double sinceJump = 1e6, sinceSlideEnd = 1e6, sinceSprintStart = 1e6;
    double slideElapsed = 0;
    // View height: one stance step at a time along the game's curves (PM_ViewHeightAdjust).
    double viewFrom = 60, viewTarget = 60, viewLerpT = 0, viewLerpDur = 0;
    int viewCurve = -1;           // ViewCurve index while lerping, -1 when settled
    bool viewDirect = false;      // a straight stand-to-prone drop (the "direct" curves)
};

struct MoveInput {
    double forward = 0, right = 0, up = 0;  // -1..1 (forward + / right +); up: noclip only
    bool jump = false;            // jump / stand button (+gostand) held
    bool crouch = false;          // crouch stance requested (the client's crouch bit)
    bool prone = false;           // prone stance requested
    bool sprint = false;          // sprint button held (+breath_sprint)
    bool tacticalSprint = false;  // tactical sprint request (the client sends it after a second sprint press)
    bool slide = false;           // slide request (tap to slide: crouch pressed while sprinting)
    bool highMantle = false;      // airborne mantle (the jump button in the air)
    double dt = 1.0 / 60.0;
};

Vec3 playerMins(const MoveParams& p);
Vec3 playerMaxs(const MoveParams& p, bool crouched);
// Current eye height above origin (PlayerState::viewHeight, lerped between stances).
double eyeHeight(const MoveParams& p, const PlayerState& s);
// The game's stance view-height curves (PM_ViewHeightTableLerp tables at 0x144599820..): fraction of the way from
// the old eye height to the new one at time fraction t (0..1) of the lerp. Some overshoot (the prone curves).
enum class ViewCurve { StandCrouch, StandCrouchDirect, CrouchProne, CrouchProneDirect, ProneCrouch, CrouchStand };
double viewCurveValue(ViewCurve c, double t);

// One movement step (dt clamped to 50 ms; longer frames are split by the caller).
void playerMove(const CollisionWorld& world, PlayerState& s, const MoveInput& in, const MoveParams& p);
// Places the player on the floor below `from` (or lifts it out of a solid); returns false when there is no floor.
// Also resets the movement modes and timers.
bool placePlayer(const CollisionWorld& world, PlayerState& s, const Vec3& from, const MoveParams& p);

// The game's client input for the play preview: the default keyboard binds and the profile defaults of Replay 1.20.
// Keys: W S A D move, Space +gostand (jump; stands up from crouch, crouches from prone), C toggle crouch (while
// sprinting: slide; during a slide: cancel into crouch), Ctrl toggle prone, Shift +breath_sprint (press to sprint, a
// second press while sprinting is tactical sprint; from crouch it stands up), F use, H autoforward.
enum class PlayKey { Forward, Back, Left, Right, Jump, Crouch, Prone, Sprint, Use, AutoForward, Count };
enum class PlayStance { Stand, Crouch, Prone };
struct PlayControls {
    bool held[int(PlayKey::Count)] = {};
    PlayStance stance = PlayStance::Stand;  // the client's stance (CL_STANCE_*), sent as the crouch / prone bits
    bool autoForward = false;
    bool slideRequest = false, slideCancel = false, tacticalTrigger = false, jumpLatched = false;
    bool wasSliding = false;

    // A key went down (call from the key event so a tap shorter than a frame still counts) or up.
    void press(PlayKey k);
    void release(PlayKey k);
    // Per movement step: the held keys (from the view's key set), the player state before the step; returns the
    // command for playerMove. Presses since the last call are applied first.
    MoveInput input(const bool heldNow[int(PlayKey::Count)], const PlayerState& s, double dt);
    void reset();

  private:
    bool pending_[int(PlayKey::Count)] = {};
    bool prevHeld_[int(PlayKey::Count)] = {};
    void onPress(PlayKey k, const PlayerState& s);
};

// Mouse look: degrees of yaw / pitch per mouse count (CL_InputMP_ApplyLookInputToCmd): m_yaw / m_pitch 0.022 x the
// profile mouse sensitivity (default 12, vertical multiplier 1) x 0.3 (CL_Input_GetAimAdjustVector) = 0.0792.
constexpr double kMouseYaw = 0.022, kMousePitch = 0.022, kMouseSensitivityScale = 0.3, kDefaultMouseSensitivity = 12;
inline double mouseDegreesPerCount(double sensitivity = kDefaultMouseSensitivity) {
    return kMouseYaw * sensitivity * kMouseSensitivityScale;
}

}  // namespace iw8r
