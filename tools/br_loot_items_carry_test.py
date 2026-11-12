"""Offline feasibility test: carry the loot item definitions of a loot table into a custom map (research 93: a custom
map never loads common_br_mp, which holds the brloot_* scriptables the loot tables spawn).

The extraction's collision JSON gets five loot points and carry_assets with one {"zone": "common_br_mp", "type":
"scriptable", "name": <item>} per item row of the table; the converter builds the map, ACTS tests the zone, and the
script counts the scriptable definitions in the map zone and the zone sizes.

Usage: py -3 -I tools/br_loot_items_carry_test.py <extraction folder> <map id> <work folder> <loot table csv>
       [--limit N] [--exe <zonetool.exe>]
Nothing is written outside the work folder. The game folder is only read."""
import csv, glob, json, os, shutil, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scriptable_offline_test as t


def items_of(table):
    out = []
    with open(table, encoding='utf-8', newline='') as f:
        for row in csv.reader(f):
            if len(row) > 1 and row[0] == 'item' and row[1] and row[1] not in out:
                out.append(row[1])
    return out


def main():
    args = sys.argv[1:]
    exe, limit = t.LIVE, 0
    if '--exe' in args:
        exe = args[args.index('--exe') + 1]
        del args[args.index('--exe'):args.index('--exe') + 2]
    if '--limit' in args:
        limit = int(args[args.index('--limit') + 1])
        del args[args.index('--limit'):args.index('--limit') + 2]
    extraction, map_id, work, table = os.path.abspath(args[0]), args[1], os.path.abspath(args[2]), args[3]
    metadata = os.path.join(os.path.dirname(extraction), 'metadata.json')
    items = items_of(table)
    if limit:
        items = items[:limit]
    print('%d loot items in %s' % (len(items), os.path.basename(table)))
    os.makedirs(work, exist_ok=True)
    copy = os.path.join(work, 'extraction')
    if os.path.isdir(copy):
        shutil.rmtree(copy)
    shutil.copytree(extraction, copy)
    shutil.copy(metadata, os.path.join(work, 'metadata.json'))
    col_path = os.path.join(copy, 'extracted', 'maps', 'mp', map_id + '.d3dbsp.replay-collision.json')
    col = json.load(open(col_path, encoding='utf-8'))
    col['loot_points'] = [{'origin': [64 * i, 0, 0], 'angles': [0, 0, 0], 'cache': i == 4} for i in range(5)]
    col['loot_zone'] = 'map_quarry'
    col['carry_assets'] = [{'zone': 'common_br_mp', 'type': 'scriptable', 'name': name} for name in items]
    json.dump(col, open(col_path, 'w', encoding='utf-8'))
    out = os.path.join(work, 'build')
    start = time.time()
    code, log = t.build(exe, copy, map_id, out, os.path.join(work, 'metadata.json'))
    print('converter exit %d in %.0f s' % (code, time.time() - start))
    for line in log.splitlines():
        low = line.lower()
        if 'carr' in low or 'missing' in low or 'error' in low:
            print('   ' + line.strip()[:220])
    if code != 0:
        print(log[-3000:])
        return 1
    zone = os.path.join(out, map_id, map_id + '.ff')
    for f in sorted(glob.glob(os.path.join(out, map_id, '*.ff'))):
        print('   %-32s %8.1f MB' % (os.path.basename(f), os.path.getsize(f) / 1e6))
    replay = t.acts_test(zone, os.path.join(work, 'acts_test'))
    print('ACTS:', replay)
    exports = os.path.join(work, 'acts_sc')
    t.acts_export(zone, 'scriptable', exports)
    names = {os.path.basename(p).split('.')[0] for p in glob.glob(os.path.join(exports, '**', 'scriptable', '**', '*.json'), recursive=True)}
    missing = [n for n in items if n not in names]
    print('%d of %d item definitions are in the map zone; missing: %s' % (len(items) - len(missing), len(items), missing[:10]))
    ok = code == 0 and replay and all(' 0 failed' in l for l in replay) and not missing
    print('RESULT: %s' % ('PASS' if ok else 'FAIL'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
