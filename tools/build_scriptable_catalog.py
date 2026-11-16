"""Builds data/asset_catalog/iw8_scriptables.tsv: every stock scriptable definition with the numbers a map build needs
(docs/research/86_scriptable_objects_plan.md).

For each stock zone it exports (ACTS, read-only on the game folder) the scriptable definitions, and for map zones also
map_ents and dynentitylist, into out/scriptable_study/<zone> (kept: a second run reuses them). It then records per
definition: its zone(s), whether a zone the game always has loaded in multiplayer holds it, the header numbers, the
first model (for drawing), and how stock maps give its instances dynamic entities (observed), or the same derived by
the rule that fits every observed definition.

Usage: py -3 -I tools/build_scriptable_catalog.py [--no-export]"""
import collections, glob, json, os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STUDY = os.path.join(ROOT, 'out', 'scriptable_study')
ACTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'bin', 'release', 'iw8r-acts.exe')
GAME = r'E:\IW8\Builds\1.20-replay'
MAPS = ['mp_aniyah_tac', 'mp_backlot2', 'mp_bm_tut', 'mp_br_quarry', 'mp_br_tut', 'mp_donetsk', 'mp_emporium',
        'mp_frontend', 'mp_frontend1', 'mp_frontend3', 'mp_hideout', 'mp_m_fork', 'mp_m_king', 'mp_m_speed',
        'mp_m_speedball', 'mp_rust', 'mp_shipment', 'mp_t_gun_course', 'mp_t_reflex', 'mp_t_sn_reflex', 'mp_village2']
# Zones every multiplayer match has loaded (stock maps name their definitions with a leading comma).
ALWAYS = ['common_core_mp', 'common_base_mp', 'common_mp', 'global_core_mp']
OTHER = ['common_br_mp']


def export(zone, kind):
    out = os.path.join(STUDY, zone)
    marker = os.path.join(out, '.exported_' + kind)
    if os.path.exists(marker):
        return
    os.makedirs(out, exist_ok=True)
    args = [ACTS, '--noUpdater', 'fastfile', '-r', 'mw19replay', '-g', os.path.join(GAME, 'game_dx12_ship_replay.exe'),
            '--oodle', os.path.join(GAME, 'oo2core_7_win64.dll'), '-a', kind, '-o', out,
            os.path.join(GAME, 'zone', zone + '.ff')]
    r = subprocess.run(args, capture_output=True, text=True, errors='replace')
    tail = (r.stdout + r.stderr).strip().splitlines()[-2:]
    print('  %s %s: %s' % (zone, kind, ' | '.join(tail)))
    if any('Parsed 1' in t for t in tail):
        open(marker, 'w').write('ok')


def first(pattern):
    found = glob.glob(pattern, recursive=True)
    return found[0] if found else None


def name_of(node):
    if isinstance(node, dict):
        return str(node.get('name') or node.get('string') or '')
    return str(node or '')


def find_models(node, out, depth=0):
    """Names of XModel references anywhere in a definition, in file order."""
    if depth > 40:
        return
    if isinstance(node, dict):
        if node.get('type') == 'XModel' and node.get('name'):
            if node['name'] not in out:
                out.append(node['name'])
            return
        for value in node.values():
            find_models(value, out, depth + 1)
    elif isinstance(node, list):
        for value in node:
            find_models(value, out, depth + 1)


def load_definitions(zone):
    defs = {}
    for path in glob.glob(os.path.join(STUDY, zone, '**', 'scriptable', '**', '*.asset.json'), recursive=True):
        try:
            fields = json.load(open(path, encoding='utf-8'))['asset']['fields']
        except Exception:
            continue
        name = name_of(fields.get('name')).lstrip(',')
        if not name:
            continue
        models = []
        find_models(fields, models)
        defs[name] = dict(
            parts=int(fields.get('partCount', 0) or 0), server=int(fields.get('serverInstancedPartCount', 0) or 0),
            usable=int(fields.get('usablePartCount', 0) or 0), dynparts=int(fields.get('maxNumDynEntPartsBase', 0) or 0),
            dynents=int(fields.get('maxNumDynEntsRequired', 0) or 0), flags=int(fields.get('flags', 0) or 0),
            model=models[0].lstrip(',') if models else '')
    return defs


def main():
    if '--no-export' not in sys.argv:
        for zone in ALWAYS + OTHER:
            export(zone, 'scriptable')
        for zone in MAPS:
            for kind in ('scriptable', 'map_ents', 'dynentitylist'):
                export(zone, kind)

    definitions = {}      # name -> numbers
    zones_of = collections.defaultdict(list)
    for zone in ALWAYS + OTHER + MAPS:
        for name, numbers in load_definitions(zone).items():
            zones_of[name].append(zone)
            definitions.setdefault(name, numbers)

    # Observations: per definition, the dynamic entities stock maps give one instance.
    observed = collections.defaultdict(collections.Counter)
    limit_rows = []
    flag_rows = []
    for zone in MAPS:
        me_path = first(os.path.join(STUDY, zone, '**', 'map_ents', '**', '*.asset.json'))
        dl_path = first(os.path.join(STUDY, zone, '**', 'dynentitylist', '**', '*.asset.json'))
        if not me_path:
            continue
        me = json.load(open(me_path, encoding='utf-8'))['asset']['fields']
        sm = me['scriptableMapEnts']
        instances = (sm.get('mapInstances') or {}).get('values') or []
        names = [name_of(i['initialDef']).lstrip(',') for i in instances]
        entries = []
        if dl_path:
            lists = json.load(open(dl_path, encoding='utf-8'))['asset']['fields']['dynEntDefList']
            entries = (lists[0] or {}).get('values') or [] if lists else []
        per = collections.defaultdict(lambda: [[], []])
        for e in entries:
            index = e['scriptableMapIndex']
            if index >= len(names):
                continue
            kind = e['type']['name']
            if kind.endswith('SCRIPTABLEINST'):
                per[index][0].append(e)
            elif kind.endswith('SCRIPTABLEPHYSICS'):
                per[index][1].append(e)
        for index, name in enumerate(names):
            inst, phys = per[index]
            sig = (len(inst), inst[0]['priority']['value'] if inst else 0, inst[0]['collisionFlags'] if inst else 0, len(phys),
                   max([x['___u15']['scriptableMaxPartCount'] for x in inst + phys] or [0]))
            observed[name][sig] += 1
        limits = set((sm.get('physicsLimits') or {}).values())
        stored = sm.get('physicsLimits') or {}
        stored_main = {stored.get(k, 0) for k in ('clientStandaloneMainRigidBodyCount', 'clientStandaloneMotionCount',
                                                  'serverStandaloneMainRigidBodyCount', 'serverStandaloneMotionCount')}
        stored_detail = {stored.get(k, 0) for k in ('clientStandaloneDetailedRigidBodyCount', 'serverStandaloneDetailedRigidBodyCount')}
        flag_rows.append((zone, collections.Counter(names), stored_main, stored_detail))
        limit_rows.append((zone, len(names), limits, collections.Counter(names),
                           dict(server=sm['serverInstanceCount'], fixed=sm['serverFixedPartCount'], usable=sm['serverUsablePartCount'],
                                loot=sm['lootInstanceCount'])))

    # The physics limits: on every map, each limit equals the number of instances of the "limit" definitions. Find the
    # property: try "definition has a usable part".
    print('physics limits per map (limit value, instances of definitions with a usable part, of sparse-occluder definitions):')
    usable_rule_ok = True
    limit_known = set()   # definitions seen on a map whose limits equal its sparse-occluder (door) instances
    for zone, count, limits, names, heads in limit_rows:
        usable = sum(n for name, n in names.items() if definitions.get(name, {}).get('usable'))
        occl = sum(n for name, n in names.items() if any(sig[1] == 3 for sig in observed[name]))
        fixed = sum(n * definitions.get(name, {}).get('server', 0) for name, n in names.items())
        server = sum(n for name, n in names.items() if definitions.get(name, {}).get('server', 0))
        us = sum(n * definitions.get(name, {}).get('usable', 0) for name, n in names.items())
        missing = [name for name in names if name not in definitions]
        print('  %-18s instances %4d limits %s usable-def instances %d occluder-def instances %d | server %d (sum %d + loot %d) fixed %d (sum %d) usable %d (sum %d)%s' % (
            zone, count, sorted(limits), usable, occl, heads['server'], server, heads['loot'], heads['fixed'], fixed, heads['usable'], us,
            '  MISSING DEFS %s' % missing if missing else ''))
        if limits and limits != {occl}:
            usable_rule_ok = False
        elif limits:
            limit_known.update(names)
    print('limit = occluder-definition instances on every map:', usable_rule_ok)

    # The rule that fits every map (research 89, tools/scriptable_limits_study.py): the main rigid body and motion
    # limits count the instances whose definition has flag bits 13 and 14, the detailed rigid body limits those with
    # bits 13 and 15.
    def main_limit(flags):
        return flags & 0x6000 == 0x6000

    def detail_limit(flags):
        return flags & 0xA000 == 0xA000
    flag_rule_ok = True
    for zone, names, stored_main, stored_detail in flag_rows:
        m = sum(n for name, n in names.items() if main_limit(definitions.get(name, {}).get('flags', 0)))
        d = sum(n for name, n in names.items() if detail_limit(definitions.get(name, {}).get('flags', 0)))
        ok = stored_main == {m} and stored_detail == {d}
        flag_rule_ok &= ok
        if not ok:
            print('  flag rule MISS on %s: main %s (rule %d), detailed %s (rule %d)' % (zone, sorted(stored_main), m, sorted(stored_detail), d))
    print('limits by definition flags (main: bits 13+14, detailed: bits 13+15) on every map:', flag_rule_ok)

    # The rule for "has an instance entry", tested against the observations.
    def derived_instance(flags):
        return bool(flags & 0x201) and not (flags & 0x20)
    bad = 0
    rows = []
    for name in sorted(definitions):
        d = definitions[name]
        obs = observed.get(name)
        if obs:
            sig = obs.most_common(1)[0][0]
            if len(obs) > 1:
                print('  note: %s observed with %d different entry sets: %s' % (name, len(obs), dict(obs)))
            inst, prio, cflags, phys, maxpart = sig
            if bool(inst) != derived_instance(d['flags']) or phys != d['dynents'] or (maxpart and maxpart != d['dynparts']):
                bad += 1
                print('  rule mismatch: %s flags 0x%X observed inst %d phys %d maxpart %d, def dynents %d dynparts %d' % (
                    name, d['flags'], inst, phys, maxpart, d['dynents'], d['dynparts']))
            source = 'observed'
        else:
            inst, prio, cflags, phys = (1 if derived_instance(d['flags']) else 0), 0, 0, d['dynents']
            source = 'derived'
        always = any(z in ALWAYS for z in zones_of[name])
        rows.append((name, ','.join(zones_of[name][:6]), 1 if always else 0, d['parts'], d['server'], d['usable'], max(d['dynparts'], 1),
                     d['dynents'], inst, 1 if prio == 3 else 0, cflags,
                     (1 if main_limit(d['flags']) else 0) if flag_rule_ok else (1 if prio == 3 else 0), '0x%X' % d['flags'], d['model'], source,
                     1 if source == 'observed' and (flag_rule_ok or name in limit_known) else 0,
                     (1 if detail_limit(d['flags']) else 0) if flag_rule_ok else (1 if prio == 3 else 0)))
    print('definitions %d, observed in stock maps %d, rule mismatches %d' % (len(rows), sum(1 for r in rows if r[14] == 'observed'), bad))

    out = os.path.join(ROOT, 'data', 'asset_catalog', 'iw8_scriptables.tsv')
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# iw8_scriptables.tsv: stock Replay 1.20 scriptable definitions (tools/build_scriptable_catalog.py, docs/research/86).\n')
        f.write('# always_loaded=1: in a zone every multiplayer match has loaded (%s): a map can place it by name.\n' % ', '.join(ALWAYS))
        f.write('# instance_dynent / sparse_occluder / collision_flags / physics_dynents: the dynamic entities one instance gets;\n')
        f.write('# source=observed: read from stock map instances; derived: a guess (no stock map places it, and the rule for\n')
        f.write('# instance_dynent is not known). physics_limit / physics_limit_detail: the definition counts in the main /\n')
        f.write('# detailed scriptable physics limits of a map (flag bits 13+14 / 13+15; the rule gives the stored limits of all 21\n')
        f.write('# exported stock maps, research 89). verified=1: observed, so every number is known. The editor builds a verified definition:\n')
        f.write('# by name when always_loaded, else (with Real stock scripted objects on) carried from its first mp_ zone.\n')
        f.write('name\tzones\talways_loaded\tparts\tserver_instanced_parts\tusable_parts\tmax_dynent_parts\tphysics_dynents\tinstance_dynent\tsparse_occluder\tcollision_flags\tphysics_limit\tflags\tmodel\tsource\tverified\tphysics_limit_detail\n')
        for r in rows:
            f.write('\t'.join(str(x) for x in r) + '\n')
    print('wrote', out, len(rows), 'rows;', sum(1 for r in rows if r[2]), 'always loaded;', sum(1 for r in rows if r[15]), 'verified;',
          'placeable:', [r[0] for r in rows if r[2] and r[15]])


if __name__ == '__main__':
    main()
