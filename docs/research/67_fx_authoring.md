# 67. Authored effects through the converter's IW3 effect route (roadmap 8.2)

Question: can the editor make the mapper's own particle effects that play in Replay, without new fork code? Answer: yes,
through the IW3 effect route the x64-zt converter already has for CoD4 ports. Built and checked against a real
`buildextraction` (2026-11-11). How the effect looks in game is NOT VERIFIED.

## Route

1. The editor file `<map dir>/fx/<name>.iw8fx.json` (core `fx_author.h`: elements with image, additive or blended,
   looping or one-shot, spawn interval and count, life, spawn radius, velocity range, gravity, size and colour from start
   to end).
2. The build (`compile_iw8.cpp`, `CompileOptions::fxRoots`) writes for every fx_origin whose `fx` key names such a file:
   - `extracted/fx/<name>.iw3.json`: the IW3 FxEffectDef graph the converter reads
     (`x64-zt iw3_fastfile.cpp ReadPreparedFx / FxElement`, schema 1, `asset_type` `iw3_fx`, layout 32 / 252 / 28);
   - per element `extracted/materials/iw8rfx_<hash>_<i>.json`: an IW3 effect material
     (`iw3_render_assets.cpp ReadFxMaterial`) and `extracted/images/iw8rfx_<hash>_<i>_c.dds` (the element's image, or
     `data/fx/iw8r_soft_particle.png`).
3. The map script already loads every placed effect by literal name; the converter's literal scan
   (`ReadScriptEffectLiterals`) declares it, converts the graph to a native Replay vfx and rewrites the script name:
   `iw3: script effect iw8r/test_campfire -> vfx/mw120r/<map>/iw8r/test_campfire`.

Build log of the check map `out/agent/mp_iw8r_fxtest` (two sprite elements):

```
iw3: validated 2 element(s) in 1 map-reachable source FX graph(s) from 1 declaration(s)
iw3: FX elements sprite=2 ...
iw3: verified 2 distinct FX material source export(s)
iw3: converted 1 of 1 map-reachable source FX graph(s) to native Replay VFX
iw3: script effect iw8r/test_campfire -> vfx/mw120r/mp_iw8r_fxtest/iw8r/test_campfire
```

## Field rules found in the converter (what the writer follows)

- Elements: looping first, then one-shot (`element_counts`). Type 0 `sprite_billboard`.
- `spawn`: one union of two ints, `raw` [a, b] = looping {interval_msec a, count b} = one_shot.count {base a,
  amplitude b}; the reader throws when the views disagree.
- Velocity samples: `vel_interval_count + 1` samples, each `local` and `world` {velocity, total_delta} ranges. The
  converter multiplies velocity by 1000 (`AddVfxVelocity`), so the graph holds units per millisecond.
- Visual samples: `vis_state_interval_count + 1` states {base, amplitude}; colour bytes are B, G, R, A
  (`AddVfxVisualCurves` reads channels 2, 1, 0, 3); `size` [w, h]; the amplitude colour is a second curve, so the
  writer repeats the base colour.
- Element flags (IW3 FX_ELEM_*): 0x2 spawn relative to the effect, 0x10 sphere offset, 0x2000000 world velocity graph,
  0x4000000 gravity. Gravity is a fraction of world gravity.
- Sprite spin (`rotation_delta`) is not converted for sprites (`AddVfxRotation` keeps only the initial rotation): the
  editor shows it in its preview only.
- Effect material: `_game` iw3, `_type` material, `_version` 1; `techniqueSet` `effect_zfeather` (blended) or
  `effect_zfeather_add` (additive); `textureAtlas` 1 x 1; `stateFlags` 16; `cameraRegion` emissive; one colour state
  (blended: srcalpha / invsrcalpha, alpha test gt0; additive: srcalpha / one, which the converter maps to the
  premultiplied additive family); depth test less_equal, no depth write; constants `colorTint` and `featherParms`
  (feather scale > 0); one `colorMap` texture with a clamp or wrap sampler, linear filter.

## Curves, lights and models (2026-11-11)

- Multi-key curves: 2 to 16 keys [t, size, r, g, b, a] are resampled to 9 evenly spaced visual states
  (`vis_state_interval_count` 8); the converter builds its colour / size graphs from them (`AddVfxVisualCurves`).
- Light elements: IW3 type 6 `omni_light` with no visual (`visual_count` 0, `visuals_present` false); the converter
  takes the radius from `size[0]` and the colour curve; no material or image is written.
- Model elements: IW3 type 5 `model` with an `xmodel` visual and dependency; the size curve is the scale. The model is a
  custom model; the build adds it to the extraction (`extracted/xmodel/<name>.json`) and the converter's FX model
  closure (`ReadSourceModel`) converts it: "converted 1 FX-referenced XModels through native model/material closure".

Build log of out/agent/mp_iw8r_fxtest with all three kinds: `FX elements sprite=2 ... model=1 omni=1`, `converted 1 of
1 map-reachable source FX graph(s) to native Replay VFX`.

## Not covered

Decal, runner (child effect), trail and sound elements, atlas animation. In-game look (size units, colour, fade, light
radius, model scale) is NOT VERIFIED.
