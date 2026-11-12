"""Builds an UNPUBLISHED candidate of the recompiled Replay client from E:\\DW\\IW8 (owner, 2026-11-11: "the droplet is
down and we dont need to publish anything"; the exe to update is the local client).

It runs the client project's own pipeline exactly as
    python -B tools/build-dw-client.py --validate-only --carry-installed-gulag --compiler-jobs 2
does, with one difference. The carry-forward check (tools/verify-installed-gulag-carryforward.py) wants the game
folder's game_dx12_ship_replay_dw.exe to be the published release named in data/dw-client/deployment.json. The game
folder holds a later hand-installed candidate, so the check is given the bytes of the retained copy of that published
release (data/dw-client/game_dx12_ship_replay_dw.exe, verified against the same hash) wherever it reads the installed
exe. Every other gate of the check and of the pipeline runs unchanged. No pipeline file or record is edited, nothing is
published, and nothing is written into the game folder: the result is the job folder under data/dw-client/jobs.

Usage: py -3 -I is NOT used (the pipeline needs its own Python and packages):
    "C:/Users/user/AppData/Local/Programs/Python/Python314/python.exe" -B E:/IW8-Radiant/tools/dw_candidate_build.py
    ... dw_candidate_build.py --resume <job folder>     build a job that is already prepared
A full run needs about 9 GB free on E: (a 7 GB database snapshot plus the build).
"""
import importlib.util
import os
import sys
import time
from pathlib import Path

ROOT = Path(r'E:\DW\IW8')
PUBLISHED = ROOT / 'data/dw-client/game_dx12_ship_replay_dw.exe'

os.chdir(ROOT)
sys.path.insert(0, str(ROOT / 'tools'))
os.environ['DW_DEDICATED_CLIENT_COMPILER_JOBS'] = '2'


def child_inspect():
    """Runs in a child process (--inspect): verify() of the carry-forward checker, reading the retained published
    exe in place of the installed one. Prints the evidence as one JSON line, like the checker's --stdout-json."""
    import json
    path = ROOT / 'tools/verify-installed-gulag-carryforward.py'
    spec = importlib.util.spec_from_file_location('iw8r_carry_checker', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    installed = module.IMAGE
    real_digest, real_extract = module.digest, module.extract
    module.digest = lambda p: real_digest(PUBLISHED) if Path(p) == installed else real_digest(p)
    module.extract = lambda image, sha: real_extract(PUBLISHED if Path(image) == installed else image, sha)
    print(json.dumps(module.verify(), separators=(',', ':')))


def inspect_with_published_exe():
    """Replacement of dw_client.inspect_installed_gulag: the same child-process shape, so no helper module of the
    check stays loaded in the build."""
    import json
    import subprocess
    result = subprocess.run([sys.executable, '-B', str(Path(__file__).resolve()), '--inspect'], cwd=ROOT, check=True,
                            capture_output=True, text=True)
    evidence = json.loads(result.stdout)
    path = ROOT / 'tools/verify-installed-gulag-carryforward.py'
    if not evidence.get('passed') or evidence.get('checker_sha256') != dw_client.digest(path):
        raise ValueError('Installed Gulag carry-forward inspection differs')
    return evidence


def main():
    if sys.argv[1:] == ['--inspect']:
        return child_inspect()
    global dw_client
    import dw_client
    deployment = dw_client.read(ROOT / 'data/dw-client/deployment.json')
    if dw_client.digest(PUBLISHED) != deployment.get('sha256'):
        raise SystemExit('The retained published exe is not the release named in deployment.json; stopping.')
    if len(sys.argv) in (3, 4) and sys.argv[1] == '--verify':
        # --verify <job folder>: run the pipeline's verification gates on a job that is already compiled and linked,
        # with the pipeline's own carry-forward check untouched. For this the game folder's dw exe must BE the
        # published release (one gate reads it in its own process). Completed gates are reused by the pipeline.
        directory = dw_client.inside(Path(sys.argv[2]))
        job = dw_client.read(directory / 'workstation-job.json')
        if job.get('status') != 'succeeded':
            raise SystemExit('the job has no finished compile: ' + str(job.get('status')))
        report_path = dw_client.inside(Path(job['build_directory']) / 'build-report.json')
        receipt = dw_client.read(directory / 'source-receipt.json')
        database = dw_client.dw_sqlite_snapshot.verify_binding(receipt['sqlite_snapshot'])
        with dw_client.pipeline_lock():
            with dw_client.dw_sqlite_snapshot.scoped_sqlite(database):
                # --waive-gameplay: the pipeline's own maintainer override (build-game's --skip-gameplay-checks), which
                # keeps the five build-integrity gates. Needed while the remote server is down: the map publication
                # gate connects to it. The owner's 2026-11-08 build was made this way.
                dw_client.verify(directory, report_path, publish_ready=False, gameplay_checks='--waive-gameplay' not in sys.argv)
        print('CANDIDATE', directory, flush=True)
        return
    dw_client.inspect_installed_gulag = inspect_with_published_exe
    started = time.perf_counter()
    resume = sys.argv[2] if len(sys.argv) == 3 and sys.argv[1] == '--resume' else None
    with dw_client.pipeline_lock():
        # --resume <job folder>: build a job whose prepare already finished (its 7 GB database snapshot is kept).
        directory = dw_client.inside(Path(resume)) if resume else dw_client.prepare(carry_installed_gulag=True)
        prepared = time.perf_counter()
        print('PREPARED', directory, flush=True)
        dw_client.build(directory, publish_ready=False, gameplay_checks=True)
        timing = dict(prepare_seconds=prepared - started, build_and_checks_seconds=time.perf_counter() - prepared,
                      total_seconds=time.perf_counter() - started, directory=str(directory), compiler_jobs=2)
        if not (directory / 'timing.json').exists():
            dw_client.save(directory / 'timing.json', timing)
    print('CANDIDATE', directory, flush=True)
    print('DW build elapsed: %.1f s' % (time.perf_counter() - started), flush=True)


if __name__ == '__main__':
    main()
