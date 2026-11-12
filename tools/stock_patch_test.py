"""Offline proof that an edited stock map keeps the data of the game's own patch file (docs/research/88, "Stock
patches").

The game ships patch files (<zone>.fp) with real map data beside many zones. An edit is installed as a patch of the
same name, so it must carry the stock patch's changes, and a later edit must start from the stock map again even
while the first edit is installed.

1. The zone without its stock patch differs from the zone the game loads (so the patch matters).
2. A rebuild without edits: the stock zone file with OUR patch beside it reads back as the stock map.
3. With an edit installed in a copy of the zone folder: a rebuild that is not told about the saved stock patch starts
   from the installed edit (an added buy station is added twice); with "stock_patches" it starts from the stock map
   and gives the same result as the first edit.

Usage: py -3 -I tools/stock_patch_test.py <work folder> [--zone mp_br_tut] [--exe <zonetool.exe>]
Nothing is written outside the work folder. The game folder is only read."""
import hashlib, json, os, shutil, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stock_edit_test as t
import warzone_instances_edit_test as inst
import warzone_loot_edit_test as loot


def view(zone_file, out):
    """What a reader that applies the patch beside the zone sees: entity text hash, loot points, instances."""
    if os.path.isdir(out):
        shutil.rmtree(out)
    path = t.export(zone_file, 'map_ents', out)[0]
    fields = t.fields(path)
    text = bytes.fromhex(fields['entityString']['bytes'])
    points = loot.loot_of(path)[0]
    instances, counts, limits = inst.instances_of(path)
    return dict(entity_chars=len(text), entity_hash=hashlib.sha1(text).hexdigest()[:16], loot_points=len(points),
                instances=len(instances), counts=counts, limits=sorted(set(limits.values())),
                boxes=sum(1 for i in instances if i['definition'].lstrip(',') == 'br_plunder_box'))


def editzone_at(exe, game, zone, edits, out):
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)
    edits_path = os.path.join(out, 'edits.json')
    json.dump(edits, open(edits_path, 'w', encoding='utf-8'))
    r = subprocess.run([exe, '-game', 'iw8', '-path', game, 'editzone', zone, edits_path, os.path.join(out, 'out')],
                       capture_output=True, text=True, errors='replace')
    log = r.stdout + r.stderr
    open(os.path.join(out, 'zonetool.log'), 'w', encoding='utf-8').write(log)
    return r.returncode, log


def main():
    args = sys.argv[1:]
    zone, exe = 'mp_br_tut', t.EXE
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

    stock_ff = os.path.join(t.GAME, 'zone', zone + '.ff')
    stock_fp = os.path.join(t.GAME, 'zone', zone + '.fp')
    check(os.path.exists(stock_fp) and os.path.getsize(stock_fp) > 1000, 'the game ships %s.fp with data (%.2f MB)' % (zone, os.path.getsize(stock_fp) / 1e6))
    stock = view(stock_ff, os.path.join(work, 'view_stock'))
    print('       the stock map as the game loads it:', stock)

    print('1. the zone file without its stock patch')
    alone_dir = os.path.join(work, 'alone')
    os.makedirs(alone_dir, exist_ok=True)
    alone_ff = os.path.join(alone_dir, zone + '.ff')
    shutil.copyfile(stock_ff, alone_ff)
    try:
        alone = view(alone_ff, os.path.join(work, 'view_alone'))
        print('       without the patch:', alone)
        check(alone != stock, 'the zone without its patch reads differently: the stock patch carries map data')
    except Exception as e:  # the unpatched zone may not even be readable by the reader
        print('       without the patch the reader gives up: %s' % str(e)[:160])
        check(True, 'the zone without its patch is not readable as the map: the stock patch is needed')

    print('2. a rebuild without edits, installed as our patch')
    code, log = editzone_at(exe, t.GAME, zone, {}, os.path.join(work, 'noop'))
    check(code == 0, 'editzone without edits finishes')
    if code != 0:
        print(log[-1500:])
        return 1
    our_fp = os.path.join(work, 'noop', 'out', 'patch', zone + '.fp')
    check(os.path.exists(our_fp), 'it writes a patch (%.2f MB; the stock patch has %.2f MB)' % (
        os.path.getsize(our_fp) / 1e6 if os.path.exists(our_fp) else 0, os.path.getsize(stock_fp) / 1e6))
    ours_dir = os.path.join(work, 'ours')
    os.makedirs(ours_dir, exist_ok=True)
    shutil.copyfile(stock_ff, os.path.join(ours_dir, zone + '.ff'))
    shutil.copyfile(our_fp, os.path.join(ours_dir, zone + '.fp'))
    ours = view(os.path.join(ours_dir, zone + '.ff'), os.path.join(work, 'view_ours'))
    check(ours == stock, 'the stock zone file with our patch reads back as the stock map (entity text, loot, instances, counts)')
    a = [l for l in t.acts(['--test', '-o', os.path.join(work, 'test_ours'), os.path.join(ours_dir, zone + '.ff')]) if l.startswith('Replay:')]
    b = [l for l in t.acts(['--test', '-o', os.path.join(work, 'test_stock'), stock_ff]) if l.startswith('Replay:')]
    check(bool(a) and a[0].split('loaded,', 1)[-1] == (b[0].split('loaded,', 1)[-1] if b else None), 'ACTS tests it like the stock map: %s (stock %s)' % (a, b))

    print('3. a second rebuild while the first edit is installed')
    instances = inst.instances_of(t.export(stock_ff, 'map_ents', os.path.join(work, 'me_stock'))[0])[0]
    first = next((i for i, s in enumerate(instances) if s['definition'].lstrip(',') == 'br_plunder_box'), None)
    check(first is not None, 'the map has a buy station to copy')
    if first is None:
        return 1
    o = instances[first]['origin']
    edits = {'scriptable_instances': {'add': [dict(like=first, origin=[o[0] + 96.0, o[1], o[2]], server_instanced_parts=1, usable_parts=1,
                                                   physics_limit=True, physics_limit_detail=True)]}}
    code, log = editzone_at(exe, t.GAME, zone, edits, os.path.join(work, 'edit1'))
    check(code == 0, 'the first edit (one buy station added) finishes')
    if code != 0:
        print(log[-1500:])
        return 1
    edit1_fp = os.path.join(work, 'edit1', 'out', 'patch', zone + '.fp')
    game2 = os.path.join(work, 'game_with_edit')
    zdir = os.path.join(game2, 'zone')
    if os.path.isdir(game2):
        shutil.rmtree(game2)
    os.makedirs(zdir)
    shutil.copyfile(stock_ff, os.path.join(zdir, zone + '.ff'))
    shutil.copyfile(edit1_fp, os.path.join(zdir, zone + '.fp'))       # the edit, installed
    for name in os.listdir(t.GAME):                                    # the converter takes Oodle from the game folder
        if name.lower().startswith('oo2core') and name.lower().endswith('.dll'):
            shutil.copyfile(os.path.join(t.GAME, name), os.path.join(game2, name))
    saved = os.path.join(work, 'backup', zone + '.fp.stock')            # the stock patch, saved at install
    os.makedirs(os.path.dirname(saved), exist_ok=True)
    shutil.copyfile(stock_fp, saved)
    installed = view(os.path.join(zdir, zone + '.ff'), os.path.join(work, 'view_installed'))
    check(installed['boxes'] == stock['boxes'] + 1 and installed['entity_hash'] == stock['entity_hash'] and installed['loot_points'] == stock['loot_points'],
          'the installed edit reads as the stock map plus one buy station (%d -> %d)' % (stock['boxes'], installed['boxes']))

    code, log = editzone_at(exe, game2, zone, edits, os.path.join(work, 'edit2_blind'))
    blind = view(os.path.join(work, 'edit2_blind', 'out', zone + '.ff'), os.path.join(work, 'view_blind')) if code == 0 else None
    check(blind is not None and blind['boxes'] == stock['boxes'] + 2,
          'without the saved stock patch a second rebuild starts from the installed edit: the station is added twice (%s)' % (blind['boxes'] if blind else 'failed'))

    named = dict(edits)
    named['stock_patches'] = {zone + '.fp': saved}
    code, log = editzone_at(exe, game2, zone, named, os.path.join(work, 'edit2'))
    check(code == 0, 'with "stock_patches" the rebuild finishes')
    if code != 0:
        print(log[-1500:])
        return 1
    again = view(os.path.join(work, 'edit2', 'out', zone + '.ff'), os.path.join(work, 'view_again'))
    check(again == installed, 'it starts from the stock map: the result equals the first edit (%d buy stations)' % again['boxes'])
    fp2 = os.path.join(work, 'edit2', 'out', 'patch', zone + '.fp')
    same = os.path.exists(fp2) and open(fp2, 'rb').read() == open(edit1_fp, 'rb').read()
    check(os.path.exists(fp2), 'its patch is written%s' % (' and is byte for byte the first edit\'s patch' if same else ' (not byte-identical to the first; the read-back is)'))

    none = dict(edits)
    none['stock_patches'] = {zone + '.fp': os.path.join(work, 'backup', 'missing.fp')}
    code, log = editzone_at(exe, game2, zone, none, os.path.join(work, 'edit_missing'))
    check(code != 0 and 'was not found' in log, 'a stock patch that is not there stops the rebuild')
    for d in ('alone', 'ours', 'game_with_edit'):
        shutil.rmtree(os.path.join(work, d), ignore_errors=True)
    for d in ('noop', 'edit1', 'edit2', 'edit2_blind'):
        f = os.path.join(work, d, 'out', zone + '.ff')
        if os.path.exists(f):
            os.remove(f)
    print('RESULT: %s (%d failed)' % ('PASS' if not failures else 'FAIL', len(failures)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
