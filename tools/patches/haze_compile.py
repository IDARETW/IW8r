import io
ROOT = 'E:/IW8-Radiant/'
def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))
C = 'src/core/compile_iw8.cpp'
patch(C, '''        const Atmosphere atmo = readAtmosphere(*worldspawn, &awarn);
''', '''        Atmosphere atmo = readAtmosphere(*worldspawn, &awarn);
        // iw8r_haze (research 126): the game's own fog and volumetric air, as vision overrides under the user's
        // iw8r_vision. The fog's brightness follows this map's sun; the ground layers sit at the spawn points' height
        // (the lowest geometry without spawns). The game then fogs every opaque pixel, so iw8r_fog (baked into the
        // surface shader) is left out.
        std::string hazeVision;
        if (atmo.haze) {
            std::vector<double> heights;
            for (const Entity* pe : points)
                if (const std::string* cn = pe->value("classname"); cn && cn->find("spawn") != std::string::npos)
                    if (auto o = pe->origin()) heights.push_back(o->z);
            double groundZ = geom.mins.z;
            if (!heights.empty()) {
                std::nth_element(heights.begin(), heights.begin() + long(heights.size() / 2), heights.end());
                groundZ = heights[heights.size() / 2];
            }
            const double sunLuma = 0.2126 * sunColor[0] + 0.7152 * sunColor[1] + 0.0722 * sunColor[2];
            hazeVision = hazeVisionText(*atmo.haze, sunLuma, sunDir, std::round(groundZ));
            warn.push_back("haze '" + formatHaze(*atmo.haze) + "': the game's own fog, sky blend and volumetric air (ground layers at height " +
                           formatNumber(std::round(groundZ)) + "; any field can be changed in iw8r_vision; research 126; not seen in game)");
            if (atmo.fog) {
                warn.push_back("iw8r_fog is left out of the build: iw8r_haze uses the game's own fog (two fogs would add up)");
                atmo.fog.reset();
            }
        }
''')
patch(C, '''        if (const std::string* vo = worldspawn->value(kVisionKey); vo && !vo->empty()) {
            std::string verr;
            const std::string json = visionOverridesJson(*vo, &verr);''', '''        const std::string* userVision = worldspawn->value(kVisionKey);
        if (const std::string visionText = mergeVisionText(hazeVision, userVision ? *userVision : std::string()); !visionText.empty()) {
            std::string verr;
            const std::string json = visionOverridesJson(visionText, &verr);''')
patch(C, '''    {"iw8r_ir_intensity", "ir_intensity", LightKind::Finite, "must be a number"},
''', '''    {"iw8r_ir_intensity", "ir_intensity", LightKind::Finite, "must be a number"},
    // The lamp lights the volumetric air (a glow and beams in fog): the game adds a lamp's light to the air only when
    // the lamp is flagged (research 126 section 1.4; stock maps flag their feature lamps, not every lamp).
    {"iw8r_volumetric", "volumetric", LightKind::Switch, "must be 0, 1, true or false"},
''')
A = 'src/core/atmosphere.cpp'
patch(A, '''#include <cmath>
''', '''#include <cmath>
#include <cstdlib>
''')
print('ok')
