"""Markdown tables from --viewport-bench reports (docs/research/85_viewport_performance.md).

Usage: python -I tools/viewport_bench_table.py out/perf/base [out/perf/new] [--scenes a,b]
One run: a table per scene with every scenario (median and worst, CPU in render() and total with the GPU, the scene
cache sync and the rest of a document change). Two runs: before / after per scenario, total median and worst.
"""
import argparse
import os
import re

SCENARIOS = ["still", "still_inside", "moving", "select", "edit", "toggle_modes", "still_lit", "select_lit", "edit_lit", "still_wireframe", "wireframe_inside"]


def read(path):
    out = {"scenarios": {}, "counts": {}, "lines": []}
    if not os.path.isfile(path):
        return None
    for line in open(path, encoding="utf-8"):
        line = line.rstrip("\n")
        out["lines"].append(line)
        parts = line.split()
        if not parts:
            continue
        kv = dict(p.split("=", 1) for p in parts[1:] if "=" in p)
        if parts[0] in SCENARIOS:
            out["scenarios"][parts[0]] = kv
        elif parts[0].startswith("counts_"):
            out["counts"][parts[0][7:]] = kv
        elif parts[0] in ("map", "memory", "reference"):
            out[parts[0]] = kv
    return out


def f(v):
    x = float(v)
    return f"{x:.0f}" if x >= 100 else f"{x:.1f}" if x >= 10 else f"{x:.2f}"


def one(run, scenes):
    for scene in scenes:
        r = read(os.path.join(run, scene, "viewport-bench.txt"))
        if not r:
            continue
        m, mem = r.get("map", {}), r.get("memory", {})
        print(f"\n**{scene}**: {m.get('items', '?')} items, opened in {m.get('open_ms', '?')} ms, first frame {m.get('first_frame_ms', '?')} ms, "
              f"peak memory {mem.get('peak_working_set_mb', '?')} MB (committed {mem.get('peak_pagefile_mb', '?')} MB)\n")
        print("| scenario | frames | render() CPU ms (median / worst) | total ms with GPU (median / worst) | scene sync ms | rest of the change ms | full rebuilds |")
        print("|---|---|---|---|---|---|---|")
        for name in SCENARIOS:
            s = r["scenarios"].get(name)
            if not s:
                continue
            print(f"| {name} | {s['frames']} | {f(s['cpu_med'])} / {f(s['cpu_max'])} | {f(s['total_med'])} / {f(s['total_max'])} | "
                  f"{f(s['sync_med'])} | {f(s['other_med'])} | {s['geometry_builds']} |")
        c = r["counts"]
        if c:
            print("\n| view | triangles | lines | batches | draw calls |")
            print("|---|---|---|---|---|")
            for name in ("textured_a", "textured_b", "lit_a", "wireframe_a", "wireframe_b", "flat_a", "flat_b"):
                if name in c:
                    print(f"| {name} | {int(c[name]['triangles']):,} | {int(c[name]['lines']):,} | {c[name]['batches']} | {c[name]['draw_calls']} |")


def two(a, b, scenes):
    print("| scene | scenario | before: total ms (median / worst) | after: total ms (median / worst) | faster (median) |")
    print("|---|---|---|---|---|")
    for scene in scenes:
        ra, rb = read(os.path.join(a, scene, "viewport-bench.txt")), read(os.path.join(b, scene, "viewport-bench.txt"))
        if not ra or not rb:
            continue
        for name in SCENARIOS:
            sa, sb = ra["scenarios"].get(name), rb["scenarios"].get(name)
            if not sa or not sb:
                continue
            # A document change costs the scene sync too (viewport code); the other panels are left out.
            ta = float(sa["total_med"]) + float(sa["sync_med"])
            tb = float(sb["total_med"]) + float(sb["sync_med"])
            wa = float(sa["total_max"]) + float(sa["sync_max"])
            wb = float(sb["total_max"]) + float(sb["sync_max"])
            print(f"| {scene} | {name} | {f(ta)} / {f(wa)} | {f(tb)} / {f(wb)} | {ta / max(tb, 1e-9):.1f}x |")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("runs", nargs="+")
    ap.add_argument("--scenes", default="sample_room,fix1,synth_2000,synth_10000,synth_50000,reference")
    a = ap.parse_args()
    scenes = [s for s in a.scenes.split(",") if s]
    if len(a.runs) == 1:
        one(a.runs[0], scenes)
    else:
        two(a.runs[0], a.runs[1], scenes)


if __name__ == "__main__":
    main()
