"""Offline proof of baked lightmaps on an editor map (roadmap 5.11; docs/research/94 and 95).

Builds tests/data/iw8_sample_room.map with worldspawn iw8r_bake_lightmaps 1 and a light through the editor's headless
build (IW8Radiant.exe --compile: bake, extraction, converter, ACTS) and checks:
  - the build log: the bake line, the converter's lightmap layers and authored lightmap, ACTS exit 0 for every zone;
  - the extraction: world.json "lightmaps" (IW3 pair) and "lightmap_encoding", every lit surface on lightmap 0 with
    lightmap UVs inside [0, 1], the sun-visibility primary bytes (lit and shadowed texels), a non-zero ambient layer;
  - the built world (ACTS gfx_map): lightmapCount 1, the packed lightmap size, every world surface on lightmapIndex 0.
The package the build writes into the editor's output folder is deleted afterwards (it is this proof's own map).

Usage: py -3 -I tools/lightmap_bake_build_test.py <work dir> [--editor <IW8Radiant.exe>]
Prints PASS / FAIL lines and RESULT ok|fail."""
import collections, json, os, re, shutil, subprocess, sys  # noqa: E401

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
args = sys.argv[1:]
editor = os.path.join(root, 'bin', 'release', 'IW8Radiant.exe')
if '--editor' in args:
    editor = args[args.index('--editor') + 1]
    del args[args.index('--editor'):args.index('--editor') + 2]
work = os.path.abspath(args[0])
ACTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'bin', 'release', 'iw8r-acts.exe')
REPLAY = r'E:/IW8/Builds/1.20-replay/game_dx12_ship_replay.exe'
OODLE = r'E:/IW8/Builds/1.20-replay/oo2core_7_win64.dll'
MAP_ID = 'mp_iw8r_lmproof'

failures = 0


def check(ok, what):
    global failures
    print(('PASS ' if ok else 'FAIL ') + what, flush=True)
    if not ok:
        failures += 1


shutil.rmtree(work, ignore_errors=True)
os.makedirs(work)
src = open(os.path.join(root, 'tests', 'data', 'iw8_sample_room.map'), encoding='utf-8').read()
src = src.replace('"classname" "worldspawn"', '"classname" "worldspawn"\n"iw8r_bake_lightmaps" "1"', 1)
src += ('// entity light\n{\n"classname" "light"\n"origin" "-160 -160 96"\n"_color" "1 0.6 0.3"\n"intensity" "2"\n'
        '"radius" "300"\n}\n')
map_path = os.path.join(work, 'iw8r_lmproof.map')
open(map_path, 'w', encoding='utf-8', newline='\n').write(src)
out = os.path.join(work, 'build')
subprocess.run([editor, '--compile', map_path, '--out', out], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
result = open(os.path.join(out, 'compile-result.txt'), encoding='utf-8', errors='replace').read() \
    if os.path.isfile(os.path.join(out, 'compile-result.txt')) else ''
log = open(os.path.join(out, 'compile-log.txt'), encoding='utf-8', errors='replace').read() \
    if os.path.isfile(os.path.join(out, 'compile-log.txt')) else ''
first = result.splitlines()[0] if result else ''
check(first.startswith('ok map=' + MAP_ID), 'build ok (%s)' % first[:120])
m = re.search(r'package=(.+)$', first)
package = m.group(1).strip() if m else ''
check('lightmap baked:' in result + log, 'bake line: ' + next((l for l in (result + log).splitlines() if 'lightmap baked' in l), '-')[:160])
check(re.search(r'lightmap layers \d+x\d+', log) is not None, 'converter: lightmap layers as textures')
check('1 authored lightmaps' in log, 'converter: 1 authored lightmap')
acts = re.findall(r'ACTS --test (\S+): exit (\d+)', log)
check(len(acts) >= 3 and all(code == '0' for _, code in acts), 'ACTS: %s' % ', '.join('%s %s' % a for a in acts))

# The extraction.
ext = os.path.join(work, 'iw8build', MAP_ID, 'extraction', 'extracted')
world_path = os.path.join(ext, 'maps', 'mp', MAP_ID + '.d3dbsp.replay-world.json')
if os.path.isfile(world_path):
    w = json.load(open(world_path, encoding='utf-8'))
    lm = w.get('lightmaps', [])
    check(len(lm) == 1 and lm[0][0]['format'] == 50 and lm[0][1]['format'] == 21 and
          lm[0][1]['height'] == 2 * lm[0][0]['height'], 'world.json lightmaps: one IW3 pair %s' %
          (json.dumps(lm)[:160] if lm else '-'))
    enc = w.get('lightmap_encoding', {})
    check(enc.get('constant_ambient') is True and enc.get('log_scale', 0) > 0 and enc.get('maximum', 0) > 0,
          'lightmap_encoding %s' % json.dumps(enc))
    surfs = w.get('surfaces', [])
    lit = [s for s in surfs if s.get('lightmap') == 0]
    check(len(lit) > 0, '%d of %d surfaces on lightmap 0' % (len(lit), len(surfs)))
    uv_ok = all(0 <= v['lightmap_uv'][0] <= 1 and 0 <= v['lightmap_uv'][1] <= 1 for s in lit for v in s['vertices'])
    distinct = len({tuple(v['lightmap_uv']) for s in lit for v in s['vertices']})
    check(uv_ok and distinct > 8, 'lightmap UVs in [0, 1] (%d distinct)' % distinct)
    if lm:
        W, H = lm[0][0]['width'], lm[0][0]['height']
        primary = open(os.path.join(ext, lm[0][0]['file']), 'rb').read()
        secondary = open(os.path.join(ext, lm[0][1]['file']), 'rb').read()
        check(len(primary) == W * H and len(secondary) == W * H * 8, 'pair sizes %d / %d' % (len(primary), len(secondary)))
        counts = collections.Counter(primary)
        check(counts[255] > 0 and counts[0] > 0, 'sun visibility: %d lit, %d shadowed texels' % (counts[255], counts[0]))
        ambient = max(max(secondary[i * 4:i * 4 + 3]) for i in range(W * H))
        check(ambient > 0, 'ambient layer not black (max byte %d)' % ambient)
else:
    check(False, 'extraction world.json at ' + world_path)

# The built world.
ff = os.path.join(package, MAP_ID + '.ff') if package else ''
if ff and os.path.isfile(ff):
    acts_out = os.path.join(work, 'acts')
    subprocess.run([ACTS, '--noUpdater', 'fastfile', '-r', 'mw19replay', '-g', REPLAY, '--oodle', OODLE, '-a', 'gfx_map',
                    '-l', '-o', acts_out, ff], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    gdir = os.path.join(acts_out, 'mw19replay', MAP_ID, 'assets', 'gfx_map', 'maps', 'mp')
    gfiles = sorted(f for f in os.listdir(gdir) if f.endswith('.asset.json')) if os.path.isdir(gdir) else []
    gpath = os.path.join(gdir, gfiles[0]) if gfiles else ''
    if gpath:
        g = json.load(open(gpath, encoding='utf-8'))
        draw = g['asset']['fields']['draw']
        packed = draw['lightmapReindexData']['packedLightmap']['values'][0]
        check(draw['lightmapCount'] == 1 and draw['lightmapType']['value'] == 1,
              'gfx_map: lightmapCount %s, type %s' % (draw['lightmapCount'], draw['lightmapType']))
        check(packed['imageWidth'] > 0 and packed['imageHeight'] > 0, 'gfx_map packed lightmap %s' % json.dumps(packed))
        surfaces = g['asset']['fields']['surfaces']['surfaces']['values']
        idx = collections.Counter(s['lightmapIndex'] for s in surfaces)
        check(set(idx) == {0}, 'gfx_map surfaces lightmapIndex %s' % dict(idx))
    else:
        check(False, 'ACTS gfx_map export')
else:
    check(False, 'built zone ' + ff)

# Parity (plan 1.11): the planes the game will sample, read from the decompressed built zone by the editor's stock
# lightmap reader, against the planes the editor decodes from its own bake (the extraction's pair), texel by texel.
tests = os.path.join(root, 'bin', 'release', 'iw8r_tests.exe')
if ff and os.path.isfile(ff) and os.path.isfile(world_path) and os.path.isfile(tests):
    dec_out = os.path.join(work, 'dec')
    os.makedirs(dec_out, exist_ok=True)
    subprocess.run([ACTS, '--noUpdater', 'fastfile', '-r', 'mw19replay', '-g', REPLAY, '--oodle', OODLE, '-d', '--noAssetDump',
                    '-l', '-o', dec_out, ff], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=dec_out)
    decs = [os.path.join(b, f) for b, _d, fs_ in os.walk(dec_out) for f in fs_ if f.endswith('.ff.dec')]
    w = json.load(open(world_path, encoding='utf-8'))
    lm = w.get('lightmaps', [])
    enc = w.get('lightmap_encoding', {})
    if decs and lm:
        env = dict(os.environ, IW8R_TEST_BUILT_DEC=decs[0],
                   IW8R_TEST_PAIR_PRIMARY=os.path.join(ext, lm[0][0]['file']), IW8R_TEST_PAIR_SECONDARY=os.path.join(ext, lm[0][1]['file']),
                   IW8R_TEST_PAIR_W=str(lm[0][0]['width']), IW8R_TEST_PAIR_H=str(lm[0][0]['height']),
                   IW8R_TEST_PAIR_LOGSCALE=repr(enc.get('log_scale', 0)), IW8R_TEST_PAIR_MAXIMUM=repr(enc.get('maximum', 0)))
        run = subprocess.run([tests], env=env, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        text = run.stdout.decode('utf-8', 'replace')
        line = next((l.strip() for l in text.splitlines() if 'built lightmap parity:' in l), '')
        size = next((l.strip() for l in text.splitlines() if 'built lightmap:' in l), '')
        check('[  OK  ] built_lightmap_matches_the_bake' in text and line != '', 'built zone against the bake: ' + (line or size or '-')[:330])
    else:
        check(False, 'decompressed built zone (%d file(s)) and the extraction pair' % len(decs))

if package and os.path.basename(package.rstrip('/\\')) == MAP_ID:
    shutil.rmtree(package, ignore_errors=True)
print('RESULT %s (%d failure(s))' % ('ok' if not failures else 'fail', failures))
sys.exit(1 if failures else 0)
