import io, sys
root = sys.argv[1]
p = root + '/src/convert/iw3_iw8/convert.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = '''                    base, base, map.c_str(), cx, compassBounds->northwestX - cx, cy,
                    compassBounds->northwestY - cy, cx, compassBounds->southeastX - cx, cy,
                    compassBounds->southeastY - cy, map.c_str());
                generated += line;
'''.replace('\n', nl)
assert s.count(a) == 1
b = a + '''                // Stock map scripts call scripts\\mp\\compass::setupminimap, which besides setminimap leaves the map's
                // size and its two corner entities in level.mapsize / level.mapcorners; the airstrike plane and the
                // support helicopter read them (plane.gsc, chopper_support.gsc). The same values for the region set
                // above (IW8 Radiant research 75, section 4.1): the size is measured along north, each corner faces
                // the other turned by 45 degrees.
                std::snprintf(
                    line, sizeof(line),
                    "    corners = getentarray( \\"minimap_corner\\", \\"targetname\\" );\\n"
                    "    if ( corners.size == 2 )\\n"
                    "    {\\n"
                    "        a = ( %.3f + %.3f * k, %.3f + %.3f * k, 0 );\\n"
                    "        b = ( %.3f + %.3f * k, %.3f + %.3f * k, 0 );\\n"
                    "        north = ( cos( getnorthyaw() ), sin( getnorthyaw() ), 0 );\\n"
                    "        size = vectordot( a - b, north );\\n"
                    "        if ( size < 0 )\\n"
                    "            size = 0 - size;\\n"
                    "        corners[0].origin = a;\\n"
                    "        corners[1].origin = b;\\n"
                    "        corners[0].angles = vectortoangles( b - a ) + ( 0, 45, 0 );\\n"
                    "        corners[1].angles = vectortoangles( a - b ) + ( 0, 45, 0 );\\n"
                    "        level.mapsize = size;\\n"
                    "        level.mapcorners = corners;\\n"
                    "    }\\n",
                    cx, compassBounds->northwestX - cx, cy, compassBounds->northwestY - cy, cx,
                    compassBounds->southeastX - cx, cy, compassBounds->southeastY - cy);
                generated += line;
'''.replace('\n', nl)
s = s.replace(a, b)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
