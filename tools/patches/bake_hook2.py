import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/core/compile_iw8.cpp', [
 ('''    std::string lightmapKeys;
    if (worldspawn''', '''    std::string lightmapKeys;
    std::vector<std::pair<fs::path, std::string>> lightmapFiles;
    if (worldspawn'''),
 ('''        // Light entities: origin, colour x intensity, radius; a spot by its target or angles and fov (as the comworld).
        for (const Entity* pe : points) {''', '''        // Light entities: origin, colour x intensity, radius; a spot by its target or angles and fov (as the comworld).
        std::map<std::string, Vec3> lightTargets;
        for (const Entity* pe : points)
            if (const std::string* tn = pe->value("targetname"); tn && !tn->empty())
                if (const auto o = pe->origin()) lightTargets.emplace(*tn, *o);
        for (const Entity* pe : points) {'''),
 ('''            l.spot = lcls == "light_spot";
            if (l.spot) {
                double a[3] = {0, 0, 0};
                if (const std::string* as = pe->value("angles")) parseVec3Text(*as, a);
                l.dir = sunDirection(-a[0], a[1]) * -1.0;  // the forward of pitch / yaw (pitch down positive)
                l.dir = Vec3{std::cos(a[0] * kPi / 180) * std::cos(a[1] * kPi / 180), std::cos(a[0] * kPi / 180) * std::sin(a[1] * kPi / 180),
                             -std::sin(a[0] * kPi / 180)};
                double outer''', '''            std::optional<Vec3> aimAt;
            if (const std::string* tk = pe->value("target"); tk && !tk->empty())
                if (const auto found = lightTargets.find(*tk); found != lightTargets.end()) aimAt = found->second - l.pos;
            l.spot = lcls == "light_spot" || (lcls == "light" && (aimAt || pe->value("fov_outer") || pe->value("fov_inner")));
            if (const std::string* fo = pe->value("fov_outer")) {
                double v = 0;
                if (parseNumber(*fo, v) && v >= 150.0) l.spot = false;  // built as an omni light (localPrimaryLights)
            }
            if (l.spot) {
                if (aimAt && length(*aimAt) > 1e-6) {
                    l.dir = normalize(*aimAt);
                } else {
                    double a[3] = {0, 0, 0};
                    if (const std::string* as = pe->value("angles")) parseVec3Text(*as, a);
                    const double p = a[0] * kPi / 180, y = a[1] * kPi / 180;
                    l.dir = Vec3{std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), -std::sin(p)};
                }
                double outer'''),
 ('''                l.cosOuter = std::cos(std::clamp(outer, 1.0, 179.0) * 0.5 * kPi / 180);
                l.cosInner = std::cos(std::clamp(std::min(inner, outer), 0.5, 179.0) * 0.5 * kPi / 180);''',
  '''                outer = std::clamp(outer, 1.0, 170.0);
                if (inner >= outer) inner = outer * 0.5;
                l.cosOuter = std::cos(outer * 0.5 * kPi / 180);
                l.cosInner = std::cos(std::max(inner, 0.0) * 0.5 * kPi / 180);'''),
 ('''            customFiles.push_back({root / "extracted" / (stem + "_primary.bin"), std::string(enc.primary.begin(), enc.primary.end())});
            customFiles.push_back({root / "extracted" / (stem + "_secondary.bin"), std::string(enc.secondary.begin(), enc.secondary.end())});''',
  '''            lightmapFiles.push_back({root / "extracted" / (stem + "_primary.bin"), std::string(enc.primary.begin(), enc.primary.end())});
            lightmapFiles.push_back({root / "extracted" / (stem + "_secondary.bin"), std::string(enc.secondary.begin(), enc.secondary.end())});'''),
 ('''        surfJson += "{\\"material\\":" + quote(materialFor(s.editorMaterial)) + ",\\"reflection_probe\\":" +''',
  '''        surfJson += "{\\"material\\":" + quote(materialFor(s.editorMaterial)) +
                    (si < lightmapped.size() && lightmapped[si] ? std::string(",\\"lightmap\\":0") : std::string()) + ",\\"reflection_probe\\":" +'''),
 ('''    for (size_t i = 0; ok && i < customFiles.size(); ++i) ok = writeFile(customFiles[i].first, customFiles[i].second, werr);''',
  '''    for (size_t i = 0; ok && i < customFiles.size(); ++i) ok = writeFile(customFiles[i].first, customFiles[i].second, werr);
    for (size_t i = 0; ok && i < lightmapFiles.size(); ++i) ok = writeFile(lightmapFiles[i].first, lightmapFiles[i].second, werr);'''),
])
print('ok')
