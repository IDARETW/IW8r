"""Installs an UNPUBLISHED client candidate (built by tools/dw_candidate_build.py) so that the local client
game_dx12_ship_replay_local_dw.exe is made from it (owner, 2026-11-11).

The local client is always made from the game folder's game_dx12_ship_replay_dw.exe by the client project's own
tools/setup-local-client.py, and Host-Local-Server.ps1 remakes it whenever that exe changes. So the candidate replaces
game_dx12_ship_replay_dw.exe (with its .map and .pdb), and the local client is then refreshed with that tool, which
keeps the ini and moves the previous local files to <game>\\dw-local-client-backup\\<time>.

Safety: refuses while any exe runs from the game folder; copies the current dw exe, map and pdb to
E:\\IW8-Radiant\\out\\dw_backup\\<time> first; verifies every copy by hash; publishes nothing; does not touch
data/dw-client/deployment.json. --dry-run prints what it would do. --rollback <backup folder> puts a backup back.

Usage: "C:/Users/user/AppData/Local/Programs/Python/Python314/python.exe" -B tools/dw_candidate_install.py <job folder> [--dry-run]
"""
import hashlib
import json
import shutil
import subprocess
import sys
import time
from pathlib import Path

DW = Path(r'E:\DW\IW8')
GAME = Path(r'E:\IW8\Builds\1.20-replay')
BACKUPS = Path(r'E:\IW8-Radiant\out\dw_backup')
MARKERS = (b'DW_DEVMAP ', b'iw8r_mapDebug', b'"gameTypes"')


def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def game_running():
    out = subprocess.run(['powershell', '-NoProfile', '-Command',
                          "Get-Process | Where-Object { $_.Path -and $_.Path.StartsWith('%s', 'OrdinalIgnoreCase') } | "
                          "ForEach-Object { $_.Path }" % str(GAME)], capture_output=True, text=True).stdout.strip()
    return [line for line in out.splitlines() if line.strip()]


def copy_verified(src, dst):
    shutil.copy2(src, dst)
    if sha(src) != sha(dst):
        raise SystemExit('copy of %s failed verification' % src)


def refresh_local():
    result = subprocess.run([sys.executable, '-B', str(DW / 'tools/setup-local-client.py'), '--client',
                             str(GAME / 'game_dx12_ship_replay_dw.exe'), '--server-data', str(DW / 'data/local-play'), '--refresh'],
                            capture_output=True, text=True)
    print(result.stdout.strip()[:1500])
    if result.returncode:
        print(result.stderr.strip()[:1500])
        raise SystemExit('setup-local-client.py failed; the dw exe was replaced, the local exe was not refreshed')


def rollback(folder):
    folder = Path(folder)
    running = game_running()
    if running:
        raise SystemExit('close the game first: ' + ', '.join(running))
    for name in ('game_dx12_ship_replay_dw.exe', 'game_dx12_ship_replay_dw.map', 'game_dx12_ship_replay_dw.pdb'):
        if (folder / name).exists():
            copy_verified(folder / name, GAME / name)
            print('restored', name)
    refresh_local()
    print('rolled back to', folder)


def main():
    args = sys.argv[1:]
    if len(args) == 2 and args[0] == '--rollback':
        return rollback(args[1])
    dry = '--dry-run' in args
    args = [a for a in args if a != '--dry-run']
    if len(args) != 1:
        raise SystemExit(__doc__)
    job = Path(args[0])
    ready = json.loads((job / 'local-readiness.json').read_text(encoding='utf-8'))
    if not ready.get('passed') or ready.get('unresolved_symbols'):
        raise SystemExit('the job did not pass its local gates')
    exe = Path(ready['executable'])
    if sha(exe) != ready['sha256']:
        raise SystemExit('the candidate exe is not the one the job verified')
    data = exe.read_bytes()
    for marker in MARKERS:
        print('marker %-16s %s' % (marker.decode(), 'present' if marker in data else 'MISSING'))
    if not all(marker in data for marker in MARKERS):
        raise SystemExit('the candidate does not contain all three client changes')
    extras = {}
    for suffix in ('.map', '.pdb'):
        found = exe.with_name('game_dx12_ship_replay_dw' + suffix)  # written beside the exe by the pipeline's symbol step
        if found.exists():
            extras[suffix] = found
    print('candidate', exe, ready['sha256'][:16], 'with', sorted(extras))
    running = game_running()
    if running:
        raise SystemExit('close the game first: ' + ', '.join(running))
    current = GAME / 'game_dx12_ship_replay_dw.exe'
    print('installed dw exe', sha(current)[:16], '-> candidate', ready['sha256'][:16])
    if dry:
        print('dry run: nothing written')
        return
    backup = BACKUPS / time.strftime('%Y%m%d-%H%M%S')
    backup.mkdir(parents=True, exist_ok=False)
    for name in ('game_dx12_ship_replay_dw.exe', 'game_dx12_ship_replay_dw.map', 'game_dx12_ship_replay_dw.pdb'):
        if (GAME / name).exists():
            copy_verified(GAME / name, backup / name)
    print('backup in', backup)
    copy_verified(exe, current)
    for suffix, path in extras.items():
        copy_verified(path, GAME / ('game_dx12_ship_replay_dw' + suffix))
    refresh_local()
    local = GAME / 'game_dx12_ship_replay_local_dw.exe'
    receipt = json.loads(local.with_suffix('.setup.json').read_text(encoding='utf-8'))
    ok = receipt.get('source_sha256') == ready['sha256'] and receipt.get('output_sha256') == sha(local)
    local_data = local.read_bytes()
    print('local exe', sha(local)[:16], 'made from the candidate:', ok, '; markers:', all(m in local_data for m in MARKERS))
    print('rollback: "%s" -B %s --rollback "%s"' % (sys.executable, Path(__file__), backup))
    if not ok:
        raise SystemExit('the local exe receipt does not match')


if __name__ == '__main__':
    main()
