"""Decodes stock map_ents exports for the Warzone survey (docs/research/87_warzone_survey.md). Read-only on inputs.

Usage: py -3 -I tools/warzone_mapents.py <out folder> <map id>... [--base mp_shipment]
Inputs: out/scriptable_study/<map>/mw19replay/<map>/assets/map_ents/**.asset.json (ACTS structured export) and the
dynentitylist export beside it. Keys: data/generated/iw8_canonical_keys.tsv.
Writes per map into <out folder>:
  <map>_entities.tsv       one row per entity: index, classname, targetname, script_noteworthy, model, origin, other keys
  <map>_families.tsv       counts of classname, (classname, targetname), script_noteworthy, model, key names
  <map>_scriptables.tsv    scriptable map instances per definition (count, targetname / noteworthy samples)
  <map>_loot.tsv           spawnGroupLoot points (origin, angles, cluster, zone) and zones
  <map>_summary.json       scalar MapEnts fields, block counts, dynentitylist counts
and fields_compare.tsv: every scalar MapEnts field for all maps side by side (the base map first)."""
import collections, glob, json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STUDY = os.path.join(ROOT, 'out', 'scriptable_study')
KEYS_TSV = os.path.join(ROOT, 'data', 'generated', 'iw8_canonical_keys.tsv')
BLOCK = re.compile(r'\{([^{}]*)\}', re.S)
PAIR = re.compile(r'^\s*(\d+)\s+"(.*)"\s*$')


def load_keys():
    keys = {}
    for line in open(KEYS_TSV, encoding='utf-8'):
        if line.startswith('#') or '\t' not in line:
            continue
        i, n = line.rstrip('\n').split('\t', 1)
        keys[int(i)] = n
    return keys


def find_one(zone, kind):
    p = glob.glob(os.path.join(STUDY, zone, 'mw19replay', zone, 'assets', kind, '**', '*.asset.json'), recursive=True)
    return p[0] if p else None


def decode(text, keys):
    ents = []
    for b in BLOCK.finditer(text):
        e = collections.OrderedDict()
        for line in b.group(1).splitlines():
            m = PAIR.match(line)
            if m:
                k = keys.get(int(m.group(1)), '#' + m.group(1))
                if k in e:
                    k = k + '#dup'
                e[k] = m.group(2)
        if e:
            ents.append(e)
    return ents


SS = []


def sval(v):
    """Script string or name dict to text ({'value': n} is an index into the zone's script_strings.json)."""
    if isinstance(v, dict):
        if 'string' in v and not isinstance(v['string'], (dict, list)):
            return str(v['string'])
        if 'value' in v and isinstance(v['value'], int):
            n = v['value']
            return (SS[n] or '') if 0 <= n < len(SS) else '#%d' % n
        for k in ('value', 'name'):
            if k in v and not isinstance(v[k], (dict, list)):
                return str(v[k])
        return ''
    return '' if v is None else str(v)


def vec(v):
    if isinstance(v, dict) and 'v' in v:
        return ' '.join('%g' % x for x in v['v'])
    return str(v)


def points_of(p):
    """SpawnGroupPoint array: JSON values or hex bytes (28 bytes: origin 3f, angles 3f, cluster u16, zone u16)."""
    if p.get('values') is not None:
        return [{'origin': vec(x.get('origin')), 'angles': vec(x.get('angles')), 'cluster': x.get('cluster'),
                 'zone': x.get('zone')} for x in p['values']]
    if p.get('bytes'):
        import struct
        raw = bytes.fromhex(p['bytes'])
        out = []
        for i in range(0, len(raw) - 27, 28):
            o = struct.unpack_from('<3f3fHH', raw, i)
            out.append({'origin': '%g %g %g' % o[0:3], 'angles': '%g %g %g' % o[3:6], 'cluster': o[6], 'zone': o[7]})
        return out
    return []


def flatten(o, prefix, out):
    if isinstance(o, dict):
        if 'values' in o and 'stride' in o:
            out[prefix + '.count'] = o.get('count')
            return
        if 'bytes' in o and 'stride' in o:
            out[prefix + '.count'] = o.get('count')
            return
        for k, v in o.items():
            if k in ('address', 'type', 'extent_evidence', 'stride', '$union_views'):
                continue
            flatten(v, prefix + '.' + k if prefix else k, out)
    elif isinstance(o, list):
        if len(o) <= 4:
            for i, v in enumerate(o):
                flatten(v, '%s[%d]' % (prefix, i), out)
        else:
            out[prefix + '.len'] = len(o)
    elif o is None:
        out[prefix] = None
    elif isinstance(o, (int, float, bool)):
        out[prefix] = o
    else:
        out[prefix] = str(o)[:60]


def main():
    args = sys.argv[1:]
    if len(args) < 2:
        raise SystemExit(__doc__)
    base = 'mp_shipment'
    if '--base' in args:
        i = args.index('--base'); base = args[i + 1]; del args[i:i + 2]
    out_dir = args[0]
    maps = args[1:]
    os.makedirs(out_dir, exist_ok=True)
    keys = load_keys()
    all_fields = collections.OrderedDict()
    order = [base] + [m for m in maps if m != base]
    for zone in order:
        path = find_one(zone, 'map_ents')
        if not path:
            print('no map_ents export for', zone)
            continue
        f = json.load(open(path, encoding='utf-8'))['asset']['fields']
        ssp = os.path.join(STUDY, zone, 'mw19replay', zone, 'script_strings.json')
        SS[:] = json.load(open(ssp, encoding='utf-8')) if os.path.exists(ssp) else []
        text = bytes.fromhex(f['entityString']['bytes']).decode('latin-1')
        ents = decode(text, keys)
        fam = collections.defaultdict(collections.Counter)
        with open(os.path.join(out_dir, zone + '_entities.tsv'), 'w', encoding='utf-8') as fh:
            fh.write('index\tclassname\ttargetname\tscript_noteworthy\tmodel\torigin\tother\n')
            for i, e in enumerate(ents):
                cls = e.get('classname', '')
                fam['classname'][cls] += 1
                fam['classname|targetname'][cls + '|' + e.get('targetname', '')] += 1
                if 'script_noteworthy' in e:
                    fam['script_noteworthy'][cls + '|' + e['script_noteworthy']] += 1
                if 'model' in e and not e['model'].startswith('*'):
                    fam['model'][cls + '|' + e['model']] += 1
                for k in e:
                    fam['key'][k] += 1
                other = ' '.join('%s=%s' % (k, v) for k, v in e.items()
                                 if k not in ('classname', 'targetname', 'script_noteworthy', 'model', 'origin'))
                fh.write('%d\t%s\t%s\t%s\t%s\t%s\t%s\n' % (i, cls, e.get('targetname', ''), e.get('script_noteworthy', ''),
                                                       e.get('model', ''), e.get('origin', ''), other))
        with open(os.path.join(out_dir, zone + '_families.tsv'), 'w', encoding='utf-8') as fh:
            fh.write('family\tvalue\tcount\n')
            for k in ('classname', 'classname|targetname', 'script_noteworthy', 'model', 'key'):
                for v, c in fam[k].most_common():
                    fh.write('%s\t%s\t%d\n' % (k, v, c))
        # scriptable instances
        sm = f.get('scriptableMapEnts') or {}
        inst = ((sm.get('mapInstances') or {}).get('values')) or []
        per = collections.OrderedDict()
        for it in inst:
            d = it.get('initialDef')
            name = sval(d) if isinstance(d, dict) else str(d or '')
            if isinstance(d, dict) and 'name' in d:
                name = str(d['name'])
            r = per.setdefault(name, [0, collections.Counter(), collections.Counter()])
            r[0] += 1
            r[1][sval(it.get('targetname'))] += 1
            r[2][sval(it.get('script_noteworthy'))] += 1
        with open(os.path.join(out_dir, zone + '_scriptables.tsv'), 'w', encoding='utf-8') as fh:
            fh.write('definition\tcount\ttargetnames\tnoteworthy\n')
            for name, (c, tn, sn) in sorted(per.items(), key=lambda kv: -kv[1][0]):
                fh.write('%s\t%d\t%s\t%s\n' % (name, c, ','.join('%s:%d' % kv for kv in tn.most_common(4)),
                                                ','.join('%s:%d' % kv for kv in sn.most_common(4))))
        # loot points
        sg = f.get('spawnGroupLoot') or {}
        pts = points_of(sg.get('points') or {})
        zones = ((sg.get('zones') or {}).get('values')) or []
        clusters = ((sg.get('clusters') or {}).get('values')) or []
        with open(os.path.join(out_dir, zone + '_loot.tsv'), 'w', encoding='utf-8') as fh:
            fh.write('kind\tindex\torigin\tangles\tcluster\tzone\textra\n')
            for i, p in enumerate(pts):
                fh.write('point\t%d\t%s\t%s\t%s\t%s\t\n' % (i, p['origin'], p['angles'], p['cluster'], p['zone']))
            for i, c in enumerate(clusters):
                b = c.get('bounds') or {}
                fh.write('cluster\t%d\t%s\t%s\t\t%s\tpoints=%s\n' % (i, vec(b.get('midPoint')), vec(b.get('halfSize')), c.get('zone'),
                                                                 ','.join(str(x) for x in (c.get('points') or {}).get('values') or [])))
            for i, z in enumerate(zones):
                b = z.get('bounds') or {}
                fh.write('zone\t%d\t%s\t%s\t\t%s\tpoints=%s clusters=%s\n' % (i, vec(b.get('midPoint')), vec(b.get('halfSize')),
                                                                         sval(z.get('name')), z.get('pointCount'), z.get('clusterCount')))
        zone_names = [sval(z.get('name')) for z in zones]
        loot_cluster = collections.Counter(p.get('cluster') for p in pts)
        loot_zone = collections.Counter(p.get('zone') for p in pts)
        # dynentitylist
        dyn = {}
        dpath = find_one(zone, 'dynentitylist')
        if dpath:
            dl = json.load(open(dpath, encoding='utf-8'))['asset']['fields']
            lists = dl.get('dynEntDefList') or []
            entries = []
            for l in lists:
                if isinstance(l, dict):
                    entries += l.get('values') or []
            types = collections.Counter(e['type']['name'] for e in entries)
            reserved = [e for e in entries if e.get('scriptableMapIndex', 0) >= len(inst)]
            dyn = {'entries': len(entries), 'types': dict(types), 'reserved_entries': len(reserved),
                   'reserved_map_indices': sorted({e.get('scriptableMapIndex') for e in reserved})[:5]}
        flat = collections.OrderedDict()
        flatten({k: v for k, v in f.items() if k != 'entityString'}, '', flat)
        flat['entityString.count'] = f['entityString'].get('count')
        all_fields[zone] = flat
        summary = {'map': zone, 'source': path, 'entities': len(ents), 'entity_string_chars': len(text),
                   'classnames': len(fam['classname']), 'scriptable_instances': len(inst), 'scriptable_definitions': len(per),
                   'loot_points': len(pts), 'loot_clusters': len(clusters), 'loot_zones': len(zones),
                   'loot_points_per_cluster_top': loot_cluster.most_common(5), 'loot_points_per_zone': sorted(loot_zone.items()),
                   'loot_zone_names': zone_names,
                   'dynentitylist': dyn, 'scalar_fields': flat}
        json.dump(summary, open(os.path.join(out_dir, zone + '_summary.json'), 'w', encoding='utf-8'), indent=1)
        print('%s: %d entities, %d classnames, %d scriptable instances (%d definitions), loot points %d clusters %d zones %d, dyn %s'
              % (zone, len(ents), len(fam['classname']), len(inst), len(per), len(pts), len(clusters), len(zones), dyn))
    names = list(all_fields)
    keys_all = []
    for z in names:
        for k in all_fields[z]:
            if k not in keys_all:
                keys_all.append(k)
    with open(os.path.join(out_dir, 'fields_compare.tsv'), 'w', encoding='utf-8') as fh:
        fh.write('field\t' + '\t'.join(names) + '\tdiffers_from_base\n')
        for k in keys_all:
            vals = [all_fields[z].get(k, '') for z in names]
            b = vals[0]
            diff = any(v != b for v in vals[1:])
            fh.write('%s\t%s\t%s\n' % (k, '\t'.join(str(v) for v in vals), 'yes' if diff else ''))


if __name__ == '__main__':
    main()
