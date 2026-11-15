import io

# tools/prefab_bounds_audit.py, 2026-11-14: the bundled stair prefabs were laid out as if tm/ee_stairs_modular_2step
# climbed along x and were 64 long and 32 high. Its bounds and geometry say otherwise: 50 wide (x), 32 deep (y, from
# -32 to 0), 16 high, climbing toward +y (y -32..-24 at z 8, the rest at z 16). Placed along x without a turn, the
# modules made a sideways, gapped staircase (half of it floating). A module turned by yaw -90 climbs along +x over
# x -32..0 of its origin; consecutive modules are 32 apart and 16 higher. Each raised module stands on a cinder-block
# fill brush down to the ground (the material of the L stair's landing), so no step hangs in the air.

LIB = 'E:/IW8-Radiant/data/prefabs/iw8r/stairs/'
FILL = 'mo/concrete_cinder_block_base_tan'
MOD = 'tm/ee_stairs_modular_2step'


def box(x0, y0, z0, x1, y1, z1, mat=FILL):
    f = ' %s 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n' % mat
    p = lambda x, y, z: '( %g %g %g )' % (x, y, z)
    return ('{\n'
            + ' ' + p(x1, y1, z0) + ' ' + p(x0, y1, z0) + ' ' + p(x0, y0, z0) + f
            + ' ' + p(x0, y0, z1) + ' ' + p(x0, y1, z1) + ' ' + p(x1, y1, z1) + f
            + ' ' + p(x0, y0, z1) + ' ' + p(x1, y0, z1) + ' ' + p(x1, y0, z0) + f
            + ' ' + p(x1, y1, z1) + ' ' + p(x0, y1, z1) + ' ' + p(x0, y1, z0) + f
            + ' ' + p(x1, y0, z1) + ' ' + p(x1, y1, z1) + ' ' + p(x1, y1, z0) + f
            + ' ' + p(x0, y1, z1) + ' ' + p(x0, y0, z1) + ' ' + p(x0, y0, z0) + f + '}\n')


def model_block(n, model, origin, angles='0 0 0'):
    return '// entity %d\n{\n"classname" "misc_model"\n"model" "%s"\n"origin" "%s"\n"angles" "%s"\n}\n' % (n, model, origin, angles)


def rewrite(name, items, brushes):
    """Keeps the worldspawn's own brushes (a landing), adds the fill brushes, writes the models."""
    p = LIB + name + '.map'
    text = io.open(p, encoding='utf-8').read()
    head = text[:text.index('// entity 1')] if '// entity 1' in text else text
    close = head.rstrip().rfind('}')
    head = head[:close] + ''.join(brushes) + head[close:]
    body = ''.join(model_block(i + 1, m, o, a) for i, (m, o, a) in enumerate(items))
    io.open(p, 'w', encoding='utf-8', newline='\n').write(head + body)


def straight(modules):
    """M modules climbing along +x, centred on the origin, and the fill under each raised one."""
    items, fills = [], []
    for k in range(modules):
        ox = -16 * modules + 32 * (k + 1)
        items.append((MOD, '%g 0 %g' % (ox, 16 * k), '0 -90 0'))
        if k > 0:
            fills.append(box(ox - 32, -25, 0, ox, 25, 16 * k))
    return items, fills


for name, m in (('stairs_straight_4', 2), ('stairs_straight_8', 4), ('stairs_straight_12', 6)):
    rewrite(name, *straight(m))
# With handrails: the bar pieces (7 long, 33 high) on both step edges of each module, turned with the stair.
items, fills = straight(2)
for k in range(2):
    for side in (-23, 23):
        items.append(('tm/stairs_industrial_handrail_bars_01a', '%g %g %g' % (-12 + 32 * k, side, 16 * k + 16), '0 -90 0'))
rewrite('stairs_with_railing', items, fills)
# L-shaped: leg 1 climbs along +x into the landing's west edge (x 16; the landing brush is x 16..112, y -112..16, top
# z 32); leg 2 climbs along +y (no turn) from the landing's north edge (y 16), centred on the landing (x 64).
rewrite('stairs_L_shaped_8', [
    (MOD, '-16 -48 0', '0 -90 0'),
    (MOD, '16 -48 16', '0 -90 0'),
    (MOD, '64 48 32', '0 0 0'),
    (MOD, '64 80 48', '0 0 0'),
], [box(-16, -73, 0, 16, -23, 16), box(39, 16, 0, 89, 48, 32), box(39, 48, 0, 89, 80, 48)])
print('ok')
