"""Read-only ACTS export of one asset type from a stock zone (the game folder is only read).
Usage: py -3 -I tools/acts_export.py <zone> <asset type> <out folder>"""
import os, subprocess, sys
ACTS = r'E:\mw124\atian-cod-tools\build\bin\Release\acts.exe'
GAME = r'E:\IW8\Builds\1.20-replay'
zone, kind, out = sys.argv[1:4]
out = os.path.abspath(out)
os.makedirs(out, exist_ok=True)
args = [ACTS, '--noUpdater', 'fastfile', '-r', 'mw19replay', '-g', GAME + r'\game_dx12_ship_replay.exe', '--oodle',
        GAME + r'\oo2core_7_win64.dll', '-a', kind, '-o', out, os.path.join(GAME, 'zone', zone + '.ff')]
r = subprocess.run(args, capture_output=True, text=True, errors='replace')
print('\n'.join((r.stdout + r.stderr).strip().splitlines()[-4:]))
