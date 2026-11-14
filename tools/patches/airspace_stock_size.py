import io

# Editor: the airspace of a built map is as large as a stock map's (research 121). Every stock tome read reaches
# at least 131072 units from the origin on every axis; the default was 6144 up and 2048 around, less than the
# Chopper Gunner's own distance (about 9000). The cell now also reaches below the map by the "out" amount.

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    write(p, s.replace(a, b))


patch('src/core/compile_iw8.cpp', '''    // The one visibility cell reaches above and around the map (research 75, owner playtest 2026-11-11: the UAV model was
    // invisible and players were invisible from the Chopper Gunner; both are outside a cell that ends at the map's
    // geometry). worldspawn iw8r_airspace "<up> [out]" sets how far (default 6144 up, 2048 out; "0" keeps the tight
    // cell). The world bounds, the light grid and the surface tree keep the map's own size.
    AABB cellBounds = bounds;
    {
        double up = 6144, outward = 2048;
''', '''    // The one visibility cell reaches above, around and below the map (research 75 and 121, owner playtest
    // 2026-11-11: the UAV model was invisible and players were invisible from the Chopper Gunner). The converter makes
    // the occlusion data cover this cell; the game fails the camera query for a camera outside that data and then
    // hides every player and vehicle (verified in the Replay exe, research 121). worldspawn iw8r_airspace
    // "<up> [out]" sets how far; the default is the reach of every stock map's occlusion data (131072 units); "0"
    // keeps the tight cell. The world bounds, the light grid and the surface tree keep the map's own size.
    AABB cellBounds = bounds;
    {
        double up = kAirspaceDefault, outward = kAirspaceDefault;
''')
patch('src/core/compile_iw8.cpp', '''                    outward = got >= 2 && b2 >= 0 ? b2 : (a2 > 0 ? 2048 : 0);
                } else {
                    warn.push_back("worldspawn iw8r_airspace '" + *as + "' not understood (<up> [out]); using 6144 2048");
                }
            }
        cellBounds.maxs.z += up;
        cellBounds.mins.x -= outward;
        cellBounds.mins.y -= outward;
''', '''                    outward = got >= 2 && b2 >= 0 ? b2 : (a2 > 0 ? kAirspaceDefault : 0);
                    // Past this the game's 16-bit cell coordinates get coarser than any stock map's.
                    if (up > kAirspaceMax || outward > kAirspaceMax) {
                        warn.push_back("worldspawn iw8r_airspace '" + *as + "' is larger than 524288; 524288 is used");
                        up = std::min(up, kAirspaceMax);
                        outward = std::min(outward, kAirspaceMax);
                    }
                } else {
                    warn.push_back("worldspawn iw8r_airspace '" + *as + "' not understood (<up> [out]); using 131072 131072");
                }
            }
        cellBounds.maxs.z += up;
        cellBounds.mins.z -= outward;
        cellBounds.mins.x -= outward;
        cellBounds.mins.y -= outward;
''')
s = read('src/core/compile_iw8.cpp')
nl = '\r\n' if '\r\n' in s else '\n'
anchor = 'bool compileIw8Extraction('
at = s.index(anchor)
# The constants go before the first function that uses them: find the start of that function's line block.
line_start = s.rfind(nl, 0, at) + len(nl)
consts = ('// The airspace of a built map (worldspawn iw8r_airspace): how far the visibility cell and the occlusion data reach' + nl +
          '// past the geometry. Every stock tome read reaches at least 131072 units from the origin (research 121).' + nl +
          'constexpr double kAirspaceDefault = 131072.0;' + nl +
          'constexpr double kAirspaceMax = 524288.0;' + nl + nl)
assert 'kAirspaceDefault = ' not in s
s = s[:line_start] + consts + s[line_start:]
write('src/core/compile_iw8.cpp', s)

o = read('data/iw8_entity_defs_overrides.json')
assert o.count('"6144 2048"') == 2
o = o.replace('"default": "6144 2048"', '"default": "131072 131072"').replace('"6144 2048",', '"131072 131072",\n            "16384 8192",'.replace('\n', '\r\n' if '\r\n' in o else '\n'))
old = ('How far the map\'s visibility cell reaches above (first number) and around (second number) the map\'s geometry, in units. '
       'Aircraft killstreaks and their cameras fly there; with a cell that ends at the geometry they were not drawn. \\"0\\" keeps the tight cell.')
new = ('How far the map\'s visibility cell and occlusion data reach above (first number) and around and below (second number) the map\'s geometry, in units. '
       'Aircraft killstreaks and their cameras fly there; from a camera outside it the game draws no players or vehicles. '
       'The default is the reach of a stock map\'s occlusion data. \\"0\\" keeps the tight cell.')
assert o.count(old) == 1, o.count(old)
write('data/iw8_entity_defs_overrides.json', o.replace(old, new))
print('ok')
