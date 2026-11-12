# Building an IW8 map with IW8 Radiant, for automated agents

This guide is for an agent (Codex, Claude, a script) that writes a map as text and builds it with no GUI.
Everything below runs from `E:\IW8-Radiant`. A person tests the finished map in game; the agent does not start the game.

## Ground rules

- Work in your own folder, e.g. `out/agent/<map_name>/`. **Do not edit or overwrite** anything in `samples/`,
  `tests/data/`, `data/` or `src/`. Copy a sample if you want a starting point.
- Never write to `E:\IW8\Builds\1.20-replay` and never start or send commands to the game.
- The editor exe may be rebuilt while you work. If `bin/release/IW8Radiant.exe` is missing or locked, wait 30 s and try again.
- The map file name sets the map id: `mp_<name>.map` builds as `mp_<name>` (lower case letters, digits, `_`;
  the editor adds `mp_` when it is missing). Pick a name no other map uses.

## 1. The map file (`iwmap 4`)

Plain text. Start from `samples/tutorial/iw8r_tutorial.map` (a small complete arena) and read it before writing your own.
Units are inches (64 units ≈ a doorway width, 128 ≈ a storey; the player is 72 tall and steps up 18).
+Z is up. Yaw angles are degrees, counter-clockwise from +X.

```
iwmap 4
"000_Global" flags active
"The Map" flags
// entity 0
{
"classname" "worldspawn"
"iw8r_title" "My arena"
"skyname" "sky_mp_shipment"
"sundirection" "-40 235 0"
"suncolor" "1 0.95 0.85"
// brush 0
{
 ( 256 256 -16 ) ( -256 256 -16 ) ( -256 -256 -16 ) tm2co/me_curb_stone_01 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0
 ... one line per face (6 for a box) ...
}
}
// entity 1
{
"classname" "mp_tdm_spawn"
"origin" "0 0 0"
"angles" "0 90 0"
}
```

**Brush faces.** Each line is one plane: three points `( x y z )`, then the material, then
`<texture width> <texture height> <x offset> <y offset> <rotation> 0`, then `lightmap_gray 16384 16384 0 0 0 0`.
Width/height = world units per texture repeat (128 is a good default). The three points go **clockwise when seen
from outside the brush** (copy the winding of the sample box faces and change only the coordinates). Keep brushes
convex and axis-aligned boxes unless you have a reason not to. Snap coordinates to whole numbers (multiples of 8 or 16).

Brush flags go on the line after `{`: ` contents detail;` for brushes that should not split the world (props,
trim, small walls). Tool materials: `caulk` (never drawn; use it on hidden faces), `clip` (invisible player
collision), `trigger` (trigger volumes), `ladder`, `sky` (the sky ceiling).

**The shell.** Give the play space a floor, walls around it and a `sky` brush across the top (see brush 0–8 of the
tutorial: the sky brush sits at z 512–528 over the whole area). Players must not be able to leave the space.

**Entities.** Every entity is a `{ ... }` block of `"key" "value"` lines; brush entities (triggers, script_brushmodel)
also hold brushes. The full list of classes and keys is in `docs/ENTITY_REFERENCE.md` and
`data/iw8_entity_defs.json` (+ `data/iw8_entity_defs_overrides.json`). The ones a playable map needs:

| Class | Keys | Notes |
|---|---|---|
| `worldspawn` | `skyname`, `sundirection` (pitch yaw 0), `suncolor`, optional `iw8r_fog` | entity 0, holds all world brushes |
| `mp_tdm_spawn`, `mp_dm_spawn` | `origin`, `angles` | 8+ of each, on the floor, 64+ units from walls |
| `mp_tdm_spawn_allies_start` / `_axis_start` | `origin`, `angles` | 4 each, on opposite sides |
| `mp_dom_spawn_allies_start` / `_axis_start`, `mp_dom_spawn` | same | for Domination |
| `info_player_start` | `origin`, `angles` | one |
| `mp_global_intermission` | `origin`, `angles` | one camera looking over the map |
| `trigger_radius` + `script_gameobjectname dom` + `script_label _a/_b/_c` + `targetname flag_primary` | `origin`, `radius`, `height` | Domination flags |
| `misc_model` | `model`, `origin`, `angles`, optional `modelscale` | static stock props |
| `script_model` | `model`, `origin`, `angles` | scripted / animated props |
| `misc_prefab` | `model` = prefab path, `origin`, `angles` | groups of brushes and props (section 3) |
| `light` | `origin`, `_color`, `radius`, `intensity` | `intensity` about 1–2 |
| `reflection_probe` | `origin` | one per room, at head height |
| `fx_origin`, `sound_emitter` | see the tutorial | optional ambience |

Use valid sky names (`sky_mp_shipment` is safe). Origins of spawns sit on the floor surface (z = floor top).

## 2. Valid stock assets

Only names marked `ok` load in Replay:

- Materials: `data/asset_catalog/iw8_materials_status.tsv` (name, `ok|grey|missing`). Use `ok` only.
  Good basics: `tm2co/me_curb_stone_01`, `tm2co/plaster_beige_01`, `tmco/metal_painted_base_01a`,
  `twc/wood_varnished_01`, `tm2co/me_plaster_barrier_fence_wall`.
- Models: `data/asset_catalog/iw8_models_status.tsv`. Use `ok` only. The catalog has no sizes, so place props
  sensibly and check the build log (section 4).
- Effects / sounds: `data/asset_catalog/iw8_common_vfx.tsv`, `iw8_common_sounds.tsv`.

Search with e.g. `grep -P "\tok\t" data/asset_catalog/iw8_models_status.tsv | grep -i barrel`.

## 3. Prefabs (doors, windows, stairs, cover, props)

A `misc_prefab` stamps a whole `.map` file at `origin`, rotated by `angles` yaw. Paths are relative to `data/prefabs/`
(or to your map's folder for your own prefabs):

- `data/prefabs/iw8r/` (120 hand-built pieces): doors (openable and static), windows, walls, floors, stairs,
  ramps, ladders, railings/fences, pillars, roofs, lamps, cover, props, gameplay. Index with sizes:
  `data/prefabs/iw8r/index.tsv`; notes and caveats in `data/prefabs/iw8r/README.md`.
  The prefab origin is the floor centre (x/y centred, floor at z 0); the size column is `x × y × z`.
- `data/prefabs/stock/` (6,038 groups taken from the shipped IW8 maps): barrels, barriers, cover, crates, debris,
  doors, fences, foliage, furniture, glass, ladders, lamps, pipes, railings, sandbags, signs, stairs, trim,
  vehicles, windows. Index: `data/prefabs/stock/index.tsv` (path, category, models, how often it appears).

```
{
"classname" "misc_prefab"
"model" "iw8r/doors/door_double_wood_frame.map"
"origin" "0 512 0"
"angles" "0 90 0"
}
```

Wall prefabs with doorways/windows are meant to replace a section of wall: leave a gap in your own wall brushes
the size of the prefab (index `size`) and put the prefab in it.

## 4. Build headless

```
bin/release/IW8Radiant.exe --compile out/agent/mp_myarena/mp_myarena.map --out out/agent/mp_myarena/build
```

It takes 1–5 minutes (longer the first time a stock model or material is used: they are fetched into the cache
first). Exit code 0 = built and validated. Then read:

- `build/compile-result.txt`: `ok map=<id> package=<folder>` or `FAIL ...`.
- `build/compile-log.txt`: line 1 is `prefetch: A of B stock model(s) and C of D stock material(s) cached` and names
  any model that is **not available**; replace those with another `ok` model and build again. Then the full log:
  `warning:` lines are worth fixing (spawn inside a wall, unknown material, leaks); `error:` lines failed the build.

`IW8R_PREFETCH_MS=<ms>` caps the asset wait (default 600000; 0 skips it).

## 5. Check without building

`bin/release/iw8r.exe` (command-line tools):

- `iw8r info <map>`: entity / brush counts, bounds, classes.
- `iw8r roundtrip <map> -o <out.map>`: checks the file parses and writes back identically.
- `iw8r diff <a.map> <b.map>`: what changed between two versions.
- `iw8r edit <in.map> -o <out.map> --select-entity mp_tdm_spawn --move 0 0 16`: scripted edits (see `iw8r` usage).
- `iw8r generate-spawns <in.map> -o <out.map> --replace [--ffa 16] [--starts 4]`: a full spawn set from the
  geometry (FFA / TDM / Domination spawns spread over the walkable floors, allies and axis starts on opposite
  sides, all facing the centre). Run it once the floors and walls exist, then adjust by hand if needed.
- `iw8r preflight <map folder>/iw8build/<id>/extraction/extracted <id>`: the build checks again on the last build
  (the build folder `iw8build/<id>` is created next to the `.map`).

## 6. Look at it

```
IW8R_SMOKE_TEXTURED=1 IW8R_SMOKE_LIT=1 IW8R_SMOKE_SETTLE_MS=15000 IW8R_CAMERA="0 -700 300 -25 90" \
  bin/release/IW8Radiant.exe --smoke-test out/agent/mp_myarena/mp_myarena.map --out out/agent/mp_myarena/shot
```

(bash syntax; in PowerShell set `$env:IW8R_CAMERA = "0 -700 300 -25 90"` etc. first.)
Writes `shot/screenshot.png` (the whole editor window: the camera view is the left pane, the top-down grid the right)
and `shot/smoke-test.txt`. `IW8R_CAMERA` is `x y z pitch yaw`: **negative pitch looks down**, yaw 90 looks along +Y. `IW8R_SMOKE_SETTLE_MS=20000` gives textures time to stream in. Take a few
shots from the spawn areas and from above to check the layout.

## 7. Checklist before handing the map to a person

- Builds with `ok`, no `error:` lines, and the prefetch line shows every model available.
- Spawns: TDM (8+ plus 4+4 starts), DM (8+), Domination starts and three flags, an intermission camera.
- No spawn inside or touching a brush; floors under every spawn; walkable routes (ramps/stairs ≤ 45°, steps ≤ 18).
- The play space is closed (walls + sky brush); hidden faces use `caulk`.
- A light or two, a reflection probe per room.
- Tell the person the map id, where the `.map` is, and what to test (routes, doors, sight lines).
