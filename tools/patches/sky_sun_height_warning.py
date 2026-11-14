import io

# Build: warn when the sky image's sun and the map's sun are at different heights (turning the sky lines up only the
# yaw; the shadows then fall from a higher or lower sun than the one painted in the sky).


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


K = 'E:/IW8-Radiant/src/core/compile_iw8.cpp'
patch(K, '''            } else if (!opt.skyDdsPath.empty() && !name.empty() && readFile(opt.skyDdsPath, src)) {
                skyRot[1] = autoSkyRotationYaw(src, sunYaw);
                skyRotAuto = true;
            }''', '''            } else if (!opt.skyDdsPath.empty() && !name.empty() && readFile(opt.skyDdsPath, src)) {
                const auto skySun = sunFromSkyDds(src);
                skyRot[1] = autoSkyRotationYaw(skySun, sunYaw);
                skyRotAuto = true;
                if (skySun && skySun->contrast >= 1.6) {
                    const double skyUp = std::asin(std::clamp(skySun->dir.z, -1.0, 1.0)) * 180.0 / 3.14159265358979323846;
                    const double mapUp = -sunPitch;
                    if (std::fabs(skyUp - mapUp) > 10)
                        warn.push_back("sky '" + *sk + "': its sun is " + formatNumber(std::round(skyUp)) + " degrees up, the map's sun " +
                                       formatNumber(std::round(mapUp)) + " (sundirection pitch " + formatNumber(sunPitch) +
                                       "): shadows fall from a different height than the sky shows; Sun & Sky > aim the sun "
                                       "at the sky's sun lines them up");
                }
            }''')
print('ok')
