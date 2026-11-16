# 140 Play preview movement parity with the Replay 1.20 pmove

Owner, 2026-11-15: "as part of map script emulation, i also want exact movement controls that the actual game uses, so
its like a 1-to-1 game match in terms of movement mechanics". This note lists every movement constant and rule the play
preview (`src/core/player_move.*`, the play input in `src/qtapp/camera_view.cpp`) uses, the game's value, where it was
read and whether it is **verified** in the Replay exe or **inferred**.

## 1. Sources and method

- **The Replay exe** (`E:\IW8\Builds\1.20-replay\game_dx12_ship_replay.exe`, sha256 `68fb1cbc...8132f0`), read through
  the owner's IDA database with the IDA bridge (decompile, xrefs, short scripts). Function names in that database are
  partly guesses: below each function is named by its role in the game and its address, with the database's name in
  quotes where it differs.
- **Dvar defaults**: a short script walked `BG_RegisterCommonDvars` (`0x141111B40`), `G_Main_RegisterDvars`
  (`0x140FC0760`) and `CL_Main_RegisterCommonDvars` and printed each `Dvar_Register*` call with the float / int loaded
  into its default register and the description string. Dvar names are hashed in Replay (`NSRPQNLSNK` = `g_speed`); the
  plain names come from the `dvarHashToText_s` pair table (`0x140C20C30`).
- **MP uses compiled values**: almost every pmove read is `if (Com_GameMode_GetActiveGameMode() == 1 /*SP*/) dvar else
  constant`. The MP constants are bytes / floats in `.data` (`DCONST_DVARMPBOOL_*`, `s_paramRecord*`); their values
  were read directly (section 7). Where the MP constant and the dvar default agree the value is marked verified.
- **Game-test source** (`E:\mw124\tools\dvar_search_web\src\combined\game-test-src-cleaned`, a decompile of a later IW8
  build, not Replay) for the meaning of the client input code and the slide; rules taken from it were confirmed in
  Replay where a constant or a call could be matched (research note 119 method).
- **The reference walk simulator** (`E:\mw124\mw120rproxy\iw8-zonetool\src\zonetool\sim`, `walk_profile.cpp`): its
  suit values come from the hash-pinned `void` suit and the M4 weapon asset. ACTS has no suit exporter, so the MP suit
  `iw8_defaultsuit_mp` (set by the MP scripts, `setsuit( "iw8_defaultsuit_mp" )`) was not read: suit values are
  inferred to match `void`.
- **Default keyboard binds**: `default_keys.cfg` from the IW8 8.19 (April 2020) rawfile dump
  (`...\combined\rawfile_source`), the same era as 1.20; not re-exported from the Replay zones (inferred).

## 2. Values

| Quantity | Game value | Where found | Status | Preview |
|---|---|---|---|---|
| Walk speed (`g_speed`) | 190 | `G_Main_RegisterDvars` `0x140FC0DB8` (int 0xBE); `ps->speed` read in PM_CmdScale | verified | `walkSpeed` 190 |
| Gravity (`g_gravity`) | 800 | `BG_RegisterCommonDvars` `0x141113371` | verified | 800 |
| Ground friction | 5.5 | `friction` dvar `0x141113D8E`; MP literal 5.5 in PM_Friction `0x141121160` ("PM_ComputeGroundMoveScales") | verified | 5.5 |
| Friction control floor (stop speed) | `max(100, speed)` | PM_Friction literal 100 | verified | `stopSpeed` 100 |
| Ground acceleration stand / crouch / prone | 9 / 12 / 19 | PM_WalkMove `0x14112A130` ("PM_UpdateJumpAndVaultMoveState") literals, x suit accel scale (1) x `player_globalAccelScale` 1.0 | verified (suit scale inferred 1) | 9 / 12 / 19 |
| Air acceleration | 1.0 | PM_AirMove `0x14111CBA0` ("PM_UpdateLandingMoveState"): accel argument 1.0 | verified | 1 |
| Acceleration wish-speed floor | `accel * dt * max(100, wishspeed)` | PM_Accelerate `0x14111C7D0` ("PM_AccelerateWishVelocityAlongFrame") | verified | `accelMinSpeed` 100 (new) |
| Alternate acceleration | off | `player_altAcceleration` default false, MP const `k_PM_accelWishAlongFrameFlag` 0 | verified | not used |
| Diagonal command | `speed * max(|f|,|r|) / len(f,r) / 127` | PM_CmdScale walk `0x14111EF60` ("PM_ComputeSlideAwareMoveScale"), air `0x14111ECD0` ("PM_GetMoveInputScale") | verified | same |
| Jog forward scale | 1.15 | PM_CmdScale literal `0x3F933333` when `enableJog` (MP const 1) and not sprinting / ADS / crouched / prone / mounted, weapon idle (`0x14111AB90`) | verified | 1.15 |
| Back scale | 0.88 | PM_CmdScale literal `0x3F6147AE` | verified | 0.88 |
| Strafe scale | 0.8 (suit `slideStrafe` while sliding) | PM_CmdScale literal 0.8 / suit+240 | verified | 0.8 |
| Strafe while sprinting | side command x 0.5 before the scale | PM_WalkMove: `rightmove = clamp(rightmove * 0.5)` when sprinting and suit+436 off | verified | `sprintStrafeScale`; the max/len factor now uses the halved command |
| Crouch / prone speed scale | 0.45 / 0.2 (suit) x 1.0 (dvar) | PM_CmdScaleForStance `0x14111ED90` ("PM_GetStanceBoundsOffsets"): suit+76 / +80 x `player_crouchSpeedScale` / `player_proneSpeedScale` 1.0 | dvar verified, suit inferred | 0.45 / 0.2 |
| Crouch <-> prone speed blend | linear in the view-lerp time fraction | PM_CmdScaleForStance + PM_GetViewHeightLerpFraction `0x141122410` | verified | `stanceSpeedScale()` (new) |
| Sprint speed scale | 1.4 | `player_sprintSpeedScale` `0x141112439`; MP flag `k_oneByte_2437AA1`=1 uses the suit's (1.4) | verified (suit inferred) | 1.4 |
| Tactical sprint scale | 1.2 | PM_CmdScale literal 1.2 while `superSprintEnable` and the super sprint is running | verified | 1.2 |
| Weapon move scale | 0.86 (M4 `moveSpeedScale`), off in ADS | PM_CmdScale `BG_GetWeaponSlideMoveScaleRange`; value from the M4 asset | rule verified, value from the asset (reference) | 0.86 |
| Sprint needs forward | `forwardmove >= 105` | PM_CmdHasMinimumSprintMovement `0x14111EC60` | verified | 105/127 |
| Tactical sprint (MP) | on | `superSprintEnable` default `mode != SP`; MP const `s_paramRecordC_2437B76` = 1 | verified | `tacticalSprintEnabled` true (new) |
| Plain sprint meter | none (unlimited) when tactical sprint is on | PM_GetSprintLeft `0x141122140`: drains only for a running super sprint, the super sprint button, or `!player_sprintUnlimited && !suit+92 && !perk`; `player_sprintUnlimited` MP 0 | rule verified; the MP suit's unlimited flag inferred (MW2019 sprints forever) | plain sprint has no meter |
| Tactical sprint meter | 2.0 s x weapon `sprintDurationScale` (1) | BG_GetMaxSprintTime `0x14110E930`: weapon+1064 x `bg_sprintTimeBase` 2.0 ("When Super Sprint is enabled, instead used for Super Sprint") | verified (weapon scale 1 inferred) | `tacticalSprintSeconds` 2.0 |
| Tactical sprint start charge | 40 % (0.8 s) | PM_DoesSprintMeterTestPass `0x14111F730` x `superSprintMinActivationChargePercent` 0.4 | verified | 0.4 |
| Tactical sprint recharge | 1 s per s, only while not sprinting | PM_ComputeSprintMeterDelta `0x141122270`, `superSprintRechargeScale` 1.0 | verified | 1.0 |
| Plain sprint start charge (legacy) | 1.0 s | PM_DoesSprintMeterTestPass (v3 = 1.0 without the super sprint button) | verified | `sprintMinActivation` (only with tactical sprint off) |
| Sprint ends | forward < 105, crouch / prone / jump / fire / melee / ADS bits, in the air with the jump state, meter out | PM_ShouldSprintEnd `0x141125A20`, PM_SprintEndingButtons `0x141125C40` (mask 0x3C5) | verified | same (fire / melee / ADS not modeled) |
| Sprint restore | an ended sprint comes back while forward is held | PM_ShouldSprintEnd -> PM_SetSprintRestore; cleared in PM_UpdateSprint `0x141128160` when the forward command drops | verified | `sprintRestore` (new) |
| Toggle vs hold sprint | toggle (profile `holdSprint` 0) | profile table `0x14461C470`; hold mode is the cmd bit `0x400000000000` checked in PM_ShouldSprintEnd | verified | `holdToSprint` false (new) |
| Sprint after a slide | 0.45 s delay | Slide_IsInSprintDelay; suit value | inferred (reference) | `slideSprintDelay` (new) |
| Jump height | 39 (suit+104; `jump_height` 39) | Jump_GetJumpHeight `0x141101930` ("Jump_GetDecayedJumpHeight"), dvar `0x141115538` | verified | 39 |
| Repeated jumps | `0.75^n`, floor 0.5, n up to 7, reset 1 s after a jump start | Jump_GetJumpHeight, Jump_Start `0x141101DB0` ("Mantle_ComputeAlmostGroundPose") | verified | same |
| Jump interval | 500 ms | Jump_Check `0x141101570` ("Jump_TryStartVault") literal 500 | verified | 0.5 s |
| Jump velocity | `sqrt(2 g h)` | `0x141101A10` ("Jump_ComputeTimedFloorScale") | verified | same |
| Jump needs a fresh press, standing or sliding | yes | Jump_Check: old cmd 0x100 held -> no jump; stance must be stand unless sliding | verified | same |
| Landing friction | x2 for 100 ms, then `1 + t/900` over the 1 s window | `0x141101D10` ("Jump_GetTimedSpeedScale"), `jump_slowdownEnable` true | verified | same |
| Sprint leap | height 50, forward x(1 + 0.4 f), f = sprint time / 300 ms | Leap_ShouldSprintLeap `0x140CC8C30`, `sprintLeap_*` dvars 50 / 0.4 / 300 | verified | same |
| Slide start | slide request, sprinting >= 200 ms, speed >= 140, ground normal z >= 0.8, < 15 in a row, not aiming | Slide_CanSlide `0x14112CE90` ("Slide_TryStartFromVelocity"): literals 200, 140, 15, `0.6^2` | verified | same; the 0.8 slope is new |
| Slide from the air | landing with >= 150 horizontal | Slide_CanSlide (flag 0x20 branch) | verified | not modeled |
| Slide time | suit (0.4 s standard); x1.4 with a perk | Slide_GetSlideTime `0x14112DCC0` (suit+260..268) | rule verified, value suit (reference) | 0.4 |
| Slide ends | time, a new crouch press, prone, jump, ADS, < 50 units / s, off the ground | Slide_EndCheck (game-test), `slide_min_continue_velocity` | inferred from game-test | same (ADS / firing not modeled) |
| Jump out of a slide | velocity x suit `slide_jump_speed_scale` (0.5) within 300 ms | Jump_Check late-slide branch (`BG_ApplySuitScaleToVec3`), `slide_lateJumpGraceMs` | verified rule, scale suit (reference) | same |
| Slide friction / boost | 0.15 / 0.001 downhill / 0.45 uphill; slide-in 0.2 s at 1.2 x 0.85^n | suit (reference `walk_slide.cpp`); post-slide decay 0.85 literal `0x14112E7C0` | partly verified | unchanged |
| Step-up | collision type 1 has **no step move**: PM_ResolveGroundedMovement `0x141130790` returns after PM_SlideMove (and a prone-only capsule move) when PhysicsQuery_AlwaysAcceptsHit; the capsule's round bottom rides up to an edge until the ground prism reaches the top, about 17.3 face-on to a side of the prism, up to 23 toward its corners. (PM_StepSlideMove `0x14112F370`, up trace 19, `19 f - 1`, step 18 / 10, is the type 0 path.) | PM_ResolveGroundedMovement `0x141130790`, PM_GroundCapsuleSweepMove `0x141131D40` | verified | the same: no step move; the movement trace rounds the capsule's bottom against upright faces' top edges so the prism reaches the top (section 8.4) |
| Step landing | type 0 only (PM_StepSlideMove `0x14112F370`: down by the step, 9 more with a ground plane; normal z >= 0.3, gained <= 36 x normal z; progress check) | PM_StepSlideMove `0x14112F370` | verified (type 0) | not used (type 1 has no step move) |
| Steep faces on the ground | a face steeper than 0.7 against the ground plane is clipped as a wall perpendicular to the ground (no riding up it) | PM_ClipSlideVelocity `0x14111E9B0` | verified | `slideMove` |
| Player collision | collision type 1: moves use the stance capsule with its bottom raised by the stick height (8 / 4 / 0); the ground is found by a flat-bottomed 8-sided prism | `s_paramRecordD_2437BA8` = 1, BG_Collision_ApplyProbeOffset `0x140CD7000`, Physics_CharacterCastPacked `0x14108D940` | verified | section 8 (new) |
| Ground trace | two passes a frame: before the move the prism, swept from stick + radius above the feet, puts the feet 0.125 above a walkable hit (or any hit when falling); after the move a ray from the top of the capsule only classifies | PM_GroundTrace_ProbeAndResolve `0x140CD90C0`, PM_GroundTrace_StanceChangeSolver `0x140CDA1B0` | verified | section 8 (new) |
| Ledge walk | grounded, not within 500 ms of a jump: a too-steep ground plane gives level support for 0.3 s from the first such pass | PM_GroundTrace_ProbeAndResolve `0x140CD90C0` (`k_PM_groundTraceProbeFlagB` / `C` = 1, time at playerState + 200) | verified | `steepSupportSeconds` 0.3 |
| Ground stick (walking down) | the ground reach is 16 below the feet while grounded, not within 500 ms of a jump; 0.25 + this frame's fall (at most 15) in the air | PM_GroundTrace_StanceChangeSolver `0x140CDA1B0` | verified | `stepDownSnap` 16 |
| Steep plane in the air | the velocity is clipped against a ground plane the player cannot stand on | PM_AirMove `0x14111CBA0` ("PM_UpdateLandingMoveState") | verified | new |
| Walkable slope | normal z >= 0.7 | PM_GroundTrace `0x140CD8650` (`k_BG_threshold07`) | verified | 0.7 |
| Capsule | radius 15, height 70 / 50 / 30 | suit; dvar "70 = standing, 50 = crouch, 30 = prone" | verified (dvar text), suit inferred | same |
| Eye heights | 60 / 40 / 11 | suit+512 / +516, BG_Suit_GetProneViewHeight; dvar "60 = standing, 40 = crouch, 11 = prone" | verified (dvar text) | same |
| View lerp times | to stand 200, stand->crouch 200 (100 mantling), prone->crouch 400, to prone 400 ms | PM_GetStanceViewLerpTime `0x141122500` | verified | same |
| View lerp curves | tables (below) | PM_ViewHeightAdjust `0x1411296C0`, PM_ViewHeightTableLerp `0x14112A000`, tables `0x144599820..0x1445999D0` | verified | `viewCurveValue()` (was smoothstep) |
| Pitch limit | +-85 | PM_ClampViewPitch `0x1411292C0` literal 85 | verified | `pitchLimit` 85 (was 89) |
| Mantle max height (MP) | 58 above the feet (ground and air); 49 / 20 for flagged edges; BR 72 | Mantle_GetMaxClimbHeight `0x141106C40` | verified (units inferred: ledge above the feet) | `mantleMaxHeight` 58 (new cap) |
| Ladder climb | `0.4 x speed/127 x forwardmove` (76 at full), accel 9, crouch slides 100 (accel 300), jump back 200 with up desire 0.3, no strafe, gravity damping without input | PM_LadderMove `0x140CC6BF0` ("Mantle_UpdateMotionCore") constants 0.4 / 100 / 300; dvars `0x141114xxx`; reference `walk_ladder.cpp` | constants verified, equations from the reference | new ladder model (was 0.55 x 190, pitch rule) |
| Ladder view pitch | no effect | `useLegacyPCLadderControls` MP const `0x142437B29` = 0 (the legacy rule `clamp(2.5 forward.z + 0.625)` is off) | verified | pitch rule removed |
| Mantle activation | jump button **held** and pushing into the ledge (ground or air; `mantle_requires_stick` MP 1); or in the air in the jump state, not aiming, not close to the ground, pushing (the auto trigger); never with the crouch / prone bits; the "jump would clear it" check is off (`k_mantleActivationConst` 0) | Mantle_CheckActivationConditions `0x141108A90`; Mantle_CloseToGround (game-test): a 36-unit trace down | verified (36 from game-test) | was a fresh press only: now held or airborne auto (`mantleAutoMinHeight` 36) |
| Ladder grab distance | 30 units from the face, then pulled onto it | reference `ladderLineQueryDistance` 30, `ladderMagnetismEnhancedSpeed` 100 over `ladderMagnetismScaleDistance` 10 | inferred (reference values) | `ladderReach` 30, `ladderMagnetism` 100; the probe looks at the ladder volumes themselves (a ladder model's collision in front no longer hides them) |
| Noclip speed | 3 x `g_speed` = 570 | PM_CmdScale (pm_type 2 -> x3, UFO x6) | verified | 570 (was 600) |

The view curves (time fraction, height fraction; the eye goes `from + (to - from) * v`, time in whole percent):

| Curve | Points |
|---|---|
| stand -> crouch `0x144599820` | (0,0) (.01,.025) (.04,.075) (.3,.2) (.8,.8) (.9,.925) (.95,.975) (1,1) |
| stand -> crouch, straight to prone `0x144599918` | (0,0) (.01,.025) (.04,.075) (.3,.2) (.8,.8) (1,1) |
| crouch -> prone `0x1445998C0` | (0,0) (.11,.069) (.22,.241) (.34,.517) (.45,.828) (.5,.862) (.55,.828) (.7,.759) (.9,.793) (1,1) |
| crouch -> prone, straight from stand `0x144599950` | (0,0) (.45,.828) (.5,.862) (.55,.828) (.7,.759) (.9,.793) (1,1) |
| prone -> crouch `0x144599990` | (0,0) (.05,-.034) (.3,.345) (.5,.483) (.67,.69) (.83,.793) (1,1) |
| crouch -> stand `0x144599870` | (0,0) (.05,.025) (.1,.075) (.2,.2) (.7,.8) (.96,.925) (.99,.975) (1,1) |

The table choice is the `cmovz` at `0x141129CFF` / `0x141129D2F` (flag `ps+0x18` bit 22 = the direct stand-to-prone
drop). The eye moves one stance at a time (stand -> crouch -> prone); a change of mind mid-way turns the segment round at
the mirrored progress (game-test `PM_ViewHeightAdjust`). The prone curves overshoot 11 and come back (0.862 of 29).

## 3. Rules in order (PM_WalkMove / PM_AirMove)

1. Sprint strafe halving (`rightmove * 0.5`), Jump_Check (jumped -> air move), PM_Friction, PM_CmdScale.
2. Wish velocity = `fwd * f * forwardScale + right * r * strafeScale`, flattened view axes, wish speed its length,
   direction projected onto the ground plane.
3. Acceleration by stance (9 / 12 / 19, 1 in the air) with the 100 wish-speed floor, x0.25 under pm flag 0x1000.
4. Velocity projected onto the ground plane keeping its length, then the slide / step move.
The noclip move is PM_NoclipMove `0x14112BAE0` ("PM_UpdateGroundedMoveState"), dispatched from `0x141124430`.

## 4. Controls (client side)

Default keyboard binds (`default_keys.cfg`): W / S / A / D `+forward` / `+back` / `+moveleft` / `+moveright`, SPACE
`+gostand`, C `+movedown` (crouch), LCTRL `+prone`, LSHIFT `+breath_sprint`, F `+activate`, H `autoforward`, Z and
MOUSE5 `+mount` (weapon mount, not modeled). There is no lean bind (the profile's `leanEnabled` 1 is the mount peek).

Profile defaults (Replay `PROFILE_DATA_FIELDS` `0x14461C470`, default float at +24 of each 48-byte row): `crouchType` 1
and `proneType` 1 (toggle), `holdSprint` 0, `tapToSlideEnabledKeyboard` 1, `stanceUpJump` 1, `autoSprintKBM` 0,
`autoMantleAirborne` 0, `viewMouseHorzSensitivity` 12, `viewMouseVertSensitivity` 1 (a multiplier of the horizontal),
`mouseAccel` 0, `invertPitchKBM` 0. `SetProfileDefaults` `0x1415DA4E0` writes 12.0 / 1.0 the same way.

Behaviour (game-test `cl_input.cpp`, the profile values verified in Replay):
- C toggles crouch (`CL_Input_ToggleStance`: the same stance again -> stand). While sprinting it is the slide (tap to
  slide, the client stance SLIDE sends bit `0x400000`); if the slide cannot start the game forces a crouch
  (`EV_STANCE_FORCE_CROUCH`). A slide ends crouched; C during the slide ends it in a crouch.
- LCTRL toggles prone.
- SPACE (`+gostand`, `stanceUpJump` 1): standing -> jump (and mantle); crouched -> stand; prone -> crouch.
- LSHIFT starts the sprint (toggle: it lasts after the key goes up). Pressed again while sprinting (after the sprint
  started) it triggers the tactical sprint, sent until the tactical sprint runs (or the sprint ends).
- H toggles autoforward; W or S ends it.

Mouse look (`CL_InputMP_ApplyLookInputToCmd` `0x1415E3300`, `CL_Input_GetAimAdjustVector` `0x1415B8940`
("sensitivity scale"), `CL_Input_ComputeAimAdjustment` `0x1415B4BD0`): yaw -= `m_yaw x (sens + accel x rate) x 0.3 x dx`,
pitch += `m_pitch x (sens x vertMult) x 0.3 x dy`; `m_yaw` = `m_pitch` = 0.022 (`0x3CB43958`, "Default yaw" / "Default
pitch"); the legacy path without 0.3 (`s_clInputTuningFlagWordA`) is off. With the default sensitivity 12: 0.0792
degrees per count. The preview uses cursor pixels x the device pixel ratio as counts (1:1 at the default Windows pointer
speed without pointer precision).

## 5. Not verified, not modeled

- The MP suit `iw8_defaultsuit_mp` itself (all suit values are the `void` suit's, via the reference) and its sprint
  unlimited flag (the plain sprint is unlimited in the preview).
- The weapon's sprint duration scale for the tactical meter (1 assumed) and its move scale (the M4's 0.86 for every
  weapon); ADS, firing, melee, reload and their sprint / slide interactions.
- Slide from the air, slide clearance traces, the slide view height and gestures, slide-to-prone; the sprint / super
  sprint view height (`enableSprintViewHeight`), view bob, fall damage (`PM_CrashLand`).
- Replay's per-frame velocity quantization (feature 55). The type 1 collision is modeled since section 8; what of it is
  still open is listed in 8.6.
- The exact wish direction on slopes (the game projects the unit wish direction onto the ground plane without
  renormalizing; the preview renormalizes).
- Mantle geometry and timing (`player_mantle.*`, unchanged): only the 58 height cap was added here. Its chest probe sits
  29 units above the feet, so in the air a ledge less than 29 above the feet is not found (the game's edge sweep finds
  them from 16).
- The late jump (200 ms after walking off a ledge, `jumpLateGraceMsec`, Mantle_UpdateAutoTrigger `0x141101B90`).
- Ladder magnetism, cresting, the top vault and ladder strafe details (the preview keeps its simple contact model).
- The tactical sprint after a jump: read as not resumed (the client clears the trigger once the sprint ends).
- Noclip with sprint: x2 (a dvar in the game, value not read).
- The binds were read from an 8.19 rawfile dump, not from the Replay zones.

## 6. Code

- `src/core/player_move.h`: `MoveParams` with the values above, `PlayerState` (`tacticalSprinting`, `sprintRestore`,
  the staged view lerp), `MoveInput` (`slide`, `tacticalSprint`), `ViewCurve` / `viewCurveValue`, `PlayControls`
  (the client side), `mouseDegreesPerCount`.
- `src/core/player_move.cpp`: PM_Accelerate floor, stance speed blend, sprint / tactical sprint / restore, slide slope,
  view curves, ladder, 16 stick, mantle cap, noclip 570, `PlayControls`.
- `src/qtapp/camera_view.cpp`: `tickPlay` feeds the held keys through `PlayControls` each movement step;
  `keyPressEvent` latches presses; `event` keeps the game keys (also with Ctrl held) away from the window shortcuts while
  playing; `mouseMoveEvent` uses the game's degrees per count and the 85 pitch limit while playing.
- Tests: `tests/movement_parity_tests.cpp` (speeds, acceleration floor and friction, tactical sprint meter, toggle sprint
  and restore, view curves, mantle cap, noclip / mouse constants, the default binds, slide end to end, a ladder behind
  a model's collision, mantle with the held jump and the airborne auto trigger, open hulls);
  `tests/play_preview_tests.cpp` (ladder and walking-down expectations updated to the game rules).
- `src/core/collision_world.*` `traceCylinder`: the octagonal ground prism (section 8).
- `src/core/collision_world.cpp` `addSolid`: a convex solid whose windings reach the +-262144 clip box (an open hull) is
  dropped. In Hideout, stock model physics pieces without caps became slabs through the whole map: the player stood
  inside one, every trace started all-solid and the play preview froze in the air (or went to noclip at a spawn).
- The real input path, offline: `IW8R_TEST_PLAY_INPUT=<script>` (`src/qtapp/play_input_test.*`) drives the play preview
  with QTest key presses to the camera view and prints the player state; run it on Qt's offscreen platform
  (`QT_QPA_PLATFORM=offscreen`) so no window or cursor moves on the desktop.

## 7. 2026-11-15 owner test: "i cant mantle, climb ladders, slide"

Through the real key path on `samples/dev/iw8r_compact_showcase.map` the mantles (32, 56), the slide (Shift tap and
hold style) and jump / crouch / prone already worked; three things were wrong: (1) the row 9 ladder: the ladder model's
collision stands ~19 units in front of the prefab's ladder brush and the ladder probe looked only 4 units ahead at the
first thing hit; (2) the mantle needed a fresh jump press (Replay: held, or by itself in the air after a jump); (3) on
the stock map the player froze in open-hull slabs (above). All three fixed; a tap shorter than one step still jumps.

### 7.1 Bobbing against low stock props (2026-11-15 evening)

The collision agent counted 98 Hideout physics models (728 placements) with low pieces (<= 24 units) carrying two or
more steep faces (45-80 degrees from level). Walking into one bobbed the player: the step-up landed on a face too steep
to stand on, the player slid back and stepped again. Replay's own rules (verified above): steep faces are walls on the
ground (PM_ClipSlideVelocity) and a step needs a landing plane with normal z >= 0.3 (PM_StepSlideMove); the 0.3 s ledge
walk keeps support on a steep plane. Those stop the bobbing for faces steeper than 72.5 degrees; for 45-72.5 degrees
the editor's round-bottomed capsule still crept and bobbed, because the game's flat ground cylinder would stand on the
floor (or the prop's top) under its rim. Editor rules for that: walkable floor within 16 under the centre counts as the
ground when the capsule touches a steep face, and a steep landing is kept only when the stepped move got at least 1
unit further and is not clipped by the steep face. The "moving up, no ground: no step" check now needs a real upward
speed (> 10, as the ground trace's own test), not the 0.1 left in the velocity by a slope clip. Tests:
`parity_no_bobbing_against_a_low_steep_wedge` (50-80 degree wedges, straight and at 30 degrees: at most one climb and
one descent, then still) and `parity_steps_onto_a_chamfered_box` (45-75 degree chamfers on a 16-unit box: on top).

## 8. Replay's player collision, type 1 (2026-11-15 night)

Section 7.1 kept editor rules for the round-bottomed capsule because Replay's own shapes were not known. They are now
read from the exe and the preview uses them; the editor rules are gone.

### 8.1 Sources

- The Replay exe through IDA (functions below). Constants were read from `.data`: the collision type
  `s_paramRecordD_2437BA8` = 1 (only ever read, so fixed), `k_PM_groundTraceProbeFlagA` / `B` / `C`
  (`0x142437ADD..F`) = 1 / 1 / 1, `k_BG_probeDistanceDivisor` (`0x1429B4A78`) = 40, `k_BG_eighthUnit` = 0.125.
- The Havok references on the NAS (limited-use licence: read for names and layouts only, nothing copied):
  `O:\Resources\Havok-Tools\Havok-SDK-Reference-2018.1-r1\2018.1.0-r1\` holds the Animation, Cloth, Common,
  ContentTools and Geometry references but no Physics one; the OpenIW8 tree
  `O:\Resources\External-Codebases\OpenIW8\workspace\iw8\shared\codware\sdk\havok\hk2018_2_0_r1\source\physics\physics\collide\`
  has the query collector, shape-cast and capsule / cylinder / sphere shape folders but no `hknpConvexShape` or
  character proxy header; `O:\Resources\Replay-Havok-Research\replay_havok_functions.csv` lists generic Havok glue
  only. So the shapes and rules below come from the exe; the references only confirmed the hknp names it calls
  (`hknpConvexShape::createFromVertices`, `hknpShape::setProperty`). Havok units in Replay are inches / 32
  (Physics_CharacterCastPacked scales by 0.03125).

### 8.2 Shapes

- **Movement capsule.** Every player capsule trace goes PM_TraceCapsuleWithFlags `0x140CF0980` ->
  PhysicsQuery_BindTraceResultSlots `0x140CF05B0` -> PhysicsQuery_LegacyCapsuleHitAccepted `0x14109A940`. With type 1
  (PhysicsQuery_AlwaysAcceptsHit `0x140CD7450` returns the type != 0), a player owner and no ground flag, the shape is
  the stance capsule after BG_Collision_ApplyProbeOffset `0x140CD7000`: `s = 8 / 40 x (halfHeight - radius)` moves the
  centre up by s and shortens the half segment by s, so the bottom rises by `2 s` = BG_GroundTrace_ComputeProbeDistance
  `0x140CD7070` = `8 / 40 x 2 (halfHeight - radius)` and the top stays: 8 standing, 4 crouched, 0 prone. (MP: the
  literal 8.0; SP: dvar `bg_standStickHeightBase` 4, "desired standing stick height, assuming a 70 inch capsule with a
  15 inch radius".)
- **Ground prism.** The ground trace sets trace flag 0x40 (hit flag 1), and LegacyCapsuleHitAccepted then builds
  Physics_CharacterCastPacked `0x14108D940` -> HavokPhysics_CreateCachedConvexHullShape `0x14161D030`: 16 points,
  8 angles `k x 45` degrees at `(r sin t, r cos t, +-halfHeight)` around the shape centre, `createFromVertices` with
  convex radius 0. A flat-bottomed octagonal prism, corners on the x and y axes and the diagonals (world aligned while
  no world-up frame is active). Its bounds (PM_GroundTrace_StanceChangeSolver `0x140CDA1B0`): half height
  `max(2 (halfHeight - radius) - stick, 1) / 2`, centred that far above the trace point, so it spans 32 units standing,
  16 crouched, 1 prone, from the trace point up.

### 8.3 The ground trace: two passes a frame

PM_Normal_Update `0x141123B90` calls PM_GroundTrace `0x140CD8650` with doFullTrace 0 after PM_CheckDuck (before the
move); PM_Normal_UpdateCollide `0x141123F30` calls it with 1 after the move. PM_GroundTrace_ProbeAndResolve
`0x140CD90C0`:

- **Reach.** 16 (`playerCollisionStickExtension`) on the ground; within 500 ms of the jump time (playerState + 0xB4),
  in the air, or with pm flags 0x80000 / 0x40: 0.25, plus `min((|vz| + gravity x frametime) x frametime, 15)` when
  falling outside the 500 ms (`k_PM_groundStanceChangeEnable`).
- **First pass (0).** The prism swept from `feet + stick + radius` down to `feet - reach`; the hit point is raised 0.125
  back along the sweep (`k_PM_groundStanceChangeFlagB`). When the trace's walkable flag (trace byte 62, normal z
  >= 0.7) is set, or the player is airborne 500 ms or more after a jump, the feet go there if they move less than 1/8 or
  the movement capsule fits there (PM_GroundTrace_TryMoveOrigin `0x140CD9F40`, a position test).
- **Second pass (1).** With flag 1 of pmove + 740 clear: a ray (zero bounds) from `feet + 2 x halfHeight` down to `feet - reach`.
  It never moves the feet (the solver's resolve argument is the doFullTrace flag).
- **Ledge walk.** A hit that is not walkable while grounded and outside the 500 ms (and pm flag 0x80000): with flags B
  and C set the first such pass stores the time (playerState + 200); while the support is at most 0.3 s old the ground
  normal is up and the hit counts as walkable. Any other pass result resets it. (Flag B without C would probe a capsule
  down 16 instead.)
- **Result.** Walkable (or the ledge walk): ground. Otherwise a ground plane only; PM_AirMove `0x14111CBA0` clips the
  velocity against it (PM_ClipSlideVelocity, the Quake 3 `groundPlane` slide). Jump_Start leaves the plane.

### 8.4 Steps: none under type 1

The move is PM_UpdatePlayerCollision `0x140CDE110` -> PM_ResolveGroundedMovement `0x141130790`: PM_SlideMove with the
ground plane (when the ground trace has one) as the first clip plane; then, when PhysicsQuery_AlwaysAcceptsHit (type
!= 0), PM_GroundCapsuleSweepMove `0x141131D40` (prone only, playerState pm flag 1: AI_TryCapsuleMove) and return. The step code
after it (step 18, 10 with pm flag 1; up 19 and the rules below) runs only for type 0; PM_StepSlideMove `0x14112F370`
(the same rules, reached from PM_AccelerateTowardWishVelocity) is not the walk's path either. So a player climbs a
step because the round bottom of the raised capsule (centre stick + radius = 23 above the feet) rides up against the
edge: at an edge h above the feet it stops sqrt(r^2 - (23 - h)^2) from it, and the flat prism, reaching 15 toward its
corners and 13.86 between them, then overlaps the top and the next ground trace stands the feet there (if walkable and
the capsule fits). That is about h < 17.3 face-on to a side of the prism and h < 23 toward a corner (world aligned:
along the x / y axes and the diagonals); a chamfered or rounded edge lets the capsule closer still.

The editor's capsule trace is plane expansion: against an upright face it stopped a full radius away at any height,
so the prism never reached the top. `CollisionWorld::traceCapsuleRounded` (used for every move) now gives the bottom
sphere its edge: an upright face (normal z within 0.02 of level) whose top (`Solid::faceTop`, the highest point of its
winding, kept per plane when a solid is added) is below the bottom sphere's centre is expanded by
`sqrt(r^2 - drop^2)` instead of r (by nothing once the edge is a radius below the centre). An axial bevel takes the top
of the face it coincides with, and is never rounded when it only touches an edge or a corner (it bounds the faces
beside it: a wedge's foot). Movers keep the square capsule. With that, the climb is the game's: no step move at all, the
capsule rides to the edge, the prism snaps the feet up (`parity_collision_type1_stick_and_step`: along an axis 16 and 20
are climbed and 24 is not; 22.5 degrees off it 16.5 is and 18 is not). A step whose edge the capsule meets before the
prism's window (16 at walk speed) costs a moment of speed, as the game's level-wall clip does.

### 8.5 What the preview does now

- `player_move.cpp`: `capsuleTrace` / `moveTrace` (the raised capsule) for the slide and step moves and the stance room
  checks; `groundTrace(settle)` as the two passes above, called before the move (`settle`) and after it (and after the
  ladder move); the ledge walk as Replay keeps it (the `jumpingState` condition is gone); the AirMove ground-plane clip;
  the jump clears the ground plane; the slide takes the ground plane (walkable or not) as its first clip plane and
  slides the gravity half-step along it; no step move (8.4); `placePlayer` drops the prism.
- `collision_world.*`: `traceCylinder`, the octagonal prism by plane expansion (support corner per plane); the solids'
  axial bevels give its flat bottom the level contact on a ridge or a corner. `traceCapsuleRounded` and
  `Solid::faceTop` (8.4).
- Dropped editor rules (7.1): the ray from the feet under a steep contact (the second pass is the game's ray, from the
  top), the steep-landing progress / no-clip rules (`steepStepMinProgress`), the post-move capsule stick-down, and the
  "moving up, no ground below: no step" check. `parity_no_bobbing_against_a_low_steep_wedge` and
  `parity_steps_onto_a_chamfered_box` pass with the game's rules alone.
- What a player notices: steps are climbed as in the game (about 17.3 to 23 by direction); bumps under 8 never
  block a walk and their walkable tops are stood on; the feet can stand up to 8 inside a steep low
  bump; the player stands over an edge until the prism leaves it (15 along the axes and diagonals, 13.9 between); a
  step lands in two stages one frame apart (capsule, then feet); a slope too steep to stand on holds the player 0.3 s,
  then slides them down it.
- Tests: `parity_collision_type1_ground_prism` (flat bottom on a ridge; the footprint over a 40-unit edge at 0 / 45 /
  22.5 degrees), `parity_collision_type1_stick_and_step` (a 6 curb does not slow the walk; the climb by direction, 8.4),
  `parity_ledge_walk_then_slide_on_a_steep_slope` (46 degrees: held 0.25 s, then slides); the ladder-top test settles
  two frames for the feet.

### 8.6 Still open

- Contacts are plane expansion (solid planes plus axial bevels, as the capsule always was): edge-against-edge contacts
  of the prism and of the capsule are not exact; the Havok cast tolerance is not known (the trace keeps 0.03125).
- The rounded edge of 8.4 is for upright faces only; a sloped face's edge against the sphere is still square.
- Flag 1 of pmove + 740 (the prism in the second pass) and pm flags 0x40 / 0x80000 were not identified; the second pass
  assumes them clear.
- Flag A's 0x200 (a hit more than `max(stick, 1)` above the feet) is not modeled.
- Hideout through the input-test hook: the cinderblock pile (#0), the broken wall cap and the soda can are steady; at
  `debris_cinderblock_pile_a_01` #1 (W held into the pile) the player is in a pocket between steep block faces, never
  grounded, z within about 3 (32.0 .. 35.1) by the rules of 8.3 and 8.7; the 7.1 editor rule stood him still there.
  Not compared with the game.
- The soda-can walk on Hideout reads "air" at the end of every frame: under the walk line no floor facet lies below the
  centre (a ray from 60 above to 200 below finds nothing, and a 1-unit-wide retry does not either), only the prism
  catches the neighbouring facets. That is a hole in the extracted stock collision, not a seam; the game's second pass
  is a ray (the game-test source: `useRayCast` -> `bounds_origin`), so no editor tolerance was kept.

### 8.7 PM_SlideMove and the end of the frame (2026-11-16)

- **PM_SlideMove** `0x14112F840`, read in full (and its readable twin in the game-test source): at most 8 planes,
  4 bumps. The first planes are the ground plane (while the ground trace has one, walkable or not), the velocity's
  direction, and a level ceiling `(0, 0, -1)` while the ledge walk supports the player (pmove + 740 bits 0x202, which
  also clamp the vertical speed to <= 0 and skip the crease projection). A trace that stops at once on a level floor
  with no ground plane takes the reversed level velocity as its plane. A plane hit again (dot > 0.999) is clipped and
  nudged out by `normal x (1/60) / frametime`. A new plane: all planes are sorted by `dot(velocity, plane)`; the most
  opposing one under 0.1 clips the velocity (and the gravity end velocity); each other one, in that order, that the
  clipped velocity still runs into (< 0.1) clips it again, and if that sends it back into the first plane the velocity
  follows the crease `cross(first, other)`; a third plane it then runs into stops it dead. The clips are
  PM_ClipSlideVelocity `0x14111E9B0` (overclip 1.001 in, 0.999 out): on the ground (`k_PM_ClipSlideConst` 1, not on a
  platform) a plane with normal z <= 0.7 against up (`k_PM_groundedMoveFlag` 1: the reference is pml + 104, the up
  vector) is clipped as the level wall under it unless the velocity leaves that wall. The preview's `slideMove` now
  does all of this (it had 6 planes, Quake 3 order, the ground normal as the wall reference and a bare `+ normal` nudge).
- **End of frame** (PM_Normal_UpdateCollide `0x141123F30`, after PM_Weapon; game-test `PM_Normal_UpdateCollide`): with
  `previous_origin` from the start of the frame (PM_InitPmoveState `0x141124690`, pml + 0x40), when
  `|origin - previous_origin|^2 / dt^2 < 0.25 |velocity + impulseFieldVelocity|^2`, the velocity becomes
  `(origin - previous_origin) / dt`, or 0 when that points against it. **PM_Friction** `0x141121160` (also first in
  PM_AirMove) stops the whole velocity under 1 unit / s (level speed on the ground, full speed in the air). Both are in
  the preview now.
- **The V pocket.** In a V of two steep faces the raised capsule touches neither face, so the slide's multi-plane rule
  never sees both: each frame clips against the one face the ground trace reports. A player dropped at the crease
  settles (the end-of-frame check and the friction stop); dropped 6 off the crease, the rules leave a 2-4 frame swing
  across it (z 25.87 .. 26.05 at 60 degrees, 31.31 .. 32.49 at 65, 41.06 .. 41.64 at 70, |x| < 0.5), never flickering
  between ground and air (`parity_settles_in_a_steep_v_pocket`). Whether the game swings the same was not checked in
  game; no rule that is not in the exe was added.
- **A side effect of the end-of-frame check:** the landing is found while the feet are still up to the fall's reach
  above the floor, the next frame's ground trace puts them down, and a jump started in that frame moves less than half
  its velocity: the check cuts it short (to 0 when the drop is larger than the jump's first step). The code reads that
  way in Replay and in the game-test source; not seen in game. `jump_has_minimum_interval_and_no_bunny_hop_gain` now
  presses its second jump one frame after the landing frame.

### 8.8 The mantle search band

The editor's mantle (`player_mantle.cpp`, after the reference walk simulator) found the ledge's face with one ray 29
above the feet (`searchCenterHeight`), so in the air a ledge less than 29 above the feet was never found. The game has
no such probe: Mantle_FindMantleEdge (game-test `bg_mantle.cpp`; Replay registers the same `mantle_edge_search_*`
dvars) queries the map's precomputed ledge edges (`EdgeOctreeQuery`, bucket MANTLE) in a frustum ahead of the player:
half width `mantle_check_radius`, half height `mantle_check_height / 2` (`_air` values in the air) centred that half
above the search origin, from `radius - mantle_edge_search_near_plane_offset` (5) to `radius + mantle_check_range + 0.125`
out. 29 is that centre (`mantle_check_height` 58). The editor has no edge data, so the face search samples the whole
band instead: rays every 4 units from 17 (the lowest ledge, 16, plus 1) to 57, the nearest steep face counting; the
rest of the candidate checks are unchanged. Effect: in the air a ledge 16..29 above the feet is mantled
(`parity_mantle_activation_held_and_airborne`, a 48 box with the feet at 30), and a 64 box is mantled near the top of a
standing jump (`parity_mantle_height_limit`: from the ground 56 mantles at once and 64 does not; 58 is
Mantle_GetMaxClimbHeight above the feet). The edge frustum's exact dvar values in Replay were not read (the reference's
58 / radius + 25 stand).

### 8.9 The ladder top-out and the view's height smoothing

- **Top-out** (PM_LadderMove `0x140CC6BF0`, IDA "Mantle_UpdateMotionCore"; game-test `PM_LadderMove`, reference
  `walk_ladder.cpp`): after the climb move, with the ladder anchor `origin.z + ladder_anchorOffset` (suit + 420; 60 in
  the reference void suit) at least `bg_ladderVaultStartDistance` (MP 15) above the ladder's top while moving up, the
  mode becomes VAULTING once: the feet go to the top's height (+1.2e-7) and 0.2 along the ladder's inward normal, pmove
  flag 0x100 with the height change (the view smooths it); the next frame's ladder check finds the anchor above
  top + 15 and lets go. Within `bg_ladderCrestingTransitionDistance` (2) below that the mode is CRESTING (an animation
  state; nothing moves). The velocity is kept (a small rise after the vault). The preview does the same (the ladder's top
  is its brush's highest point; the editor's old "small hop off the top" is gone, and leaving the face sideways just lets
  go). `parity_ladder_top_out_vault`: on a 120 ladder the feet go from 74.8 to 120 in one frame, the eye moves 0 that
  frame, and the player walks onto the platform.
- **View-Z smoothing** (game-test `CgPMove` step smoothing and `CG_View`; `cg_viewZSmoothing*` defaults; the Replay
  client function was not located): a frame whose feet were snapped (pmove flag 0x100: the ground trace moving them more
  than 1, `k_PM_stanceChangeLimitOne`; the ladder vault) by at least `cg_viewZSmoothingMin` 1 starts easing the view
  over the change (plus what was left of the last one, at most `cg_viewZSmoothingMax` 60), over
  `cg_viewZSmoothingUpTimeMax` / `DownTimeMax` 0.4 s at 32 units / s or slower down to 0.15 s at 100 or faster, curve
  `cg_viewZSmoothingInterpType` 2: `(sin((t - 0.5) pi) + 1) / 2`. `eyeHeight()` applies it (`PlayerState::stepView*`).
  `parity_view_smooths_a_step_snap`: the feet go up 16 in one frame, the eye at most 1.04 a frame.

### 8.10 The Hideout walk end spots (2026-11-16)

Three spawn walks ended standing still but "air" at the end of the frame. Probed with the input-test hook:

- **walk 0** (-996, -1418, 35.8): on a prop's table top (a convex hull 2.2 thick, 48 x 34, top 35.7, about 32 above the
  floor), the centre 2 past its edge, an object on the table in front and a shelf 0.7 above the head. Reached by walking
  up a 43 degree walkable prop face (the ground clip keeps the speed along the slope: 129 up), off its top and onto the
  table by the airborne snap. Jump: the shelf stops it (no headroom); crouch and sprint work.
- **walk 3 / jumpwalk 3** (-1265, -1848, 39.3): on a crate's top (hull 33 x 33, top 39.1, about 31 above the floor), the
  centre 1.7 past its edge. Reached by rounded climbs of 7.8, 13 and 21 (each under 23 along a prism corner, 8.4).
  Jump, crouch and sprint work.
- **jumpwalk 7** (154.8, 679.6, 27.2): on a wall top 3.5 wide (a facet strip, 27 high), the centre 11.6 off it, the prism's
  corner over it by 3.4; reached by the jump's airborne snap. A second contact there was false: the prism diagonally off
  the end corner of the next strip (2 wide) "overlapped" it only through plane expansion (fixed below). Jump (with a new
  press), crouch and sprint work.

All three tops are walkable planes under the prism (normal z 1), as the game's snap requires; none is reached through a
top the game's rules would refuse (Replay 1.20 still snaps an airborne player onto any hit; the later game-test build's
`playerCharacterCollisionWallClimbFix` limits that to walkable ones). "Air" at the frame end is the game's own second
pass, a centre ray, missing a top under the prism's rim; the ray queries the same world (props included). Changes:
`traceCylinder` now also separates on the prism's eight sides against the solid's bounds (no phantom footing off a
thin solid's corner; `parity_prism_no_phantom_footing_off_a_corner`), and `PlayerState::standing` keeps the first
pass's verdict for the HUD ("walk", the hook logs "stood").
