"""Compares the screenshots of two --viewport-bench runs pixel by pixel (docs/research/85_viewport_performance.md).

Usage: python -I tools/compare_bench_images.py out/perf/base out/perf/new [--scenes a,b] [--diff-dir out/perf/diff]
Prints one line per scene and screenshot: identical, or the number of differing pixels and the largest channel
difference. With --diff-dir, a picture of where the pixels differ is saved for every screenshot that is not identical.
Exit code 0 when every pair is identical, 1 otherwise.
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image


def compare(a_path, b_path, diff_path=None):
    a = np.asarray(Image.open(a_path).convert("RGB"), dtype=np.int16)
    b = np.asarray(Image.open(b_path).convert("RGB"), dtype=np.int16)
    if a.shape != b.shape:
        return None, None, f"sizes differ {a.shape} / {b.shape}"
    d = np.abs(a - b).max(axis=2)
    n = int((d > 0).sum())
    if n and diff_path:
        os.makedirs(os.path.dirname(diff_path), exist_ok=True)
        Image.fromarray(np.where(d > 0, 255, 0).astype(np.uint8)).save(diff_path)
    return n, int(d.max()), None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--scenes", default="")
    ap.add_argument("--diff-dir", default="")
    args = ap.parse_args()
    scenes = [s for s in args.scenes.split(",") if s] or sorted(
        d for d in os.listdir(args.a) if os.path.isdir(os.path.join(args.a, d)) and os.path.isdir(os.path.join(args.b, d)))
    bad = 0
    for scene in scenes:
        names = sorted(f for f in os.listdir(os.path.join(args.a, scene)) if f.endswith(".png"))
        same = 0
        for name in names:
            pa, pb = os.path.join(args.a, scene, name), os.path.join(args.b, scene, name)
            if not os.path.isfile(pb):
                print(f"{scene}/{name}: missing in {args.b}")
                bad += 1
                continue
            diff_path = os.path.join(args.diff_dir, scene, name) if args.diff_dir else None
            n, mx, err = compare(pa, pb, diff_path)
            if err:
                print(f"{scene}/{name}: {err}")
                bad += 1
            elif n:
                print(f"{scene}/{name}: {n} pixels differ, largest difference {mx}")
                bad += 1
            else:
                same += 1
        print(f"{scene}: {same} of {len(names)} screenshots identical")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
