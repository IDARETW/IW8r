import io
import json
import subprocess
import sys

# Volumetric boxes with the game's units (research 127 part A): density is extinction per world unit, added to the
# air as it is (the old default 0.2 was about 1000 times a stock box: 256 units of it let e^-51 through); falloff is
# the soft border in world units, at most the smallest half size; height_half (new) is the height above the box's
# bottom where the fog is half as thick (written as height_fade = 6.931 / height_half, the game clamps it to 0..1);
# color and anisotropy do nothing in Replay 1.20 (the vision's volumetricAlbedo / volumetricSunAnisotropy rule).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


C = 'src/core/compile_iw8.cpp'
patch(C, '''    // Volumetric boxes (5.7, research 46): `iw8r_volume` point entities (origin, mins / maxs relative to it, default
    // +-128; angles; density >= 0, default 0.2; falloff > 0, default 64; color; height_fade; anisotropy) become
    // world.json `volumetrics` (iw3_render_assets.cpp 5682, validated by replay_render.cpp 1406-1465). Bad values are
    // reported and the volume left out, so it cannot stop the build. Units and look NOT VERIFIED in game.''',
      '''    // Volumetric boxes (5.7, research 46 and 127 part A): `iw8r_volume` point entities (origin, mins / maxs relative to
    // it, default +-128; angles) become world.json `volumetrics` (iw3_render_assets.cpp 5682, validated by
    // replay_render.cpp 1406-1465). The game's units: density is extinction per world unit, added to the vision's air
    // (default 0.001; stock boxes hold 0.0002 to 0.04); falloff is the soft border in world units (default 32, at most
    // the smallest half size); height_half is the height above the box's bottom where the fog is half as thick
    // (height_fade = 6.931 / height_half, which the game clamps to 0..1; height_fade itself is still read). color and
    // anisotropy are written but Replay 1.20 reads neither (the vision's volumetricAlbedo and volumetricSunAnisotropy
    // apply). A box has no light of its own: the sun, the light grid and lamps flagged iw8r_volumetric light it, by
    // the vision's volumetric*Brightness. Bad values are reported and the volume left out, so it cannot stop the
    // build. Look NOT VERIFIED in game.''')
patch(C, '''        const double density = number("density", 0.2, 0, false), falloff = number("falloff", 64, 0, true);
        const double heightFade = number("height_fade", 0, -1e9, false), anisotropy = number("anisotropy", 0, -1, false);''',
      '''        const double density = number("density", 0.001, 0, false);
        double falloff = number("falloff", 32, 0, true);
        const double smallestHalf = std::min({mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2]}) / 2;
        if (falloff > smallestHalf) {
            warn.push_back(where + ": falloff " + num(falloff) + " is more than its smallest half size; using " + num(smallestHalf));
            falloff = smallestHalf;
        }
        if (density > 0.05)
            warn.push_back(where + ": density " + num(density) + " is very thick (extinction per unit: 256 units of 0.05 let 0.0003 % of the light "
                           "through; stock boxes hold 0.0002 to 0.04, 0.001 is a light haze)");
        double heightFade = std::clamp(number("height_fade", 0, 0, false), 0.0, 1.0);
        if (const std::string* hh = pe->value("height_half"); hh && !hh->empty()) {
            double h = 0;
            if (parseNumber(*hh, h) && std::isfinite(h) && h >= 6.931) heightFade = 6.931 / h;
            else warn.push_back(where + ": height_half '" + *hh + "' must be 6.931 or more (units above the box's bottom where the fog is half as thick); ignored");
        }
        const double anisotropy = number("anisotropy", 0, -1, false);''')
patch('src/qtapp/main_window.cpp', '''{"mins", "-128 -128 -64"}, {"maxs", "128 128 128"}, {"density", "0.2"}, {"falloff", "64"}, {"color", "1 1 1"}},''',
      '''{"mins", "-128 -128 -64"}, {"maxs", "128 128 128"}, {"density", "0.001"}, {"falloff", "32"}},''')
patch('src/qtapp/main_window.cpp', '''            ctx_.documentEdited("Volumetric box (mins / maxs, density, falloff, color); in-game look not verified");''',
      '''            ctx_.documentEdited("Volumetric box: fog inside the box (density per unit, falloff border, height_half); the sun and lamps "
                                "with iw8r_volumetric light it; in-game look not verified");''')
print('source ok')

p = ROOT + 'data/iw8_entity_defs_overrides.json'
text = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in text else '\n'
j = json.loads(text)


def find_named(node, name):
    if isinstance(node, dict):
        if name in node and isinstance(node[name], dict):
            return node[name]
        for v in node.values():
            r = find_named(v, name)
            if r is not None:
                return r
    elif isinstance(node, list):
        for v in node:
            r = find_named(v, name)
            if r is not None:
                return r
    return None


vol = find_named(j, 'iw8r_volume')
assert vol is not None
keymap = None
for k, v in vol.items():
    if isinstance(v, dict) and 'density' in v:
        keymap = v
assert keymap is not None, list(vol.keys())
print('volume keys before:', sorted(keymap.keys()))


def key(typ, default, description, examples=()):
    return {'type': typ, 'default': default, 'description': description, 'required': False, 'examples': list(examples)}


keymap['density'] = key('float', '0.001', 'How thick the fog in the box is: extinction per world unit, added to the map\'s air. 0.0002 is a '
                        'light haze, 0.001 the default (256 units let 77 % through), 0.005 thick; above 0.05 the build warns. '
                        'Stock boxes hold 0.0002 to 0.04. Research 127.', ['0.0005', '0.004'])
keymap['falloff'] = key('float', '32', 'The soft border in world units: the fog is full this far inside each face and fades to nothing '
                        'at the face, with rounded corners. At most the box\'s smallest half size. Stock boxes use 8 to 20.', ['16', '64'])
keymap['height_half'] = key('float', '', 'Height in units above the box\'s bottom (along the box\'s own up axis) where the fog is half as '
                            'thick: a ground mist. 6.931 or more; empty = the same thickness at every height. Written as height_fade.',
                            ['64', '200'])
if 'height_fade' in keymap:
    keymap['height_fade']['description'] = ('The game\'s own height value, 0 to 1 (it clamps): thickness x exp(-0.1 x height_fade x height '
                                            'above the box\'s bottom). height_half is the easier key and wins when both are set.')
for name, field in (('color', 'volumetricAlbedo'), ('anisotropy', 'volumetricSunAnisotropy')):
    if name in keymap:
        keymap[name]['description'] = ('No effect in Replay 1.20: the game\'s volumetric pass does not read a box\'s ' + name +
                                       '. Tint the air with the vision field ' + field + ' (iw8r_vision, or a haze preset).')
io.open(p, 'w', encoding='utf-8', newline='').write(json.dumps(j, indent=2, ensure_ascii=False).replace('\n', nl) + nl)
r = subprocess.run([sys.executable, '-I', ROOT + 'tools/gen_iw8_entity_defs.py'], capture_output=True, text=True, cwd=ROOT)
print(r.stdout[-160:], r.stderr[-300:])
