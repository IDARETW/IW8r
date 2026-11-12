"""Checks how a stock map's map-wide scriptable counts follow from its instances and their definitions
(docs/research/86_scriptable_objects_plan.md, step F2c). Read-only; prints a report.

Usage: py -3 -I tools/scriptable_counts.py <map_ents asset json> <folder of scriptable definition asset json>...
The map_ents JSON is an ACTS structured export (the editor caches one per opened stock map under stock_cache); the
definition folders are ACTS exports made with `-a scriptable` (the map's own zone and the common zones)."""
import glob, json, os, sys


def load_defs(folders):
    defs = {}
    for folder in folders:
        for path in glob.glob(os.path.join(folder, '**', '*.asset.json'), recursive=True):
            try:
                d = json.load(open(path, encoding='utf-8'))
            except Exception:
                continue
            a = d.get('asset', {})
            if a.get('type') not in (None, 'ScriptableDef') and 'scriptable' not in path.replace('\\', '/').lower():
                continue
            f = a.get('fields', {})
            name = f.get('name')
            if isinstance(name, dict):
                name = name.get('value') or name.get('string')
            if not name:
                name = os.path.basename(path).split('.')[0]
            defs[str(name).lstrip(',')] = {k: v for k, v in f.items() if isinstance(v, (int, float, bool))}
    return defs


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    ents = json.load(open(sys.argv[1], encoding='utf-8'))
    sm = ents['asset']['fields']['scriptableMapEnts']
    defs = load_defs(sys.argv[2:])
    head = {k: v for k, v in sm.items() if not isinstance(v, (dict, list))}
    print('map-wide:', head)
    print('physicsLimits:', sm.get('physicsLimits'))
    values = (sm.get('mapInstances') or {}).get('values') or []
    print('instances in the export:', len(values), ' definitions loaded:', len(defs))
    per = {}
    unknown = {}
    for inst in values:
        d = inst.get('initialDef')
        name = d.get('name') if isinstance(d, dict) else d
        name = str(name or '')
        cross = name.startswith(',')
        key = name.lstrip(',')
        per.setdefault(key, [0, cross])[0] += 1
        if key not in defs:
            unknown[key] = unknown.get(key, 0) + 1
    fields = ['partCount', 'numParts', 'serverInstancedPartCount', 'serverControlledPartCount', 'usablePartCount',
              'maxNumDynEntPartsBase', 'maxNumDynEntsRequired']
    sums = {f: 0 for f in fields}
    known_instances = 0
    print('\n%-58s %5s %s' % ('definition', 'count', ' '.join(f[:14].rjust(14) for f in fields)))
    for key, (count, cross) in sorted(per.items(), key=lambda kv: -kv[1][0]):
        d = defs.get(key)
        row = ' '.join(str(d.get(f, '?') if d else '?').rjust(14) for f in fields)
        print('%-58s %5d %s%s' % ((',' if cross else '') + key, count, row, '' if d else '   (definition not exported)'))
        if d:
            known_instances += count
            for f in fields:
                sums[f] += count * int(d.get(f, 0) or 0)
    print('\ninstances with a known definition: %d of %d' % (known_instances, len(values)))
    print('sums over those instances:', sums)
    if unknown:
        print('definitions missing from the given folders:', unknown)


if __name__ == '__main__':
    main()
