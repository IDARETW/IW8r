# Entity keys reference

IW8 Radiant adds its own entity keys, all starting with `iw8r_`, that control how a map is built and how its objects
behave in the game without any scripting. Set them in the Entity inspector like any other key; the inspector also
shows help for each key, and Help > Entity Class Reference describes every entity class.

Values in quotes are a single string. Times are in seconds and distances in map units.

## light and light_spot

A light with a `target` or a `fov` key is a spot. Both classes take the same keys below.

| Key | Value format | Example | What it does |
|---|---|---|---|
| `iw8r_ies` | path to an LM-63 `.ies` file, absolute or relative to the map folder | `lights/cone_profile.ies` | Sets the spot's light profile from an IES file. |
| `iw8r_bulb_radius` | float, above 0 (default 1) | `2` | Bulb radius. Widens the light's volume. |
| `iw8r_bulb_length` | `x y z` | `24 0 0` | Tube/linear light bulb axis half-vector in world units. |
| `iw8r_shadow_bias` | float (default 0.75) | `0.75` | Shadow map bias. |
| `iw8r_shadow_near_bias` | float (default 0) | `6` | Shadow near-plane bias (`shadowNearPlaneBias`). |
| `iw8r_shadow_softness` | float (default 0) | `0.75` | Shadow penumbra softness. |
| `iw8r_shadow_area` | float (default 0) | `0.003` | Shadow area parameter (`shadowArea`). |
| `iw8r_dynamic_shadows` | 0 or 1 (default 0) | `1` | Enables dynamic shadow casting (`needsDynamicShadows`). |
| `iw8r_up` | `x y z` | `0 1 0` | Explicit local light up axis for oriented IES/area spots. |
| `iw8r_falloff` | float, above 0 (default 0.2) | `0.2` | Distance falloff. |
| `iw8r_fade` | `near far` (default 0 0) | `0 400` | Fade range for the light. |
| `iw8r_shadows` | 0 or 1 (default 0) | `1` | 1 gives the light a shadow map. Spot lights only; an omni ignores it. |
| `iw8r_intensity_units` | float, 0 or more (unset) | `4` | Intensity in light units. When set, the radius brightness calibration is skipped. When unset, it is calibrated from the radius. |
| `iw8r_uv_intensity` | float (default 0) | `0.5` | UV vision intensity. |
| `iw8r_ir_intensity` | float (default 0) | `0.5` | Thermal vision intensity. |
| `iw8r_volumetric` | 0 or 1 (default 0) | `1` | 1 makes the lamp light the volumetric air: a glow around it and beams in fog. It shows only when the map has volumetric air (worldspawn `iw8r_haze`, or `volumetricAirDensity` in `iw8r_vision`). |

## dynamic_prop

A physics prop. The build writes it as clutter that falls and can be knocked around.

| Key | Value format | Example | What it does |
|---|---|---|---|
| `iw8r_material` | material name | `tm2co/me_curb_stone_01` | Puts one material on every surface of the model. Drag a material from the Materials browser onto the model. The build makes a copy of the stock model with that material. Custom models keep their own materials. |

## misc_model

A static model. The keys `iw8r_spin` and `iw8r_bob` make it a live script model, and the build fills in a missing `targetname`.

| Key | Value format | Example | What it does |
|---|---|---|---|
| `iw8r_spin` | `<seconds per turn> [yaw\|pitch\|roll]` | `4` or `2 roll` | Turns the model one full circle every given number of seconds. |
| `iw8r_bob` | `<height> [seconds]` (seconds default 2) | `16 2` | Moves the model up by the height and back down, once every given number of seconds. |
| `iw8r_material` | material name | `tm2co/me_curb_stone_01` | Puts one material on every surface of the model. Custom models keep their own materials. |
| `iw8r_no_collision` | `1` | `1` | The model has no player collision. It stays a live `script_model`, and bullets still hit it. A `misc_model` with this key is built as a `script_model`. Make Ladder uses it for the ladder model. |
| `iw8r_mat:<material>` | material name (the new material) | `iw8r_mat:tm2co/me_curb_stone_01` = `tm2co/me_curb_stone_02` | Replaces one material slot of this model. The key names the model's material to replace, and the value is the new material. Slot keys win over `iw8r_material`. Set with Tools > Replace One Material of the Selected Model. |

## script_brushmodel

A brush model that scripts use. It is referenced as `*N`.

| Key | Value format | Example | What it does |
|---|---|---|---|
| `iw8r_door` | `rotate <yaw> [time] [hold]` or `slide <x> <y> <z> [time] [hold]` | `rotate 90 1 3` or `slide 0 0 96 1.5 3` | A door. It needs a `targetname`, and a `trigger_use` or `trigger_multiple` with its own `targetname` whose `target` is this door. A player opens it, and it closes after the hold time. |
| `iw8r_hinge` | `far` or `x y z` | `far` or `128 64 0` | Where a hinged door (`iw8r_door rotate`) or spinning brush model (`iw8r_spin`) turns. Without it, a door hinges on the lower end of its longer side and a spinner turns around its brush centre. |
| `iw8r_pivot` | `x y z` | `128 64 0` | Explicit rotation pivot point for `iw8r_spin` on a `script_brushmodel` (takes precedence over `iw8r_hinge` and brush centre). |
| `iw8r_lift` | `<dz> [time] [wait]` | `128 2 2` | A lift. It needs a `targetname`. It moves up `dz` units over `time` seconds, waits, moves back down, and repeats. |
| `iw8r_spin` | `<seconds per turn> [yaw\|pitch\|roll]` | `4` or `2 roll` | Turns the brush model one full circle every given number of seconds. |
| `iw8r_bob` | `<height> [seconds]` (seconds default 2) | `16 2` | Moves the brush model up by the height and back down, once every given number of seconds. |
| `iw8r_brush_visual` | `0` or `1` (default 1) | `0` | A moving brush model is drawn by a generated model that the map script links to it. Its own surfaces did not show in game. `0` turns the generated model off for this brush model. |

## script_model

A point model that a script places. The model must be live, so it needs a `targetname` that something targets.

| Key | Value format | Example | What it does |
|---|---|---|---|
| `iw8r_anim` | stock animation name (xanim) | `desktop_fan_fn_01_loop` | Plays a stock animation on this model in game. |
| `iw8r_anim_loop` | float, seconds (default 0) | `2` | Replays the animation every N seconds. 0 plays it once. |
| `iw8r_spin` | `<seconds per turn> [yaw\|pitch\|roll]` | `4` or `2 roll` | Turns the model one full circle every given number of seconds. |
| `iw8r_bob` | `<height> [seconds]` (seconds default 2) | `16 2` | Moves the model up by the height and back down, once every given number of seconds. |
| `iw8r_material` | material name | `tm2co/me_curb_stone_01` | Puts one material on every surface of the model. Custom models keep their own materials. |
| `iw8r_no_collision` | `1` | `1` | The model has no player collision. It stays a live `script_model`, and bullets still hit it. A `misc_model` with this key is built as a `script_model`. Make Ladder uses it for the ladder model. |
| `iw8r_mat:<material>` | material name (the new material) | `iw8r_mat:tm2co/me_curb_stone_01` = `tm2co/me_curb_stone_02` | Replaces one material slot of this model. The key names the model's material to replace, and the value is the new material. Slot keys win over `iw8r_material`. Set with Tools > Replace One Material of the Selected Model. |

## trigger_multiple

A brush trigger volume. The keys below make it a teleporter, jump pad, damage zone, vision volume or audio zone. Most of them need the trigger to have a `targetname`.

These keys work on `trigger_multiple`.

| Key | Value format | Example | What it does |
|---|---|---|---|
| `iw8r_vision` | `name=value; ...` (Replay vision fields) | `tonemapExposure=2.5` | Changes the vision and fog inside the volume. A player inside sees it. Leaving restores the map's vision. |
| `iw8r_teleport` | targetname of a destination (`script_origin`) | `tp_exit_1` | A player who touches the trigger moves to the destination's origin and angles. The destination needs its own `targetname`. |
| `iw8r_jump` | `<vx> <vy> <vz>`, launch velocity in units per second | `0 0 700` | A jump pad. A player who touches the trigger is launched. |
| `iw8r_damage` | `<amount> [interval]` (interval default 1) | `10 1` | A damage zone. A player inside takes the amount of damage every interval. |
| `iw8r_audio_zone` | lower-case zone name | `hall` | Inside this trigger's convex volume (axial or slanted), the zone's reverb (`iw8r_reverb`) and ambient audio apply. |
| `iw8r_reverb` | `<room> [dry wet fade]` | `iw8_md_room` | The reverb room type. Use one of the 59 stock room types. The build writes the map sound table. |
| `iw8r_audio_priority` | float (default 30) | `50` | Priority where zones overlap. The higher number wins. The map-wide zone is 0. |
| `iw8r_audio_interior` | 0 or 1 (default 0) | `1` | 1 uses the stock "interior" occlusion and filter presets and sets `exterior = 0`. 0 uses the stock "default" presets (`exterior = 1`). |
| `iw8r_ambient` | map sound or stock alias | `wind_bed` | A 2D loop that plays while a player is in the zone. Supports both custom map sounds and stock aliases. |
| `iw8r_ambient_oneshots` | `<min s> <max s> <sound>[:weight] ...` | `6 12 bird_call:3 dog_bark` | One of the listed sounds (custom or stock) plays at random every min to max seconds while a player is in the zone. |

## script_origin (audio helpers)

| Key | Value format | Example | What it does |
|---|---|---|---|
| `iw8r_audio_blend` | `<zoneA> <zoneB>` | `mp_mymap hall` | Places a reverb/ambient blend transition node between two audio zones at this `script_origin`. |
| `iw8r_audio_pa_speaker` | speaker name | `pa_lobby` | Registers a `MapEnts` audio PA speaker at this `script_origin`. |

## worldspawn

The map's global settings. There is one worldspawn per map.

| Key | Value format | Example | What it does |
|---|---|---|---|
| `iw8r_bake_lightmaps` | `1` to bake | `1` | Bakes a lightmap for the world when the map is built: the sky light and the bounce of the sun and lights, plus where the sun is blocked. The game still draws the sun and the lights live on top. |
| `iw8r_bake_sky` | `r g b intensity` | `0.55 0.65 0.8 0.35` | Sets the sky light used by the bake. The default is a pale blue at 0.35 of the sun (`suncolor`). |
| `iw8r_lightmap_texel` | world units per texel (default 16) | `8` | Sets the lightmap's detail. Smaller is sharper and bigger. The build makes it coarser by itself if the atlas would pass 2048 x 2048. |
| `iw8r_bake_rays` | sky rays per texel (default 32, at most 1024) | `64` | More rays make a smoother ambient but a slower bake. |
| `iw8r_sun_shadow_range` | two cascade multipliers (default `3 2`), or `0` | `4 3` or `0` | Sets how far sun shadows reach. Sent to every player. Longer values push sun shadows further. `0` turns it off. |
| `iw8r_reverb` | `<room> [dry wet fade]` | `iw8_ext_open_space_med_1` | The map's reverb everywhere outside audio zones. Uses one of the 59 stock room types. |
| `iw8r_audio_interior` | 0 or 1 (default 0) | `1` | 1 uses the stock "interior" occlusion and filter presets. 0 uses the stock "default" presets. |
| `iw8r_ambient` | name of one of the map's own sounds | `factory_hum` | An ambient bed: a 2D loop played while a player is in the zone. |
| `iw8r_ambient_oneshots` | `<min s> <max s> <sound>[:weight] ...` | `6 12 bird_call:3 dog_bark` | Sounds that play at random every min to max seconds. |
| `iw8r_script` | GSC file, relative to the map folder | `mp_mymap_logic.gsc` | The map's own script. It must define `user_main()`, which runs after the load script. Edited in the Script dock. |
| `iw8r_mode_dvars` | `name=value; name=value` | `scr_br_teamsize=3; loot_table_name=mp/loot_item_defs_s3.csv` | Game mode settings the map's script sets at every start (Tools > Game Modes > Carry in this map). `loot_table_name` also picks the loot items a Warzone map carries. |
| `iw8r_mode_rules` | `health=<1..1000>; speed=<0.2..3>; weapon=<name>; ammo=infinite` | `health=50; weapon=iw8_pi_golf21_mp` | Rules applied to every player at each spawn by the map's script, which runs after the game type's start. |
| `iw8r_gametype_script` | GSC file beside the map; its file name is the game type name | `gametypes/teamkills.gsc` | A game type of the map's own. The build checks it and compiles it into the map zone; `map.json` lists the name. |
| `iw8r_gametypes` | game type names, spaces between them | `war dm dom` | The game types `map.json` lists, instead of the detected ones. |
| `iw8r_br` | words `nocircle`, `nogulag`, `skipdrop` | `nocircle nogulag skipdrop` | What a small map switches off in the Warzone (br) game type; also marks the map as a Warzone map. |
| `iw8r_loot_zone` | lower-case name (default `map_quarry`) | `tutorial_scavenger` | The loot zone name of the map's loot points when no `iw8r_loot_zone` brush entity names one. The loot tables pick the loot by it. |
| `iw8r_no_loot` | 0 or 1 | `1` | 1 writes an empty loot list (all loot of a stock Warzone map is removed on Rebuild Stock Zone). |
| `iw8r_stock_retexture` | `from=to; from=to` (material names) | `tm2co/a=tm2co/b` | Stock maps: world material swaps on Rebuild Stock Zone; each new material is carried from its catalogue zone. |
| `iw8r_sky_from` | a stock map id | `mp_backlot2` | Stock maps: use that map's sky (File > Stock Map > Relight Stock Map Sky); Rebuild Stock Zone carries it in. |
| `iw8r_sun_color` | linear `r g b`, 0 to 16 | `1 0.55 0.35` | Stock maps: the sun's new colour (File > Stock Map > Relight Stock Map Sun); Rebuild Stock Zone writes it. |
| `iw8r_sun_intensity` | number 0 to 1000000 | `7.5` | Stock maps: the sun's new intensity (stock: mp_shipment 8.6, mp_br_tut 30.7). |
| `iw8r_sun_direction` | `pitch yaw` in degrees, as `sundirection`; pitch -90 to -1 (negative = above the horizon) | `-45 120` | Stock maps: the sun's new direction (File > Stock Map > Relight Stock Map Sun; stock mp_shipment `-20.261 -15.4`). Rebuild Stock Zone turns the sun light, the vision's sun fog and the worldspawn `sundirection`, and bakes the map's sun shadow again. The lightmaps and light grid keep the old sun's bounce light, the sky texture its old sun. A shadow too big for a wide map is cropped to the play space and a margin. Warzone maps (streamed sun shadow) are refused. |
| `iw8r_remove_world_sun` | `rebake` (default) or `keep` | `keep` | Stock maps with `iw8r_remove_world` boxes: `rebake` bakes the map's sun shadow again at the stock sun direction without the removed triangles on Rebuild Stock Zone. `keep` leaves the stock sun shadow, so the removed geometry still casts its baked shadow. |
| `iw8r_collision` | `auto`, `one` (or `full`) or `tiles` | `tiles` | How the build keeps the map's collision: in one piece or as the game's streamed tiles. `auto`: tiles for a map wider than 20,000 units. |
| `iw8r_sun_sprite` | `<size 1..64> [flare alpha 0..1] [blind darken 0..1] [glare lighten 0..1]` | `16 0.5` | Draws a sun disc in the sky in the sun direction. Adds a lens flare when the player looks at it, plus blind and glare strength. |
| `iw8r_airspace` | `<up> [out]` in units (default `131072 131072`), or `0` | `16384 8192` | How far the map's visibility cell and occlusion data reach above (first number) and around and below (second number) the map's geometry. Aircraft killstreaks and their cameras fly there; from a camera outside it the game draws no players or vehicles. `0` keeps the tight cell. |
| `iw8r_haze` | `<preset> [amount]`; presets `desert`, `clear`, `overcast`, `dusk`, `fog`; amount 0.1 to 4 (default 1) | `desert 0.7` | The game's own fog, sky blend and volumetric air for a built map (Compile > Atmosphere). The amount scales how thick it is. `iw8r_fog` is left out of the map script when a haze is set; fields in `iw8r_vision` win over the preset. |
| `iw8r_sky_brightness` | float 0.1 to 16 (default 1) | `3` | Brightness of the built sky (Compile > Sun & Sky). 2 doubles it (its brightest parts go to white), 0.5 halves it. |
| `iw8r_mount_edges` | `1` (default), `brushes` or `0` | `brushes` | Mount edges: where a player can rest a weapon on cover (open outside wall corners at least 32 units tall and ledge tops at least 16 units long). `1`: on the map's visible solid brushes and on placed models' collision hulls; `brushes`: brushes only; `0`: none. |

## fx_origin

A placed effect. The build writes it into the map script, and it plays the effect in game.

| Key | Value format | Example | What it does |
|---|---|---|---|
| `soundalias` | one of the map's own sounds, or a stock alias | `fire_loop` | Plays a sound at the effect. It loops unless `sound_loop` is 0. |
| `sound_loop` | 0 or 1 (default 1) | `0` | 1: the sound loops. 0: the sound plays once each time the `delay` passes, or once only when there is no delay. |
| `iw8r_damage` | `<amount> [radius] [interval seconds]` (radius default 48, interval default 1) | `10 48 0.5` | The effect hurts. A player within the radius takes the damage every interval. No trigger is needed. |

## misc_prefab

These keys are set on a prefab placement.

| Key | Value format | Example | What it does |
|---|---|---|---|
| `iw8r_ovr:<targetname \| class=<class> \| *>:<key>` | the value for that key | `iw8r_ovr:class=light:radius` = `300` | Sets a key on the matching entities inside this prefab instance. Applied in the views and in builds. File > Prefabs > Prefab Parameters lists the keys. |
| `iw8r_pid` | placement id, set by the editor | `p7` | The placement id of a bundle placement: names local to the prefab become `<pid>_<name>` in the build, so two placements never share one. |
| `angles` | `pitch yaw roll` | `0 90 0` | Turns the placement. A wall bundle gets its yaw from the wall it is placed on; File > Prefabs > Rotate Selected Prefab adds the bundle's yaw step. |
| `iw8r_mat:<from>` | material name | `iw8r_mat:tm2co/me_curb_stone_01` = `tm2co/me_curb_stone_02` | Swaps the material `<from>` for the given material in this instance. Used for variants. |

## Warzone loot entities

| Class / key | Value format | Example | What it does |
|---|---|---|---|
| `iw8r_loot_point` `loot_cluster` | `free`, `cache`, a cluster number, or empty (auto) | `cache` | A loot point. `cache` makes a supply box point. On a stock map, a number keeps the point in that cluster while it stays inside its box. |
| `iw8r_loot_point` `loot_zone` | a zone number, or empty (auto) | `3` | The loot zone of a stock map's point (the first zone box that holds it when left empty). |
| `iw8r_loot_cluster` (brush entity) | its brushes give the box | | A new loot cluster: the points inside it join it (stock maps and your own maps). |
| `iw8r_loot_zone` (brush entity) `name` | lower-case zone name | `map_quarry` | A new named loot zone; use a name the loot tables know. |

## Stock world removal

`iw8r_remove_world` is a brush entity for stock maps. Give its brushes a tool
texture. Each brush or patch is one box. File > Stock Map > Rebuild Stock Zone takes out the stock world geometry inside the boxes:
every world triangle whose three vertices are all inside a box stops drawing, and the world collision triangles and
brush hulls inside it go. A triangle that reaches outside the box stays, so make the box a little larger than the
object, but keep the ground out of it. Props (static models) are not world geometry; hide them with Hide Stock Props
In Selection. The entity never goes into the entity text. If removed geometry was part of the map's
occlusion data, the rebuild replaces that data so that nothing behind the removed object is hidden.

| Class / key | Value format | Example | What it does |
|---|---|---|---|
| `iw8r_remove_world` (brush entity) | its brushes give the boxes | | Removes the stock world geometry inside each box (drawing and collision). |
| `iw8r_remove_world` `render` | 0 or 1 (default 1) | `0` | 0 keeps the triangles drawn: only the collision goes (a walk-through wall). |
| `iw8r_remove_world` `collision` | 0 or 1 (default 1) | `0` | 0 keeps the collision: only the drawing goes (an invisible wall). |

## Custom material definition (`.iw8mat.json`)

One file per material, `<name>.iw8mat.json`, in the map's `materials` folder (File > New Custom Material writes it).
Only `color` is required. Image names are relative to the definition file.

| Field | Value format | Example | What it does |
|---|---|---|---|
| `name` | material name: letters, digits, `_ . / -`; no `..`, no leading or trailing `/`; not under `mc/` | `custom/brick_red` | The name the map uses. Default: the file's path under the materials folder, without `.iw8mat.json`. |
| `color` | image file (PNG, TGA, JPG, BMP or uncompressed DDS) | `brick_red.png` | Colour image. Required. |
| `normal` | image file | `brick_red_n.png` | Normal map. |
| `specular` | image file | `brick_s.png` | Reflectance (RGB). Its alpha is the gloss. |
| `detail` | image file | `brick_detail_n.png` | Detail normal map blended over the base normal map. |
| `mask` | image file | `brick_mask.png` | Mask image for detail/opacity blending. |
| `detailScale` | `[u, v]` | `[4, 4]` | UV tiling scale for the detail map. |
| `uvAnim` | `[scrollU, scrollV]` | `[0.06, 0.03]` | Continuous UV scroll velocity (used by animated surfaces such as water). |
| `maskUvAnim` | `[scrollU, scrollV]` | `[0.02, -0.01]` | Continuous UV scroll velocity for the mask texture. |
| `transmission` | `[r, g, b]` | `[0.4, 0.6, 0.2]` | Back-lit transmission colour for foliage, cloth, and thin surfaces. |
| `emissiveColor` | `[r, g, b]` | `[1.0, 0.7, 0.3]` | Emissive RGB multiplier (`replayEmission`). |
| `water` | `true` or `false` | `true` | Marks the material as a water surface with reflective translucent water shading. |
| `surface` | surface type name (from the game's surface list) | `brick` | Sets the footstep and bullet sounds. An unknown name is an error. |
| `alphaTest` | `true` or `false` | `true` | The colour alpha is a cut-out mask (fences, foliage). |
| `blend` | `true` or `false` | `true` | The colour alpha is coverage (decals, glass). Same as `"blendMode": "alpha"`. |
| `twoSided` | `true` or `false` | `true` | Drawn from both sides (no back-face culling). |
| `scale` | `[width, height]`, above 0, up to 65536, in map units | `[128, 128]` | Default texture size in the editor. |
| `blendMode` | `"alpha"`, `"add"` or `"multiply"` | `"add"` | `add` is additive (glows). `multiply` darkens (stains). `alpha` is the same as `blend`. |
| `unlit` | `true` or `false` | `true` | Takes no light from the map. For signs and screens. |
| `emission` | number, 0 to 64 | `2` | Glow factor. Above 1 makes the surface glow. |
| `tint` | `[r, g, b]`, each 0 to 4 | `[1, 0.6, 0.6]` | Multiplies the colour image. |
| `sortKey` | whole number, 0 to 40 | `12` | Decal layer, 9 to 40. Higher draws on top. 0 is the default. |
| `polygonOffset` | 0, 1 or 2 | `1` | Pulls a coplanar layer (a decal) off the surface under it. |

## Sound settings file (`<name>.iw8snd.json`)

A sidecar file next to a sound, with the same name and folder as the wav. Every field is optional. Unknown fields are
ignored. Tools > Sound Settings edits it.

| Field | Value format | Example | What it does |
|---|---|---|---|
| `volume` | number, 0 to 1 | `0.8` | Loudness of the sound. |
| `dist_min` | number, 0 or more | `120` | Start of the sound's distance range, in map units. |
| `dist_max` | number, 0 or more, not below `dist_min` | `1500` | End of the sound's distance range, in map units. |
| `loop` | `true` or `false` | `true` | Loops the sound. |
| `two_d` | `true` or `false` | `true` | Plays the sound as 2D (no position in the world). |
| `channel` | channel name | `sfx` | Sound channel / bus name. |
| `variants` | array of variant objects | `[{"file":"step_01.wav","vol_min":0.8,"vol_max":1.0,"pitch_min":0.9,"pitch_max":1.1,"probability":0.6,"lfe_percentage":0.2,"center_percentage":0.1,"start_delay":15,"secondary":"step_sub"}]` | Multi-variant weighted sound pool: each entry selects a file with its own volume/pitch random range, probability weight, LFE/center send, start delay (ms), and optional secondary alias. |

## iw8r_decal

A projected decal volume. Projects onto brushes and Bezier patches.

| Key | Value format | Example | What it does |
|---|---|---|---|
| `iw8r_size` | `width height depth` | `64 64 32` | Projection box size in map units. |
| `material` | material name | `decal_grime` | Material projected onto underlying geometry. |
| `opacity` | float, 0 to 1 (default 1) | `0.65` | Decal opacity multiplier. |
| `feather` | float, 0 to 1 (default 0) | `0.25` | Edge/depth fade feathering factor. |
| `draw_order` | integer (default 0) | `2` | Layering order when decals overlap (higher draws on top). |
| `fade_distance` | float (default 0) | `1024` | Distance fade start in map units. |

## iw8r_volume (masks)

| Key | Value format | Example | What it does |
|---|---|---|---|
| `mask`, `mask2`, `mask3`, `mask4` | `<picture> [x\|y\|z] <sizeU> [<sizeV> [<scrollU> <scrollV> [<offsetU> <offsetV>]]]` | `clouds_soft 800 800 50 0` | On `iw8r_volume`: a picture that thins the box's fog (white keeps it, black removes it), projected through the box along the axis (default z) and repeated every size units; scroll is in units per second. Pictures: `<map folder>/volume_masks/<picture>.png` (or TGA, JPG, BMP), or the editor's `clouds_soft` and `clouds_hard`. Up to four multiply. Game Lighting shows it. |
