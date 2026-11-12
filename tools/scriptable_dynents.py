"""Tabulates how a stock map's dynamic-entity list follows from its scriptable instances (research 86).
Usage: py -3 -I tools/scriptable_dynents.py <zone folder under out/scriptable_study> <map_ents json> [definition folders...]"""
import glob, json, os, sys, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from scriptable_counts import load_defs

zone_dir, ents_path = sys.argv[1], sys.argv[2]
defs = load_defs(sys.argv[3:] + [zone_dir])
p = glob.glob(os.path.join(zone_dir, '**', 'dynentitylist', '**', '*.asset.json'), recursive=True)[0]
dl = json.load(open(p, encoding='utf-8'))['asset']['fields']
entries = dl['dynEntDefList'][0]['values']
me = json.load(open(ents_path, encoding='utf-8'))['asset']['fields']
inst = me['scriptableMapEnts']['mapInstances']['values']
names = []
for i in inst:
    d = i['initialDef']
    names.append(str(d.get('name') if isinstance(d, dict) else d).lstrip(','))
per = collections.defaultdict(lambda: {'INST': [], 'PHYS': []})
reserved = []
for idx, e in enumerate(entries):
    t = 'INST' if e['type']['name'].endswith('SCRIPTABLEINST') else 'PHYS' if e['type']['name'].endswith('SCRIPTABLEPHYSICS') else e['type']['name']
    if e['scriptableMapIndex'] >= len(inst):
        reserved.append(e)
    else:
        per[e['scriptableMapIndex']][t].append(e)
print('entries', len(entries), 'instances', len(inst), 'reserved entries (map index beyond instances)', len(reserved),
      {(e['scriptableMapIndex'], e['___u15']['scriptableMaxPartCount'], e['type']['name'][12:]) for e in reserved})
rows = collections.defaultdict(lambda: collections.Counter())
for mi, name in enumerate(names):
    e = per.get(mi, {'INST': [], 'PHYS': []})
    sig = (len(e['INST']), len(e['PHYS']),
           tuple(sorted({x['___u15']['scriptableMaxPartCount'] for x in e['INST'] + e['PHYS']})),
           tuple(sorted({(x['priority']['value'], x['spawnActive'], x['noPhysics'], x['collisionFlags'], x['clientId'][0]) for x in e['INST']})),
           tuple(sorted({(x['priority']['value'], x['spawnActive'], x['noPhysics'], x['collisionFlags'], x['clientId'][0]) for x in e['PHYS']})))
    rows[name][sig] += 1
keys = ['partCount', 'serverInstancedPartCount', 'serverControlledPartCount', 'usablePartCount', 'maxNumDynEntPartsBase', 'maxNumDynEntsRequired']
for name, sigs in sorted(rows.items()):
    d = defs.get(name, {})
    print('%-50s %s' % (name, ' '.join('%s=%s' % (k[:16], d.get(k, '?')) for k in keys)))
    extra = {k: v for k, v in d.items() if k not in keys and v not in (0, False)}
    print('      other non-zero def numbers:', extra)
    for sig, n in sigs.items():
        print('      %3d inst: INST entries %d, PHYS entries %d, maxPart %s, INST(prio,active,noPhys,collFlags,clientId) %s PHYS %s' % ((n,) + sig))
print('MapEnts: dynEntCount', me['dynEntCount'], 'noSpatial', me['dynEntNoSpatialCount'], 'maxPosePart', me['dynEntMaxPosePartCount'],
      'withExtraPoseParts', me['dynEntsWithExtraPosePartsCount'])
mp = [e['___u15']['scriptableMaxPartCount'] for e in entries]
print('scriptableMaxPartCount: max', max(mp), ' entries >1:', sum(1 for x in mp if x > 1))
print('INST total', sum(len(v['INST']) for v in per.values()), 'PHYS total incl reserved', sum(len(v['PHYS']) for v in per.values()) + len(reserved))
print('list noSpatial head/tail', dl['dynEntNoSpatialList'], dl['dynEntNoSpatialListTail'])
pop = dl['dynEntSpatialPopulation'][0]['values'][0]
print('list pop0 partitionCount', pop['partitionCount'], 'currentPopulation', pop['currentPopulation'], 'ids', pop['partitionIds']['values'] if pop['partitionIds'] else None)
