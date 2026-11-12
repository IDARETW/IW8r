#!/usr/bin/env python3
"""Build data/asset_catalog/iw8_streamed_images.tsv from structured GfxImage exports.

Input: ACTS structured image exports (`mw19-asset-json`, pool `image`, root GfxImage). Produce them with
ACTS_REPLAY_STRUCTURED=1 (without it the image pool exports DDS only, see docs/research/52_streamed_images.md):

  set ACTS_REPLAY_STRUCTURED=1
  acts.exe --noUpdater fastfile -r mw19replay -g <replay exe> --oodle <oo2core dll> --xpak <each populated xpak> \
      -a image -l -o <out> <zone>.ff

Per image (fields from GfxImage, see the research doc for file:line):
  full_width/full_height     = width / height
  streamed                   = 1 when flags & 0x40 (ACTS mw19_image.cpp:31) and streamedPartCount > 0, else 0
  resident_width/height      = top mip of the smallest stream part (fewest mips, see stream_part_mips), when streamed;
                               full size otherwise. UNVERIFIED as the game-resident level: ACTS reads every part
                               through XPak keys and never reads pixel bytes from the .ff for a streamed image.

Output: TSV, one row per image, header comment lines starting with '#', then a column header row.
Usage: py -3 -I tools/build_streamed_images.py [--exports DIR] [--out TSV] [--coverage TEXT]
"""

import argparse
import glob
import json
import os
import sys

STREAMED_FLAG = 0x40
COLUMNS = ["image", "full_width", "full_height", "resident_width", "resident_height", "streamed"]


def repo_root():
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def stream_part_mips(level_count_and_size):
    # Replay packs the cumulative mip count in the low 4 bits and the cumulative byte size above them
    # (ACTS mw19_image.cpp:103-104: cumulativeSize = packed >> 4, cumulativeMips = packed & 15).
    return level_count_and_size & 15


def value_of(node):
    """Structured fields are either plain numbers or {"value": n, ...} objects."""
    if isinstance(node, dict):
        return node.get("value", 0)
    return node if node is not None else 0


def describe(path, fields):
    """Return (row dict, None) or (None, reason)."""
    name_node = fields.get("name")
    name = name_node.get("string") if isinstance(name_node, dict) else name_node
    if not name:
        return None, "no name"
    try:
        full_w = int(fields["width"])
        full_h = int(fields["height"])
        flags = int(value_of(fields.get("flags")))
        levels = int(fields["levelCount"])
        parts = int(fields.get("streamedPartCount", 0))
    except (KeyError, TypeError, ValueError) as err:
        return None, "missing field: %s" % err
    if full_w <= 0 or full_h <= 0:
        return None, "bad full size"

    streamed = bool(flags & STREAMED_FLAG) and parts > 0
    res_w, res_h = full_w, full_h
    if streamed:
        streams = fields.get("streams") or []
        live = []
        for s in streams:
            key = int((s.get("xpakEntry") or {}).get("key", 0))
            lcs = int(value_of(s.get("levelCountAndSize")))
            if key == 0 or s.get("width", 0) == 0:
                continue
            live.append((stream_part_mips(lcs), int(s["width"]), int(s["height"])))
        if len(live) != parts:
            return None, "stream parts %d listed %d" % (parts, len(live))
        # The parts must cover the whole chain, else the image cannot be rebuilt (ACTS mw19_image.cpp:120-121).
        if max(m for m, _, _ in live) != levels:
            return None, "stream parts do not cover %d mips" % levels
        mips, res_w, res_h = min(live)
        if mips <= 0 or res_w <= 0 or res_h <= 0:
            return None, "bad resident part"
    return {
        "image": name,
        "full_width": full_w,
        "full_height": full_h,
        "resident_width": res_w,
        "resident_height": res_h,
        "streamed": 1 if streamed else 0,
    }, None


def collect(exports):
    pattern = os.path.join(exports, "**", "*.asset.json")
    rows = {}
    skipped = []
    for path in sorted(glob.glob(pattern, recursive=True)):
        if "assets" + os.sep + "image" not in path and "/assets/image/" not in path.replace(os.sep, "/"):
            continue
        try:
            with open(path, encoding="utf-8") as fh:
                doc = json.load(fh)
        except (OSError, ValueError) as err:
            skipped.append((path, "unreadable: %s" % err))
            continue
        if doc.get("format") != "mw19-asset-json" or doc.get("pool") != "image":
            skipped.append((path, "not an image asset"))
            continue
        fields = (doc.get("asset") or {}).get("fields")
        if not isinstance(fields, dict):
            skipped.append((path, "no fields"))
            continue
        row, why = describe(path, fields)
        if row is None:
            skipped.append((path, why))
            continue
        if row["image"] in rows:
            skipped.append((path, "duplicate of an earlier export"))
            continue
        rows[row["image"]] = row
    return rows, skipped


def main(argv):
    root = repo_root()
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--exports", default=os.path.join(root, "out", "streamed_probe", "p2"),
                        help="folder that holds assets/image/*.asset.json (searched recursively)")
    parser.add_argument("--out", default=os.path.join(root, "data", "asset_catalog", "iw8_streamed_images.tsv"))
    parser.add_argument("--coverage", default="sample: image pool of mp_backlot2.ff (Replay 1.20, 2671 image assets exported)",
                        help="what the catalogue covers, written into the header comment")
    args = parser.parse_args(argv)
    if not os.path.isdir(args.exports):
        print("error: exports folder not found: %s (run the structured ACTS export first, see the module doc)"
              % args.exports, file=sys.stderr)
        return 2

    rows, skipped = collect(args.exports)
    if not rows:
        print("error: no structured image exports under %s" % args.exports, file=sys.stderr)
        return 2
    names = sorted(rows)
    streamed = sum(rows[n]["streamed"] for n in names)
    header = [
        "# iw8_streamed_images.tsv: stock Replay 1.20 images, streamed mip state (docs/research/52_streamed_images.md)",
        "# coverage: %s" % args.coverage,
        "# full_* = GfxImage width/height. streamed=1 when flags&0x40 and streamedPartCount>0 (ACTS mw19_image.cpp:31).",
        "# resident_* = top mip of the smallest XPak stream part when streamed (UNVERIFIED as the game-resident level), else full size.",
        "# images=%d streamed=%d built by tools/build_streamed_images.py from %s" % (
            len(names), streamed, os.path.basename(os.path.normpath(args.exports))),
    ]
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(header) + "\n")
        fh.write("\t".join(COLUMNS) + "\n")
        for n in names:
            r = rows[n]
            fh.write("\t".join(str(r[c]) for c in COLUMNS) + "\n")
    print("rows=%d streamed=%d skipped=%d -> %s" % (len(names), streamed, len(skipped), args.out))
    for path, why in skipped[:20]:
        print("skip: %s (%s)" % (path, why), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
