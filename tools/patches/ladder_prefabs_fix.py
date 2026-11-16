import io
import math

# The six ladder prefabs (data/prefabs/iw8r/ladders) had their stock model turned 90 degrees from its climbing brush
# (owner re-check 2026-11-15: "ladder is 90 degrees rotated incorrectly"): every stock ladder model is wide along its own
# y and thin along x, while the brush was wide along x. Rewritten from the models' real bounds (model cache GLBs):
# each model is turned 90 degrees so its rungs run along x, its back on the wall line (-y, the wall side), the
# climbable brush over the rungs' front (+y), full height; the steel ladder (hung from its top, z -200..0) is raised
# to stand on the floor, and each prefab is as tall as its model. Origin = footprint centre at floor level, as the
# library's other prefabs.

ROOT = 'E:/IW8-Radiant/data/prefabs/iw8r/'

# slug: (model, model x min, model x max, model y min, model y max, model z min, model z max, title)
LADDERS = {
    'ladder_steel_128': ('tm/com_steel_ladder', 0.0, 8.2, -15.5, 15.5, -200.0, 0.0, 'Steel ladder, 200 high, climbable brush'),
    'ladder_wood_construction_128': ('tm/construction_ladder_wood_01', -2.0, 2.0, -11.2, 11.2, -0.3, 192.3,
                                     'Construction wood ladder, 193 high'),
    'ladder_old_wood_72': ('tm/ladder_old_wood_01_72', 0.3, 6.3, -11.5, 11.5, 0.0, 76.3, 'Old wood ladder, 76 high'),
    'ladder_old_wood_144': ('tm/ladder_old_wood_01_144', 0.3, 6.3, -11.5, 11.5, 0.0, 148.3, 'Old wood ladder, 148 high'),
    'ladder_old_wood_160': ('tm/ladder_old_wood_01_160', 0.2, 6.2, -11.5, 11.5, 0.0, 160.3, 'Old wood ladder, 160 high'),
    'ladder_radio_tower_128': ('tm/ee_buildings_industrial_radiotower_ladder_128_02', -6.3, 1.6, -12.2, 12.8, 0.5, 128.0,
                               'Radio tower ladder section, 128 high'),
}


def fmt(v):
    v = round(v, 3)
    return str(int(v)) if v == int(v) else ('%.3f' % v).rstrip('0').rstrip('.')


def box_faces(x0, y0, z0, x1, y1, z1, mat='ladder'):
    tail = f' {mat} 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0'
    P = lambda x, y, z: f'( {fmt(x)} {fmt(y)} {fmt(z)} )'
    return [
        f' {P(x1, y1, z0)} {P(x0, y1, z0)} {P(x0, y0, z0)}' + tail,
        f' {P(x0, y0, z1)} {P(x0, y1, z1)} {P(x1, y1, z1)}' + tail,
        f' {P(x0, y0, z1)} {P(x1, y0, z1)} {P(x1, y0, z0)}' + tail,
        f' {P(x1, y1, z1)} {P(x0, y1, z1)} {P(x0, y1, z0)}' + tail,
        f' {P(x1, y0, z1)} {P(x1, y1, z1)} {P(x1, y1, z0)}' + tail,
        f' {P(x0, y1, z1)} {P(x0, y0, z1)} {P(x0, y0, z0)}' + tail,
    ]


sizes = {}
for slug, (model, mx0, mx1, my0, my1, mz0, mz1, title) in LADDERS.items():
    depth = mx1 - mx0                       # the model's thickness (its x), the prefab's y after the turn
    half = math.ceil(max(-my0, my1))        # half its width (its y), the prefab's x after the turn
    h = round(mz1 - mz0)
    # Before centring: the model's back on y = 0, the brush from the rungs' front (y = depth) 4 units out.
    # Yaw 90 turns model (x, y) to (-y, x): model x min goes to y = mx0 + pos.y, so pos.y = -mx0 puts the back on 0.
    cy = (depth + 4) / 2.0
    pos = (0.0, -mx0 - cy, -mz0)
    brush = box_faces(-half, depth - cy, 0, half, depth + 4 - cy, h)
    text = '\n'.join(['iwmap 4', '"000_Global" flags active', '"The Map" flags ', '// entity 0', '{', '"classname" "worldspawn"',
                      '// brush 0', '{'] + brush + ['}', '}', '// entity 1', '{', '"classname" "misc_model"',
                      f'"model" "{model}"', f'"origin" "{fmt(pos[0])} {fmt(pos[1])} {fmt(pos[2])}"', '"angles" "0 90 0"', '}', ''])
    path = ROOT + 'ladders/' + slug + '.map'
    old = io.open(path, encoding='utf-8').read()
    assert old.startswith('iwmap 4') and model in old, path
    io.open(path, 'w', encoding='utf-8', newline='\n').write(text)
    sizes[slug] = (f'{fmt(2 * half)}x{fmt(depth + 4)}x{h}', title)

# The index (and the README's list, if it names them) follow.
for name in ('index.tsv', 'README.md'):
    p = ROOT + name
    s = io.open(p, encoding='utf-8', newline='').read()
    lines = s.split('\n')
    for i, line in enumerate(lines):
        for slug, (size, title) in sizes.items():
            if name == 'index.tsv' and line.startswith(f'ladders/{slug}.map\t'):
                cols = line.split('\t')
                cols[2], cols[3] = title, size
                lines[i] = '\t'.join(cols)
            elif name == 'README.md' and line.startswith(f'- `ladders/{slug}.map`: '):
                lines[i] = f'- `ladders/{slug}.map`: {title} (size {size})'
    io.open(p, 'w', encoding='utf-8', newline='').write('\n'.join(lines))
print(sizes)
