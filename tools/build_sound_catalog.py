"""Build the stock sound-alias catalog for the editor's Sounds dock (roadmap 4.5, 6.7).

Input: x64-zt verifyzone traces with soundbank alias lines ("S <alias> <loop 0/1> <distMax>", x64-zt
src/games/iw8/zone/loader.cpp), made with
  IW8_READ_TRACE=<dir>/trace_<zone>.txt zonetool -game iw8 -path <Replay> verifyzone <zone>
for the always-loaded MP zones (common_mp, common_base_mp, code_post_gfx).
Output: data/asset_catalog/iw8_common_sounds.tsv: alias<TAB>loop<TAB>distMax<TAB>zones (sorted).
Usage: py -3 -I tools/build_sound_catalog.py [--traces out/sound-trace] [--out data/asset_catalog]
"""
import argparse
import os
import sys


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--traces', default=os.path.join('out', 'sound-trace'))
    ap.add_argument('--out', default=os.path.join('data', 'asset_catalog'))
    a = ap.parse_args()
    files = sorted(f for f in os.listdir(a.traces) if f.startswith('trace_') and f.endswith('.txt'))
    if not files:
        print('error: no trace_*.txt in ' + a.traces, file=sys.stderr)
        return 1
    found = {}
    for f in files:
        zone = f[len('trace_'):-len('.txt')]
        with open(os.path.join(a.traces, f), encoding='utf-8', errors='replace') as fh:
            for line in fh:
                if not line.startswith('S '):
                    continue
                parts = line.split()
                if len(parts) != 4:
                    continue
                name, loop, dist = parts[1], parts[2], parts[3]
                entry = found.setdefault(name.lower(), [name, loop, dist, set()])
                entry[3].add(zone)
    os.makedirs(a.out, exist_ok=True)
    path = os.path.join(a.out, 'iw8_common_sounds.tsv')
    with open(path, 'w', encoding='utf-8', newline='\n') as out:
        out.write('# Replay 1.20 sound aliases in the always-loaded MP soundbanks (x64-zt verifyzone traces).\n')
        out.write('# Played by name from a map script (sound_emitter entities). alias<TAB>loop<TAB>distMax<TAB>zones\n')
        for key in sorted(found):
            name, loop, dist, zones = found[key]
            out.write(f'{name}\t{loop}\t{dist}\t{",".join(sorted(zones))}\n')
    print(f'wrote {path}: {len(found)} aliases')
    return 0


if __name__ == '__main__':
    sys.exit(main())
