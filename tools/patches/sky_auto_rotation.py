import io

# The sky's sun on the map's sun (owner test 2026-11-13: the toon Cinderrail's sky sun and shadows did not line up).
# A map without worldspawn "skyrotation" gets the yaw that puts the sky image's sun on the sun's yaw, in the build (the
# sky cube is turned by it) and in the viewport (the same turn). Stock maps set skyrotation to their sun's yaw; their
# sky images hold the sun near yaw 0.


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'E:/IW8-Radiant/src/core/sky_sun.h'
patch(H, '''std::optional<SkySun> sunFromSkyDds(const std::string& ddsBytes, std::string* err = nullptr);
''', '''std::optional<SkySun> sunFromSkyDds(const std::string& ddsBytes, std::string* err = nullptr);

// The skyrotation yaw (degrees, -180..180) of a map that sets none: the turn that puts the sky image's sun on the map
// sun's yaw (worldspawn sundirection yaw), as stock maps do with their skyrotation. A sky without a clear sun (contrast
// below 1.6, or none found) is taken to hold it at yaw 0, the stock convention. *imageSunYaw gets the yaw found.
double autoSkyRotationYaw(const std::string& skyDdsBytes, double sunYaw, double* imageSunYaw = nullptr);
''')
C = 'E:/IW8-Radiant/src/core/sky_sun.cpp'
patch(C, '''    return sunFromSkyCube(cube);
}
''', '''    return sunFromSkyCube(cube);
}

double autoSkyRotationYaw(const std::string& dds, double sunYaw, double* imageSunYaw) {
    double image = 0;
    if (const auto s = sunFromSkyDds(dds); s && s->contrast >= 1.6 && (s->dir.x != 0 || s->dir.y != 0))
        image = std::atan2(s->dir.y, s->dir.x) * 180.0 / 3.14159265358979323846;
    if (imageSunYaw) *imageSunYaw = image;
    double r = std::fmod(sunYaw - image, 360.0);
    if (r > 180) r -= 360;
    if (r <= -180) r += 360;
    return r;
}
''')
s = io.open(C, encoding='utf-8').read()
if '#include <cmath>' not in s:
    patch(C, '#include "sky_sun.h"\n', '#include "sky_sun.h"\n\n#include <cmath>\n')

K = 'E:/IW8-Radiant/src/core/compile_iw8.cpp'
patch(K, '''            SkyConvertOptions so;
            so.faceSize = opt.skyFaceSize;
            SkyConvertInfo si;''', '''            SkyConvertOptions so;
            so.faceSize = opt.skyFaceSize;
            SkyConvertInfo si;
            // The sky turned as the game turns a stock map's (worldspawn skyrotation), else so that its sun is on the
            // map sun's yaw (the game draws a converted sky unturned).
            double skyRot[3] = {0, 0, 0};
            bool skyRotAuto = false;
            if (const std::string* sr = worldspawn->value("skyrotation"); sr && parseVec3Text(*sr, skyRot)) {
            } else if (!opt.skyDdsPath.empty() && !name.empty() && readFile(opt.skyDdsPath, src)) {
                skyRot[1] = autoSkyRotationYaw(src, sunYaw);
                skyRotAuto = true;
            }
            for (int k = 0; k < 3; ++k) so.rotation[k] = float(skyRot[k]);''')
patch(K, '''                skyNote = note;
            }
            break;''', '''                skyNote = note;
                if (skyRot[0] != 0 || skyRot[1] != 0 || skyRot[2] != 0)
                    skyNote += ", turned " + formatNumber(skyRot[1]) + " degrees" +
                               (skyRotAuto ? " (its sun on the map sun's yaw)" : " (worldspawn skyrotation)");
            }
            break;''')
s = io.open(K, encoding='utf-8').read()
if '#include "sky_sun.h"' not in s:
    patch(K, '#include "iw8_contents.h"\n', '#include "iw8_contents.h"\n#include "sky_sun.h"\n')

V = 'E:/IW8-Radiant/src/qtapp/camera_view.cpp'
patch(V, '''        float skyRot[3] = {0, 0, 0};
        for (const auto& e : ctx_->doc.map().entities)
            if (e.isWorldspawn()) {
                if (const std::string* r = e.value("skyrotation")) std::sscanf(r->c_str(), "%f %f %f", &skyRot[0], &skyRot[1], &skyRot[2]);
                break;
            }
        const QString sky = skySource_();''', '''        float skyRot[3] = {0, 0, 0};
        bool skyRotSet = false;
        float sunYaw = 45;  // the build's default sundirection -50 45 0
        for (const auto& e : ctx_->doc.map().entities)
            if (e.isWorldspawn()) {
                if (const std::string* r = e.value("skyrotation"))
                    skyRotSet = std::sscanf(r->c_str(), "%f %f %f", &skyRot[0], &skyRot[1], &skyRot[2]) >= 2;
                float p = 0, y = 0;
                if (const std::string* d = e.value("sundirection"); d && std::sscanf(d->c_str(), "%f %f", &p, &y) == 2) sunYaw = y;
                break;
            }
        const QString sky = skySource_();
        // No skyrotation: the build turns the sky so its sun is on the map sun's yaw (core sky_sun); the preview does too.
        if (!skyRotSet && !sky.isEmpty()) {
            const QString autoKey = sky + QString("|%1").arg(sunYaw);
            if (autoKey != skyAutoKey_) {
                skyAutoKey_ = autoKey;
                std::string bytes;
                skyAutoYaw_ = readFile(sky.toStdString(), bytes) ? float(autoSkyRotationYaw(bytes, sunYaw)) : 0.0f;
            }
            skyRot[1] = skyAutoYaw_;
        }''')
VH = 'E:/IW8-Radiant/src/qtapp/camera_view.h'
s = io.open(VH, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
i = s.index('    QString skyPath_')
s = s[:i] + ('    QString skyAutoKey_;     // sky path | sun yaw of skyAutoYaw_' + nl + '    float skyAutoYaw_ = 0;   // core autoSkyRotationYaw for a map without skyrotation' + nl) + s[i:]
io.open(VH, 'w', encoding='utf-8', newline='').write(s)
s = io.open(V, encoding='utf-8').read()
if '#include "sky_sun.h"' not in s:
    patch(V, '#include "sky_image.h"\n', '#include "sky_image.h"\n#include "sky_sun.h"\n')
print('ok')
