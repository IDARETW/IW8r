"""Build the stock map catalog for the IW8 Radiant Suite (roadmap 11.1).

Scans the game install by file name only (os.walk + stat, no file is opened) for fastfiles (.ff) and
streaming packs (.xpak). Related variants are grouped under one map id:
  prefixes  techsets_, ww_, eng_, srv_     (e.g. srv_mp_shipment.ff, techsets_mp_shipment.xpak)
  suffixes  _tr, _cg, _load                (e.g. mp_shipment_tr.ff)
A map id is accepted when its files include a main <id>.ff or an .xpak named for the id. The main .ff
and the other files come from the install's zone/ folder when the id is there; otherwise from another
copy in the install (the copy is named in the notes). Ids without an mp/cp/br prefix (kind other) are
accepted only when a pack named for the zone exists and the name is not an engine family (common, global,
ui, code_, dw_, iw8_, mw120r_, ...), so the shared engine zones are skipped.

Output: data/asset_catalog/iw8_stock_maps.tsv
  map_id, kind, main_ff, companion_ffs, xpaks, total_bytes, can_open_in_editor, notes  (tab separated, sorted by map_id)
Usage: py -3 -I tools/build_stock_map_catalog.py [--install E:\\IW8\\Builds\\1.20-replay] [--out data/asset_catalog]
"""
import argparse
import datetime
import os
import re
import sys
import time
from collections import defaultdict

DEFAULT_INSTALL = r'E:\IW8\Builds\1.20-replay'
PREFIX_VARIANTS = ('techsets_', 'ww_', 'eng_', 'srv_')
SUFFIX_VARIANTS = ('_tr', '_cg', '_load')
MAP_NAME_RE = re.compile(r'^[a-z0-9_]+$')
EXCLUDED_PREFIXES = ('mp_av_',)  # attachment / viewmodel / cosmetic asset packs, not maps
# Engine, common and weapon zones: not maps even when they carry srv_/techsets_ companions.
ENGINE_PREFIXES = ('common', 'global', 'ui', 'code_', 'comms_', 'dw_', 'iw8_', 'mw120r_', 'weapon_', 'vehicle_',
                   'patch_', 'pak_', 'watch_')
VARIANT_SUFFIXES = ('_tac', '_pm', '_am', '_gw')
HEADER_SIZE = 32768  # xpak files in the install are this size (header only); noted, not assumed
KINDS = ('mp', 'cp', 'br')


def location_label(d):
    if d == 'zone':
        return None
    if d == '.':
        return 'install root'
    if d.startswith('mods/'):
        return 'mod folder ' + d
    if d.startswith('.proxy'):
        return 'backup copy under .proxy'
    if d.startswith('replay-compiler'):
        return 'review-build copy'
    return 'copy in ' + d


def parse(name):
    """Return (base, variant, ext) for an .ff/.xpak basename, or None. base is the map-style stem."""
    ext = os.path.splitext(name)[1].lower()
    if ext not in ('.ff', '.xpak'):
        return None
    stem = name[:-len(ext)]
    variant = ''
    for p in PREFIX_VARIANTS:
        if stem.startswith(p) and len(stem) > len(p):
            variant = p
            stem = stem[len(p):]
            break
    for s in SUFFIX_VARIANTS:
        if stem.endswith(s) and len(stem) > len(s):
            variant += s
            stem = stem[:-len(s)]
            break
    return stem, variant, ext


def kind_of(base):
    head = base.split('_', 1)[0]
    return head if head in KINDS else 'other'


def notes_for(mid, kind, group_dir, other_dirs, has_main, xpaks, xpak_sizes):
    notes = []
    if not has_main:
        notes.append('xpak-only: no main .ff' if xpaks else 'no main .ff')
    if xpak_sizes and all(s == HEADER_SIZE for s in xpak_sizes):
        notes.append('xpaks are 32 KB header-size files')
    label = location_label(group_dir)
    if label:
        notes.append(f'taken from {label} (no copy in zone/)')
    if other_dirs:
        counts = {}
        for d in other_dirs:
            lab = location_label(d) or 'zone/'
            counts[lab] = counts.get(lab, 0) + 1
        notes.append('other copies: ' + ', '.join(f'{v} x {k}' for k, v in sorted(counts.items())))
    if kind == 'other':
        notes.append('non-mp/cp/br zone name')
    if mid.startswith('mp_frontend'):
        notes.append('frontend menu scene')
    if mid.startswith('mp_br_'):
        notes.append('battle royale (mp_br_ prefix)')
    if '_tut' in mid or mid.startswith('mp_t_'):
        notes.append('training/tutorial range')
    for s in VARIANT_SUFFIXES:
        if mid.endswith(s):
            notes.append(f'variant suffix {s}')
            break
    return '; '.join(notes)


def main() -> int:
    t0 = time.perf_counter()
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser()
    ap.add_argument('--install', default=DEFAULT_INSTALL)
    ap.add_argument('--out', default=os.path.join(repo, 'data', 'asset_catalog'))
    a = ap.parse_args()
    install = os.path.abspath(a.install)
    if not os.path.isdir(install):
        print('error: install not found: ' + install, file=sys.stderr)
        return 1

    # records[base][group_dir] -> list of (rel_path, size, variant, ext)
    records = defaultdict(lambda: defaultdict(list))
    scanned = 0
    stat_errors = 0
    for root, _dirs, files in os.walk(install):
        for name in files:
            scanned += 1
            p = parse(name)
            if p is None:
                continue
            base, variant, ext = p
            if not MAP_NAME_RE.match(base) or base.startswith(EXCLUDED_PREFIXES):
                continue
            full = os.path.join(root, name)
            try:
                size = os.stat(full).st_size
            except OSError:
                stat_errors += 1
                continue
            rel = os.path.relpath(full, install).replace(os.sep, '/')
            group_dir = os.path.dirname(rel) or '.'
            records[base][group_dir].append((rel, size, variant, ext))
    scan_secs = time.perf_counter() - t0

    rows = []
    skipped = 0
    for base in sorted(records):
        kind = kind_of(base)
        groups = records[base]
        # Prefer zone/ (the stock set); otherwise the copy holding a main .ff, then path order.
        def rank(d):
            has_main = any(e == '.ff' and v == '' for _, _, v, e in groups[d])
            return (d != 'zone', not has_main, d)
        group_dir = min(groups, key=rank)
        entries = list(groups[group_dir])
        if group_dir == 'zone':
            # Localised eng_/ww_ variants and packs live in zone/<language>/ folders; they belong to the zone's set.
            for d in groups:
                if d.startswith('zone/'):
                    entries.extend(groups[d])

        main = sorted(rel for rel, _, v, e in entries
                      if e == '.ff' and v == '' and (os.path.dirname(rel) or '.') == group_dir)
        companions = sorted(rel for rel, _, v, e in entries if e == '.ff' and v != '')
        xpaks = sorted(rel for rel, _, v, e in entries if e == '.xpak')
        has_main = bool(main)
        if not has_main and not xpaks:
            skipped += 1
            continue
        if kind == 'other':
            # Map-zone packaging only: a pack named for the zone itself, outside the engine families.
            named_pack = any(e == '.xpak' and v == '' for _, _, v, e in entries)
            if base.startswith(ENGINE_PREFIXES) or not named_pack or 'zone' not in groups:
                skipped += 1
                continue
        xpak_sizes = [size for rel, size, _, e in entries if e == '.xpak']
        total = sum(size for _, size, _, _ in entries)
        other_dirs = [d for d in groups
                      if d != group_dir and not (group_dir == 'zone' and d.startswith('zone/'))]
        rows.append({
            'map_id': base,
            'kind': kind,
            'main_ff': main[0] if main else '',
            'companion_ffs': ','.join(companions),
            'xpaks': ','.join(xpaks),
            'total_bytes': total,
            'can_open_in_editor': 'yes' if has_main else 'no',
            'notes': notes_for(base, kind, group_dir, other_dirs, has_main, xpaks, xpak_sizes),
        })

    os.makedirs(a.out, exist_ok=True)
    path = os.path.join(a.out, 'iw8_stock_maps.tsv')
    today = datetime.date.today().isoformat()
    with open(path, 'w', encoding='utf-8', newline='\n') as out:
        out.write(f'# Replay 1.20 map ids in {install} (file-name scan, {today}).\n')
        out.write('# Columns: map_id, kind (mp/cp/br from prefix, else other), main_ff (install-relative .ff, empty if none), '
                  'companion_ffs, xpaks (comma lists), total_bytes, can_open_in_editor (yes when main_ff exists: '
                  'File > Open IW8 Stock Map uses ACTS on the .ff), notes\n')
        out.write('# Generated by tools/build_stock_map_catalog.py. Re-run: py -3 -I tools/build_stock_map_catalog.py\n')
        for r in rows:
            out.write('\t'.join(str(r[k]) for k in (
                'map_id', 'kind', 'main_ff', 'companion_ffs', 'xpaks', 'total_bytes', 'can_open_in_editor', 'notes')) + '\n')

    print(f'wrote {path}: {len(rows)} map ids')
    print(f'scanned {scanned} files in {scan_secs:.1f}s (stat errors: {stat_errors}); skipped {skipped} non-map stems')
    print('per kind (rows / openable):')
    for k in ('mp', 'cp', 'br', 'other'):
        sel = [r for r in rows if r['kind'] == k]
        print(f'  {k:6} {len(sel):4} / {sum(1 for r in sel if r["can_open_in_editor"] == "yes")}')
    total_open = sum(1 for r in rows if r['can_open_in_editor'] == 'yes')
    print(f'  total  {len(rows):4} / {total_open}')
    stock_open = [r['map_id'] for r in rows if r['kind'] == 'mp' and r['main_ff'].startswith('zone/')]
    print(f'openable mp from zone/ (stock set): {len(stock_open)}')
    print(f'xpak-only (no main .ff): {len(rows) - total_open}')
    print(f'elapsed {time.perf_counter() - t0:.1f}s')
    return 0


if __name__ == '__main__':
    sys.exit(main())
