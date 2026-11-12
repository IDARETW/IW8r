"""Offline proof of the converter's editzone command (docs/research/88_stock_map_editing.md).

Edits a stock map zone in memory (entities with the compiled spawn list, hidden static models, assets carried from
another stock zone) and checks the result with ACTS. Nothing is written outside the work folder; the game folder is
only read.

Usage: py -3 -I tools/stock_edit_test.py <work folder> [--zone mp_shipment] [--exe <zonetool.exe>]"""
import glob, json, os, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = r'C:\zsw-iw8r\scriptables\build\bin\x64\Release\zonetool.exe'
GAME = r'E:\IW8\Builds\1.20-replay'
ACTS = r'E:\mw124\atian-cod-tools\build\bin\Release\acts.exe'
NUL = chr(0)
LF = chr(10)


def acts(tail):
    args = [ACTS, '--noUpdater', 'fastfile', '-r', 'mw19replay', '-g', os.path.join(GAME, 'game_dx12_ship_replay.exe'),
            '--oodle', os.path.join(GAME, 'oo2core_7_win64.dll')] + tail
    r = subprocess.run(args, capture_output=True, text=True, errors='replace')
    return (r.stdout + r.stderr).splitlines()


def export(zone_file, kind, out):
    shutil.rmtree(out, ignore_errors=True)
    acts(['-a', kind, '-o', out, zone_file])
    return glob.glob(os.path.join(out, '**', kind, '**', '*.json'), recursive=True)


def fields(path):
    d = json.load(open(path, encoding='utf-8'))
    return d.get('asset', d).get('fields', d)


def strings_of(export_file):
    root = export_file
    while os.path.basename(root) != 'assets':
        root = os.path.dirname(root)
    return json.load(open(os.path.join(os.path.dirname(root), 'script_strings.json'), encoding='utf-8'))


def entity_text(me):
    return bytes.fromhex(me['entityString']['bytes']).decode('latin-1').rstrip(NUL)


def editzone(exe, zone, edits, out):
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    edits_path = out + '.edits.json'
    json.dump(edits, open(edits_path, 'w', encoding='utf-8'))
    r = subprocess.run([exe, '-game', 'iw8', '-path', GAME, 'editzone', zone, edits_path, out], capture_output=True, text=True, errors='replace')
    log = r.stdout + r.stderr
    open(os.path.join(out, 'editzone.log'), 'w', encoding='utf-8').write(log)
    report = {}
    rp = os.path.join(out, zone + '.edit-report.json')
    if os.path.exists(rp):
        report = json.load(open(rp, encoding='utf-8'))
    return r.returncode, log, report


def spawn_rows(me, strings):
    """Compiled spawn records with their script strings resolved through the zone's own string list."""
    def text(node):
        i = node['value']
        return strings[i] if i and i < len(strings) else None
    rows = []
    for v in (me['spawnList'].get('spawns') or {}).get('values') or []:
        rows.append((v['index'], text(v['name']), text(v['target']), text(v['script_noteworthy']),
                     tuple(round(x, 3) for x in v['origin']['v']), tuple(round(x, 3) for x in v['angles']['v'])))
    return rows


def spawn_entities(rows):
    """The compiled spawns as the editor shows them: one entity each, numeric keys."""
    def num(x):
        return ('%.6f' % x).rstrip('0').rstrip('.')
    out = ''
    for index, name, target, noteworthy, origin, angles in rows:
        lines = ['{', '212 "%s"' % name, '709 "%s"' % ' '.join(num(x) for x in origin), '80 "%s"' % ' '.join(num(x) for x in angles)]
        if target:
            lines.append('1070 "%s"' % target)
        if noteworthy:
            lines.append('845 "%s"' % noteworthy)
        lines.append('}')
        out += LF.join(lines) + LF
    return out


def main():
    args = sys.argv[1:]
    zone, exe = 'mp_shipment', EXE
    while '--zone' in args:
        i = args.index('--zone'); zone = args[i + 1]; del args[i:i + 2]
    while '--exe' in args:
        i = args.index('--exe'); exe = args[i + 1]; del args[i:i + 2]
    work = os.path.abspath(args[0])
    os.makedirs(work, exist_ok=True)
    failures = []

    def check(ok, text):
        print(('  ok   ' if ok else '  FAIL ') + text)
        if not ok:
            failures.append(text)

    stock_file = os.path.join(GAME, 'zone', zone + '.ff')
    stock_export = export(stock_file, 'map_ents', os.path.join(work, 'stock_me'))[0]
    stock_me = fields(stock_export)
    text = entity_text(stock_me)
    stock_spawns = spawn_rows(stock_me, strings_of(stock_export))
    print('stock %s: %d entity characters, %d compiled spawns' % (zone, len(text) + 1, len(stock_spawns)))
    stock_test = [l for l in acts(['--test', '-o', os.path.join(work, 'stock_test'), stock_file]) if l.startswith('Replay:')]

    print('1. the stock entity text with the stock spawns as entities, written back')
    ents_a = os.path.join(work, 'a.ents')
    open(ents_a, 'w', encoding='latin-1', newline='').write(text + spawn_entities(stock_spawns))
    code, log, rep = editzone(exe, zone, {'entities': ents_a}, os.path.join(work, 'a'))
    check(code == 0, 'editzone finishes (%s)' % rep)
    if code != 0:
        print(log[-2000:])
        return 1
    a_file = os.path.join(work, 'a', zone + '.ff')
    test = [l for l in acts(['--test', '-o', os.path.join(work, 'a_test'), a_file]) if l.startswith('Replay:')]
    check(test == stock_test and bool(test), 'ACTS loads it like the stock zone: %s' % test)
    a_export = export(a_file, 'map_ents', os.path.join(work, 'a_me'))[0]
    a_me = fields(a_export)
    check(entity_text(a_me) == text, 'the entity string is the stock one again (the spawn entities were taken out)')
    a_spawns = spawn_rows(a_me, strings_of(a_export))
    check(a_spawns == stock_spawns, 'the spawn list compiled from the entities equals the stock list (%d records, names included)' % len(stock_spawns))
    if a_spawns != stock_spawns:
        print('       counts', len(stock_spawns), len(a_spawns))
        for i in [i for i, (x, y) in enumerate(zip(stock_spawns, a_spawns)) if x != y][:3]:
            print('       stock', stock_spawns[i])
            print('       ours ', a_spawns[i])

    print('2. a moved spawn, a new script_model, two hidden props and a model carried from mp_backlot2')
    moved_spawns = list(stock_spawns)
    r = moved_spawns[3]
    moved_spawns[3] = (r[0], r[1], r[2], r[3], tuple(round(x + 16.0, 3) for x in r[4]), r[5])
    model = 'door_wooden_panel_03_painted_left_mp_nophysics'
    added = LF.join(['{', '212 "script_model"', '709 "0 0 64"', '80 "0 0 0"', '712 "%s"' % model, '1071 "iw8r_added_door"', '}']) + LF
    new_text = text.rstrip(LF) + LF + added
    ents_b = os.path.join(work, 'b.ents')
    open(ents_b, 'w', encoding='latin-1', newline='').write(new_text + spawn_entities(moved_spawns))
    edits = {'entities': ents_b, 'hide_static_models': [0, 5],
             'carry': [{'zone': 'mp_backlot2', 'type': 'xmodel', 'name': model}]}
    code, log, rep = editzone(exe, zone, edits, os.path.join(work, 'b'))
    check(code == 0, 'editzone finishes (%s)' % rep)
    if code != 0:
        print(log[-2500:])
        return 1
    for line in log.splitlines():
        if 'scriptables:' in line or 'warn' in line.lower():
            print('       ' + line.strip()[:220])
    b_file = os.path.join(work, 'b', zone + '.ff')
    b_tech = os.path.join(work, 'b', 'techsets_' + zone + '.ff')
    test = [l for l in acts(['--test', '-o', os.path.join(work, 'b_test'), b_file]) if l.startswith('Replay:')]
    check(bool(test) and ' 0 failed' in test[0], 'ACTS loads the edited zone: %s' % test)
    check(os.path.exists(b_tech), 'a techsets companion was written for the carried materials')
    if os.path.exists(b_tech):
        ttest = [l for l in acts(['--test', '-o', os.path.join(work, 'b_ttest'), b_tech]) if l.startswith('Replay:')]
        check(bool(ttest) and ' 0 failed' in ttest[0], 'ACTS loads the edited techsets zone: %s' % ttest)
    b_export = export(b_file, 'map_ents', os.path.join(work, 'b_me'))[0]
    b_me = fields(b_export)
    got = entity_text(b_me)
    check(got == new_text, 'the entity string read back is the edited text without the spawn entities (%d characters)' % len(got))
    b_spawns = spawn_rows(b_me, strings_of(b_export))
    changed = [i for i, (x, y) in enumerate(zip(stock_spawns, b_spawns)) if x != y]
    check(len(b_spawns) == len(stock_spawns) and changed == [3], 'exactly the one compiled spawn moved (%s)' % changed)
    if changed == [3]:
        d = [round(b - a, 3) for a, b in zip(stock_spawns[3][4], b_spawns[3][4])]
        check(d == [16.0, 16.0, 16.0], 'it moved by 16 units on each axis (%s)' % d)
    models = [os.path.basename(p) for p in export(b_file, 'xmodel', os.path.join(work, 'b_xm'))]
    check(any(p.startswith(model + '.') for p in models), 'the carried model is in the edited zone (%d models)' % len(models))
    listed = False
    for p in export(b_file, 'netconststrings', os.path.join(work, 'b_ncs')):
        if os.path.basename(p).startswith('ncs_mdl_level'):
            listed = model in fields(p).get('strings', [])
    check(listed, 'the level model table names the carried model')
    check(rep.get('hidden_static_models') == 2 and rep.get('static_model_instances', 0) > 5,
          'the report counts 2 hidden instances of %s' % rep.get('static_model_instances'))
    world = export(b_file, 'gfx_map', os.path.join(work, 'b_world'))
    stock_world = export(stock_file, 'gfx_map', os.path.join(work, 'stock_world'))
    if world and stock_world:
        def scales(path):
            data = fields(path).get('smodels', {}).get('smodelInstanceData')
            values = data.get('values') if isinstance(data, dict) else None
            return [v.get('scale') for v in values] if values else None
        ours, theirs = scales(world[0]), scales(stock_world[0])
        if ours and theirs:
            diff = [i for i, (x, y) in enumerate(zip(theirs, ours)) if x != y]
            check(diff == [0, 5] and ours[0] == 0 and ours[5] == 0, 'only instances 0 and 5 changed, to scale 0 (%s)' % diff[:8])
        else:
            print('       (ACTS does not decode the instance scales; the hide is checked through the report only)')
    print('3. the install patch: a copy of the stock zone with the patch beside it')
    patch = os.path.join(work, 'b', 'patch', zone + '.fp')
    check(os.path.exists(patch), 'editzone wrote %s.fp (%.1f MB; the edited zone is %.1f MB)' % (
        zone, os.path.getsize(patch) / 1e6 if os.path.exists(patch) else 0, os.path.getsize(b_file) / 1e6))
    if os.path.exists(patch):
        staged = os.path.join(work, 'staged')
        shutil.rmtree(staged, ignore_errors=True)
        os.makedirs(staged)
        shutil.copy(stock_file, os.path.join(staged, zone + '.ff'))
        shutil.copy(patch, os.path.join(staged, zone + '.fp'))
        lines = acts(['--test', '-o', os.path.join(work, 'staged_test'), os.path.join(staged, zone + '.ff')])
        test = [l for l in lines if l.startswith('Replay:')]
        check(any('fp patch' in l for l in lines), 'ACTS applies the patch to the stock file')
        check(bool(test) and ' 0 failed' in test[0], 'the patched stock zone loads: %s' % test)
        s_export = export(os.path.join(staged, zone + '.ff'), 'map_ents', os.path.join(work, 'staged_me'))
        if s_export:
            s_me = fields(s_export[0])
            check(entity_text(s_me) == new_text, 'stock file + patch reads back as the edited entity text')
            check(spawn_rows(s_me, strings_of(s_export[0])) == b_spawns, 'and as the edited spawn list')
        else:
            check(False, 'ACTS exports the entities of the patched stock zone')
        shutil.rmtree(staged, ignore_errors=True)  # the 190 MB copy is not kept
    print('RESULT: %s (%d failed)' % ('PASS' if not failures else 'FAIL', len(failures)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
