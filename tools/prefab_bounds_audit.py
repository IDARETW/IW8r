"""Audit the bundled prefab library (data/prefabs/iw8r) against the real model bounds.

For every prefab .map: the bounds of its brushes (from their plane points) and of its placed models (the model cache's
bounds, turned by the yaw), the size that gives against the size listed in index.tsv, and models that float (their
bottom more than 2 units above every surface under them and above the ground). Read-only; prints a report.

usage: py -3 tools/prefab_bounds_audit.py [--model-index <model_cache/index.json>] [--fix-index]
  --fix-index     rewrites the size column of index.tsv with the measured sizes (the README is not touched).
  --fix-floating  drops each floating model (not in windows/ or lamps/, which are wall-mounted by design) onto the top
                  of what is under its footprint, else onto the prefab's ground, by rewriting its origin line.
"""
import argparse
import json
import math
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
LIB = os.path.join(ROOT, 'data', 'prefabs', 'iw8r')


def default_model_index():
    base = os.environ.get('LOCALAPPDATA', '')
    return os.path.join(base, 'IW8 Radiant', 'IW8 Radiant', 'model_cache', 'index.json')


def parse_map(text):
    """Entities as (keyvalues dict, list of brushes; a brush = list of points)."""
    ents = []
    depth = 0
    cur = None
    brush = None
    for line in text.splitlines():
        s = line.strip()
        if s.startswith('//') or not s:
            continue
        if s == '{':
            depth += 1
            if depth == 1:
                cur = ({}, [])
            elif depth == 2:
                brush = []
            continue
        if s == '}':
            if depth == 2 and brush is not None:
                cur[1].append(brush)
                brush = None
            elif depth == 1:
                ents.append(cur)
            depth -= 1
            continue
        if depth == 1:
            m = re.match(r'"([^"]*)"\s+"([^"]*)"', s)
            if m:
                cur[0][m.group(1)] = m.group(2)
        elif depth >= 2 and brush is not None:
            for x, y, z in re.findall(r'\(\s*(-?[\d.eE+-]+)\s+(-?[\d.eE+-]+)\s+(-?[\d.eE+-]+)\s*\)', s):
                brush.append((float(x), float(y), float(z)))
            m = re.match(r'v\s+(-?[\d.]+)\s+(-?[\d.]+)\s+(-?[\d.]+)', s)
            if m:
                brush.append(tuple(float(v) for v in m.groups()))
    return ents


def model_box(bounds, origin, yaw):
    """The world box of a model's bounds turned by yaw (degrees) about z and moved to origin."""
    x0, y0, z0, x1, y1, z1 = bounds
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    xs, ys = [], []
    for x in (x0, x1):
        for y in (y0, y1):
            xs.append(origin[0] + c * x - s * y)
            ys.append(origin[1] + s * x + c * y)
    return [min(xs), min(ys), origin[2] + z0, max(xs), max(ys), origin[2] + z1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--model-index', default=default_model_index())
    ap.add_argument('--fix-index', action='store_true')
    ap.add_argument('--fix-floating', action='store_true')
    a = ap.parse_args()
    models = json.load(open(a.model_index, encoding='utf-8'))['models']
    index_path = os.path.join(LIB, 'index.tsv')
    rows = [l.rstrip('\n').split('\t') for l in open(index_path, encoding='utf-8')]
    head, body = rows[0], rows[1:]
    size_col = head.index('size')
    checked = mismatched = floating_total = unknown = 0
    report = []
    for r in body:
        path = os.path.join(LIB, r[0].replace('/', os.sep))
        if not os.path.isfile(path):
            continue
        ents = parse_map(open(path, encoding='utf-8', errors='replace').read())
        parts = []  # (kind, name, box)
        missing = False
        for kv, brushes in ents:
            cls = kv.get('classname', '')
            for b in brushes:
                if b:
                    xs, ys, zs = zip(*b)
                    parts.append(('brush', cls, [min(xs), min(ys), min(zs), max(xs), max(ys), max(zs)], None))
            if cls in ('misc_model', 'script_model') and kv.get('model'):
                m = models.get(kv['model'])
                if not m or m.get('failed') or not m.get('bounds'):
                    missing = True
                    continue
                o = [float(v) for v in kv.get('origin', '0 0 0').split()]
                ang = [float(v) for v in kv.get('angles', '0 0 0').split()] if kv.get('angles') else [0, 0, 0]
                parts.append(('model', kv['model'], model_box(m['bounds'], o, ang[1] if len(ang) > 1 else 0), kv.get('origin', '0 0 0')))
        if not parts:
            continue
        checked += 1
        if missing:
            unknown += 1
        box = [min(p[2][0] for p in parts), min(p[2][1] for p in parts), min(p[2][2] for p in parts),
               max(p[2][3] for p in parts), max(p[2][4] for p in parts), max(p[2][5] for p in parts)]
        size = '%dx%dx%d' % tuple(round(box[3 + k] - box[k]) for k in range(3))
        listed = r[size_col] if size_col < len(r) else ''
        notes = []
        if listed and not missing:
            try:
                lx, ly, lz = (float(v) for v in listed.split('x'))
                mx, my, mz = (box[3 + k] - box[k] for k in range(3))
                if max(abs(lx - mx), abs(ly - my), abs(lz - mz)) > max(16, 0.25 * max(mx, my, mz)):
                    notes.append('listed %s, measured %s' % (listed, size))
                    mismatched += 1
            except ValueError:
                pass
        ground = box[2]
        wall_mounted = r[0].startswith('windows/') or r[0].startswith('lamps/')
        text_new = None
        for kind, name, b, origin in parts:
            if kind != 'model' or b[2] - ground < 2:
                continue
            under = False
            support = ground
            for kind2, name2, b2, _ in parts:
                if b2 is b:
                    continue
                overlap = b2[0] < b[3] and b2[3] > b[0] and b2[1] < b[4] and b2[4] > b[1]
                # Supported: a part below whose top reaches the model's bottom (within 6 units) or passes it (posts
                # set into a deck).
                if overlap and b2[2] <= b[2] + 2 and b2[5] >= b[2] - 6:
                    under = True
                    break
                if overlap and b2[5] <= b[2] + 2:
                    support = max(support, b2[5])
            if not under:
                notes.append('%s floats %.0f units above the ground with nothing under it' % (name, b[2] - ground))
                floating_total += 1
                if a.fix_floating and not wall_mounted:
                    if text_new is None:
                        text_new = open(path, encoding='utf-8', errors='replace').read()
                    o = [float(v) for v in origin.split()]
                    o[2] -= b[2] - support
                    old = '"model" "%s"\n"origin" "%s"' % (name, origin)
                    new = '"model" "%s"\n"origin" "%s"' % (name, ' '.join(('%g' % v) for v in o))
                    if old in text_new:
                        text_new = text_new.replace(old, new, 1)
                        b[2], b[5] = b[2] - (b[2] - support), b[5] - (b[2] - support)
                        notes[-1] += ' (dropped to %g)' % support
        if text_new is not None:
            with open(path, 'w', encoding='utf-8', newline='\n') as f:
                f.write(text_new)
        if notes:
            report.append('%s: %s' % (r[0], '; '.join(notes)))
        if a.fix_index and not missing:
            while len(r) <= size_col:
                r.append('')
            r[size_col] = size
    print('prefab bounds audit: %d prefabs measured (%d with a model not in the cache), %d size mismatches, %d floating models'
          % (checked, unknown, mismatched, floating_total))
    for line in report:
        print('  ' + line)
    if a.fix_index:
        with open(index_path, 'w', encoding='utf-8', newline='\n') as f:
            for r in rows:
                f.write('\t'.join(r) + '\n')
        print('index.tsv sizes rewritten')
    return 0


if __name__ == '__main__':
    sys.exit(main())
