import io

def load(p):
    s = io.open(p, encoding='utf-8', newline='').read()
    return s, ('\r\n' if '\r\n' in s else '\n')

def save(p, s):
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

p = 'docs/ROADMAP.md'
s, nl = load(p)
lines = s.split(nl)
hit = [i for i, l in enumerate(lines) if l.startswith('| 5.8 |')]
assert len(hit) == 1
cells = lines[hit[0]].split(' | ')
assert len(cells) == 5
assert cells[3].startswith('researched, not built')
cells[3] = ('partial: editor side done offline (2026-11-11): the point entity iw8r_decal (origin, angles, size, material, opacity, feather, draw_order, uv_rotate, uv_scale, uv_shift) is in the entity definitions, '
            'and the compiler writes each valid one into world.json decal_volumes in the form of research 80 section 6; a bad entity is left out with a warning; a map without decals is written exactly as before; '
            'the pre-build check lets a decal name a stock material. A map with a decal builds and validates with today\'s converter, which ignores the data (core test compile_iw8_decal_volumes). '
            'Converter side in progress in an isolated worktree (branch iw8radiant/decal-volumes, research 82 when done). ' + cells[3][0].upper() + cells[3][1:])
assert '|' not in cells[3]
lines[hit[0]] = ' | '.join(cells)
save(p, nl.join(lines))

p = 'docs/USER_GUIDE.md'
s, nl = load(p)
a = '### Effects'
assert s.count(a) == 1
sec = [
 '### Decals',
 '',
 'A decal is a picture projected onto the surfaces inside a box: a stain, a painted marking, a scorch mark.',
 '',
 '1. Place the point entity `iw8r_decal` where the picture should be.',
 '2. Set `angles` so that the entity\'s forward direction points into the surface. For a floor use `90 0 0`.',
 '3. Set `size` to the box: depth (along the projection), then width and height. The default is `8 64 64`.',
 '4. Set `material` to a material of your custom library (the alpha of its picture is the shape of the decal) or to',
 '   the name of a stock decal material.',
 '5. Optional keys: `opacity` (0 to 1), `feather` (soft fade at the front and back, 0 to 1), `draw_order` (-4 to 3,',
 '   higher on top), `uv_rotate`, `uv_scale`, `uv_shift`.',
 '',
 'The build log says how many decals were written. Decals need a converter with the decal producer, which is not',
 'installed yet: until then the map builds and plays, without the decals. Not seen in game.',
 '',
]
s = s.replace(a, nl.join(sec) + nl + a)
save(p, s)

p = 'docs/PROGRESS.md'
s, nl = load(p)
a = '  - Still open: impacts (F.12), the script-played loop of an own sound (F.6), reverb, probes, game modes, killstreak'
assert s.count(a) == 1
add = [
 '  - DECALS (5.8): editor half done (compile_iw8.cpp decal loop -> world.json decal_volumes; entity iw8r_decal in',
 '    the overrides JSON; build_preflight skips decal_volumes when it collects surface materials). Converter half: an',
 '    agent works in the worktree C:\\zsw-iw8r\\decals on branch iw8radiant/decal-volumes and will write research 82;',
 '    it must not touch E:\\mw124\\x64-zt or the live zonetool. Adopting it = merge the branch, rebuild zonetool when the',
 '    owner is not mid-test, delete zones.key. Tests: core 1079 / 0 (136,702 checks).',
]
s = s.replace(a, nl.join(add) + nl + a)
save(p, s)
print('ok')
