"""Offline proof of the masks of a volumetric box on a built map (research 127 part A).

Builds the sample room with one iw8r_volume that has two masks through the editor's headless build: the editor's own
clouds_soft picture along z, scrolling, and a striped picture from the map's volume_masks folder along x with an
offset. Then reads the built world and its images back with ACTS and checks:
  - the build log counts the masks and their pictures; every zone passes the ACTS loader test;
  - the world's volumetric record: one box, masks 0 and 1 of the kind the game draws (opacity map, type 2) with the
    axes, sizes, scrolls and offsets of the keys; masks 2 and 3 unused;
  - each mask names an image that is in the map's own zone, with the picture's size; the striped picture's pixels come
    back as they were drawn (the red channel is what the game multiplies by).
The package the build writes into the editor's output folder is deleted afterwards (this proof's own map).

Usage: py -3 -I tools/volume_mask_build_test.py <work dir> [--editor <IW8Radiant.exe>] [--exe <zonetool.exe>]
Prints PASS / FAIL lines and RESULT ok|fail."""
import glob, json, os, re, shutil, struct, subprocess, sys, zlib  # noqa: E401

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
args = sys.argv[1:]
editor = os.path.join(root, 'bin', 'release', 'IW8Radiant.exe')
converter = ''
for flag in ('--editor', '--exe'):
    if flag in args:
        value = os.path.abspath(args[args.index(flag) + 1])
        del args[args.index(flag):args.index(flag) + 2]
        if flag == '--editor':
            editor = value
        else:
            converter = value
work = os.path.abspath(args[0])
ACTS = r'E:/mw124/atian-cod-tools/build/bin/Release/acts.exe'
REPLAY = r'E:/IW8/Builds/1.20-replay/game_dx12_ship_replay.exe'
OODLE = r'E:/IW8/Builds/1.20-replay/oo2core_7_win64.dll'
MAP_ID = 'mp_iw8r_vmprf'

failures = 0


def check(ok, what):
    global failures
    print(('PASS ' if ok else 'FAIL ') + what, flush=True)
    if not ok:
        failures += 1


shutil.rmtree(work, ignore_errors=True)
os.makedirs(os.path.join(work, 'volume_masks'))
W = H = 8
STRIPES = [[255 if (x // 2) % 2 else 40 for x in range(W)] for _ in range(H)]
raw = b''.join(b'\x00' + bytes(row) for row in STRIPES)


def chunk(t, d):
    return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)


open(os.path.join(work, 'volume_masks', 'stripes.png'), 'wb').write(
    b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 0, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))
src = open(os.path.join(root, 'tests', 'data', 'iw8_sample_room.map'), encoding='utf-8').read()
box = ('{\n"classname" "iw8r_volume"\n"origin" "-120 120 64"\n"mins" "-110 -110 -64"\n"maxs" "110 110 64"\n"density" "0.02"\n'
       '"mask" "clouds_soft 800 800 50 0"\n"mask2" "stripes x 64 32 0 -10 8 4"\n}\n')
map_path = os.path.join(work, 'iw8r_vmprf.map')
open(map_path, 'w', encoding='utf-8', newline='\n').write(src.rstrip() + '\n' + box)
out = os.path.join(work, 'build')
env = dict(os.environ)
if converter:
    env['IW8R_TOOL_ZONETOOL'] = converter
subprocess.run([editor, '--compile', map_path, '--out', out], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)


def text_of(name):
    p = os.path.join(out, name)
    return open(p, encoding='utf-8', errors='replace').read() if os.path.isfile(p) else ''


result, log = text_of('compile-result.txt'), text_of('compile-log.txt')
first = result.splitlines()[0] if result else ''
check(first.startswith('ok map=' + MAP_ID), 'build ok (%s)' % first[:110])
m = re.search(r'package=(.+)$', first)
package = m.group(1).strip() if m else ''
line = next((l for l in (result + '\n' + log).splitlines() if 'volumetric box(es) written' in l), '')
check('with 2 mask(s) of 2 picture(s)' in line, 'build log: ' + (line.strip()[:150] or '-'))
conv = next((l for l in log.splitlines() if 'world volumetrics' in l), '')
check('1 world volumetrics, 2 mask images' in conv, 'converter: ' + (conv.strip()[:100] or '-'))
acts_lines = re.findall(r'ACTS\s+(?:--test\s+)?(\S+?):?\s+exit\s+(-?\d+)', log)
check(bool(acts_lines) and all(code == '0' for _, code in acts_lines), 'ACTS loader test: %s' % ', '.join('%s %s' % a for a in acts_lines))

ff = os.path.join(package, MAP_ID + '.ff') if package else ''
scratch = os.path.join(os.path.splitdrive(work)[0] + os.sep, 't', 'vmk')
shutil.rmtree(scratch, ignore_errors=True)
if ff and os.path.isfile(ff):
    subprocess.run([ACTS, '--noUpdater', 'fastfile', '-r', 'mw19replay', '-g', REPLAY, '--oodle', OODLE, '-a', 'gfx_map,image', '-l', '-o', scratch, ff],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
assets = os.path.join(scratch, 'mw19replay', MAP_ID, 'assets')
worlds = glob.glob(os.path.join(assets, 'gfx_map', '**', '*.asset.json'), recursive=True)
if worlds:
    draw = json.load(open(worlds[0], encoding='utf-8'))['asset']['fields']['draw']['volumetrics']
    boxes = (draw.get('volumetrics') or {}).get('values', [])
    check(draw.get('volumetricCount') == 1 and len(boxes) == 1, 'the world has one volumetric box')
    if boxes:
        b = boxes[0]
        check(abs(b['density'] - 0.02) < 1e-6 and b['obb']['halfSize']['v'] == [110.0, 110.0, 64.0], 'the box: density 0.02, half size 110 110 64')
        masks = b['masks']
        want = [(2, 2, [800.0, 800.0], [0.0, 0.0], [50.0, 0.0]), (2, 0, [64.0, 32.0], [8.0, 4.0], [0.0, -10.0])]
        ok = len(masks) == 4
        for k, (kind, axis, scale, offset, scroll) in enumerate(want):
            mk = masks[k]
            ok = ok and mk['type']['value'] == kind and mk['axis']['value'] == axis and mk['scale']['v'] == scale and \
                mk['offset']['v'] == offset and mk['scroll']['v'] == scroll and bool(mk['image'])
        ok = ok and all(masks[k]['type']['value'] == 0 and masks[k]['image'] is None for k in (2, 3))
        check(ok, 'masks: an opacity map along z (800 x 800, scroll 50 0) and one along x (64 x 32, offset 8 4, scroll 0 -10); two unused')
        names = [masks[k]['image']['name'] for k in (0, 1) if masks[k]['image']]
        files = {n: glob.glob(os.path.join(assets, 'image', *n.split('/')) + '.*.dds') for n in names}
        check(len(names) == 2 and names[0] != names[1] and all(files[n] for n in names),
              'both mask images are in the map\'s zone: ' + ', '.join(n.split('/')[-1] for n in names))
        # The striped picture: 8 x 8, the red channel as drawn (40 and 255 in pairs).
        if len(names) == 2 and files[names[1]]:
            data = open(files[names[1]][0], 'rb').read()
            height, width = struct.unpack_from('<II', data, 12)
            start = 128 + (20 if data[84:88] == b'DX10' else 0)
            pixels = data[start:start + width * height * 4]
            check(width == W and height == H and len(pixels) == W * H * 4, 'the striped picture: %d x %d in the zone' % (width, height))
            if len(pixels) == W * H * 4:
                channels = [[pixels[(y * W + x) * 4 + c] for x in range(W) for y in (0,)] for c in range(3)]
                check(any(ch == STRIPES[0] for ch in channels), 'its stripes come back as drawn (first row %s)' % channels[0])
        if len(names) == 2 and files[names[0]]:
            data = open(files[names[0]][0], 'rb').read()
            height, width = struct.unpack_from('<II', data, 12)
            check(width == 256 and height == 256, 'the editor\'s clouds_soft picture: %d x %d in the zone' % (width, height))
else:
    check(False, 'built world export (ACTS) from ' + (ff or '(no package)'))

shutil.rmtree(scratch, ignore_errors=True)
if package and os.path.basename(package.rstrip('/\\')) == MAP_ID:
    shutil.rmtree(package, ignore_errors=True)
print('RESULT ' + ('ok' if failures == 0 else 'fail'))
sys.exit(0 if failures == 0 else 1)
