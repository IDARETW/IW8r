import io
import sys

# Docs for 2026-11-14 afternoon: the toon ink from the scene depth (own maps and stock maps), the haze (the game's
# fog, sky blend and volumetric air), sky brightness, volumetric boxes with real units, fog on see-through surfaces,
# the headless deploy. Argument: the converter commit.

ROOT = 'E:/IW8-Radiant/'
ZT = sys.argv[1] if len(sys.argv) > 1 else 'the next commit'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    write(p, s.replace(a, b))


patch('CHANGELOG.md', '''## 2026-11-14

''', '''## 2026-11-14

- **Toon lines redone: found on screen from the scene's depth.** The lines of the toon shader now come from what is
  drawn around each pixel, not from each brush face's edges. Faces in one plane have no line between them, lines run
  into each other without ends or gaps, a wall standing in a floor gets its line, and models get lines too.
  Outlines are the full width, inner lines thinner and thinner still on a soft fold; far lines thin out and fade.
  New shader inputs for any map shader: `s.pixel`, `s.viewDepth`, `s.eye` and the function `iw8rSceneDepth(pixel)`.
  The viewport keeps the scene depth with the game's own precision (a 16-bit float), so it shows what the game will.
  The build binds the game's scene depth for the map's shader (converter `''' + ZT + '''`; proof
  `tools/scene_depth_build_test.py`). Not seen in game.
- **Toon lines on stock maps.** A map shader may have a third function, `iw8rFilterScreen(colour, pixel, viewDepth,
  pixelSize)`: the filter with the screen. File > Rebuild Stock Zone builds it into every stock shader that has the
  screen position and a slot for the scene depth, and the plain filter into the rest. The toon shader uses it for the
  same ink lines on a stock map's world and props. The slot is the one the thermal sight's mask used, so thermal
  sights look wrong on a map rebuilt this way. Not seen in game.
- **Haze: the game's own fog, sky blend and volumetric air for a built map** (research 126). Compile > Atmosphere has
  a Haze group: a preset (Desert haze, Clear day, Overcast mist, Golden dusk, Dense fog) and an amount. It is the
  worldspawn key `iw8r_haze "<preset> [amount]"`. The game then fogs the map and the sky itself: distance and ground
  fog with a glow toward the sun, blended into the low sky, and volumetric air that the sun lights and shadows. Any
  single value can be changed in Vision overrides (the volumetric fields and `veilStrength` now have names). It
  replaces the old baked fog (`iw8r_fog`), which never reached the sky. View > Game Lighting previews it on an own
  map. Not seen in game.
- **Lamps that glow in the air:** the light key `iw8r_volumetric 1`. The game adds a lamp's light to the volumetric
  air only when the lamp is flagged; stock maps flag their feature lamps.
- **Volumetric boxes have the game's units** (research 127). `density` is extinction per unit (default 0.001; the old
  default 0.2 was about 1000 times a stock box and turned a box black), `falloff` is the soft border in units,
  `height_half` (new) the height where the fog is half as thick. `color` and `anisotropy` do nothing in this game
  version; the definitions say so. The viewport's volumetric preview draws the boxes.
- **Fog on glass and other see-through surfaces.** The game's fog pass covers solid surfaces only. With a haze, the
  volumetric air or a volumetric box in the map, the built map's blended, additive and multiply surfaces now fog
  themselves and take the air before them, as the game's own see-through shaders do (research 127 part B).
- **Sky brightness:** Compile > Sun & Sky has a Sky brightness value (worldspawn `iw8r_sky_brightness`, 0.1 to 16). A
  built map's sky is an 8-bit picture exposed so that its brightest clouds stay below white; 2 or 3 gives a bright
  day sky. The viewport shows it, and now also honours Map Info's exposure (`iw8r_exposure_adjust`) on an own map.
- **Build and install from the command line:** `IW8Radiant.exe --compile <map> --out <folder> --deploy` installs the
  built package into the game's maps folder after a good build (the same steps as F6).

''')

patch('docs/USER_GUIDE.md', '''- The whole toon look is in `toon.iw8shader.hlsl`: its constants at the top set the line width (`kLinePixels`), the
  colour strength, the tones and the shade level. Make a copy with New to change them for your map.''',
      '''- **Screen inputs.** `s.pixel` is the pixel's place on screen, `s.viewDepth` its depth along the view, `s.eye` the
  camera's position, and `iw8rSceneDepth(pixel)` gives the depth of what is drawn at any pixel (1e9 for the sky, 0
  where there is none). With them a shader finds outlines and folds from the picture itself: the toon shader's lines
  work this way, so they join without ends, and a wall standing in a floor gets its line. The game keeps this depth
  as a 16-bit number; `iw8rSceneDepthStep` (1 / 1024) is how exact it is, and the viewport uses the same precision.
- The whole toon look is in `toon.iw8shader.hlsl`: its constants at the top set the line width (`kLinePixels` near
  the eye, `kLineMaxUnits` on the surface, so far lines are thinner), the inner lines' share (`kCrease`), how soft a
  fold may be and still get a line (`kFoldLow`, `kFoldHigh`), the colour strength, the tones and the shade level.
  Make a copy with New to change them for your map.''')
patch('docs/USER_GUIDE.md', '''  Map Into Game installs the changed zone as for any stock edit. The filter cannot use textures or the edge inputs,
  and it does not reach characters, weapons or effects.''',
      '''  Map Into Game installs the changed zone as for any stock edit. The filter cannot use textures or the edge inputs,
  and it does not reach characters, weapons or effects.
- **Lines on a stock map.** A shader may add `float3 iw8rFilterScreen(float3 colour, float2 pixel, float viewDepth,
  float pixelSize)`: the filter with the screen. It may call `iw8rSceneDepth`. The rebuild uses it in every stock
  shader that can take it and `iw8rFilter` in the rest; the log says how many. The toon shader has it, so a stock map
  gets the same ink lines. One cost: the rebuilt map's thermal sights look wrong (the lines use the slot of the
  thermal mask).''')
patch('docs/USER_GUIDE.md', '''- **Compile > Sun & Sky**: sun pitch/yaw/colour and a stock sky (`sky_mp_*`; exported and converted at build time).''',
      '''- **Compile > Sun & Sky**: sun pitch/yaw/colour and a stock sky (`sky_mp_*`; exported and converted at build time).
  **Sky brightness** makes the built sky brighter or darker (1 keeps its brightest clouds just below white; 2 or 3
  is a bright day sky, with the brightest parts going to white).
- **Haze (Compile > Atmosphere).** Pick a preset and an amount and the game itself fogs your map: fog by distance and
  near the ground, with a glow toward the sun, blended into the low sky, plus volumetric air that the sun lights, so
  light shafts show through gaps. Presets: Desert haze, Clear day, Overcast mist, Golden dusk, Dense fog. Amount 1
  is the preset as designed, 0.5 half as thick, 2 twice. Every value behind a preset is a vision field you can
  change in the Vision overrides line or Map > Vision Fields (`fog...`, `volumetric...`, `veilStrength`); your
  values win. View > Game Lighting shows the result. The older exponential fog in the same dialog is baked into the
  surfaces and never reaches the sky; a haze replaces it.
- **Lamps in the haze**: give a `light` the key `iw8r_volumetric 1` and it glows in the volumetric air. Only flagged
  lamps do, as in the stock maps.
- **Volumetric boxes** (Primitives > Volumetric Box): fog inside a box, for a misty corner or a dusty room. `density`
  is how thick (0.0002 light, 0.001 default, 0.005 thick), `falloff` the soft border in units, `height_half` the
  height where it is half as thick (a ground mist). The sun, the sky light and flagged lamps light it.''')
patch('docs/TEST_CHECKLIST.md', '''- [ ] Killstreak aircraft that fly high or start far out''', '''- [ ] Toon lines (Cinder Rail Toon, rebuilt 2026-11-14): lines join without ends or gaps, no lines between faces in
      one plane, outlines heavier than inner lines, far lines thin. If the lines look like the old ones (square
      ends), the game's scene depth did not reach the shader: say so.
- [ ] Toon lines on Shipment (stock rebuild with the screen filter): the world and props have ink lines.
- [ ] Haze (Cinder Rail Toon: Desert haze 0.7, sky brightness 3, exposure +1): warm haze toward the horizon that
      meets the sky, light shafts where the sun passes gaps, glass and other see-through surfaces fogged like the
      walls behind them, nothing black or washed out.
- [ ] Killstreak aircraft that fly high or start far out''')
patch('docs/research/README.md', '''| 125 | [The edge list: mantle and mount](125_edge_list_mantle_mount.md) |''',
      '''| 127 | [Volumetric boxes and fog on see-through surfaces](127_volumetric_boxes_translucent_fog.md) | The game's volumetric box record with units (density per unit, falloff, height fade; colour and anisotropy unread), stock boxes, and how the game's blended shaders apply the analytic fog and the volumetric images, with the shader lines the converter now has |
| 126 | [Volumetric sky and fog for a built map](126_volumetric_sky_fog.md) | Which vision fields drive the distance fog, the sky blend and the volumetric air, stock values (rust, shipment, village2), what a built map had (nothing), the change list behind iw8r_haze, the lamp flag |
| 125 | [The edge list: mantle and mount](125_edge_list_mantle_mount.md) |''')
print('ok')
