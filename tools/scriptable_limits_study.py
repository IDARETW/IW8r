"""Looks for the property of a scriptable definition that sets a map's ScriptablePhysicsLimits (research 86 open
point, research 89): per map, limit = sum over instances of some per-definition number. Read-only on the exports
under out/scriptable_study.

For every definition it collects features (flag bits, header numbers, model-event switches, state and event types),
sums each feature over the instances of every exported map (optionally plus loot or cache points) and prints the
features whose sums equal the stored limits on every map.

Usage: py -3 -I tools/scriptable_limits_study.py"""
import collections, glob, json, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_scriptable_catalog as cat
import warzone_loot_rules as rules

STUDY = cat.STUDY


def walk(node, visit, depth=0):
    if depth > 60:
        return
    if isinstance(node, dict):
        visit(node)
        for v in node.values():
            walk(v, visit, depth + 1)
    elif isinstance(node, list):
        for v in node:
            walk(v, visit, depth + 1)


def features_of(fields):
    f = collections.Counter()
    flags = int(fields.get('flags', 0) or 0)
    for b in range(32):
        if flags >> b & 1:
            f['flag%02d' % b] = 1
    for key in ('partCount', 'serverInstancedPartCount', 'serverControlledPartCount', 'usablePartCount', 'maxNumDynEntsRequired',
                'maxNumDynEntPartsBase', 'maxNumDynEntPartsForSpawning', 'numXModels'):
        v = int(fields.get(key, 0) or 0)
        f[key] = v
        f[key + '>0'] = 1 if v else 0
    f['spatial=%s' % fields.get('spatialActivationMode')] = 1
    f['animtree'] = 1 if (fields.get('animationTreeName') or 0) else 0
    f['one'] = 1

    def visit(node):
        for key in ('neverMoves', 'dynamicSimulation', 'activatePhysics'):
            if key in node and isinstance(node[key], bool):
                f['any %s=%s' % (key, node[key])] = 1
                f['n %s=%s' % (key, node[key])] += 1
        t = node.get('type')
        if isinstance(t, dict) and isinstance(t.get('name'), str) and t['name'].startswith('Scriptable_'):
            f['any ' + t['name']] = 1
            f['n ' + t['name']] += 1
        # part flags
        if 'scrName' in node and 'flags' in node and 'numStates' in node:
            pf = int(node['flags'] or 0)
            for b in range(32):
                if pf >> b & 1:
                    f['anypart flag%02d' % b] = 1
                    f['nparts flag%02d' % b] += 1
    walk(fields.get('parts'), visit)
    return f


def main():
    feats = {}
    for zone in cat.ALWAYS + cat.OTHER + cat.MAPS:
        for path in glob.glob(os.path.join(STUDY, zone, '**', 'scriptable', '**', '*.asset.json'), recursive=True):
            try:
                fields = json.load(open(path, encoding='utf-8'))['asset']['fields']
            except Exception:
                continue
            name = cat.name_of(fields.get('name')).lstrip(',')
            if name and name not in feats and fields.get('parts'):
                feats[name] = features_of(fields)
    print('definitions with features:', len(feats))
    rows = []
    for zone in cat.MAPS:
        me_path = cat.first(os.path.join(STUDY, zone, '**', 'map_ents', '**', '*.asset.json'))
        if not me_path:
            continue
        me = json.load(open(me_path, encoding='utf-8'))['asset']['fields']
        sm = me['scriptableMapEnts']
        names = [cat.name_of(i['initialDef']).lstrip(',') for i in (sm.get('mapInstances') or {}).get('values') or []]
        limits = sm.get('physicsLimits') or {}
        loot = me['spawnGroupLoot']
        raw = rules.array(loot.get('points'), '<6f2H', 28)
        clusters = [p['cluster'] if isinstance(p, dict) else p[6] for p in raw]
        rows.append(dict(zone=zone, names=collections.Counter(names), main=limits.get('serverStandaloneMainRigidBodyCount', 0),
                         detail=limits.get('serverStandaloneDetailedRigidBodyCount', 0), motion=limits.get('serverStandaloneMotionCount', 0),
                         loot=len(clusters), cache=sum(1 for c in clusters if c == 0xFFFB), free=sum(1 for c in clusters if c == 0xFFFD),
                         missing=[n for n in set(names) if n not in feats]))
    for r in rows:
        print('  %-16s main %5d detail %5d motion %5d | instances %5d loot %5d cache %4d free %5d%s' % (
            r['zone'], r['main'], r['detail'], r['motion'], sum(r['names'].values()), r['loot'], r['cache'], r['free'],
            '  no definition: %s' % r['missing'][:4] if r['missing'] else ''))
    keys = sorted({k for f in feats.values() for k in f})
    extras = {'': lambda r: 0, ' + loot': lambda r: r['loot'], ' + cache': lambda r: r['cache'], ' + free': lambda r: r['free']}
    for target in ('main', 'detail'):
        print('features whose instance sums give "%s" on every map:' % target)
        scored = []
        for k in keys:
            for label, extra in extras.items():
                sums = [sum(n * feats.get(name, {}).get(k, 0) for name, n in r['names'].items()) + extra(r) for r in rows]
                want = [r[target] for r in rows]
                hit = sum(1 for a, b in zip(sums, want) if a == b)
                scored.append((hit, k + label, sums))
        scored.sort(key=lambda s: -s[0])
        for hit, k, sums in scored[:12]:
            miss = [(r['zone'], s, r[target]) for r, s in zip(rows, sums) if s != r[target]]
            print('   %2d of %d  %-40s misses %s' % (hit, len(rows), k, miss[:5]))


if __name__ == '__main__':
    main()
