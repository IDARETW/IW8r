import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b, 1)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('docs/ROADMAP.md', [
 ('''| 11.5 | Hide or remove stock static models and surfaces | IW8 | done offline (2026-11-12): hide (scale 0) and the hidden props' collision removed on the small maps''',
  '''| 11.5 | Hide or remove stock static models and surfaces | IW8 | done offline (2026-11-12): world geometry too (x64-zt 373d6ad2 / 626bfcc9, research 96): iw8r_remove_world boxes; Rebuild Stock Zone stops the triangles inside from drawing (degenerate indices, everything else stock) and from colliding (the world Havok mesh edited in place as the game's own removal does), and bakes the sun shadow again without them (worldspawn iw8r_remove_world_sun keep skips it); tools/stock_world_remove_test.py on mp_shipment (414 triangles, 276 collision triangles, 12 hulls gone; the rest unchanged; ACTS as stock). Stays: occlusion data, baked lighting, faces the compiler hid under the removed object; Warzone refused. Props: hide (scale 0) and the hidden props' collision removed on the small maps'''),
])
patch('CHANGELOG.md', [
 ('''Warzone and stock maps (later the same day):
''', '''Warzone and stock maps (later the same day):

- **Remove stock world geometry.** An `iw8r_remove_world` box on a stock map removes the world inside it (walls,
  crates baked into the world) from drawing and from collision when the zone is rebuilt, and bakes the sun shadow
  again without it. Baked lighting and occlusion keep the old geometry. Proven offline on Shipment
  (tools/stock_world_remove_test.py). Not seen in game.
- **Stock reflections and specular in Game Lighting.** The stock world shows its own reflection probes (read from the
  zone once, cached), the game's environment BRDF table (read from the install) and the GGX sun highlight, as the
  stock world shader does. About 2.2 ms per frame on Backlot2 at 1920 x 1080.
'''),
])
patch('docs/PROGRESS.md', [
 ('''  tools/stock_sun_direction_test.py in run_offline_proofs.py. Compile panel Bake lightmaps checkbox.''',
  '''  tools/stock_sun_direction_test.py in run_offline_proofs.py. Compile panel Bake lightmaps checkbox.
- Then stock world removal (x64-zt 373d6ad2 / 626bfcc9, live; old exe zonetool_f9c14d9f.exe; live vcxproj got
  stock_world_remove.cpp by hand): iw8r_remove_world brush class, editzone remove_world_surfaces, research 96,
  tools/stock_world_remove_test.py in the proofs. Stock reflections: core stock_probes (BC6H probes from the
  decompressed zone, probes.iw8rprb), WorldCache probesFile / probesReady, render.cpp stockProbeLight (box parallax,
  priority order, feather, two mips), game_look_assets (ggx_env_brdf_lut from the install via ACTS, cached in
  AppLocalData/game_look). Ground War (11.10) deferred by the owner's scope decision.'''),
])
print('ok')
