import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b, 1)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('CHANGELOG.md', [
 ('''## 2026-11-12

''', '''## 2026-11-12

Lighting:

- **Baked lightmaps for own maps.** Worldspawn `iw8r_bake_lightmaps 1` bakes the world's lightmap when the map is
  built: charts packed into one atlas, the sky light and one bounce of the sun and lights, and where the sun is
  blocked. The game draws the sun and the lights live on top, as it does for converted maps, so they are not baked
  twice. `iw8r_bake_sky`, `iw8r_lightmap_texel` and `iw8r_bake_rays` tune it. The converter turns it into the
  game's lightmap and every world surface uses it (tools/lightmap_bake_build_test.py: build, ACTS, the built
  world's lightmap table). Not seen in game.

'''),
])

patch('docs/PROGRESS.md', [
 ('''- Tests at the last commit: core 139,800 checks / 0, GUI 948 / 0.''',
  '''- Then (owner asked: lightmap baking plus a viewport that matches the game): core ray_bvh.* and lightmap_bake.*
  (charts, atlas, sun / lights / sky / one bounce, IW3 pair encoding), the build hook (worldspawn iw8r_bake_lightmaps,
  compile_iw8.cpp before the surface JSON). Editor maps draw with the converter's own world shader
  (map_surface_realtime.hlsl, not the stock tw/ shader): light = lightmap (A N.z + B sat(N.D)) + realtime sun x
  visibility + realtime local lights, so the bake leaves the direct sun and lights out (research 95 section "Editor
  maps in game"). Proof tools/lightmap_bake_build_test.py (in run_offline_proofs.py). Next: the viewport renders with
  that shader's math and the baked lightmap.
- Tests at the last commit: core 139,820 checks / 0, GUI 948 / 0.'''),
])

io.open('docs/research/95_lit_shader_math.md', 'a', encoding='utf-8', newline='').write('''
## Editor maps in game: the converter's world shader (VERIFIED from the converter source and a built map)

The sections above are the **stock** world shader. An editor-built map does not draw its world with it. The editor's
maps go through the converter's IW3 route (doc 55), and with a lightmap present that route gives every world surface a
generated native world material `w/mw120r_<map>_world_<id>` (the built gfx_map of tools/lightmap_bake_build_test.py:
all world surfaces on lightmapIndex 0 with these materials). Its pixel shader is
`x64-zt src/convert/iw3_iw8/resources/map_surface_realtime.hlsl`, built with `MAP_SOURCE_SUN_MASK 1`,
`MAP_SOURCE_CHANNELS 1`, `MAP_NATIVE_WORLD`, `MAP_LIGHTMAP_TEXTURES` (+ `MAP_LIGHTMAP_LOG` for log-encoded pairs) and,
in the iw8 style, `MAP_IW8_LIGHTING` and `MAP_LIGHT_UNIT_SCALE` (iw3_vision.cpp). The source is in the fork, so the
viewport can follow it line by line. Its light (main, around line 1336-1615):

- Lightmap (`SourceIndirect`): the two IW3 layers sampled bilinear at the lightmap UV (A rgb + slope X in A.a, B rgb +
  slope Y in B.a), log-decoded `(2^(x log2(1 + sM)) - 1) / s`; slope `(a * 4.08 - 2.08, b * 4.0645 - 2.0645)`;
  `indirect = A * Nt.z + B * sat(dot(normalize(slope, 1), Nt))` with Nt the tangent-space shading normal; times
  `MAP_INDIRECT_GAIN` (1).
- Sun visibility: the baked sun depth where it covers the point, else min(visibility, the primary byte) (the IW3 sun
  mask; the editor writes its sun visibility there).
- Specular (iw8 style): F0 .04 and gloss .15 unless the material gives a specular map; probe reflection scaled by the
  indirect luminance, specular occlusion LUT, environment BRDF; indirect diffuse times 1 - max(F).
- `indirect *= cb[43].z * gtao / bounce`; `light = indirect + visibility * (N.L + transmitted) * sun` with
  `sun = cb[57] / MAP_LIGHT_UNIT_SCALE` (the realtime sun), colour = albedo x light, plus the realtime local lights
  `(albedo x diffuse + specular) / MAP_LIGHT_UNIT_SCALE`, then fog, then `x MAP_LIGHT_UNIT_SCALE x cb[42].x` into the
  stock post chain (exposure and tonemap of the sections above).

Consequence for the bake: the sun and the converted lights are realtime, so the lightmap holds only the sky, their
bounce and the sun visibility (`BakeScene::sunDirect = false`, `BakeLight::direct = false` for the lights within the
primary light limit). Consequence for the viewport: a 1-to-1 preview of an editor map follows this shader (the
lightmap formula above, the realtime sun with the visibility, the local lights), then the stock exposure and tonemap.
''')
print('ok')
