"""Cross-references stock map entity names with the literal strings of the 1.20 scripts (research 87). Read-only.

Usage: py -3 -I tools/warzone_script_refs.py <mapents folder> <strings tsv>... > out.tsv
<mapents folder>: output of tools/warzone_mapents.py (reads <map>_families.tsv for classname, targetname and
script_noteworthy values). <strings tsv>: output of tools/warzone_gscbin_strings.py ('<script>\\t<string>' lines).
Prints one row per entity name: family, value, per-map counts, and the scripts whose literal strings contain it
exactly (a string match only: it shows the name is used by that script, not how)."""
import collections, glob, os, sys

MAPS = ['mp_shipment', 'mp_backlot2', 'mp_bm_tut', 'mp_br_tut', 'mp_br_quarry', 'mp_donetsk']


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    folder = sys.argv[1]
    users = collections.defaultdict(set)
    for path in sys.argv[2:]:
        for line in open(path, encoding='utf-8'):
            p = line.rstrip('\n').split('\t', 1)
            if len(p) == 2:
                script = p[0].split('/')[-1].split('.gsc')[0]
                users[p[1]].add(script)
    names = collections.defaultdict(dict)
    for m in MAPS:
        fp = os.path.join(folder, m + '_families.tsv')
        if not os.path.exists(fp):
            continue
        for line in open(fp, encoding='utf-8'):
            p = line.rstrip('\n').split('\t')
            if len(p) != 3 or p[0] == 'family':
                continue
            fam, val, c = p
            if fam == 'classname':
                names[('classname', val)][m] = names[('classname', val)].get(m, 0) + int(c)
            elif fam in ('classname|targetname', 'script_noteworthy'):
                v = val.split('|', 1)[1]
                if v:
                    key = ('targetname' if fam != 'script_noteworthy' else 'script_noteworthy', v)
                    names[key][m] = names[key].get(m, 0) + int(c)
    print('family\tvalue\t' + '\t'.join(MAPS) + '\tscripts')
    for (fam, val), c in sorted(names.items(), key=lambda kv: (kv[0][0], -max(kv[1].values()))):
        s = sorted(users.get(val, ()))
        print('%s\t%s\t%s\t%s' % (fam, val, '\t'.join(str(c.get(m, '')) for m in MAPS), ','.join(s)))


if __name__ == '__main__':
    main()
