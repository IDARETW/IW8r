"""Checks, on stock zones, how a scriptable instance's dynamic entity copies its placement (read only):
  - DynEntityDef.initialPose.origin == the instance's initialOrigin;
  - initialPose.quat == the quaternion of the instance's initialAngles (pitch, yaw, roll; Quake / CoD order);
  - spatialPartitionIndex == the leaf reached by walking the list's spatial partition tree from the origin
    (node i: axis 3 = leaf; else origin[axis] < dist -> i + 1, else rightChild).

Usage: py -3 -I tools/dynent_pose_rules.py <work folder> [zone ...]"""
import math, os, shutil, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stock_edit_test as t
from warzone_instances_edit_test import instances_of


def quat(p, y, r):
    """CoD AnglesToQuat: yaw about Z, pitch about Y, roll about X (R = Rz(yaw) Ry(pitch) Rx(roll)); (x, y, z, w)."""
    hp, hy, hr = (math.radians(a) * 0.5 for a in (p, y, r))
    cp, sp, cy, sy, cr, sr = math.cos(hp), math.sin(hp), math.cos(hy), math.sin(hy), math.cos(hr), math.sin(hr)
    return (sr * cp * cy - cr * sp * sy, cr * sp * cy + sr * cp * sy, cr * cp * sy - sr * sp * cy, cr * cp * cy + sr * sp * sy)


def same_rotation(a, b, tol=2e-3):
    return all(abs(x - y) < tol for x, y in zip(a, b)) or all(abs(x + y) < tol for x, y in zip(a, b))


def partitions_of(dyn):
    pop = dyn.get('dynEntSpatialPopulation')
    if isinstance(pop, list):
        pop = (pop[0] or {}) if pop else {}
    tree = (pop or {}).get('tree')
    if isinstance(tree, dict) and 'values' in tree:
        tree = (tree['values'] or [None])[0]
    parts = (tree or {}).get('partitions')
    if isinstance(parts, dict):
        parts = parts.get('values')
    return parts or []


def leaf(parts, o):
    i = 0
    for _ in range(100000):
        if i >= len(parts):
            return None
        n = parts[i]
        axis, dist, right = n.get('axis'), n.get('dist'), n.get('rightChild')
        if axis == 3:
            return i
        i = i + 1 if o[axis] < dist else right
    return None


def main():
    work = os.path.abspath(sys.argv[1])
    zones = sys.argv[2:] or ['mp_backlot2', 'mp_shipment', 'mp_br_tut', 'mp_br_quarry']
    for z in zones:
        zf = os.path.join(t.GAME, 'zone', z + '.ff')
        st, _, _ = instances_of(t.export(zf, 'map_ents', os.path.join(work, z, 'me'))[0])
        dyn = t.fields(t.export(zf, 'dynentitylist', os.path.join(work, z, 'dyn'))[0])
        parts = partitions_of(dyn)
        n = origin_ok = quat_ok = part_ok = 0
        bad = []
        for e in (dyn['dynEntDefList'][0] or {}).get('values') or []:
            i = e.get('scriptableMapIndex', 500000)
            if i >= len(st):
                continue
            n += 1
            pose = e['initialPose']
            o, q = pose['origin']['v'], pose['quat']['v']
            s = st[i]
            origin_ok += all(abs(a - b) < 1e-3 for a, b in zip(o, s['origin']))
            if same_rotation(q, quat(*s['angles'])):
                quat_ok += 1
            elif len(bad) < 3:
                bad.append((s['definition'], s['angles'], [round(x, 4) for x in q]))
            lf = leaf(parts, o) if parts else None
            part_ok += (lf == e.get('spatialPartitionIndex'))
        print('%s: %d scriptable dynents; origin %d, quat %d, partition %d (tree %d nodes) %s' %
              (z, n, origin_ok, quat_ok, part_ok, len(parts), bad))
        shutil.rmtree(os.path.join(work, z), ignore_errors=True)


if __name__ == '__main__':
    main()
