"""Build the stock IW8 material/model name catalog for the editor's asset browsers.

Input: x64-zt verifyzone traces (IW8_READ_TRACE) of Replay 1.20 stock zones, one `A <pool id> <name>` line per
asset (out/x64zt-verify/runs/trace_<zone>.txt; produced by `zonetool -game iw8 verifyzone <zone>` with
IW8_READ_TRACE set). Pool ids follow ACTS docs/mw19/pools.md: 9 xmodel, 11 material, 19 image.
Output: data/asset_catalog/iw8_materials.tsv and iw8_models.tsv (name<TAB>zones, comma-separated, sorted).

Usage: python -I tools/build_asset_catalog.py [--traces out/x64zt-verify/runs] [--out data/asset_catalog]
"""
import argparse
import collections
import os
import re
import sys

POOLS = {9: 'models', 11: 'materials', 19: 'images'}
LINE = re.compile(r'^A (\d+) (.+)$')


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--traces', default=os.path.join('out', 'x64zt-verify', 'runs'))
    ap.add_argument('--out', default=os.path.join('data', 'asset_catalog'))
    a = ap.parse_args()
    found = {k: collections.defaultdict(set) for k in POOLS.values()}
    files = sorted(f for f in os.listdir(a.traces) if f.startswith('trace_') and f.endswith('.txt'))
    if not files:
        print('error: no trace_*.txt in ' + a.traces, file=sys.stderr)
        return 1
    for f in files:
        zone = f[len('trace_'):-len('.txt')]
        with open(os.path.join(a.traces, f), encoding='utf-8', errors='replace') as fh:
            for line in fh:
                m = LINE.match(line.rstrip('\n'))
                if not m:
                    continue
                kind = POOLS.get(int(m.group(1)))
                if kind:
                    name = m.group(2).lstrip(',')
                    found[kind][name].add(zone)
    os.makedirs(a.out, exist_ok=True)
    for kind, names in found.items():
        path = os.path.join(a.out, f'iw8_{kind}.tsv')
        with open(path, 'w', encoding='utf-8', newline='\n') as out:
            out.write(f'# Replay 1.20 stock {kind} from x64-zt verifyzone traces of {len(files)} zones\n')
            for name in sorted(names):
                out.write(name + '\t' + ','.join(sorted(names[name])) + '\n')
        print(f'{kind}: {len(names)} names -> {path}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
