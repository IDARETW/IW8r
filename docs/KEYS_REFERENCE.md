# Editor keys reference

These are the `iw8r_` keys, plus `soundalias` and `sound_loop`, that the editor's build understands.
You set them in the Entity inspector like any other key, and they need no scripting.
The descriptions are shortened from `data/iw8_entity_defs_overrides.json`, which is merged over the generated entity definitions and is the source for the key help.

For the editor's own key help, see Help > Entity Class Reference. For how each behaviour is used, see docs/USER_GUIDE.md.

Values in quotes are one string. Times are in seconds and distances are in map units.

## light and light_spot

A light with a `target` or a `fov` key is a spot. Both classes take the same keys below.

| Key | Value format | Example | What it does |
|---|---|---|---|
| `iw8r_ies` | path to an LM-63 `.ies` file, absolute or relative to the map folder | `lights/cone_profile.ies` | Sets the spot's light profile from an IES file. In game look not verified. |
| `iw8r_bulb_radius` | float, above 0 (default 1) | `2` | Bulb radius. Widens the light's volume. |
| `iw8r_shadow_bias` | float (default 0.75) | `0.75` | Shadow map bias. |
| `iw8r_falloff` | float, above 0 (default 0.2) | `0.2` | Distance falloff. |
| `iw8r_fade` | `near far` (default 0 0) | `0 400` | Fade range for the light. |
| `iw8r_shadows` | 0 or 1 (default 0) | `1` | 1 gives the light a shadow map. Spot lights only; an omni ignores it. |
| `iw8r_intensity_units` | float, 0 or more (unset) | `4` | Intensity in light units. When set, the radius brightness calibration is skipped. When unset, it is calibrated from the radius. |
| `iw8r_uv_intensity` | float (default 0) | `0.5` | UV vision intensity. |
| `iw8r_ir_intensity` | float (default 0) | `0.5` | Thermal vision intensity. |

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

The overrides file has no `trigger_hurt` or `trigger_radius` keys with `iw8r_` names. It has only `trigger_multiple`.

| Key | Value format | Example | What it does |
|---|---|---|---|
| `iw8r_vision` | `name=value; ...` (Replay vision fields) | `tonemapExposure=2.5` | Changes the vision and fog inside the volume. A player inside sees it. Leaving restores the map's vision. |
| `iw8r_teleport` | targetname of a destination (`script_origin`) | `tp_exit_1` | A player who touches the trigger moves to the destination's origin and angles. The destination needs its own `targetname`. |
| `iw8r_jump` | `<vx> <vy> <vz>`, launch velocity in units per second | `0 0 700` | A jump pad. A player who touches the trigger is launched. |
| `iw8r_damage` | `<amount> [interval]` (interval default 1) | `10 1` | A damage zone. A player inside takes the amount of damage every interval. |
| `iw8r_audio_zone` | lower-case zone name | `hall` | Inside this trigger's box, the zone's reverb (`iw8r_reverb`) applies. In game not verified. |
| `iw8r_reverb` | `<room> [dry wet fade]` | `iw8_md_room` | The reverb room type. Use one of the 59 stock room types. The build writes the map sound table. In game not verified. |
| `iw8r_audio_priority` | float (default 30) | `50` | Priority where zones overlap. The higher number wins. The map-wide zone is 0. |
| `iw8r_audio_interior` | 0 or 1 (default 0) | `1` | 1 uses the stock "interior" occlusion and filter presets. 0 uses the stock "default" presets. In game not verified. |
| `iw8r_ambient` | name of one of the map's own sounds | `wind_bed` | A 2D loop that plays while a player is in the zone. Stock aliases are silent in the map bank, so use a looping sound emitter for those. In game not verified. |
| `iw8r_ambient_oneshots` | `<min s> <max s> <sound>[:weight] ...` | `6 12 bird_call:3 dog_bark` | One of the listed sounds plays at random every min to max seconds while a player is in the zone. In game not verified. |

## worldspawn

The map's global settings. There is one worldspawn per map.

| Key | Value format | Example | What it does |
|---|---|---|---|
| `iw8r_sun_shadow_range` | two cascade multipliers (default `3 2`), or `0` | `4 3` or `0` | Sets how far sun shadows reach. Sent to every player. Longer values push sun shadows further. `0` turns it off. |
| `iw8r_reverb` | `<room> [dry wet fade]` | `iw8_ext_open_space_med_1` | The map's reverb everywhere outside audio zones. Uses one of the 59 stock room types. In game not verified. |
| `iw8r_audio_interior` | 0 or 1 (default 0) | `1` | 1 uses the stock "interior" occlusion and filter presets. 0 uses the stock "default" presets. In game not verified. |
| `iw8r_ambient` | name of one of the map's own sounds | `factory_hum` | An ambient bed: a 2D loop played while a player is in the zone. In game not verified. |
| `iw8r_ambient_oneshots` | `<min s> <max s> <sound>[:weight] ...` | `6 12 bird_call:3 dog_bark` | Sounds that play at random every min to max seconds. In game not verified. |
| `iw8r_sun_sprite` | `<size 1..64> [flare alpha 0..1] [blind darken 0..1] [glare lighten 0..1]` | `16 0.5` | Draws a sun disc in the sky in the sun direction. Adds a lens flare when the player looks at it, plus blind and glare strength. In game not verified. |

## fx_origin

A placed effect. The build writes it into the map script, and it plays the effect in game.

| Key | Value format | Example | What it does |
|---|---|---|---|
| `soundalias` | one of the map's own sounds, or a stock alias | `fire_loop` | Plays a sound at the effect. It loops unless `sound_loop` is 0. |
| `sound_loop` | 0 or 1 (default 1) | `0` | 1: the sound loops. 0: the sound plays once each time the `delay` passes, or once only when there is no delay. |
| `iw8r_damage` | `<amount> [radius] [interval seconds]` (radius default 48, interval default 1) | `10 48 0.5` | The effect hurts. A player within the radius takes the damage every interval. No trigger is needed. |

## misc_prefab

The overrides file has no `iw8r_` keys for `misc_prefab`. These two key forms are set on a prefab instance and come from the done row 8.8 in docs/ROADMAP.md.

| Key | Value format | Example | What it does |
|---|---|---|---|
| `iw8r_ovr:<targetname \| class=<class> \| *>:<key>` | the value for that key | `iw8r_ovr:class=light:radius` = `300` | Sets a key on the matching entities inside this prefab instance. Applied in the views and in builds. File > Prefab Parameters lists the keys. |
| `iw8r_mat:<from>` | material name | `iw8r_mat:tm2co/me_curb_stone_01` = `tm2co/me_curb_stone_02` | Swaps the material `<from>` for the given material in this instance. Used for variants. |

## Custom material definition (`.iw8mat.json`)

One file per material, `<name>.iw8mat.json`, in the map's `materials` folder (File > New Custom Material writes it).
Only `color` is required. Image names are relative to the definition file.

| Field | Value format | Example | What it does |
|---|---|---|---|
| `name` | material name: letters, digits, `_ . / -`; no `..`, no leading or trailing `/`; not under `mc/` | `custom/brick_red` | The name the map uses. Default: the file's path under the materials folder, without `.iw8mat.json`. |
| `color` | image file (PNG, TGA, JPG, BMP or uncompressed DDS) | `brick_red.png` | Colour image. Required. |
| `normal` | image file | `brick_red_n.png` | Normal map. |
| `specular` | image file | `brick_s.png` | Reflectance (RGB). Its alpha is the gloss. |
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

## Counts

- 48 key rows in the tables above. light and light_spot share one table of 9 keys.
- 32 distinct key names from the overrides file: 30 `iw8r_` keys, plus `soundalias` and `sound_loop`.
- Key forms, not key names: `iw8r_mat:<material>` on models, and the 2 forms for `misc_prefab`.
- 15 rows in the custom material table and 5 rows in the sound settings table.
