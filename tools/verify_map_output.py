"""Inspect a BUILT Replay map package (what the game will load), not the extraction.

Runs ACTS over <package>/<id>.ff (map_ents + xmodel) and techsets_<id>.ff (material), decodes the shipped entity
string and prints a summary: entity classnames with counts, model keys, xmodels and materials in the zones.
Optional checks fail the run (exit 1) so a change can be verified against the real output:

  py -3 -I tools/verify_map_output.py <package dir> [--expect-class node_pathnode>=1] [--expect-model iw8r_stock_x]
                                      [--expect-material iw8r_stock/tmo/x] [--json out.json]

Package dir: the x64-zt output folder of the map (mp_<id>.ff, techsets_mp_<id>.ff, ...). Never writes there.
Tools: ACTS / Replay / Oodle paths default to the owner's workstation and can be overridden with --acts/--replay/--oodle.
"""
import argparse
import collections
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

ACTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'bin', 'release', 'iw8r-acts.exe')
REPLAY = r"E:/IW8/Builds/1.20-replay/game_dx12_ship_replay.exe"
OODLE = r"E:/IW8/Builds/1.20-replay/oo2core_7_win64.dll"


def run_acts(args, ff, types, out):
    cmd = [args.acts, "--noUpdater", "fastfile", "-r", "mw19replay", "-g", args.replay, "--oodle", args.oodle,
           "-a", types, "-l", "-o", out, ff]
    subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    zone = os.path.splitext(os.path.basename(ff))[0]
    journal = os.path.join(out, "mw19replay", zone, "assets.jsonl")
    rows = []
    if os.path.isfile(journal):
        with open(journal, encoding="utf-8") as f:
            rows = [json.loads(line) for line in f if line.strip()]
    return rows, os.path.join(out, "mw19replay", zone)


def parse_entities(text):
    """Entity string records: { <key id or "name"> "value" ... }. Returns a list of string-value lists per entity."""
    ents = []
    for body in re.findall(r"\{([^{}]*)\}", text):
        ents.append(re.findall(r'"((?:[^"\\]|\\.)*)"', body))
    return ents


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("package")
    ap.add_argument("--acts", default=ACTS)
    ap.add_argument("--replay", default=REPLAY)
    ap.add_argument("--oodle", default=OODLE)
    ap.add_argument("--expect-class", action="append", default=[], help="classname>=N (or just classname: >=1)")
    ap.add_argument("--expect-model", action="append", default=[])
    ap.add_argument("--expect-material", action="append", default=[])
    ap.add_argument("--json")
    args = ap.parse_args()

    pkg = os.path.abspath(args.package)
    main_ff = [f for f in os.listdir(pkg) if f.endswith(".ff") and not f.startswith(("eng_", "srv_", "ww_", "techsets_"))]
    if len(main_ff) != 1:
        print("cannot find the main zone in", pkg)
        return 2
    mid = os.path.splitext(main_ff[0])[0]
    tmp = tempfile.mkdtemp(prefix="iw8r_verify_")
    try:
        rows, zdir = run_acts(args, os.path.join(pkg, mid + ".ff"), "map_ents,xmodel", os.path.join(tmp, "main"))
        ents = []
        for r in rows:
            if r.get("type") == "map_ents" and r.get("status") == "ok":
                d = json.load(open(os.path.join(zdir, r["file"]), encoding="utf-8"))
                hexs = d["asset"]["fields"]["entityString"]["bytes"]
                ents = parse_entities(bytes.fromhex(hexs).decode("utf-8", "replace"))
        xmodels = sorted(r["name"] for r in rows if r.get("type") == "xmodel")
        tech = os.path.join(pkg, "techsets_" + mid + ".ff")
        mats = []
        if os.path.isfile(tech):
            trows, _ = run_acts(args, tech, "material", os.path.join(tmp, "tech"))
            mats = sorted(r["name"] for r in trows if r.get("type") == "material")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    # The classname is the value that names a known class pattern; keys are numeric ids, so take the string values.
    classes = collections.Counter()
    models = collections.Counter()
    for vals in ents:
        cls = next((v for v in vals if re.fullmatch(r"[a-z][a-z0-9_]*", v) and ("_" in v or v in ("worldspawn",))), None)
        if cls:
            classes[cls] += 1
        for v in vals:
            if v.startswith("iw8r_stock_") or v.startswith("*"):
                models[v] += 1
    summary = {"zone": mid, "entities": len(ents), "classes": dict(classes), "model_values": dict(models),
               "xmodels": xmodels, "materials": mats}
    print(f"{mid}: {len(ents)} entities, {len(xmodels)} xmodels, {len(mats)} materials")
    for c, n in sorted(classes.items()):
        print(f"  class {c}: {n}")
    for m, n in sorted(models.items()):
        print(f"  model value {m}: {n}")
    print("  xmodels:", ", ".join(xmodels) if xmodels else "(none)")
    print("  materials:", ", ".join(mats) if mats else "(none)")
    if args.json:
        json.dump(summary, open(args.json, "w", encoding="utf-8"), indent=1)

    failed = []
    for e in args.expect_class:
        name, _, n = e.partition(">=")
        if classes.get(name, 0) < int(n or 1):
            failed.append(f"class {name}: {classes.get(name, 0)} < {n or 1}")
    for m in args.expect_model:
        if m not in xmodels and m not in models:
            failed.append(f"model {m} missing")
    for m in args.expect_material:
        if m not in mats:
            failed.append(f"material {m} missing")
    for f in failed:
        print("FAIL", f)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
