import io

# Docs for the specular and normal maps of own materials in Game Lighting, and ROADMAP rows that still described the
# state before the lightmap bake, the occlusion bake and the zone install.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('CHANGELOG.md', '''## 2026-11-14

''', '''## 2026-11-14

- **Game Lighting draws the normal and specular maps of your own materials.** A material made with File > New / Edit
  Custom Material that has a normal image and a specular image is now drawn with them in View > Game Lighting, by the
  rule of the build's own world shader: the specular map times a view-angle factor (0.8 head-on to 4 at a grazing
  angle) times the reflection probe plus the sun's highlight, whose size comes from the specular map's alpha. A map
  without a captured probe reflects the build's default grey probe, so a bright specular map makes the whole surface
  lighter; that is what the build does, not a viewport effect. Before, such a material was drawn flat. Checked by two
  renders of one room (same floor with and without the specular map). In game: not checked.
- docs: FEATURE_MATRIX.md and MILESTONES.md brought to the current state (57 status cells; what the owner saw in game
  on 2026-11-11 is named as such, everything later says "in game: not checked"); ROADMAP rows 2.16, 5.10, 5.11, 5.21
  and 11.2 no longer describe the state before the lightmap bake, the occlusion bake and the zone install.

''')
patch('docs/PROGRESS.md', '''  The web list of plan 3.3 / 3.4 is done except:''', '''  Viewport: own materials' normal and specular images in Game Lighting (tools/patches/viewport_spec_maps.py):
  TexturedBatch::specTexture / SceneBatch::specTexture, scene.specFor, CameraView::specFor + setMaterialSpecular,
  normal lookup "slope:<file>" (IW3 slope map -> replayNormalPack), spec0 at t29, batchFlags2.w = 1 on an own batch
  with a specular map (w is the stock reference's lightmap flag only while z = 1). Formula = the converter's
  SourceSunSpecular with the default envMapParms (.8, 4, 2.5, .625); probe = probeCube when a capture is bound, else
  grey 128. Not drawn for a material with a custom surface shader (the build adds none there either). Test scene:
  out/agent/spec (shot.ps1 renders a map through /api/frame of the test editor copy).
  The web list of plan 3.3 / 3.4 is done except:''')
patch('docs/HANDOVER.md', '''- Viewport: highlights of your own materials that have a specular map; volumetric fog is uncalibrated and off.''',
      '''- Viewport: volumetric fog is uncalibrated and off. The normal and specular maps of your own materials are drawn by
  the build shader's rule, which has not been compared with the game.''')
patch('docs/USER_GUIDE.md', '''- Additive and multiply blending are not shown in the editor camera. The camera does show a custom material's tint.''',
      '''- Additive and multiply blending are not shown in the editor camera. The camera does show a custom material's tint.
- A custom material's normal and specular images are drawn in View > Game Lighting as the build draws them: the
  specular image's colour is how strongly the surface reflects, its alpha how tight the sun's highlight is. A bright
  specular image also reflects the map's reflection probe everywhere (a grey one until you capture probes), so start
  with a dark specular image (40 to 80 of 255) and raise it where the surface should shine.''')
patch('docs/TEST_CHECKLIST.md', '''- [ ] The same map built with Bake lightmaps **on**: corners and covered places are darker, as View > Game Lighting
      shows it with the key on.''', '''- [ ] The same map built with Bake lightmaps **on**: corners and covered places are darker, as View > Game Lighting
      shows it with the key on.
- [ ] A floor with a custom material that has a specular image (2026-11-14): looking toward the sun across it, the
      highlight and the overall lightening of the floor are as View > Game Lighting shows them.''')

R = 'docs/ROADMAP.md'
patch(R, '''done: data/iw8_entity_defs.json (98 classes)''', '''done: data/iw8_entity_defs.json (113 classes)''')
patch(R, '''| IW8 | one cell: every built map is a single visibility cell''', '''| IW8 | partial (2026-11-14): Compile > Occlusion bake (opt-in, worldspawn `iw8r_umbra_bake`, research 118) gives a built map cells and portals from its opaque box brushes, read back and ray-tested offline; in game: not checked. Without it, one cell: every built map is a single visibility cell''')
patch(R, '''portal and cell authoring, MDAO volumes and Umbra data are not built and would need converter work and looks in game.''',
      '''portals drawn by hand, occluders that are not boxes and MDAO volumes are not built.''')
patch(R, '''Lightmaps are not built: the converter writes no lightmap data, so a built map is lit by the sun, the light grid and its dynamic lights (row 1.8). A lightmap baker needs a new writer in the converter and repeated looks in game; it is not part of this build.''',
      '''Lightmaps: done offline (2026-11-12 / 13): Compile > Bake lightmaps (worldspawn `iw8r_bake_lightmaps`, Bake Settings for the sky and the detail) bakes the world's lightmap on the CPU and the converter writes it; without it a built map is lit by a flat sky light, the sun, the light grid and its dynamic lights (research 117). View > Game Lighting shows either state. In game: not checked.''')
patch(R, '''GPU lightmap bake open (the converter does not read the lightmap scale)''',
      '''The lightmap bake itself is row 5.11 (CPU, Compile > Bake lightmaps; the texel size is a Bake Setting). A GPU bake and a per-face lightmap scale in the build are not built''')
patch(R, '''| IW8 | done offline for spawns; fast path open Update (2026-11-12, research 88;''',
      '''| IW8 | done offline for spawns; fast path open. Install (2026-11-13, research 116): a rebuilt stock zone is installed as a whole zone file (File > Install Edited Stock Map Into Game moves the stock files to `iw8r_stock`); the earlier patch-file install stopped the game and is gone. In game: not checked. Update (2026-11-12, research 88;''')
print('ok')
