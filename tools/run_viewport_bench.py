"""Runs IW8Radiant.exe --viewport-bench on every benchmark scene (docs/research/85_viewport_performance.md).

The scenes come from tools/make_perf_maps.py (run it first). The editor's caches are never written: the benchmark
reads the scratch copies under out/perf/cache, and the stock world of mp_backlot2 is read straight from the cached
export (IW8Radiant.exe --viewport-bench ... --reference <folder>).

Usage: python -I tools/run_viewport_bench.py --exe out/perf_bin_base/IW8Radiant.exe --out out/perf/base
           [--scenes sample_room,fix1,synth_2000,synth_10000,synth_50000,reference] [--frames 120] [--profile]
"""
import argparse
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
USER_CACHE = os.path.join(os.environ.get("LOCALAPPDATA", ""), "IW8 Radiant Suite", "IW8 Radiant Suite")
SCENES = {
    "sample_room": ("tests/data/iw8_sample_room.map", None),
    "fix1": ("out/perf/fix1/mp_iw8r_fix1.map", None),
    "synth_2000": ("out/perf/maps/synth_2000.map", None),
    "synth_10000": ("out/perf/maps/synth_10000.map", None),
    "synth_50000": ("out/perf/maps/synth_50000.map", None),
    # The stock reference path: the sample room with the cached stock world of mp_backlot2 under it.
    "reference": ("tests/data/iw8_sample_room.map", os.path.join(USER_CACHE, "world_cache", "mp_backlot2")),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--scenes", default=",".join(SCENES))
    ap.add_argument("--frames", type=int, default=120)
    ap.add_argument("--profile", action="store_true")
    ap.add_argument("--only", default="", help="comma list of scenario names (IW8R_BENCH_ONLY)")
    a = ap.parse_args()
    env = dict(os.environ)
    env["IW8R_MODEL_CACHE_ROOT"] = os.path.join(ROOT, "out", "perf", "cache", "model_cache").replace("\\", "/")
    env["IW8R_MATERIAL_CACHE_ROOT"] = os.path.join(ROOT, "out", "perf", "cache", "material_cache").replace("\\", "/")
    env["IW8R_BENCH_FRAMES"] = str(a.frames)
    if a.profile:
        env["IW8R_BENCH_PROFILE"] = "1"
    if a.only:
        env["IW8R_BENCH_ONLY"] = a.only
    rc = 0
    for name in a.scenes.split(","):
        if name not in SCENES:
            print("unknown scene", name)
            rc = 2
            continue
        map_file, ref = SCENES[name]
        map_file = os.path.join(ROOT, map_file)
        if not os.path.isfile(map_file):
            print(f"{name}: skipped, no {map_file}")
            continue
        if ref and not os.path.isdir(os.path.join(ref, "assets")):
            print(f"{name}: skipped, the stock world is not cached ({ref})")
            continue
        out = os.path.join(ROOT, a.out, name)
        cmd = [os.path.join(ROOT, a.exe), "--viewport-bench", map_file, "--out", out]
        if ref:
            cmd += ["--reference", ref]
        t = time.time()
        r = subprocess.run(cmd, env=env, cwd=ROOT)
        print(f"{name}: exit {r.returncode}, {time.time() - t:.0f} s", flush=True)
        rc = rc or r.returncode
    sys.exit(rc)


if __name__ == "__main__":
    main()
