"""Offline proof of drivable stock vehicles on a custom map (docs/research/89_warzone_work.md, "Vehicles").

The stock Warzone maps place vehicles with structs in their entity text (script_struct_mp_atv, _tacrover, _jeep,
_cargotruck, _littlebird: model, vehicletype, targetname <kind>_spawn); the vehicle definitions live in the map zone
of mp_br_quarry / mp_donetsk, not in an always-loaded zone. A custom map gets them through "carry_assets" in its
collision JSON.

1. A map without carry_assets: the new converter gives byte-identical zones.
2. The same map with one ATV spawn struct and carry_assets [{mp_br_quarry, vehicle, atango_physics_mp}]:
   - the converter finishes; the map zone holds the vehicle definition, and it reads back equal to the stock one;
   - the struct is in the entity text with its keys;
   - the vehicle's model is in the level model table;
   - ACTS loads the map zone and the techsets zone with 0 failed.

Usage: py -3 -I tools/vehicle_carry_test.py <extraction folder> <map id> <work folder> [--new <zonetool.exe>]
Nothing is written outside the work folder. The game folder is only read."""
import glob, json, os, re, shutil, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scriptable_offline_test as t

VEHICLE = 'atango_physics_mp'
MODEL = 'veh8_mil_lnd_atango_physics_mp'
STRUCT = {'classname': 'script_struct_mp_atv', 'model': MODEL, 'vehicletype': VEHICLE, 'targetname': 'atv_spawn',
          'script_gameobjectname': 'br', 'origin': '64 96 0', 'angles': '0 38 0'}


def strip(node):
    if isinstance(node, dict):
        return {k: strip(v) for k, v in node.items() if k not in ('address',)}
    if isinstance(node, list):
        return [strip(v) for v in node]
    return node


def export(zone_file, kind, out):
    if os.path.isdir(out):
        shutil.rmtree(out)
    t.acts_export(zone_file, kind, out)
    return sorted(glob.glob(os.path.join(out, '**', kind, '**', '*.json'), recursive=True))


def resolved(fields, export_dir):
    """The fields with script-string indices ({"value": n} under a name-like key) replaced by their text: a carried
    definition is written with the indices of its new zone."""
    tables = glob.glob(os.path.join(export_dir, '**', 'script_strings.json'), recursive=True)
    strings = json.load(open(tables[0], encoding='utf-8')) if tables else []

    def walk(node):
        if isinstance(node, dict):
            if set(node.keys()) == {'value'} and isinstance(node['value'], int) and 0 <= node['value'] < len(strings):
                return {'string': strings[node['value']]}
            return {k: walk(v) for k, v in node.items() if k != 'address'}
        if isinstance(node, list):
            return [walk(v) for v in node]
        return node
    return walk(fields)


def main():
    args = sys.argv[1:]
    new = t.NEW
    if '--new' in args:
        new = args[args.index('--new') + 1]
        del args[args.index('--new'):args.index('--new') + 2]
    extraction, map_id, work = os.path.abspath(args[0]), args[1], os.path.abspath(args[2])
    metadata = os.path.join(os.path.dirname(extraction), 'metadata.json')
    os.makedirs(work, exist_ok=True)
    failures = []

    def check(ok, text):
        print(('  ok   ' if ok else '  FAIL ') + text)
        if not ok:
            failures.append(text)

    print('1. without carry_assets')
    code_a, _ = t.build(t.LIVE, extraction, map_id, os.path.join(work, 'live'), metadata)
    code_b, _ = t.build(new, extraction, map_id, os.path.join(work, 'plain'), metadata)
    za, zb = t.zones(os.path.join(work, 'live'), map_id), t.zones(os.path.join(work, 'plain'), map_id)
    check(code_a == 0 and code_b == 0 and len(za) == 5 and za.keys() == zb.keys(), 'both converters finish with the five zones')
    for name in sorted(za):
        check(za[name] == zb.get(name), '%s is byte-identical' % name)

    print('2. one ATV spawn, the ATV carried from mp_br_quarry')
    copy = os.path.join(work, 'extraction')
    if os.path.isdir(copy):
        shutil.rmtree(copy)
    shutil.copytree(extraction, copy)
    shutil.copy(metadata, os.path.join(work, 'metadata.json'))
    mp = os.path.join(copy, 'extracted', 'maps', 'mp')
    col_path = os.path.join(mp, map_id + '.d3dbsp.replay-collision.json')
    col = json.load(open(col_path, encoding='utf-8'))
    # The vehicle definition does not name its model: script spawns the model by name with the vehicle type, so both
    # are carried (the model also joins the level model table).
    col['carry_assets'] = [{'zone': 'mp_br_quarry', 'type': 'vehicle', 'name': VEHICLE}, {'zone': 'mp_br_quarry', 'type': 'xmodel', 'name': MODEL}]
    json.dump(col, open(col_path, 'w', encoding='utf-8'))
    ents_path = os.path.join(mp, map_id + '.d3dbsp.ents')
    ents = open(ents_path, 'rb').read().decode('latin1')
    nl = '\r\n' if '\r\n' in ents else '\n'
    ents = ents.rstrip('\0') + '{' + nl + ''.join('"%s" "%s"%s' % (k, v, nl) for k, v in STRUCT.items()) + '}' + nl
    open(ents_path, 'wb').write(ents.encode('latin1'))
    out = os.path.join(work, 'carried')
    code, log = t.build(new, copy, map_id, out, os.path.join(work, 'metadata.json'))
    check(code == 0, 'the converter finishes (exit %d)' % code)
    for line in log.splitlines():
        if 'carry' in line.lower() or 'scriptables:' in line:
            print('       ' + line.strip()[:220])
    if code != 0:
        print(log[-3000:])
        return 1
    zone = os.path.join(out, map_id, map_id + '.ff')
    techsets = os.path.join(out, map_id, 'techsets_' + map_id + '.ff')

    built = export(zone, 'vehicle', os.path.join(work, 'acts_vehicle'))
    stock = export(os.path.join(t.GAME, 'zone', 'mp_br_quarry.ff'), 'vehicle', os.path.join(work, 'acts_vehicle_stock'))
    mine = [resolved(json.load(open(f, encoding='utf-8'))['asset']['fields'], os.path.join(work, 'acts_vehicle')) for f in built]
    theirs = [resolved(json.load(open(f, encoding='utf-8'))['asset']['fields'], os.path.join(work, 'acts_vehicle_stock')) for f in stock]
    name_of = lambda f: (f.get('name') or {}).get('string') if isinstance(f.get('name'), dict) else f.get('name')
    m = [f for f in mine if name_of(f) == VEHICLE]
    s = [f for f in theirs if name_of(f) == VEHICLE]
    check(len(m) == 1, 'the map zone holds the vehicle definition %s' % VEHICLE)
    if m and s:
        check(strip(m[0]) == strip(s[0]), 'it reads back equal to the stock definition (%d bytes of JSON)' % len(json.dumps(strip(s[0]))))

    me = export(zone, 'map_ents', os.path.join(work, 'acts_me'))
    if me:
        f = json.load(open(me[0], encoding='utf-8'))['asset']['fields']
        text = bytes.fromhex(f['entityString']['bytes']).decode('latin1')
        found = [e for e in re.findall(r'\{([^{}]*)\}', text) if 'script_struct_mp_atv' in e]
        xm = export(zone, 'xmodel', os.path.join(work, 'acts_xmodel'))
        check(any(os.path.basename(x).startswith(MODEL + '.') for x in xm), 'the map zone holds the vehicle model %s' % MODEL)
        check(len(found) == 1 and VEHICLE in found[0] and MODEL in found[0] and 'atv_spawn' in found[0], 'the ATV spawn struct is in the entity text with its keys')
    ncs = export(zone, 'netconststrings', os.path.join(work, 'acts_ncs'))
    listed = False
    for path in ncs:
        if os.path.basename(path).startswith('ncs_mdl_level') and MODEL in open(path, encoding='utf-8').read():
            listed = True
    check(listed, 'the vehicle model %s is in the level model table' % MODEL)

    for z, label in ((zone, 'the map zone'), (techsets, 'the techsets zone')):
        replay = t.acts_test(z, os.path.join(work, 'acts_test_' + os.path.basename(z)[:-3]))
        check(bool(replay) and all(' 0 failed' in l for l in replay), 'ACTS loads %s with 0 failed: %s' % (label, replay))
    print('RESULT: %s (%d failed)' % ('PASS' if not failures else 'FAIL', len(failures)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
