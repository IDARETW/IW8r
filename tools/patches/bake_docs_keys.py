import io, json


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


keys = '''        "iw8r_bake_lightmaps": {
          "type": "string",
          "default": "0",
          "required": false,
          "examples": [
            "1"
          ],
          "description": "1: the build bakes a lightmap for the world (5.11): the sky and the bounce of the sun and lights (the game draws the sun and the lights in real time on top), and the sun visibility. The converter turns it into the game's lightmap (offline proof tools/lightmap_bake_build_test.py; not seen in game)."
        },
        "iw8r_bake_sky": {
          "type": "string",
          "default": "",
          "required": false,
          "examples": [
            "0.55 0.65 0.8 0.35"
          ],
          "description": "Lightmap bake: the sky light as \\"r g b intensity\\" (default a pale blue at 0.35 x sunlight)."
        },
        "iw8r_lightmap_texel": {
          "type": "float",
          "default": "16",
          "required": false,
          "examples": [
            "8",
            "32"
          ],
          "description": "Lightmap bake: world units per lightmap texel (grows by itself when the atlas does not fit 2048 x 2048)."
        },
        "iw8r_bake_rays": {
          "type": "float",
          "default": "32",
          "required": false,
          "examples": [
            "64"
          ],
          "description": "Lightmap bake: sky rays per texel (more rays: smoother ambient, slower bake; at most 1024)."
        },
        "iw8r_sun_shadow_range": {'''
patch('data/iw8_entity_defs_overrides.json', [('        "iw8r_sun_shadow_range": {', keys)])
json.load(open('data/iw8_entity_defs_overrides.json', encoding='utf-8'))

patch('docs/KEYS_REFERENCE.md', [
 ('''| `iw8r_sun_shadow_range` |''', '''| `iw8r_bake_lightmaps` | `1` to bake | `1` | Bakes a lightmap for the world when the map is built: the sky light and the bounce of the sun and lights, plus where the sun is blocked. The game still draws the sun and the lights live on top. Proven offline; not yet seen in game. |
| `iw8r_bake_sky` | `r g b intensity` | `0.55 0.65 0.8 0.35` | Sets the sky light used by the bake. The default is a pale blue at 0.35 times `sunlight`. |
| `iw8r_lightmap_texel` | world units per texel (default 16) | `8` | Sets the lightmap's detail. Smaller is sharper and bigger. The build makes it coarser by itself if the atlas would pass 2048 x 2048. |
| `iw8r_bake_rays` | sky rays per texel (default 32, at most 1024) | `64` | More rays make a smoother ambient but a slower bake. |
| `iw8r_sun_shadow_range` |'''),
])
print('ok')
