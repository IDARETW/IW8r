import io

# The minimap was a quarter turn off in game (owner, 2026-11-14 evening). The picture was always drawn for a map whose
# north is +X (the frame the converter turns by 90 - northyaw), while the build writes northyaw 90 since 2026-11-11, so
# the converter turned it by 0 and the game, which shows the picture with +Y up, had +X up. The picture is now drawn
# with the map's north up, whatever quarter that is.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


H = 'src/core/minimap.h'
patch(H, '''// The image is what x64-zt's converter reads as images/compass_map_<id>.dds (research: docs/research/27_minimap.md).
// Frame: the converter's SOURCE frame, the one it turns by 90 - northyaw degrees clockwise (convert.cpp:2264-2281).
// The Radiant .ents has no northyaw, so the converter turns the image 90 degrees clockwise. For that reason:
//   - row 0 (top) is world +X (north of the source), and the bottom row is -X;
//   - column 0 (left) is world +Y, and the right column is -Y.
// After the converter's clockwise turn, the in-game minimap has +X on the right and +Y at the top.
//''', '''// The image is what x64-zt's converter reads as images/compass_map_<id>.dds (research: docs/research/27_minimap.md).
// Frame: the game shows the picture with world +Y at the top and +X on the right. The converter takes a picture
// drawn with the map's north (worldspawn northyaw) at the top and turns it clockwise by 90 - northyaw degrees
// (convert.cpp, "compass: rotating the minimap"), which gives the game's frame for every quarter turn. So the picture
// is drawn north up, for the northyaw the build writes (MinimapImage::northQuarter):
//   quarter 0 (northyaw 0, north +X):   row 0 is +X, column 0 is +Y   (the converter turns it by 90)
//   quarter 1 (northyaw 90, north +Y):  row 0 is +Y, column 0 is -X   (not turned: the build's default)
//   quarter 2 (northyaw 180, north -X): row 0 is -X, column 0 is -Y
//   quarter 3 (northyaw 270, north -Y): row 0 is -Y, column 0 is +X
// (Until 2026-11-14 it was always drawn as quarter 0 while the build wrote northyaw 90: a quarter turn off in game.)
//''')
patch(H, '''// its centre to the longer span as convert.cpp:2226-2245 does. Without them it is the drawn geometry's XY box,
// squared about its centre. Pixel (px, py) covers world
//   x = worldRect.maxs.x - (py + 0.5) * span / height,   y = worldRect.maxs.y - (px + 0.5) * span / width.''',
      '''// its centre to the longer span as convert.cpp:2226-2245 does. Without them it is the drawn geometry's XY box,
// squared about its centre. With quarter 0, pixel (px, py) covers world
//   x = worldRect.maxs.x - (py + 0.5) * span / height,   y = worldRect.maxs.y - (px + 0.5) * span / width;
// minimapPixel gives the pixel of a world point for any quarter.''')
patch(H, '''    // minimap_corner pair.
    AABB worldRect;
};''', '''    // minimap_corner pair.
    AABB worldRect;
    // Which way is up in the picture: 0 +X, 1 +Y, 2 -X, 3 -Y (the map's north, in quarter turns of northyaw).
    int northQuarter = 0;
};

// The quarter turn of a northyaw in degrees (0..3); *exact is false when it is not a multiple of 90 (the nearest
// quarter is returned).
int minimapNorthQuarter(double northYawDegrees, bool* exact = nullptr);
// The northyaw a build writes for a map: worldspawn northyaw, or 90 (north = +Y, the top of the editor's XY view).
double mapNorthYaw(const MapFile& map);
// The quarter a build draws the map's minimap with. A northyaw that is no quarter turn is left unturned by the
// converter, so the picture is drawn in the game's own frame (quarter 1) and *warning says so.
int minimapQuarterForBuild(const MapFile& map, std::string* warning = nullptr);
// The pixel (column, row; fractions) a world point has in a picture of `size` pixels over `rect` drawn with `quarter`.
void minimapPixel(const AABB& rect, int size, int quarter, double x, double y, double& column, double& row);''')
patch(H, '''MinimapImage renderMinimap(const MapFile& map, int size = 512);''', '''// `northQuarter`: which way is up (see the top of this file); a build passes minimapQuarterForBuild(map).
MinimapImage renderMinimap(const MapFile& map, int size = 512, int northQuarter = 0);''')

C = 'src/core/minimap.cpp'
patch(C, '''MinimapImage renderMinimap(const MapFile& map, int size) {
    MinimapImage img;
    img.width = img.height = normalizeSize(size);''', '''int minimapNorthQuarter(double yaw, bool* exact) {
    const double turns = yaw / 90.0;
    const double nearest = std::round(turns);
    if (exact) *exact = std::isfinite(yaw) && std::abs(turns - nearest) < 1e-4;
    if (!std::isfinite(yaw)) return 1;
    return int(((static_cast<long long>(nearest) % 4) + 4) % 4);
}

double mapNorthYaw(const MapFile& map) {
    for (const Entity& e : map.entities) {
        if (!e.isWorldspawn()) continue;
        if (const std::string* v = e.value("northyaw"); v && !v->empty()) {
            char* end = nullptr;
            const double yaw = std::strtod(v->c_str(), &end);
            if (end != v->c_str() && std::isfinite(yaw)) return yaw;
        }
        break;
    }
    return 90.0;
}

int minimapQuarterForBuild(const MapFile& map, std::string* warning) {
    bool exact = true;
    const double yaw = mapNorthYaw(map);
    const int quarter = minimapNorthQuarter(yaw, &exact);
    if (exact) return quarter;
    if (warning) {
        char buf[200];
        std::snprintf(buf, sizeof buf, "northyaw %.6g is not a quarter turn: the minimap is drawn with +Y up, and the compass's north will not "
                                       "point along it (use 0, 90, 180 or 270)", yaw);
        *warning = buf;
    }
    return 1;
}

void minimapPixel(const AABB& rect, int size, int quarter, double x, double y, double& column, double& row) {
    const double span = rect.maxs.x - rect.mins.x;
    const double scale = span > 0 ? size / span : 0.0;
    switch (((quarter % 4) + 4) % 4) {
    case 1: column = (x - rect.mins.x) * scale, row = (rect.maxs.y - y) * scale; break;
    case 2: column = (y - rect.mins.y) * scale, row = (x - rect.mins.x) * scale; break;
    case 3: column = (rect.maxs.x - x) * scale, row = (y - rect.mins.y) * scale; break;
    default: column = (rect.maxs.y - y) * scale, row = (rect.maxs.x - x) * scale; break;
    }
}

MinimapImage renderMinimap(const MapFile& map, int size, int northQuarter) {
    MinimapImage img;
    img.northQuarter = ((northQuarter % 4) + 4) % 4;
    img.width = img.height = normalizeSize(size);''')
patch(C, '''    const int n = img.width;
    const double scale = n / span;

    // Source frame (minimap.h): column = (maxY - y) * scale, row = (maxX - x) * scale.
    const auto toPixel = [&](const Vec3& p) {
        return Vec3{(rect.maxs.y - p.y) * scale, (rect.maxs.x - p.x) * scale, p.z};
    };''', '''    const int n = img.width;

    // North up (minimap.h): quarter 0 gives column = (maxY - y) * scale, row = (maxX - x) * scale.
    const auto toPixel = [&](const Vec3& p) {
        double column = 0, row = 0;
        minimapPixel(rect, n, img.northQuarter, p.x, p.y, column, row);
        return Vec3{column, row, p.z};
    };''')

K = 'src/core/compile_iw8.cpp'
patch(K, '''        const MinimapImage mm = renderMinimap(map, 512);
        std::string merr;''', '''        // Drawn with the map's north up, for the northyaw the entity text above carries (default 90): the converter
        // turns it by 90 - northyaw and the game shows +Y up.
        std::string northWarning;
        const MinimapImage mm = renderMinimap(map, 512, minimapQuarterForBuild(map, &northWarning));
        if (!northWarning.empty()) warn.push_back(northWarning);
        std::string merr;''')

T = 'tests/minimap_tests.cpp'
s = io.open(ROOT + T, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
s = s.rstrip('\r\n') + nl + '''
// The owner's game test of 2026-11-14: the minimap was a quarter turn off. The picture is drawn with the map's north
// up; the build's default north is +Y (northyaw 90), which the converter does not turn and the game shows as it is.
TEST(minimap_follows_the_maps_north) {
    // A floor with one raised block in the +X +Y corner.
    const auto box = [](double x0, double y0, double z0, double x1, double y1, double z1) {
        char buf[1024];
        const char* m = "custom/a 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0";
        std::snprintf(buf, sizeof buf,
                      "{\\n ( %g %g %g ) ( %g %g %g ) ( %g %g %g ) %s\\n ( %g %g %g ) ( %g %g %g ) ( %g %g %g ) %s\\n"
                      " ( %g %g %g ) ( %g %g %g ) ( %g %g %g ) %s\\n ( %g %g %g ) ( %g %g %g ) ( %g %g %g ) %s\\n"
                      " ( %g %g %g ) ( %g %g %g ) ( %g %g %g ) %s\\n ( %g %g %g ) ( %g %g %g ) ( %g %g %g ) %s\\n}\\n",
                      x1, y1, z0, x0, y1, z0, x0, y0, z0, m, x0, y0, z1, x0, y1, z1, x1, y1, z1, m,
                      x0, y0, z1, x1, y0, z1, x1, y0, z0, m, x1, y1, z1, x0, y1, z1, x0, y1, z0, m,
                      x1, y0, z1, x1, y1, z1, x1, y1, z0, m, x0, y1, z1, x0, y0, z1, x0, y0, z0, m);
        return std::string(buf);
    };
    const auto mapWith = [&](const char* worldKeys) {
        const std::string text = std::string("iwmap 4\\n{\\n\\"classname\\" \\"worldspawn\\"\\n") + worldKeys + box(-256, -256, -16, 256, 256, 0) +
                                 box(128, 128, 0, 256, 256, 200) + "}\\n";
        Id next = 1;
        return parseMap(text, next);
    };
    // Where the block's top shows: the brightest quadrant of the picture.
    const auto blockQuadrant = [](const MinimapImage& img) {
        double best = -1;
        int at = -1;
        for (int q = 0; q < 4; ++q) {
            double sum = 0;
            const int x0 = (q % 2) * img.width / 2, y0 = (q / 2) * img.height / 2;
            for (int y = y0; y < y0 + img.height / 2; ++y)
                for (int x = x0; x < x0 + img.width / 2; ++x) sum += img.rgba[(size_t(y) * img.width + x) * 4];
            if (sum > best) best = sum, at = q;
        }
        return at;  // 0 top left, 1 top right, 2 bottom left, 3 bottom right
    };
    ParseResult plain = mapWith("");
    CHECK(plain.map.has_value());
    if (!plain.map) return;
    // The build's default: north +Y. The +X +Y block is at the top right, as the game shows the map.
    CHECK(mapNorthYaw(*plain.map) == 90.0);
    std::string warning;
    CHECK(minimapQuarterForBuild(*plain.map, &warning) == 1 && warning.empty());
    const MinimapImage north = renderMinimap(*plain.map, 64, 1);
    CHECK(north.northQuarter == 1 && blockQuadrant(north) == 1);
    // A map that says north is +X: the picture has +X up, +Y left (the converter turns it a quarter clockwise).
    ParseResult east = mapWith("\\"northyaw\\" \\"0\\"\\n");
    CHECK(east.map.has_value() && minimapQuarterForBuild(*east.map) == 0);
    if (east.map) CHECK(blockQuadrant(renderMinimap(*east.map, 64, 0)) == 0);
    CHECK(blockQuadrant(renderMinimap(*plain.map, 64, 2)) == 2);  // north -X: the block at the bottom left
    CHECK(blockQuadrant(renderMinimap(*plain.map, 64, 3)) == 3);  // north -Y: at the bottom right
    // Turning the quarter-0 picture clockwise, as the converter does for northyaw 0, gives the quarter-1 picture.
    const MinimapImage zero = renderMinimap(*plain.map, 64, 0);
    size_t differ = 0;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            // clockwise by 90: the pixel (x, y) of the result comes from (y, 63 - x) of the source
            const uint8_t* a = &north.rgba[(size_t(y) * 64 + x) * 4];
            const uint8_t* b = &zero.rgba[(size_t(63 - x) * 64 + y) * 4];
            differ += a[0] != b[0] || a[3] != b[3];
        }
    CHECK(differ == 0);
    // Quarters of a yaw, and a yaw that is none: drawn in the game's frame, with a warning.
    bool exact = false;
    CHECK(minimapNorthQuarter(0, &exact) == 0 && exact);
    CHECK(minimapNorthQuarter(-90, &exact) == 3 && exact);
    CHECK(minimapNorthQuarter(450, &exact) == 1 && exact);
    CHECK(minimapNorthQuarter(45, &exact) >= 0 && !exact);
    ParseResult odd = mapWith("\\"northyaw\\" \\"30\\"\\n");
    CHECK(odd.map.has_value() && minimapQuarterForBuild(*odd.map, &warning) == 1 && warning.find("not a quarter turn") != std::string::npos);
    // A world point's pixel.
    AABB rect;
    rect.add({-256, -256, 0});
    rect.add({256, 256, 0});
    double col = 0, row = 0;
    minimapPixel(rect, 64, 1, 256, 256, col, row);
    CHECK(col == 64 && row == 0);  // +X +Y: the top right corner
    minimapPixel(rect, 64, 0, 256, 256, col, row);
    CHECK(col == 0 && row == 0);   // north +X: the top left corner
}
'''.replace('\n', nl)
io.open(ROOT + T, 'w', encoding='utf-8', newline='').write(s)
print('ok')
