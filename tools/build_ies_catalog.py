#!/usr/bin/env python3
"""Extract the stock IES profile list of the x64-zt fork (replay_ies) into a TSV catalog.

Reads (read only):
  <fork>/src/games/iw8/assets/replay_ies.h     kLookupName, the Profile layout
  <fork>/src/games/iw8/assets/replay_ies.cpp   which strip segment holds which profile, and its LightDef names
  <fork>/src/games/iw8/assets/replay_ies_data.inc  the level-0..7 mip chains (255 texels per profile)
Writes data/asset_catalog/iw8_ies_profiles.tsv in this repository.

The fork holds normalized 128-texel R8 curves, not candela tables, so the catalog records texel metadata
(peak, half-peak texel, zero texel) and not photometric values.

Usage: py -3 -I tools/build_ies_catalog.py [--fork E:\\mw124\\x64-zt] [--out <path>]
"""
import argparse
import os
import re
import sys

TEXELS = 128  # level-0 texels of one segment (replay_ies.h: one 128-texel segment per profile)
LEVELS = 8    # level 0 .. 7: 128, 64, ... 1 texels (255 values per profile)
ASSETS = os.path.join("src", "games", "iw8", "assets")


def fail(message):
    print("build_ies_catalog: " + message, file=sys.stderr)
    sys.exit(1)


def read_text(path):
    try:
        with open(path, encoding="utf-8") as handle:
            return handle.read()
    except OSError as error:
        fail("cannot read %s: %s" % (path, error))


def line_of(text, offset):
    return text.count("\n", 0, offset) + 1


def parse_arrays(inc_text):
    """Map array name -> list of byte values for every constexpr std::array<uint8_t, N> in the .inc."""
    arrays = {}
    pattern = re.compile(r"constexpr\s+std::array<uint8_t,\s*(\d+)>\s+(\w+)\s*\{\{(.*?)\}\};", re.S)
    for match in pattern.finditer(inc_text):
        size = int(match.group(1))
        values = [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})", match.group(3))]
        if len(values) != size:
            fail("%s holds %d values, declared %d" % (match.group(2), len(values), size))
        if size != TEXELS * 2 - 1:
            fail("%s has %d values; expected the 255-value mip chain" % (match.group(2), size))
        arrays[match.group(2)] = values
    if not arrays:
        fail("no uint8_t arrays found in the .inc")
    return arrays


def parse_cpp(cpp_text):
    """Return (segment -> (array name, line)), (segment -> (lightdef, profile, line)), white fill line."""
    segment_arrays = {}
    for match in re.finditer(r"profiles\[(\d+)\]\s*=\s*(\w+)\.data\(\);", cpp_text):
        segment_arrays[int(match.group(1))] = (match.group(2), line_of(cpp_text, match.start()))
    lightdefs = {}
    pattern = re.compile(r'RegisterLightDef\(writer,\s*"([^"]+)",\s*"([^"]+)",\s*(\d+),\s*width\)')
    for match in pattern.finditer(cpp_text):
        segment = int(match.group(3))
        lightdefs[segment] = (match.group(1), match.group(2), line_of(cpp_text, match.start()))
    white = re.search(r"if \(segment == 0\)", cpp_text)
    if not white:
        fail("replay_ies.cpp: white segment fill not found")
    return segment_arrays, lightdefs, line_of(cpp_text, white.start())


def shape_of(values):
    """Peak, first texel at or below half the peak, and first texel from which every later texel is zero."""
    level0 = values[:TEXELS]
    peak = max(level0)
    half = next((j for j, v in enumerate(level0) if v * 2 <= peak), None)
    zero_from = None
    for j in range(TEXELS):
        if all(v == 0 for v in level0[j:]):
            zero_from = j
            break
    return peak, half, zero_from


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--fork", default=r"E:\mw124\x64-zt", help="x64-zt fork root (read only)")
    parser.add_argument("--out", default=None, help="output TSV (default: data/asset_catalog/iw8_ies_profiles.tsv)")
    args = parser.parse_args()

    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out_path = args.out or os.path.join(repo, "data", "asset_catalog", "iw8_ies_profiles.tsv")
    assets = os.path.join(args.fork, ASSETS)

    header_path = os.path.join(assets, "replay_ies.h")
    cpp_path = os.path.join(assets, "replay_ies.cpp")
    inc_path = os.path.join(assets, "replay_ies_data.inc")
    header_text = read_text(header_path)
    cpp_text = read_text(cpp_path)
    inc_text = read_text(inc_path)

    lookup = re.search(r'kLookupName\s*=\s*"([^"]+)"', header_text)
    if not lookup:
        fail("kLookupName not found in replay_ies.h")
    arrays = parse_arrays(inc_text)
    segment_arrays, lightdefs, white_line = parse_cpp(cpp_text)

    rows = []
    peak0 = 0xFF
    rows.append({
        "index": 0, "name": "white", "lightdef": "", "array": "", "peak": peak0, "half": None, "zero": None,
        "source": "replay_ies.cpp:%d" % white_line,
        "shape": "flat 255 (no profile; lights without one sample segment 0)",
    })
    for segment in sorted(segment_arrays):
        if segment == 0:
            fail("replay_ies.cpp assigns a profile to segment 0")
        array_name, array_line = segment_arrays[segment]
        if array_name not in arrays:
            fail("array %s used by replay_ies.cpp is missing from the .inc" % array_name)
        if segment not in lightdefs:
            fail("segment %d has no RegisterLightDef call" % segment)
        lightdef, profile, lightdef_line = lightdefs[segment]
        peak, half, zero = shape_of(arrays[array_name])
        half_text = "none" if half is None else "half at texel %d (coord %.3f)" % (half, half / 255.0)
        zero_text = "zero from texel %d" % zero if zero is not None else "never zero"
        rows.append({
            "index": segment, "name": profile, "lightdef": lightdef, "array": array_name, "peak": peak,
            "half": half, "zero": zero,
            "source": "replay_ies.cpp:%d,%d; replay_ies_data.inc" % (lightdef_line, array_line),
            "shape": "peak 0x%02X at texel 0; %s; %s" % (peak, half_text, zero_text),
        })

    lookup_name = lookup.group(1)
    lines = [
        "\t".join(["index", "name", "lightdef", "array", "texels", "levels", "peak", "half_texel", "zero_texel",
                   "lookup_strip", "source", "shape"]),
    ]
    for row in rows:
        lines.append("\t".join([
            str(row["index"]), row["name"], row["lightdef"], row["array"], str(TEXELS), str(LEVELS),
            "%d" % row["peak"],
            "" if row["half"] is None else str(row["half"]),
            "" if row["zero"] is None else str(row["zero"]),
            lookup_name, row["source"], row["shape"],
        ]))

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines) + "\n")
    print("wrote %d rows (%d profiles plus white) to %s" % (len(rows), len(rows) - 1, out_path))


if __name__ == "__main__":
    main()
