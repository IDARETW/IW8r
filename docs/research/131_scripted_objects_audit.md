# 131. Scripted objects: build vs viewport audit

Read-only audit of the source tree (2026-11-14). Scope: everything a mapper places whose behaviour comes from the
generated map script or the game's scripts: the `iw8r_*` behaviour keys, triggers, script_brushmodels, script_models,
scriptables, dynamic props, vehicles, effects and sounds, game-mode objects, prefab contents. Line numbers are those
of the working tree at the time of reading. Nothing was built or run. "UNVERIFIED" marks a claim that depends on the
converter or the game and could not be checked from this tree.

## Summary

The build side is broad and mostly consistent: every `iw8r_*` key the inspector offers is read by
`compile_iw8.cpp` / `map_script.cpp`, and every mis-set key gets a warning. The weak points are:

- **Pivots and transforms.** A spinning `script_brushmodel` turns about the map origin in game and in the preview
  (no origin is ever computed for a brush model, and origin brushes are dropped as a tool material). Prefab
  placements transform only `origin` / `angles`, so direction and point keys (`iw8r_hinge "x y z"`, `iw8r_door slide`,
  `iw8r_jump`) and radius / height keys keep their prefab-local values.
- **Volumes the viewport does not show.** `trigger_radius` (Domination and CTF templates), the `fx_origin iw8r_damage`
  cylinder and the door's automatic use area are drawn as nothing or as a 16-unit marker cube.
- **Preview coverage.** The Behaviour Preview covers spin (yaw only on brush models), bob, lift (brush models
  only) and doors. It does not cover model lifts, pitch / roll spin of brush models, xanims, effect orientation,
  effects inside prefabs, or anything in Play mode (doors stay shut, triggers do nothing, models do not collide).
- **Trigger semantics.** `trigger_damage` is turned into a hurt volume (5 damage every 50 ms by default) that also
  fires on player presence.

## Object kinds

| Kind / key | Build writes | Viewport draws | Behaviour Preview | Gaps (see list) |
|---|---|---|---|---|
| script_brushmodel (plain) | brush model `*N`: entity keys + `model *N`, no origin (compile_iw8.cpp:2669-2676); surfaces replaced by a generated `<name>_vis` script_model linked to it (1283-1409) | its brushes, textured (scene.cpp:875-906) | - | G12 |
| script_brushmodel `iw8r_door` slide | `iw8r_door_N` thread, moveto open/hold/close (map_script.cpp:734-745; compile 3809-3878) | brushes | slide loop (scene.cpp:305-310) | G7 (trigger without name), G2 (prefab), G9 |
| script_brushmodel `iw8r_door` hinge / rotate, `iw8r_hinge` | pivot script_origin at `doorHingeOf`, door linkto pivot, rotateyaw away from the user (map_script.cpp:713-732) | brushes; hinge point not marked | swings about the same hinge (scene.cpp:311-318); default time 1 s vs build 0.6 s | G2, G7, G9, G15 |
| script_brushmodel `iw8r_lift` | `iw8r_lift_N`, moveto up/down forever (map_script.cpp:690-698) | brushes | yes (scene.cpp:291-297) | - |
| script_brushmodel `iw8r_spin` | `iw8r_spin_model` rotateyaw/pitch/roll 360 about the entity origin (map_script.cpp:641-647) | brushes | yaw only, about origin key or map origin (scene.cpp:320-327) | **G1**, G6 |
| script_brushmodel `iw8r_bob` | `iw8r_bob_model` movez (map_script.cpp:648-654) | brushes | yes | - |
| script_brushmodel `iw8r_brush_visual 0` / glass | no generated model; own surfaces kept (compile 1298-1304) | brushes drawn as normal | - | G12 |
| misc_model / script_model `iw8r_spin` / `iw8r_bob` | misc_model promoted to script_model, name filled (compile 1486-1532); thread as above; made solid (3757-3783) | model mesh | spin pitch/yaw/roll, bob (scene.cpp:229-265, 925-929) | - |
| script_model / misc_model `iw8r_lift` | accepted on script_model and promoted misc_model (compile 1490, 3927) though defs offer it only on script_brushmodel | model mesh | none: model preview handles spin / bob only (scene.cpp:925) | G6 |
| script_model `iw8r_anim`, `iw8r_anim_loop` | `iw8r_anim_N` scriptmodelplayanim with the literal name; only when a target points at the model (compile 3973-3988) | model in bind pose | none | G11 |
| script_model / misc_model `iw8r_no_collision` | kept live via `level.iw8r_keep` (compile 3775-3782) | model mesh | - | (Play mode, G10) |
| `iw8r_material`, `iw8r_mat:*` on models / dynamic_prop | re-skinned copy (compile 1190-1277) | same override (scene.cpp:365-386) | - | - |
| trigger_* brush (multiple, use, use_touch, hurt) | brush model with trigger contents (compile 1736-1781) + script-tested volume (map_script.cpp:248-336) | brushes in trigger colour | - | - |
| trigger_radius | cylinder volume radius / height from origin up (map_script.cpp:282-294) | 16-unit marker cube (scene.cpp:656-657) | - | **G3** |
| trigger_damage | hurt volume, default dmg 5 every 50 ms, notified on presence (map_script.cpp:327-332) | brushes | - | **G4** |
| trigger `iw8r_teleport` / `iw8r_jump` / `iw8r_damage` | one thread each (map_script.cpp:661-689; compile 3880-3923) | trigger brushes; destination link not drawn | - | G2 (jump vector), G15 |
| trigger `iw8r_vision` | vision file + `iw8r_vision_volumes` (compile 3784-3807; map_script.cpp:747-764) | trigger brushes | - | - |
| fx_origin `fx`, `angles`, `delay` | loadfx + spawnfx/triggerfx or playfx loop with forward/up from angles (map_script.cpp:832-851) | diamond marker; particles in Effect Preview at the origin (camera_view.cpp:1633-1655) | particles ignore angles and delay | **G8** |
| fx_origin `iw8r_damage` | nameless damage cylinder (map_script.cpp:262-277) | nothing for the radius | - | G3 |
| fx_origin `soundalias`, sound_emitter | `iw8r_place_sounds` (map_script.cpp:380-408, 816-831) | octahedron marker | - | - |
| scriptable | collision json scriptable instance, or left out with a warning (compile 2244-2353) | the definition's first model (scriptable_model.h:18-24; scene.cpp:917-920) | - | G10b |
| dynamic_prop | collision json dynamic model + physic preset (compile 2465-2512) | model mesh | - | - |
| script_struct_mp_* vehicles | struct written; vehicle and model carried (compile 2436-2464) | 16-unit marker cube although `model` and def `size` are known | - | G10a |
| iw8r_loot_point / cluster / zone | loot points and boxes (compile 2354-2435) | marker / brushes | - | - |
| game-mode templates (dom, hp, sd, ctf) | entities as stock (gamemode_templates.cpp:40-143) | dom / ctf triggers as cubes (G3); sd intact and destroyed crates overlap (G13) | - | G3, G13 |
| misc_prefab contents | flattened; names scoped for bundles (prefab.cpp:483-597) | contents drawn (scene.cpp:449-606) | contents never animated; fx not previewed | **G2**, G8, G14 |
| Make Scripted / Make Destructible | script_model + script_origin targeting it / targetname destructable (scripted_objects.cpp:23-71) | model mesh | - | - |

## Ranked gaps

### G1 (high) Spinning brush models orbit the map origin; no way to set the pivot
- Build: `iw8r_spin_model` calls `ent rotateyaw(360, seconds)` on the brush model itself (map_script.cpp:641-647).
  A brush model is written with its keys and `model *N` and no origin (compile_iw8.cpp:2669-2676); its surfaces and
  collision are in world space. The door code states the consequence: "A brush model turns about its own origin
  (the map origin for an ordinary brush), so it is linked to a script_origin there" (map_script.cpp:714-715). Spin
  does not do that, so a fan or turntable away from 0,0 swings around the map origin. The linked `_vis` model follows.
- Origin brushes do not help: `origin` is a tool material (compile_iw8.cpp:1177), no code computes an origin from
  one, yet the Rotating door snippet says "Give the script_brushmodel an origin brush at the hinge"
  (script_check.cpp:200). Whether a hand-set `origin` key offsets the geometry in the converter is UNVERIFIED.
- Preview reproduces it (pivot = origin key or map origin, scene.cpp:270, 280), so the mapper sees an orbit with no
  fix offered.
- Fix: in `mapScriptGsc`, give each spinner that is a script_brushmodel a pivot like hinged doors: spawn a
  script_origin at the brush bounds centre (or at `iw8r_hinge` / a new `iw8r_pivot`), `linkto` it and rotate the
  pivot. Pass the centre from compile_iw8.cpp:3939-3958 (add `pivot` to `MapScriptExtras::Spinner`). Use the same
  centre in `brushBehaviourPose`. Fix or remove the snippet text at script_check.cpp:198-217.

### G2 (high) Prefab placements do not transform direction, point or size keys
- `transformKeys` rewrites only `origin` and `angles` (prefab.cpp:205-229). Inside a rotated or scaled prefab:
  `iw8r_hinge "x y z"` stays in prefab space (the hinge can land near the map origin), `iw8r_door "slide x y z"` and
  `iw8r_jump "vx vy vz"` keep their unrotated vectors, and `trigger_radius radius/height`, `fx_origin iw8r_damage`
  radius, `iw8r_lift` / `iw8r_bob` heights do not scale. The viewport uses the same `instantiatePrefab`
  (scene.cpp:453), so it agrees with the build but both are wrong.
- Fix: in `transformKeys`, rotate (and scale) the vector keys (`iw8r_door slide`, `iw8r_jump`), transform the point
  key (`iw8r_hinge` when it parses as three numbers) with `pl.point`, and scale the length keys by `inst.scale`. A
  small table of key -> kind (vector / point / length) keeps it in one place; add prefab tests.

### G3 (high) Script-tested volumes the viewport does not draw
- `trigger_radius` is built as a cylinder of `radius` / `height` from the origin up (map_script.cpp:282-294) but is a
  point entity in the scene and falls to the generic 16-unit marker cube (scene.cpp:620-658). The Domination flags and CTF
  pickup / capture zones are trigger_radius (gamemode_templates.cpp:49-52, 129-140), so their capture areas are
  invisible.
- `fx_origin iw8r_damage "<amount> [radius]"` makes a damage cylinder (map_script.cpp:262-277); nothing draws the
  radius.
- A door with no trigger gets a use area of its bounds grown by 40 units (compile_iw8.cpp:3822-3841); not drawn.
- Fix: in `buildPointEntity` add a `trigger_radius` case that draws a wire cylinder (radius, height) in trigger
  colour, and an `fx_origin` ring when `iw8r_damage` is set. Draw the door use box as an overlay when the door is
  selected (camera_view / render overlay list). Selection still by bounds.

### G4 (medium-high) trigger_damage is treated as a hurt volume
- `readTriggerVolumes` puts `trigger_damage` with `trigger_hurt`: `v.damage = d ? atof(dmg) : 5`, every 50 ms
  (map_script.cpp:327-332), and the poll notifies "trigger" whenever a player is inside (map_script.cpp:146-168). A
  stock-style trigger_damage (the shoot-to-explode exploder trigger, trigger_templates.cpp:87-90) would hurt players
  standing in it at about 100 damage a second and fire its exploder on touch. That the stock class fires on damage
  rather than on touch is the usual CoD meaning; its Replay behaviour is UNVERIFIED.
- Fix: in `readTriggerVolumes`, leave trigger_damage out of the damage branch (and out of the presence poll); if a
  script-side substitute is wanted, notify on `damage` of a spawned entity instead.

### G5 (medium) Rotating door snippet relies on origin brushes that the build drops
- See G1: script_check.cpp:198-217 inserts `door rotateyaw(yaw, ...)` and asks for an origin brush; the build has no
  origin-brush handling (no reference to one anywhere in src/), so the inserted door orbits the map origin.
- Fix: make the snippet spawn a pivot at a named script_origin (as the generated hinged door does) or point users at
  `iw8r_door hinge`.

### G6 (medium) Behaviour Preview misses moves the build makes
- Brush models: `iw8r_spin` with `pitch` / `roll` is ignored (scene.cpp:323 requires `axis == "yaw"`); the build
  does rotatepitch / rotateroll (map_script.cpp:645-646).
- Models: `iw8r_lift` on script_model / misc_model is built (compile_iw8.cpp:1490, 3927) but the model path only
  previews spin and bob (scene.cpp:925; `behaviourPose` 229-265). The defs offer `iw8r_lift` only on
  script_brushmodel, so it is also undiscoverable on models.
- Axis names: build lower-cases them (compile_iw8.cpp:3947), preview compares case-sensitively (scene.cpp:249-250).
- Fix: in `brushBehaviourPose` turn about X / Y for pitch / roll (`BrushPose` needs a full rotation, not `yawDeg`);
  add `iw8r_lift` to `behaviourPose` and to the condition at scene.cpp:925; lower-case the axis; add `iw8r_lift` to
  the script_model / misc_model defs if it is meant to be offered.

### G7 (medium) A door trigger without a targetname is silently ignored
- The trigger that targets the door is used only if it has a targetname (compile_iw8.cpp:3814-3817); otherwise the
  door falls back to its own use area with no warning. The key help promises "A trigger_use or trigger_multiple whose
  target is the door opens it".
- Fix: give such a trigger a generated name (as brush visuals do, compile_iw8.cpp:1361-1371) or warn
  "trigger targets door X but has no targetname; the door uses its own use area".

### G8 (medium) Effect Preview ignores orientation, timing and prefab effects
- Particles are placed at `origin + p.pos` with no rotation (camera_view.cpp:1642-1643); the build orients each
  effect with `anglestoforward / anglestoup(angles)` (map_script.cpp:846, 850). `delay > 0` repeats every `delay`
  seconds in game; the preview loops by the effect duration (camera_view.cpp:1641).
- Only `fx_origin` entities of the document are sampled (camera_view.cpp:1633-1634); an fx_origin inside a
  misc_prefab is built (flattened) but never previewed.
- Fix: rotate `p.pos` by `anglesToMatrix(angles)` in `appendEffectParticles`; use `delay` as the loop period when set;
  include fx_origins from prefab contents (the scene already instantiates them, scene.cpp:510-560).

### G9 (medium) Play preview has none of the scripted behaviour
- `CollisionWorld::build` blocks only worldspawn, func_group and script_brushmodel brushes (collision_world.cpp:21-24,
  52-53). So in Play mode: doors and lifts are static solid brushes (a door cannot be opened), teleporters, jump pads,
  damage and vision volumes do nothing, misc_prefab contents and placed models have no collision (the build gives
  models hulls, compile_iw8.cpp:1795-1904), and `tickPlay` never tests a trigger (camera_view.cpp:1119-1154).
- Fix (incremental): add prefab brushes and model hulls to the play world; drive script_brushmodel poses from the
  behaviour clock while playing (rebuild their solids at `brushBehaviourPose`); test `readTriggerVolumes` volumes
  each tick for teleport / jump / door-use. `readTriggerVolumes` and `doorHingeOf` are already shared core code.

### G10 (medium) Objects with a known model drawn as a marker, or drawn when the build drops them
- a. Vehicle structs (`script_struct_mp_atv` ... `_littlebird`) carry `model` and a def `size` of 96 x 96 x 64, but
  `isModelClass` is misc_model / script_model / dynamic_prop only (scene.cpp:427) and `buildPointEntity` draws the
  generic cube. Fix: treat a point entity with a `model` key and `vehicletype` as a model in scene.cpp:923.
- b. Scriptables: only the definition's first model is drawn (scriptable_catalog.h:36), so multi-part definitions
  (`parts`, doors with frame and leaf) may show one part (UNVERIFIED per definition). A scriptable the build will
  leave out (not verified, Warzone zone on a non-Warzone map, carry preference off; compile_iw8.cpp:2265-2312) looks
  the same as one that is built. The build also accepts the key `definition` (compile_iw8.cpp:2264) which the viewport
  does not read (scriptable_model.h:21). Fix: tint or label left-out scriptables using the same rules (move the
  decision into a core function shared by compile and scene).

### G11 (medium) Animated models: no playback, no name check, inconsistent liveness rule
- `iw8r_anim` is only a literal passed to `scriptmodelplayanim` (map_script.cpp:765-772); nothing checks the xanim
  exists or suits the model, and the camera never plays it (no xanim code under src/viewport or src/qtapp; ROADMAP
  4.21 "camera playback open").
- The build demands that something targets the model (compile_iw8.cpp:3978), while spin / bob only need a
  targetname, and the collision pass already treats any named `iw8r_anim` model as live and gives it no static box
  (compile_iw8.cpp:1873). A named but untargeted animated model gets neither animation nor a collision box.
  Whether the converter keeps it live is UNVERIFIED.
- Fix: align the two rules (either add `iw8r_anim` to `isMoverModel` so it is named and kept like spinners, or drop
  it from `staysEntity`); validate the name against the asset catalogue if one lists xanims; show the anim name as a
  label until playback exists.

### G12 (low-medium) Brush models whose own surfaces do not show in game are drawn normally
- The build's reason for generated visuals is that "their own surfaces did not show in game" (compile_iw8.cpp:
  1279-1282, key help of `iw8r_brush_visual`). With `iw8r_brush_visual 0` the viewport still draws the brushes as
  if visible. Fix: draw such a brush model as a wireframe or ghost and say so in the tooltip / Check Map.

### G13 (low-medium) In-game start state of exploder pieces not shown
- The S&D template places the intact crate and the destroyed crate (`targetname exploder`) at the same spot
  (gamemode_templates.cpp:91-99); the viewport draws both overlapping. In stock behaviour the destroyed piece is
  hidden until the exploder fires (UNVERIFIED in a built map). Fix: draw `targetname exploder` models ghosted.

### G14 (low) Duplicate names from bare prefabs break the generated threads
- A bare prefab placed twice keeps the same targetnames; the build only warns that one trigger drives the others
  (prefab.cpp:583-595). The generated door, lift and teleport threads use `getent(name, "targetname")`
  (map_script.cpp:694, 719, 741, 664), which with two matches is a script error in IW scripts (UNVERIFIED for
  Replay), and two `iw8r_door_N` threads drive the same name. Fix: in compile_iw8.cpp:3809-3938 warn per duplicated
  name used by a behaviour key, or use `getentarray` in the generated threads.

### G15 (low) Smaller mismatches
- Hinge / rotate door default move time: build 0.6 s when not given (compile_iw8.cpp:3870), preview 1 s
  (scene.cpp:306).
- Links drawn only for `target` (scene.cpp:945-946): no line from a teleporter to its `iw8r_teleport` destination,
  or from a brush model to its `_vis` model.
- `iw8r_teleport` accepts any entity with that targetname (compile_iw8.cpp:3893-3894); a script_struct destination is
  not an entity at runtime, so `getent` fails and the teleporter silently does nothing. Warn unless the class is a
  script_origin / script_model.
- `script_check.cpp:11-16` reserved names list misses most generated functions (`iw8r_trigger*`, `iw8r_in_trigger`,
  `iw8r_link_visual`, `iw8r_spin_model`, `iw8r_bob_model`, `iw8r_teleport_N`, `iw8r_jump_N`, `iw8r_damage_N`,
  `iw8r_lift_N`, `iw8r_door_N` above 1, `iw8r_anim_N` above 2, `iw8r_prefab_inst`, `iw8r_pname`, sound / fx helpers).
  A user_main script defining one fails at compile time instead of in Check Script.
- The trigger behaviour keys (`iw8r_teleport`, `iw8r_jump`, `iw8r_damage`, `iw8r_vision`) are offered only on
  trigger_multiple in the defs, but the build accepts them on any `trigger_*` (compile_iw8.cpp:3883); `trigger_hurt`
  reads `spawnflags & 16` (map_script.cpp:330-331) which its defs do not list.
- `iw8r_anim` on a misc_model is silently ignored (compile_iw8.cpp:3974).

## Dead keys and markers

- No `iw8r_*` key in `data/iw8_entity_defs_overrides.json` is unread: every one has a reader in src/.
- Keys with no quoted reference in src/ (passed to `.ents` verbatim by `entityKeys`, so not dead for the build, but
  nothing in the editor interprets them): script_brushmodel / trigger_multiple `physasset`, script_model `lgvmerge`,
  `receivevolumedecals`, `sound_csv_include`.
- Classes the build accepts that the viewport draws as a generic marker although a shape or model is known:
  `trigger_radius` (radius / height), `script_struct_mp_*` vehicles (model, def size), fx_origin with `iw8r_damage`
  (radius). Scriptables with a catalogue model are drawn as the model.

## Test coverage seen

compile_iw8_tests.cpp covers teleport, jump, damage, lift, spin, bob, door + hinge and brush visuals; qt_gui_test.cpp
covers the hinged-door swing, lift and bob in the Behaviour Preview (5173-5364). Not covered: brush-model spin pivot,
prefab-rotated behaviour keys, trigger_radius drawing, trigger_damage semantics, effect orientation, iw8r_anim.

## Fixes (viewport side)

Scene builder only (`src/viewport/scene.cpp/.h`, one call in `camera_geometry.cpp`). Tests:
`tests/viewport_scene_behaviour_tests.cpp` (compiles scene.cpp into iw8r_tests, as texture_decode_tests does, and checks
the scene's items, bounds, colours, line counts and Behaviour Preview poses) and
`tests/viewport_camera_geometry_helpers_tests.cpp` (the kept camera geometry draws the same helper lines).

- **G3.** `trigger_radius` is a cylinder item (`buildPointEntity`, `addCylinder`, `triggerRadiusShape`): radius / height
  (defaults 64 / 72, none when not positive, as `readTriggerVolumes`) from the origin up, 16 sides, `volume` (wire
  outline in the trigger colour, filled only when selected, never a shadow caster or in a capture), picked by its
  bounds; the 2D views draw its rings. The `fx_origin iw8r_damage` cylinder (radius 48 default, 16 below the origin to
  the radius above it) and a door's own use area (bounds grown 40 units to the four sides, when no trigger with a
  targetname targets the door: compile_iw8.cpp 3813-3841) are `SceneCache::helperLines`, drawn by `appendHelperLines`
  (damage always, depth-tested; use area on top while the door is selected). Tests `scene_trigger_radius_cylinder`,
  `scene_fx_damage_cylinder`, `scene_door_use_area_and_teleport_line`, `camera_geometry_draws_behaviour_helpers`.
- **G6.** `BrushPose` holds pitch / yaw / roll (`anglesToMatrix`); `iw8r_spin` on a brush model turns about the centre of
  its brushes' bounds on its own axis (the build side moves spinners to that pivot); the axis word is read
  case-insensitively and an unknown word does not spin (`spinAxisOf`, as the build). Models: `iw8r_lift` is previewed
  (`liftHeight`, shared with brush models) and only misc_model / script_model move (`movesInPreview`, the build's
  `isMoverModel`). Tests `scene_behaviour_brush_spin_axes_about_centre`, `scene_behaviour_model_lift_and_spin`.
- **G10a.** Vehicle spawn structs (`isVehicleStruct`: `script_struct_mp_*` with a `vehicletype`) are drawn as their
  `model` through `modelSource` like other models, else as the definition's box (-48 -48 0 .. 48 48 64; the defs give
  all five vehicles this placeholder size, the real vehicle sizes are not in the tree); not a collision object. Test
  `scene_vehicle_struct_model_or_box`.
- **G10b not done.** Whether the build leaves a scriptable out depends on the scriptable catalogue (verified flag,
  zones, always-loaded), the Real stock scripted objects preference and the Warzone-map test (compile_iw8.cpp
  2261-2312); the scene only has `scriptableModelFor` (definition -> model), so it cannot tell. Needs that decision
  moved into a core function and the catalogue / preference handed to the scene.
- **G12.** A script_brushmodel with `iw8r_brush_visual 0` is a `volume` (outline in its class colour, no textured faces,
  no shadow; still solid in Collision mode). `PrimGeom::look` (classname and that key) rebuilds the brushes when the key
  changes. Test `scene_brush_visual_off_is_an_outline`.
- **G13.** A misc_model / script_model with targetname `exploder` (the S&D template's destroyed crate, unchanged) is drawn
  as a 16-unit marker instead of its model unless `SceneCache::showExploderPieces` (no menu wired). Test
  `scene_exploder_piece_hidden_by_default`.
- **G15 (preview parts).** Hinge / rotate doors without a time move in 0.6 s in the preview (build: compile_iw8.cpp
  3870; slide keeps 1 s). A teleporter trigger with a targetname gets a violet line to its destination's centre, on top
  while the trigger or the destination is selected. Tests `scene_behaviour_hinged_door_default_time`,
  `scene_door_use_area_and_teleport_line`.
