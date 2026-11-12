"""Offline proof of brushes added to a stock map (roadmap 11.4, docs/research/88_stock_map_editing.md).

The editor builds the added brushes as a model of a small helper map with its own headless build (the same path as
File > Rebuild Stock Zone: IW8Radiant.exe --compile), then the converter's editzone carries that model from the
helper map's package into the stock zone and a script_model places it.

1. The helper map (a room, two spawns, a script_model with an OBJ model of a box in a stock material) builds.
2. editzone on the stock zone with the entity text plus the script_model, and carry {zone, xmodel, name, folder}:
   the zone holds the model with its physics (collision) and its materials, the model is in the level model table,
   the script_model is in the entity text, and ACTS tests the zone like the stock one.

Usage: py -3 -I tools/stock_overlay_test.py <work folder> [--zone mp_br_tut] [--exe <zonetool.exe>] [--editor <IW8Radiant.exe>]
Nothing is written outside the work folder and the editor's own output folder. The game folder is only read."""
import glob, json, os, re, shutil, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stock_edit_test as t

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAP_ID = 'mp_iw8rovl'
MODEL = 'iw8r_ovl_prooftest01'
MATERIAL = 'tm2co/me_curb_stone_01'


def box_obj(hx, hy, hz, material):
    """A box centred on the origin as a Y-up OBJ (x, z, -y), outward faces, as the editor writes added brushes."""
    corners = [(x, y, z) for z in (-hz, hz) for y in (-hy, hy) for x in (-hx, hx)]
    faces = [(0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)]
    o = ['g box', 'usemtl ' + material]
    o += ['v %g %g %g' % (x, z, -y) for x, y, z in corners]
    o += ['vt %g %g' % (x / 64.0, y / 64.0) for x, y, z in corners]
    for f in faces:
        for tri in ((f[0], f[1], f[2]), (f[0], f[2], f[3])):
            o.append('f ' + ' '.join('%d/%d' % (i + 1, i + 1) for i in tri))
    return '\n'.join(o) + '\n'


def room_map(models):
    def brush(x0, y0, z0, x1, y1, z1):
        v = lambda a, b, c: '( %d %d %d )' % (a, b, c)
        m = ' iw8r_overlay_room 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n'
        return ('{\n ' + v(x1, y1, z0) + ' ' + v(x0, y1, z0) + ' ' + v(x0, y0, z0) + m + ' ' + v(x0, y0, z1) + ' ' + v(x0, y1, z1) + ' ' +
                v(x1, y1, z1) + m + ' ' + v(x0, y0, z1) + ' ' + v(x1, y0, z1) + ' ' + v(x1, y0, z0) + m + ' ' + v(x1, y1, z1) + ' ' + v(x0, y1, z1) +
                ' ' + v(x0, y1, z0) + m + ' ' + v(x1, y0, z1) + ' ' + v(x1, y1, z1) + ' ' + v(x1, y1, z0) + m + ' ' + v(x0, y1, z1) + ' ' +
                v(x0, y0, z1) + ' ' + v(x0, y0, z0) + m + '}\n')
    t_ = 'iwmap 4\n// entity 0\n{\n"classname" "worldspawn"\n'
    for b in ((-256, -256, -16, 256, 256, 0), (-256, -256, 256, 256, 256, 272), (-272, -256, 0, -256, 256, 256), (256, -256, 0, 272, 256, 256),
              (-256, -272, 0, 256, -256, 256), (-256, 256, 0, 256, 272, 256)):
        t_ += brush(*b)
    t_ += '}\n'
    for cls, x in (('mp_tdm_spawn', -128), ('mp_tdm_spawn_allies_start', -160), ('mp_tdm_spawn_axis_start', 160), ('mp_dm_spawn_start', 128),
                   ('mp_global_intermission', 0)):
        t_ += '{\n"classname" "%s"\n"origin" "%d 0 0"\n}\n' % (cls, x)
    # A script_model that another entity targets stays a live model of its own name (with its physics, so it
    # collides); an untargeted one would be built into the world as a static prop.
    for m in models:
        t_ += '{\n"classname" "script_model"\n"model" "%s"\n"targetname" "%s"\n"origin" "0 0 64"\n}\n' % (m, m)
        t_ += '{\n"classname" "script_origin"\n"target" "%s"\n"origin" "0 0 32"\n}\n' % m
    return t_


def main():
    args = sys.argv[1:]
    zone, exe, editor = 'mp_br_tut', t.EXE, os.path.join(ROOT, 'bin', 'release', 'IW8Radiant.exe')
    for flag in ('--zone', '--exe', '--editor'):
        while flag in args:
            i = args.index(flag)
            if flag == '--zone': zone = args[i + 1]
            elif flag == '--exe': exe = args[i + 1]
            else: editor = args[i + 1]
            del args[i:i + 2]
    work = os.path.abspath(args[0])
    if os.path.isdir(work):
        shutil.rmtree(work)
    os.makedirs(os.path.join(work, 'overlay', 'models'))
    failures = []

    def check(ok, text):
        print(('  ok   ' if ok else '  FAIL ') + text)
        if not ok:
            failures.append(text)

    print('1. the helper map with the added brushes as a model')
    open(os.path.join(work, 'overlay', 'models', MODEL + '.obj'), 'w', newline='\n').write(box_obj(64, 48, 40, MATERIAL))
    map_path = os.path.join(work, 'overlay', MAP_ID + '.map')
    open(map_path, 'w', newline='\n').write(room_map([MODEL]))
    result = os.path.join(work, 'overlay', 'result')
    r = subprocess.run([editor, '--compile', map_path, '--out', result], capture_output=True, text=True, errors='replace', timeout=3600)
    res = open(os.path.join(result, 'compile-result.txt'), encoding='utf-8').read().strip() if os.path.exists(os.path.join(result, 'compile-result.txt')) else ''
    check(r.returncode == 0 and res.startswith('ok'), 'the editor builds the helper map (exit %d)' % r.returncode)
    if not res.startswith('ok'):
        print(open(os.path.join(result, 'compile-log.txt'), encoding='utf-8', errors='replace').read()[-3000:])
        return 1
    package = res.split('package=', 1)[1].strip()
    check(os.path.exists(os.path.join(package, MAP_ID + '.ff')), 'its package has %s.ff' % MAP_ID)

    print('2. the model carried into %s and placed' % zone)
    stock_file = os.path.join(t.GAME, 'zone', zone + '.ff')
    me = t.fields(t.export(stock_file, 'map_ents', os.path.join(work, 'stock_me'))[0])
    text = bytes.fromhex(me['entityString']['bytes']).decode('latin1').rstrip('\0')
    if not text.endswith('\n'):
        text += '\n'
    text += '{\n212 "script_model"\n652 "%s"\n709 "100 200 40"\n}\n' % MODEL
    ents = os.path.join(work, 'edit.ents')
    open(ents, 'wb').write(text.encode('latin1'))
    edits = {'entities': ents, 'carry': [{'zone': MAP_ID, 'type': 'xmodel', 'name': MODEL, 'folder': package}]}
    code, log, rep = t.editzone(exe, zone, edits, os.path.join(work, 'out'))
    check(code == 0, 'editzone finishes (%s)' % rep)
    if code != 0:
        print(log[-2000:])
        return 1
    out_zone = os.path.join(work, 'out', zone + '.ff')
    xm = t.export(out_zone, 'xmodel', os.path.join(work, 'acts_xm'))
    mine = [f for f in xm if os.path.basename(f).startswith(MODEL + '.')]
    check(len(mine) == 1, 'the zone holds the model %s' % MODEL)
    if mine:
        f = json.load(open(mine[0], encoding='utf-8'))['asset']['fields']
        s = json.dumps(f)
        pa = f.get('physicsAsset')
        check(isinstance(pa, dict) and pa.get('type') == 'PhysicsAsset' and pa.get('name') == MODEL, 'the model has its physics asset (collision): %s' % (pa or {}).get('name'))
    ncs = glob.glob(os.path.join(t.export(out_zone, 'netconststrings', os.path.join(work, 'acts_ncs'))[0].rsplit('netconststrings', 1)[0], 'netconststrings', 'ncs_mdl_level*.json'))
    check(bool(ncs) and MODEL in open(ncs[0], encoding='utf-8').read(), 'the model is in the level model table')
    me2 = t.fields(t.export(out_zone, 'map_ents', os.path.join(work, 'out_me'))[0])
    text2 = bytes.fromhex(me2['entityString']['bytes']).decode('latin1')
    check(re.search(r'212 "script_model"\s+652 "%s"\s+709 "100 200 40"' % MODEL, text2) is not None, 'the script_model is in the entity text')
    test = [l for l in t.acts(['--test', '-o', os.path.join(work, 'test'), out_zone]) if l.startswith('Replay:')]
    stock_test = [l for l in t.acts(['--test', '-o', os.path.join(work, 'stock_test'), stock_file]) if l.startswith('Replay:')]
    tail = lambda lines: lines[0].split('tested,', 1)[1].strip() if lines and 'tested,' in lines[0] else None
    check(tail(test) is not None and tail(test) == tail(stock_test), 'ACTS tests it like the stock zone: %s (stock %s)' % (test, stock_test))
    techsets = os.path.join(work, 'out', 'techsets_' + zone + '.ff')
    if os.path.exists(techsets):
        tt = [l for l in t.acts(['--test', '-o', os.path.join(work, 'test_tech'), techsets]) if l.startswith('Replay:')]
        check(bool(tt) and ' 0 failed' in tt[0], 'the techsets zone with the carried material loads: %s' % tt)
    print('RESULT: %s (%d failed)' % ('PASS' if not failures else 'FAIL', len(failures)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
