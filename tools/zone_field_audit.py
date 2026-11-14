"""Stock parity audit of one asset of a built zone (research 124).

Compares every scalar field, array count and null pointer of an ACTS asset export of a built zone with the same
field in the stock zones the editor has cached, and prints the fields where the built zone falls outside what
every stock zone does:
  - a field that is the same in every stock zone and different in the built one;
  - a count or value that is 0 in the built zone and never 0 in stock;
  - a pointer that is null in the built zone and never null in stock (and the reverse);
  - a number further outside the stock range than half that range.
A flag is a question, not a fault: research 124 lists the answers for the world asset (gfx_map) and the light
asset (com_map). The occlusion box (research 121) and the probe level (research 123) were both of this kind.

Usage: py -3 -I tools/zone_field_audit.py <built asset .json> <kind>
  kind: gfx_map (stock: world_cache/<zone>/assets/gfx_map/**) or com_map (world_cache/<zone>/com_map/**).
Prints one line per flag and "<n> flagged of <m> fields, <k> stock zones"."""
import glob, json, os, sys  # noqa: E401

built_path, kind = sys.argv[1], sys.argv[2]
cache = os.path.join(os.environ.get('LOCALAPPDATA', ''), 'IW8 Radiant Suite', 'IW8 Radiant Suite', 'world_cache')
SKIP = ('address', 'type', 'extent_evidence', 'stride')


def fields_of(path):
    return json.load(open(path, encoding='utf-8'))['asset']['fields']


def flat(o, path, out):
    if isinstance(o, dict):
        if 'count' in o and 'values' in o:
            out[path + '#count'] = o['count']
            return
        if '$union_views' in o and 'v' in o:
            v = o['v']
            if isinstance(v, list) and all(isinstance(x, (int, float)) for x in v):
                for i, x in enumerate(v):
                    out['%s[%d]' % (path, i)] = x
            return
        for k, v in o.items():
            if k not in SKIP:
                flat(v, path + '/' + k, out)
    elif isinstance(o, list):
        if len(o) <= 16 and all(isinstance(x, (int, float)) and not isinstance(x, bool) for x in o):
            for i, x in enumerate(o):
                out['%s[%d]' % (path, i)] = x
        else:
            out[path + '#len'] = len(o)
    elif o is None:
        out[path] = None
    elif isinstance(o, bool):
        out[path] = int(o)
    elif isinstance(o, (int, float)):
        out[path] = o


def stock_exports():
    found = []
    for zone in sorted(os.listdir(cache)) if os.path.isdir(cache) else []:
        base = os.path.join(cache, zone)
        if kind == 'gfx_map':
            hits = glob.glob(os.path.join(base, 'assets', 'gfx_map', '**', '*.asset.json'), recursive=True)
        else:
            hits = glob.glob(os.path.join(base, kind, '**', 'assets', kind, '**', '*.asset.json'), recursive=True)
        # Multiplayer maps only: the Warzone zones are another shape (streamed tiles).
        if hits and zone.startswith('mp_') and not zone.startswith(('mp_br', 'mp_don', 'mp_front')):
            found.append((zone, sorted(hits, key=os.path.getsize)[-1]))
    return found


built = {}
flat(fields_of(built_path), '', built)
ranges = {}
zones = stock_exports()
for _zone, path in zones:
    try:
        d = {}
        flat(fields_of(path), '', d)
    except (ValueError, KeyError, OSError):
        continue
    for k, v in d.items():
        ranges.setdefault(k, []).append(v)
n = max((len(v) for v in ranges.values()), default=0)
flags = []
for k, vals in sorted(ranges.items()):
    if len(vals) < n:
        continue
    if k not in built:
        if not all(v is None for v in vals):
            flags.append((k, 'absent in the built zone; stock %s' % str(vals[:3])[:50]))
        continue
    b = built[k]
    nums = [v for v in vals if isinstance(v, (int, float))]
    if all(v is None for v in vals):
        if b is not None:
            flags.append((k, 'set in the built zone, null in every stock zone'))
        continue
    if b is None:
        if not any(v is None for v in vals):
            flags.append((k, 'null in the built zone, never in stock'))
        continue
    if len(nums) != len(vals) or not isinstance(b, (int, float)):
        continue
    lo, hi = min(nums), max(nums)
    if lo == hi:
        if b != lo:
            flags.append((k, 'stock always %g, built %g' % (lo, b)))
    elif b == 0 and (lo > 0 or hi < 0):
        flags.append((k, 'built 0, stock %g .. %g' % (lo, hi)))
    elif '#' not in k and (b < lo - 0.5 * (hi - lo) or b > hi + 0.5 * (hi - lo)):
        flags.append((k, 'built %g, stock %g .. %g' % (b, lo, hi)))
for k, what in flags:
    print('%-64s %s' % (k[:64], what))
print('%d flagged of %d fields, %d stock zones' % (len(flags), len(ranges), n))
