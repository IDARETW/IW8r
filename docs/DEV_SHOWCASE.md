# Dev showcase 2: test sheet

Map: `samples/dev/mp_iw8r_show2.map` (map id `mp_iw8r_show2`). Written by `py -3 -I tools/make_dev_showcase2.py`; run it again to get the same files back.

## Build and play

1. Open `samples\dev\mp_iw8r_show2.map` in the editor.
2. Press **F5**. The editor builds the map, deploys it and starts a Team Deathmatch on it. (F7 builds only; Ctrl+F7 builds and deploys.)
3. You spawn on the south strip and face north. The stations are in 9 rows in front of you; number 01 is the first pad on your left.
4. Headless build, no window: `bin\release\IW8Radiant.exe --compile samples\dev\mp_iw8r_show2.map --out out\showcase2-build`.

The file is not called `iw8r_dev_showcase2.map` because the build takes the map id from the file name and cuts it to 15 characters: that name gives `mp_iw8r_dev_sho`, the id of the first showcase, and a build would overwrite it.

## How to tell where you are

- **Sign**: over the south edge of every pad. The number is made of bright brushes; the group and the name are on the board beside it. Signs face south, so read them walking north.
- **Message**: walk onto a pad and the number and name show in the middle of the screen, once per visit.
- **Editor**: every station has a `trigger_radius` named `s<NN>_<name>` and its other entities start with `s<NN>_` too, so the Outliner reads in station order. Game-mode objects keep the fixed names the game looks for.
- **Pad colour**: one colour per group: world (2), lighting (10), brushes (12), materials (12), models (12), scripted (12), triggers (6), fx + sound (9), nav + cameras (4), stock kinds (4), game modes (10).
- The arena is 2432 x 2784 units; a pad is 192 units; the lighting row is under a roof so the sun does not hide the lights.

Legend: **seen working** = docs/ROADMAP.md or docs/TEST_CHECKLIST.md records that you confirmed it in game. **not seen** = built, never confirmed. **waiting** = needs something that is not built yet.

Write OK or what is wrong next to each number and send the list back.


## World

| No | What it is | Class and keys | In game | State |
|---|---|---|---|---|
| 01 | World settings: sun, stock sky, fog, film grade, sun sprite, map reverb | worldspawn: sundirection, suncolor, skyname, iw8r_fog, iw8r_film, iw8r_sun_sprite, iw8r_reverb | Shipment sky, light fog, a slight film grade. The thin post is a sun dial: its shadow points away from the bright spot in the sky (north-east). Sun disc and flare in the sky: not seen yet. | seen working |
| 02 | Spawns: the Team Deathmatch set on the south strip (2 starts, 6 spawns), free-for-all spawns and a player start here | mp_tdm_spawn, mp_tdm_spawn_allies_start, mp_tdm_spawn_axis_start, mp_dm_spawn, mp_dm_spawn_start, info_player_start: origin, angles | You spawn on the south strip facing the stations. In free-for-all you spawn on this pad. | seen working |

## Lighting

| No | What it is | Class and keys | In game | State |
|---|---|---|---|---|
| 03 | Omni light, white | light: _color, radius, intensity | A round white pool of light on the floor and the barrel. | seen working |
| 04 | Omni lights, coloured | light: _color 1 0.25 0.2 and 0.3 0.5 1, intensity 1.5 | A red pool on the left and a blue pool on the right. | seen working |
| 05 | Spot light aimed by angles | light: fov_outer 60, fov_inner 35, angles 90 0 0 (straight down) | A warm round spot on the floor with a soft edge. | seen working |
| 06 | Spot light aimed at a target | light_spot: target = a script_origin; fov_outer 50, fov_inner 30 | A greenish spot that falls slanted from the south-west corner onto the small bright plate. | not seen |
| 07 | Spot light with an IES profile | light: iw8r_ies lights/show2_spot_narrow.ies (the editor's stock narrow profile) | Two spots that differ only in the profile: the left pool is plain, the right pool is narrower with a bright core. | not seen |
| 08 | Spot light that casts shadows | light: iw8r_shadows 1 (spot lights only), target | The pillar throws a sharp shadow to the east, away from the light. Walk through the beam: you throw one too. | not seen |
| 09 | Light tuning keys | light: iw8r_intensity_units 4 + iw8r_bulb_radius 4 (left); iw8r_falloff, iw8r_fade, iw8r_shadow_bias, iw8r_uv_intensity, iw8r_ir_intensity (right) | Two white pools that differ in brightness and edge. The right one should fade out when you stand more than 300 units away. | not seen |
| 10 | Wide spot lights (fix queue F.9) | light: fov_outer 140 (a real spot) and 160 (150 or more is built as an omni light) | Both sides are lit. If the left side is dark, a wide spot still does not show (F.9). The build log notes the right one. | not seen |
| 11 | Reflection probe pair (fix queue F.10) | reflection_probe: mins, maxs, priority, feather (left); origin only (right) | The left dryer should reflect the red wall, the right dryer the blue wall. In the last playtest probes changed nothing. | not seen |
| 12 | Volumetric box | iw8r_volume: mins, maxs, density 0.4, falloff 64, color | Lit haze inside a 160 unit box, with the spot light showing as a cone in it. Listed as not expected to work yet. | not seen |

## Brushes

| No | What it is | Class and keys | In game | State |
|---|---|---|---|---|
| 13 | Structural and detail brush | worldspawn brush; brush line 'contents detail;' (Ctrl+M) | Two blocks that look alike and both stop you and bullets. Editor: Camera Mode > Contents shows the right one green. | not seen |
| 14 | Brush flags: non-colliding and weapon clip | brush lines 'contents nonColliding;' and 'contents weaponClip;' | You walk through the wooden block (left). The metal block (right) is solid. | not seen |
| 15 | Clip brushes | tool materials clip, clip_player, clip_weapon, clip_nosight (west to east, one over each bright plate) | All invisible. 1: stops you and bullets. 2: stops you only. 3: stops bullets only. 4: walk through; blocks AI sight. | seen working |
| 16 | Ramp and stairs | worldspawn brushes: a 27 degree wedge and six 8-unit steps | Both can be walked up. | seen working |
| 17 | Curved patch, drawn from both sides | worldspawn patch (mesh 3 x 3). worldspawn iw8r_patch_one_sided 1 would turn back faces off | An arch you can walk under from the side. It is visible from outside and from inside. | seen working |
| 18 | Terrain patch | worldspawn patch (mesh 5 x 5) with a stock grass material | A bumpy grass sheet you can walk on. | not seen |
| 19 | Glass pane | script_brushmodel with the built-in material iw8r_glass (Primitives > Scripted Objects > Make Glass Pane) | A see-through pane that shatters when shot. | seen working |
| 20 | Ladder | tool material 'ladder' on a thin slab + script_model with iw8r_no_collision 1 (Make Ladder layout) | Walk into the ladder on the east face of the tower: you climb from the floor to the top, hands on the rungs. | not seen |
| 21 | Mantle ledges | worldspawn brushes 40, 56 and 72 units high (there is no mantle key: any ledge can be mantled) | Jump at each block: you pull yourself up onto it. | seen working |
| 22 | Water volume | tool material 'water' (not solid; Replay has no swim volumes) | A blue rippled box that you walk through. | seen working |
| 23 | Prefabs | misc_prefab: model = a prefab .map of the Prefabs dock (a stock barrel group, the editor's table lamp) | A group of blue barrels on the left; a small table lamp on the floor on the right, with its own light. | not seen |
| 24 | Decal | iw8r_decal: angles 90 0 0 (onto the floor), size 8 96 96, material, opacity, feather, draw_order | Nothing yet. Later: a round orange mark on the floor. | waiting: the converter has no decal producer yet |

## Materials

| No | What it is | Class and keys | In game | State |
|---|---|---|---|---|
| 25 | Stock materials | brush faces with stock names: painted metal, wood, brick | Three textured pillars with normal maps and gloss. | seen working |
| 26 | Stock cut-out material | stock chainlink fence material (alpha-tested) | A fence panel you can see through. | seen working |
| 27 | Custom material | materials/show2/solid.iw8mat.json: color, surface metal, scale | An orange and dark checker block. | seen working |
| 28 | Custom alpha-tested material | show2/cutout: alphaTest true, twoSided true | A grate panel with holes, visible from both sides. | seen working |
| 29 | Custom alpha-blended material | show2/blend: blend true, twoSided true | A half see-through blue panel. | not seen |
| 30 | Additive material | show2/add: blendMode add, unlit true, twoSided true | A soft orange glow you can see through, from both sides. | not seen |
| 31 | Multiply material | show2/multiply: blendMode multiply | A dark stain band in front of the wall: the wall shows through it, darker. | not seen |
| 32 | Unlit material | show2/unlit: unlit true | A checker block with the same brightness on every side, also on the shaded side. | not seen |
| 33 | Emissive material | show2/emissive: emission 8 | A block that glows green. | not seen |
| 34 | Tinted material | show2/tint: the checker image with tint [1, 0.35, 0.35] | Left: the plain checker. Right: the same checker, red. | not seen |
| 35 | Decal layer on a surface | show2/decal on a flat patch that lies exactly on the wall: blend true, sortKey 12, polygonOffset 1 | A round orange mark on the south face of the wall, with no flicker. | not seen |
| 36 | Tool materials | caulk, nodraw, hint, skip (west to east, one over each bright plate) | All invisible. caulk and nodraw are solid; hint and skip are walk-through. | seen working |

## Models

| No | What it is | Class and keys | In game | State |
|---|---|---|---|---|
| 37 | Stock static models | misc_model: model, origin, angles | A barrel, a table and a chair. They block you and bullets. | seen working |
| 38 | Scaled models | misc_model: modelscale 0.5, 1, 1.5 | Three barrels: half size, normal, one and a half. | not seen |
| 39 | Shape models | misc_model: model iw8r_shape_cube_show2 (models folder); right: modelscale .5 + iw8r_material show2/solid | A 64 unit plaster cube you can stand on, and a half-size cube wearing the checker. | not seen |
| 40 | Custom model (OBJ) | misc_model: model show2_crate (models/show2_crate.obj) | Two checker crates, one turned 30 degrees. | seen working |
| 41 | Material on a model | misc_model: iw8r_material = a stock material (middle) or a custom one (right) | Blue barrel, stone barrel, checker barrel. | not seen |
| 42 | One material slot of a model replaced | misc_model: iw8r_mat:tmo/cinderblock_01 = show2/solid | Two pallets of cinder blocks. On the right one only the blocks wear the checker; pallet and wood stay. | not seen |
| 43 | Dynamic props | dynamic_prop: model, mass, material (default; mass 2 rubber; mass 40 metal). North cone: the same model as a misc_model | The three cones in the row fall over and slide when shot or pushed; the right one is heavy. The cone behind them stands still. | seen working |
| 44 | Live script models | script_model: targetname that a script_origin targets (left); iw8r_no_collision 1 (right) | Left barrel is solid. You walk through the right barrel; bullets still hit it. | not seen |
| 45 | Static model flags | misc_model: lod_scale 4; no_shadow 1; ground_lighting 0.8 0.2 0.2 | Three chairs. The left keeps its detail from far away, the middle casts no shadow from lights, the right is lit reddish. | seen working |
| 46 | Animated model | script_model: iw8r_anim desktop_fan_fn_01_loop, iw8r_anim_loop 2; a script_origin targets it | A desk fan on the table. Its blades turn. | not seen |
| 47 | Foliage model (cut-out leaves) | misc_model: a stock bush with alpha-tested leaf materials, modelscale 0.6 | A bush with separate leaves, not flat cards. | not seen |
| 48 | Model angles | misc_model: angles 0 45 0 (yaw), 30 0 0 (pitch), 0 0 30 (roll) | Three chairs: turned, tipped forward, tipped sideways. Compare with the editor camera. | not seen |

## Scripted

| No | What it is | Class and keys | In game | State |
|---|---|---|---|---|
| 49 | Spinners | misc_model: iw8r_spin 4 (left); script_model: iw8r_spin '2 roll' (right) | The barrel turns once every 4 seconds. The floating crate rolls once every 2 seconds. | seen working |
| 50 | Bobber | script_model: iw8r_bob 24 2 | The barrel moves up 24 units and back every 2 seconds. | seen working |
| 51 | Moving brush models | script_brushmodel: iw8r_spin 6 (left), iw8r_bob 24 3 (right); drawn by a generated model | A checker slab that turns and a metal plate that moves up and down. Both are visible and solid. | not seen |
| 52 | Lift | script_brushmodel: iw8r_lift 128 2 2 | Stand on the metal plate: it carries you up 128 units; step off onto the ledge. | not seen |
| 53 | Sliding door | script_brushmodel: iw8r_door 'slide 0 0 96 1.5 3'; a trigger_use with target = the door | Hold Use at the wooden door: it slides up, waits 3 seconds and comes down. It is visible, and it blocks you when shut. | not seen |
| 54 | Rotating door (closes by itself) | script_brushmodel: iw8r_door 'rotate 90 1 3'; a trigger_multiple with target = the door | Walk up to the door from the south: it swings open away from you and shuts after 3 seconds. | not seen |
| 55 | Hinged door | script_brushmodel: iw8r_door 'hinge 100 0.6' (Make Hinged Door); no trigger; iw8r_hinge moves the hinge | Press Use near the door: it swings away from you and stays open. Use again shuts it. Say if you can walk through. | not seen |
| 56 | Teleporter pair | trigger_multiple: iw8r_teleport = targetname of a script_origin (the exit) | Step on the bright plate: you are moved to the north-east corner of the pad and turned to face back. | seen working |
| 57 | Jump pad | trigger_multiple: iw8r_jump 0 0 700 | Step on the bright plate: you are thrown straight up. | seen working |
| 58 | Map script (your own GSC) | worldspawn iw8r_script mp_iw8r_show2.gsc: show2_blink() hides and shows a script_model | The checker crate vanishes for 1 second every 3 seconds. | not seen |
| 59 | HUD message | trigger_use + the Script dock's HUD message code (newhudelem, settext) in the map script | Stand on the plate and press Use: a line of text shows in the middle of the screen for 3 seconds. | not seen |
| 60 | Vision volume | trigger_multiple: iw8r_vision tonemapExposure=2.5 | Step on the big plate: the picture gets much brighter. Step off: back to normal. | not seen |

## Triggers

| No | What it is | Class and keys | In game | State |
|---|---|---|---|---|
| 61 | Hurt trigger (fix queue F.1) | trigger_hurt: dmg 10, spawnflags 16 (once a second) | Stand on the plate: you lose 10 health every second. In the last playtest it fired but did no damage. | not seen |
| 62 | Damage zone (fix queue F.1) | trigger_multiple: iw8r_damage 10 1 | Stand on the plate: you lose 10 health every second. In the last playtest it fired but did no damage. | not seen |
| 63 | Use trigger | trigger_use (Primitives > Triggers > Use) + a message thread in the map script | Stand on the plate: nothing. Press Use: the line 'trigger_use fired' shows. | not seen |
| 64 | Touch trigger | trigger_multiple (Primitives > Triggers > Multiple) + a message thread | Step on the plate: 'trigger_multiple fired' shows. | not seen |
| 65 | Radius trigger | trigger_radius: origin, radius 48, height 72 + a message thread | Walk to the middle of the pad: 'trigger_radius fired' shows. | not seen |
| 66 | Other trigger classes | trigger_use_touch (left); trigger_damage dmg 5 spawnflags 16 (right) | Left plate: press Use for 'trigger_use_touch fired'. Right plate: 'trigger_damage fired' and 5 damage a second. | not seen |

## Fx + sound

| No | What it is | Class and keys | In game | State |
|---|---|---|---|---|
| 67 | Stock effects that keep running | fx_origin: fx = a stock vfx name, no delay | A small fire on the left, light smoke on the right. | seen working |
| 68 | Stock effects on a timer | fx_origin: delay 6 (explosion), delay 2 (sparks) | An explosion every 6 seconds, sparks every 2 seconds. | seen working |
| 69 | Effect with sound and damage (fix queue F.6) | fx_origin: soundalias show2_hum (the map's own loop), iw8r_damage '10 48 0.5' | A fire that hums and hurts within 48 units (10 damage twice a second). The sound was not heard in the last playtest. | not seen |
| 70 | Authored effect | fx_origin: fx show2/sparkle (fx/show2/sparkle.iw8fx.json, made for Tools > Effect Editor) | A fountain of orange sparks with a little grey smoke, replayed about once a second. | not seen |
| 71 | Stock sound, looping | sound_emitter: soundalias emt_smokestack_lp, loop 1 | A smokestack rumble that gets louder as you walk in. | seen working |
| 72 | Stock sound, repeating one-shot | sound_emitter: soundalias emt_rock_med_debris_short, loop 0, delay 4 | Falling debris every 4 seconds. | seen working |
| 73 | Your own sounds (fix queue F.6 / F.7) | sound_emitter: soundalias show2_hum loop 1 (sounds/show2_hum.wav); show2_chirp delay 3 | A steady 220 Hz hum on the left, a rising chirp every 3 seconds on the right. | not seen |
| 74 | Reverb zone (fix queue F.7) | trigger_multiple: iw8r_audio_zone, iw8r_reverb iw8_lg_tunnel, iw8r_audio_priority 40, iw8r_audio_interior 1 | Fire a shot on this pad, then one on the walkway: on the pad it rings like a tunnel, outside it sounds open. | not seen |
| 75 | Ambient zone | trigger_multiple: iw8r_audio_zone, iw8r_ambient show2_bed, iw8r_ambient_oneshots '3 6 show2_chirp' | On this pad only: a low drone, and a chirp every 3 to 6 seconds. Both stop when you leave. | not seen |

## Nav + cameras

| No | What it is | Class and keys | In game | State |
|---|---|---|---|---|
| 76 | Path nodes | node_pathnode: origin (nine nodes; fixed class, no targetname) | With bots: they walk across this pad. The nodes themselves are invisible. | seen working |
| 77 | Traversal (jump down) | node_negotiation_begin: target, animscript traverse_jumpdown_40; node_negotiation_end: targetname | With bots: they jump down from the 40 unit block. | seen working |
| 78 | Match cameras | mp_global_intermission: angles; script_struct camera_intro -> camera_intro_path (Primitives > Game Modes > Match Cameras) | At the end of a match the camera hangs 288 units over this pad and looks south over the arena. | not seen |
| 79 | Minimap corners | two script_origin with targetname minimap_corner, at the south-west and north-east arena corners | The minimap shows the whole arena, north up. | not seen |

## Stock kinds

| No | What it is | Class and keys | In game | State |
|---|---|---|---|---|
| 80 | Killstreak markers (stock kinds, fix queue F.14) | script_origin airstrikeheight and heli_leave; script_struct nuke_expl_pos; script_struct_heli ks_heli_entrance x4 -> ks_heli_goal x4 (script_linkto / script_linkname loop). All inside the arena at z 400 to 440: stock maps put them far outside, but the build's map bounds include every point entity | Nothing on the pad. Call a helicopter or an air strike and say what flies where: both should stay low over the arena. | not seen |
| 81 | Airdrop crate and pallet (stock kind) | script_model targetname airdrop_crate, target = a script_brushmodel with script_gameobjectname airdrop_pallet | A beige plastic case on a thin wooden plate. Stock maps keep this pair outside the map; what the game does with it is not known. | not seen |
| 82 | Callout area and battle chatter trigger (stock kinds) | trigger_multiple targetname callout_area, script_noteworthy center (left); trigger_multiple_bcs_center (right; all 21 trigger_multiple_bcs_* classes work the same way) | Left plate: the location name under the minimap may read 'center'. Right plate: soldiers may call out 'center' in a match with bots. | not seen |
| 83 | Boundary triggers (stock kinds) | trigger_multiple with targetname OutOfBounds (north-west plate), kill_border_trigger (north-east), dcover_blocker (south-west), white_phosphorus_damage (south-east) | Step on each plate for a moment and say what happens: an out-of-bounds warning, death, or nothing. | not seen |

## Game modes

| No | What it is | Class and keys | In game | State |
|---|---|---|---|---|
| 84 | Domination: flags A, B, C and the spawn set | trigger_radius x3: script_gameobjectname 'dom onslaught', script_label _a/_b/_c, targetname flag_primary, radius 120, height 128, nophysics true; mp_dom_spawn x2, mp_dom_spawn_allies_start, mp_dom_spawn_axis_start | In Domination: three flags to capture at the small plates. In Team Deathmatch: nothing (the game removes other modes' objects). | not seen |
| 85 | Search and Destroy: sites A and B, the bomb, spawns | per site: trigger_use_touch bombzone (script_label, script_bombmode_original 1, target) -> script_model crate (script_exploder, target) -> trigger_use_touch defuse; destroyed crate (targetname exploder); script_model sd_bomb; trigger_multiple sd_bomb_pickup_trig; mp_sd_spawn_attacker, mp_sd_spawn_defender | In Search and Destroy: a crate at each end to plant on, the briefcase bomb in the middle. In Team Deathmatch: nothing. | not seen |
| 86 | Capture the Flag: both flags, zones, spawn set | per team: trigger_radius ctf_trig_<team> (target), script_model ctf_flag_<team> (highway_flag0, spawnflags 4), trigger_radius ctf_zone_<team>; all script_gameobjectname ctf; mp_ctf_spawn x2 and both starts | In Capture the Flag: a flag on each half of the pad. In Team Deathmatch: nothing. | not seen |
| 87 | Spawn classes of the other modes | one of each: mp_tdm_spawn_secondary, mp_dm_spawn_secondary, mp_dom_spawn_secondary, mp_ctf_spawn_secondary, mp_arena_spawn_allies_start, mp_arena_spawn_axis_start, mp_ball_spawn, mp_ball_spawn_allies_start, mp_ball_spawn_axis_start, mp_ball_spawn_secondary, mp_cyber_spawn_allies, mp_cyber_spawn_axis, mp_dd_spawn_attacker, mp_dd_spawn_attacker_a, mp_dd_spawn_attacker_b, mp_dd_spawn_attacker_start, mp_dd_spawn_defender, mp_dd_spawn_defender_a, mp_dd_spawn_defender_b, mp_dd_spawn_defender_start, mp_front_spawn_allies, mp_front_spawn_axis, mp_koth_spawn, mp_koth_spawn_allies_start, mp_koth_spawn_axis_start, mp_koth_spawn_secondary, mp_rugby_spawn_allies, mp_rugby_spawn_allies_secondary, mp_rugby_spawn_allies_start, mp_rugby_spawn_axis, mp_rugby_spawn_axis_secondary, mp_rugby_spawn_axis_start (origin, angles) | Nothing to see. In a mode that uses one of these classes you can spawn on this pad. | not seen |
| 88 | Headquarters radio (stock kind) | script_model targetname hqloc (military_hq_crate_01_proxy, spawnflags 4, target) -> script_brushmodel; both script_gameobjectname hqloc | In Headquarters: a radio crate to capture. In Team Deathmatch: nothing. | not seen |
| 89 | Grind bank (stock kind) | trigger_radius: targetname grind_location, script_gameobjectname grind, script_label a, nophysics true | In Grind: a place to bank tags. In Team Deathmatch: nothing. | not seen |
| 90 | Hardpoint: five zones | trigger_multiple x5: script_gameobjectname hardpoint, script_label 1..5, targetname hardpoint_zone (each zone is one 192 x 192 x 128 cell of this pad) | In Hardpoint: the zone moves along the five cells of this pad, west to east. In Team Deathmatch: nothing. | not seen |
| 91 | Cyber Attack: EMP, pickup, two zones (stock kind) | script_model cyber_emp (modelscale 2) + trigger_use cyber_emp_pickup_trig; per team: trigger_use_touch cyber_empzone_<team> (target = crate), trigger_use_touch cyber_emp_defuse_<team>, script_model crate; all script_gameobjectname cyber | In Cyber Attack: an EMP device at the north edge and a crate on each half. In Team Deathmatch: nothing. | not seen |
| 92 | Demolition: two bomb sites (stock kind) | per site: trigger_use_touch targetname dd_bombzone (script_label, script_bombmode_original 1, target) -> script_model crate (script_exploder, target) -> trigger_use_touch defuse; all script_gameobjectname dd_bombzone | In Demolition: a bomb site crate on each half. In Team Deathmatch: nothing. | not seen |
| 93 | Flag goals and a ground zone (stock kinds) | trigger_radius targetname flag_goal_defender, flag_arena, flag_goal_attacker (radius, height 128, nophysics true); trigger_multiple script_gameobjectname grnd, script_label 1, targetname grnd | Used by the arena and drop-zone style modes; which one reads them is not known. In Team Deathmatch: nothing. | not seen |

## Not on the map

| Kind | Why |
|---|---|
| Stock clip brush models (clip*, player*, mantle*, mount*, ladder*, nosight*, stairs*, tactical_*_col, smoke_grenade_sight_clip_*, care_package_col) | stock maps keep these as collision templates about 9,000 units outside the playable area; a brush that far away would stretch the map bounds, the light grid and the minimap |
| Helicopter height mesh (script_brushmodel heli_height_mesh / heli_pilot_mesh) | a brush 2,400 units up would stretch the map bounds the same way |
| Tactical ladder marker (script_struct, model misc_vm_tactical_ladder) | it marks one of the far-away clip templates above |
| Search and Destroy / Demolition / Cyber site body brushes and dd_bombzone_clip_* | their role is unknown and the editor's own templates leave them out |
| Rugby end zones and juggernaut crate, infil structs and doors, export_volume, collmap_scriptable, node_3d_pathnode | the build does not accept these classes (buildAccepted is false) |
| Weapon pickup and drop-zone flare structs (script_struct weapon_pickup / dz_flare with a weapon model) | the build check stops on a model name that is not in the build, and weapon models are not built as copies |
| triggermodeldummydefault | the build accepts the class, but in stock data it is only a physics asset name, never an entity |
| pf*_auto* chain structs and delete_on_load structs | generated link names and stock clean-up markers; not something a mapper places |
| func_group | editor-only grouping; nothing reaches the game |
| Mantle key or brush | the editor has none: any ledge can be mantled (station MANTLE) |
| Killstreak templates | the editor has no templates for them; the stock markers are on the KILLSTREAK MARKERS station instead |

## Notes

- Near-identical classes share a station: all spawn classes of the other modes are on OTHER MODE SPAWNS; the 21 `trigger_multiple_bcs_*` classes are shown by one of them on CALLOUT / BATTLE CHATTER; `light` and `light_spot` take the same keys.
- Game-mode stations (teal) show nothing in Team Deathmatch: the game removes objects of other modes. To see them, start that mode with Live Link > Quick match. Picking other modes in the lobby needs the client with the game-mode list (fix queue F.11).
- The message uses the Script dock's "Trigger message" code. The editor has no key for it, so the map script (`mp_iw8r_show2.gsc`) starts one thread per station; it asks `iw8r_trigger()` for the trigger, as the user guide says.
- Expected build warnings: the build ends with `preflight: 0 error(s)` and every `Replay:` line says `0 failed`. These lines are expected: the ladder model 'is built as a static prop' (the Make Ladder layout always gets this note; station 20); three screen materials of the bomb and EMP models 'build grey' (the game files have no colour image for them); the 160 degree spot 'built as an omni light' (station 10 is there to show it); one misc_model traffic cone 'stands still' (on purpose, station 43); the decal, the volumetric box, the dynamic props, the authored effect, the sound zones and the map's own sounds are reported as not verified in game; `show2_hum` 'may be silent' on the fire effect (it is the map's own sound; station 69 tests it); 30 sky faces 'not rendered' (the sky comes from the sky asset); the converter says it drops `trigger_damage` and the editor's `iw8r_` keys (the map script handles both) and that the four stock effects have 'no extracted IW3 FX graph' (they are stock effects and play by name).
