import io

# docs/HANDOVER.md brought up to the evening of 2026-11-14: what was seen in game that day, the look features (lines
# from the scene depth, haze, sky brightness, First-Person Look), mount edges, the occlusion bake's state, the counts.

p = 'E:/IW8-Radiant/docs/HANDOVER.md'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'


def rep(a, b):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (s.count(a), a[:70])
    s = s.replace(a, b)


rep('''(Compile > HUD Images Folder) and show your own text (Compile > Map Text File).
''', '''(Compile > HUD Images Folder) and show your own text (Compile > Map Text File). The build marks where a weapon can
be mounted (open wall corners and ledge tops of your brushes; View > Show Compiled Collision draws them in yellow).
''')
rep('''**Your own look.** Tools > Shader Editor: a shader file for your map's surfaces (the toon shader is the example), and
a colour filter for a stock map's surfaces.
''', '''**Your own look.** Tools > Shader Editor: a shader file for your map's surfaces (the toon shader is the example), and
a filter for a stock map's surfaces. A shader can read the scene's depth, so outlines work on your own maps and on
stock maps alike. File > First-Person Look (weapon and hands) builds the same filter into the weapon and arm
materials; they belong to a zone every map loads, so the look is on every map while it is installed (Install Into
Game / Restore In Game). The toon look is built and waits there for Install.

**Air and sky.** Compile > Atmosphere: a haze preset (desert, clear, overcast, dusk, fog) with an amount sets the
game's own fog, sky blend and volumetric air for a built map; `iw8r_volume` boxes add local air, a lamp with
`iw8r_volumetric` glows in it, and Compile > Sun & Sky has the sky's brightness. The viewport shows all of it.
''')
rep('''**Occlusion (optional).** Compile > Occlusion bake gives your map cells and portals so the game skips what is behind
walls. Off by default; View > Umbra Visibility shows what a stock map's own occlusion data hides.
''', '''**Occlusion (optional).** Compile > Occlusion bake gives your map cells and portals so the game skips what is behind
walls. Off by default, and leave it off for a map you want to play: its first game test (2026-11-14) lost surfaces
and models. The cause was found and fixed the same day (research 129) and the fix is proven on that very map
offline, but it has not been seen in game since. View > Umbra Visibility shows what a stock map's own occlusion data
hides.
''')
rep('''- `bin\\release\\iw8r_tests.exe`: about 181,500 checks in 1,346 tests of the core (map format, geometry, builds, bakes,
  decoders, the install logic, the occlusion bake).
- `IW8Radiant.exe --gui-test tests\\data\\iw8_sample_room.map --out <dir>`: about 1,080 checks of the editor itself,''',
    '''- `bin\\release\\iw8r_tests.exe`: about 182,800 checks in 1,384 tests of the core (map format, geometry, builds, bakes,
  decoders, the install logic, the occlusion bake, the mount edges).
- `IW8Radiant.exe --gui-test tests\\data\\iw8_sample_room.map --out <dir>`: about 1,090 checks of the editor itself,''')
rep('''- `py -3 -I tools\\run_offline_proofs.py <work dir>`: 25 end-to-end proofs; all passed on the last build of 2026-11-14. Each one edits or builds a real map with the''',
    '''- `py -3 -I tools\\run_offline_proofs.py <work dir>`: 27 end-to-end proofs; all passed on the last build of 2026-11-14. Each one edits or builds a real map with the''')
rep('''(more than 110 notes, the latest numbered 120) and `CHANGELOG.md`.''', '''(more than 120 notes, the latest numbered 129) and `CHANGELOG.md`.''')

start = s.index('## 4. Only the game can settle these')
end = s.index('## 5. Not built, or built in part')
s = s[:start] + '''## 4. Only the game can settle these

**Seen in game on 2026-11-14** (the owner's runs): an installed stock edit loads (the toon zone of Shipment); the toon
lines from the scene depth on Cinder Rail Toon and on Shipment's stock geometry; the haze; the bright sky.

**Reported from those runs and not settled:**
- Shipment with the toon zone: "objects appear in and out". No cause found. Ruled out offline: missing pipeline
  states, the order of shader arguments, technique flags, shaders the driver refused, the image the lines take over
  (all 192 stock shaders read it only in thermal view). The zone changes shaders only, not what the game draws or
  hides. File > Restore Stock Map In Game takes the zone out.
- Cinder Rail Toon lost surfaces and models: that build had the occlusion bake on. The map in the game folder is a
  build without it (since 16:25 that day); not looked at since.

**Built and proven offline, not seen in game.** `docs\\TEST_CHECKLIST.md` has the steps:
1. **The first-person look** after File > First-Person Look > Install Into Game.
2. **Weapon mounting** on a map built on or after 2026-11-14.
3. **Your own map looks like View > Game Lighting**, with Bake lightmaps off and on.
4. **A stock door carried into your own map opens** (real stock scripted objects).
5. **A stock map rebuilt with removed world geometry**: the object is gone and you walk through where it was.
6. **Copied loot, doors and extraction sites on Quarry** work like the originals.
7. **The occlusion bake after its fix** (only if you want it): nothing is missing anywhere on a map built with it.

'''.replace('\n', nl) + s[end:]
rep('''- Viewport: volumetric fog is uncalibrated and off. The normal and specular maps of your own materials are drawn by
  the build shader's rule, which has not been compared with the game.''',
    '''- Viewport: the volumetric air of a haze follows the values the build writes, but its picture has not been compared
  with the game's. The normal and specular maps of your own materials are drawn by the build shader's rule, which
  has not been compared with the game either.
- Mounting: placed models give no mount edges (only brushes do), and there are no mantle edges: mantling uses the
  game's own trace, as before.
- The first-person look is all maps or none: the weapon and hands cannot take a look for one map only.''')
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
