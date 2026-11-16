"""Stock material colour tints (owner playtest 2026-11-11: traffic cones came out grey).

A stock material can tint its colour image: MaterialConstantDef index 9 holds an RGB colour in the first four bytes of
its literal (R, G, B, 0), a plain multiplier where FF FF FF is neutral. mo/me_traffic_cone_01_top has ED 37 0F (orange)
over the grey image it shares with mo/me_traffic_cone_01_base (80 80 80: the dark rubber base).

This exports every material body of each stock zone with ACTS (bodies are resident in the fastfile: no XPaks needed) and
writes data/asset_catalog/iw8_material_tints.tsv: material, RRGGBB, zone. Only non-neutral tints are listed.

    py -3 -I tools/build_material_tints.py [--zones a,b] [--scratch DIR]
"""
import argparse
import json
import os
import shutil
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ACTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'bin', 'release', 'iw8r-acts.exe')
REPLAY = r'E:\IW8\Builds\1.20-replay\game_dx12_ship_replay.exe'
OODLE = r'E:\IW8\Builds\1.20-replay\oo2core_7_win64.dll'
OUT = os.path.join(ROOT, 'data', 'asset_catalog', 'iw8_material_tints.tsv')


def zones_from_catalog():
    zones = set()
    with open(os.path.join(ROOT, 'data', 'asset_catalog', 'iw8_materials.tsv'), encoding='utf-8') as f:
        for line in f:
            if line.startswith('#') or '\t' not in line:
                continue
            for z in line.rstrip('\n').split('\t')[1].split(','):
                if z:
                    zones.add(z)
    return sorted(zones)


def tint_of(fields):
    table = fields.get('constantTable') or {}
    for c in table.get('values') or []:
        if c.get('index') != 9:
            continue
        v = (c.get('literal') or {}).get('v') or []
        if not v:
            return None
        if not isinstance(v[0], (int, float)):
            return None  # NaN / inf: not a packed colour (its fourth byte would not be 0)
        try:
            b = struct.pack('<f', v[0])
        except (struct.error, OverflowError):
            return None
        if b[3] != 0:
            return None  # not a packed colour
        return b[0], b[1], b[2]
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--zones', default='')
    ap.add_argument('--scratch', default=os.path.join(ROOT, 'out', 'tint_scratch'))
    args = ap.parse_args()
    zones = [z for z in args.zones.split(',') if z] or zones_from_catalog()
    zone_dir = os.path.join(os.path.dirname(REPLAY), 'zone')
    tints = {}
    seen = 0
    stats = {}
    for i, zone in enumerate(zones):
        ff = os.path.join(zone_dir, zone + '.ff')
        if not os.path.isfile(ff):
            continue
        shutil.rmtree(args.scratch, ignore_errors=True)
        os.makedirs(args.scratch, exist_ok=True)
        cmd = [ACTS, '--noUpdater', 'fastfile', '-r', 'mw19replay', '-g', REPLAY, '--oodle', OODLE, '-a', 'material', '-l',
               '-o', args.scratch, ff]
        subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        base = os.path.join(args.scratch, 'mw19replay', zone, 'assets', 'material')
        n = 0
        for dirpath, _, files in os.walk(base):
            for fn in files:
                if not fn.endswith('.asset.json'):
                    continue
                try:
                    with open(os.path.join(dirpath, fn), encoding='utf-8') as f:
                        fields = json.load(f)['asset']['fields']
                except (OSError, ValueError, KeyError):
                    continue
                name = (fields.get('name') or {}).get('string')
                if not name:
                    continue
                n += 1
                t = tint_of(fields)
                key = 'none' if t is None else 'neutral' if t == (0xFF, 0xFF, 0xFF) else 'tinted'
                stats[key] = stats.get(key, 0) + 1
                if key == 'tinted' and name not in tints:
                    tints[name] = ('%02X%02X%02X' % t, zone)
        seen += n
        print('[%d/%d] %s: %d material(s), %d tint(s) so far' % (i + 1, len(zones), zone, n, len(tints)), flush=True)
    shutil.rmtree(args.scratch, ignore_errors=True)
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# Stock material colour tints (constant 9, RRGGBB multiplier, FFFFFF = neutral and not listed). tools/build_material_tints.py\n')
        for name in sorted(tints):
            f.write('%s\t%s\t%s\n' % (name, tints[name][0], tints[name][1]))
    print('wrote %s: %d tinted of %d material bodies (%s)' % (OUT, len(tints), seen, stats))
    return 0


if __name__ == '__main__':
    sys.exit(main())
