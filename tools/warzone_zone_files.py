"""Lists the install files of the battle royale maps and zones for research 87 (file names and sizes only; no file is
opened). Usage: py -3 -I tools/warzone_zone_files.py [install folder] [out tsv]
Default install E:\\IW8\\Builds\\1.20-replay (zone, zone\\english, zone\\worldwide); default out
out/warzone_study/zone_files.tsv. Prints a per-map summary."""
import collections, os, re, sys

INSTALL = sys.argv[1] if len(sys.argv) > 1 else r'E:\IW8\Builds\1.20-replay'
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                                                         'out', 'warzone_study', 'zone_files.tsv')
MAPS = ['mp_donetsk', 'mp_br_quarry', 'mp_br_tut', 'mp_bm_tut', 'common_br_mp', 'mp_shipment']


def classify(name, m):
    stem, ext = os.path.splitext(name)
    for pre in ('srv_', 'techsets_', 'eng_', 'ww_'):
        if stem == pre + m:
            return pre.rstrip('_') + ext
    if stem == m:
        return 'main' + ext
    if re.fullmatch(re.escape(m) + r'_\d+_tr', stem):
        return 'numbered_tr' + ext
    if stem.startswith(m + '_') and stem.endswith('_tr'):
        return 'named_tr' + ext
    if stem.startswith(m + '_'):
        return 'other_' + ext.lstrip('.')
    return None


def main():
    rows = []
    for sub in ('zone', os.path.join('zone', 'english'), os.path.join('zone', 'worldwide')):
        d = os.path.join(INSTALL, sub)
        if not os.path.isdir(d):
            continue
        for name in os.listdir(d):
            if name.startswith('mp_donetsk') or name.startswith('donetsk_'):
                pass
            for m in MAPS:
                k = classify(name, m)
                if k is None and m == 'mp_donetsk' and name.startswith('donetsk_'):
                    k = 'donetsk_sabs' if name.endswith('.sabs') else 'donetsk_other'
                if k:
                    rows.append((m, k, os.path.join(sub, name), os.path.getsize(os.path.join(d, name))))
                    break
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, 'w', encoding='utf-8') as fh:
        fh.write('map\tkind\tpath\tbytes\n')
        for r in sorted(rows):
            fh.write('%s\t%s\t%s\t%d\n' % r)
    summ = collections.defaultdict(lambda: [0, 0])
    for m, k, p, b in rows:
        summ[(m, k)][0] += 1
        summ[(m, k)][1] += b
    for m in MAPS:
        tot = sum(v[1] for (mm, _), v in summ.items() if mm == m)
        print('%s: %.1f MB total' % (m, tot / 1e6))
        for (mm, k), (n, b) in sorted(summ.items()):
            if mm == m:
                print('   %-18s %5d files %12.1f MB' % (k, n, b / 1e6))


if __name__ == '__main__':
    main()
