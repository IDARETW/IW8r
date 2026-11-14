import io
import sys

# Docs for the probe level fix (research 123, 2026-11-14). Argument: the converter commit.

ROOT = 'E:/IW8-Radiant/'
ZT = sys.argv[1]


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

- **Reflections on a built map: the cause of "probes make no visible change" was found and fixed** (research 123).
  The game multiplies a probe's picture by the brightness of the baked light, so its own probes are stored with a
  mean brightness of about 0.9 and in colour. A built map's probe was stored grey at 0.14 (the converter read it by
  the rules made for Call of Duty 4 captures): every reflection in a built map, on its own surfaces and on weapons,
  players and stock props, was six to seven times too dark and had no colour.
  - A built map's probes now keep their colour and are stored at the stock level (measured on a built zone: mean
    0.907, colour 0.188 where it was 0.001; stock maps 0.82 to 0.93).
  - The capture is 128 texels a face, was 64, and each texel is the mean of four samples: sharper mirror-like
    reflections without sparkle.
  - Converter (x64-zt `''' + ZT + '''`, deployed): only probes the editor marks as its own captures are read this way;
    converted Call of Duty 4 and IW7 maps are read as before.
  - New offline proof `tools/probe_capture_build_test.py` (24 proofs in the set). Rebuild a map to get it. Not seen
    in game.

''')
patch('docs/ROADMAP.md', '''Also true: a probe is chosen per surface (nearest probe), not by where the player stands, and matte materials without a specular map barely show it. In game NOT SEEN. |''',
      '''Also true: a probe is chosen per surface (nearest probe), not by where the player stands, and matte materials without a specular map barely show it. Update (2026-11-14, research 123): the second and larger cause was measured on built zones. The game multiplies a probe by the baked light's luminance, so stock probes are stored at a mean luminance of 0.82 to 0.93 with colour; a built map's probe was stored grey at 0.139 (the converter's Call of Duty 4 rules), so every reflection was six to seven times too dark. Fixed: the build marks its probe pictures as editor captures, the converter (''' + ZT + ''') keeps their colour and stores them at the stock level; the capture is 128 texels a face. Offline proof tools/probe_capture_build_test.py (built zone: mean 0.907, colour 0.188). Fixed offline, NOT SEEN in game. |''')
patch('docs/ROADMAP.md', '''the camera renderer captures every probe (six 64x64 faces in the converter cube layout, markers/outlines hidden)''',
      '''the camera renderer captures every probe (six 128x128 faces since 2026-11-14, was 64, in the converter cube layout, markers/outlines hidden; stored in colour at the stock probes' level, research 123)''')
patch('docs/ROADMAP.md', '''What is left in this queue (F.7 reverb, F.9 the wide spot, F.10 probes, F.11 game modes,''',
      '''What is left in this queue (F.7 reverb, F.9 the wide spot, F.11 game modes; F.10 probes has a measured cause and a fix since 2026-11-14, research 123;''')
s = read('docs/PLAN_REMAINING.md')
nl = '\r\n' if '\r\n' in s else '\n'
start = s.index('| 1.11 HDR probe capture |')
end = s.index(nl, start) + len(nl)
s = s[:start] + ('| 1.11 a capture of the renderer\'s linear frame for probes | Probes in colour at the stock level, 128 texels a face (2026-11-14, research 123), '
                 'captured before every build | The level and the colour decide whether reflections show; the linear range only sharpens the highlights of very bright '
                 'things in mirror-like surfaces. |' + nl) + s[end:]
write('docs/PLAN_REMAINING.md', s)
patch('docs/PLAN_REMAINING.md', '''- **Reflection probes:**
  - captured with Game Lighting on, in game light units, HDR / BC6H, at a higher resolution;
  - this fixes F.10 ("probes make no visible change").''', '''- **Reflection probes:** DONE 2026-11-14 for what F.10 needed (research 123): a built map's probes are stored in
  colour at the stock probes' level (they were grey and six to seven times too dark) and captured at 128 texels a
  face; offline proof `tools/probe_capture_build_test.py`. Left: a capture of the linear frame (highlights only).
  - captured with Game Lighting on, in game light units, HDR / BC6H, at a higher resolution;
  - this fixes F.10 ("probes make no visible change").''')
patch('docs/USER_GUIDE.md', '''- Reflection probes make no visible change in game.''',
      '''- Reflection probes made no visible change in game. The cause was found on 2026-11-14 (they were stored grey and
  far too dark) and fixed; rebuild the map. Not seen in game yet.''')
patch('docs/TEST_CHECKLIST.md', '''- [ ] Reflection probes now carry a picture of their surroundings (captured before every build): glossy surfaces
      near a probe should reflect the room, not flat grey.''', '''- [ ] Reflection probes now carry a picture of their surroundings (captured before every build): glossy surfaces
      near a probe should reflect the room, not flat grey. Since 2026-11-14 they are stored in colour at the
      game's own level (research 123): a weapon's metal and a glossy floor should reflect about as strongly as on
      a stock map (rebuild the map first).''')
patch('docs/HANDOVER.md', '''23 end-to-end proofs''', '''24 end-to-end proofs''')
patch('docs/research/README.md', '''| 122 | [The UAV's minimap sweep and enemy pings](122_uav_radar_sweep.md) |''',
      '''| 123 | [A built map's probe level](123_built_map_probe_level.md) | Why a built map's reflections showed nothing (probes stored grey at a mean of 0.14 where stock probes are at 0.9 in colour), the measurements, and what the build and the converter store now |
| 122 | [The UAV's minimap sweep and enemy pings](122_uav_radar_sweep.md) |''')
patch('docs/research/73_reflection_probe_path.md', '''Short answer:''', '''Update 2026-11-14: research 123 found the larger cause (the stored level and the missing colour) and its fix.

Short answer:''')
print('ok')
