import io
p = 'tools/build_scriptable_catalog.py'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
def rep(a, b):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (s.count(a), a[:70])
    s = s.replace(a, b)
rep('''        limits = set((sm.get('physicsLimits') or {}).values())
''', '''        limits = set((sm.get('physicsLimits') or {}).values())
        stored = sm.get('physicsLimits') or {}
        stored_main = {stored.get(k, 0) for k in ('clientStandaloneMainRigidBodyCount', 'clientStandaloneMotionCount',
                                                  'serverStandaloneMainRigidBodyCount', 'serverStandaloneMotionCount')}
        stored_detail = {stored.get(k, 0) for k in ('clientStandaloneDetailedRigidBodyCount', 'serverStandaloneDetailedRigidBodyCount')}
        flag_rows.append((zone, collections.Counter(names), stored_main, stored_detail))
''')
rep('''    observed = collections.defaultdict(collections.Counter)
    limit_rows = []
''', '''    observed = collections.defaultdict(collections.Counter)
    limit_rows = []
    flag_rows = []
''')
rep('''    print('limit = occluder-definition instances on every map:', usable_rule_ok)
''', '''    print('limit = occluder-definition instances on every map:', usable_rule_ok)

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
''')
rep('''                     d['dynents'], inst, 1 if prio == 3 else 0, cflags, 1 if prio == 3 else 0, '0x%X' % d['flags'], d['model'], source,
                     1 if source == 'observed' and name in limit_known else 0))
''', '''                     d['dynents'], inst, 1 if prio == 3 else 0, cflags,
                     (1 if main_limit(d['flags']) else 0) if flag_rule_ok else (1 if prio == 3 else 0), '0x%X' % d['flags'], d['model'], source,
                     1 if source == 'observed' and (flag_rule_ok or name in limit_known) else 0,
                     (1 if detail_limit(d['flags']) else 0) if flag_rule_ok else (1 if prio == 3 else 0)))
''')
rep('''        f.write('# instance_dynent is not known). verified=1: observed, and seen on a map whose physics limits are explained\\n')
        f.write('# (each limit = its door instances), so physics_limit is known too. The editor builds a verified definition:\\n')
''', '''        f.write('# instance_dynent is not known). physics_limit / physics_limit_detail: the definition counts in the main /\\n')
        f.write('# detailed scriptable physics limits of a map (flag bits 13+14 / 13+15; the rule gives the stored limits of all 21\\n')
        f.write('# exported stock maps, research 89). verified=1: observed, so every number is known. The editor builds a verified definition:\\n')
''')
rep('''physics_limit\\tflags\\tmodel\\tsource\\tverified\\n')''', '''physics_limit\\tflags\\tmodel\\tsource\\tverified\\tphysics_limit_detail\\n')''')
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
