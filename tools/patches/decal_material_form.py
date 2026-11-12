import io
p = 'src/core/compile_iw8.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
def rep(a, b, count=1):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (s.count(a), a[:60])
    s = s.replace(a, b)

# 1. the decal-form writer, after customMaterialJson
rep('''        json += ",{\\"name\\":\\"specularMap\\",\\"semantic\\":\\"specularMap\\",\\"image\\":" + quote(stem + "_s") + "}";
    return json + "]}";
}
''', '''        json += ",{\\"name\\":\\"specularMap\\",\\"semantic\\":\\"specularMap\\",\\"image\\":" + quote(stem + "_s") + "}";
    return json + "]}";
}

// Material JSON of a custom material used by a decal volume (5.8, research 82 section "How material is resolved").
// The converter's decal producer only takes the effect-decal form: metadata iw3 / material / 1, a technique set named
// [mw]c_l_sm_b0c0(n0)(s0), a 1 x 1 atlas, camera region "decal", state flags 52, one to three textures and a colorTint.
// It is written under its own name (<material>_dcl), so the same library material can also be painted on a face.
std::string customDecalMaterialJson(const CustomMaterial& cm, const std::string& outName) {
    const std::string stem = customImageStem(outName);
    std::string json = "{\\"_game\\":\\"iw3\\",\\"_type\\":\\"material\\",\\"_version\\":1,\\"techniqueSet\\":" +
                       quote(std::string("wc_l_sm_b0c0") + (cm.normalImage.empty() ? "" : "n0") + (cm.specImage.empty() ? "" : "s0")) +
                       ",\\"textureAtlas\\":{\\"rows\\":1,\\"columns\\":1},\\"stateFlags\\":52,\\"cameraRegion\\":\\"decal\\"";
    const double r = cm.hasTint() ? cm.tint[0] : 1, g = cm.hasTint() ? cm.tint[1] : 1, b = cm.hasTint() ? cm.tint[2] : 1;
    json += ",\\"constants\\":[{\\"name\\":\\"colorTint\\",\\"literal\\":[" + num(r) + "," + num(g) + "," + num(b) + ",1]}]";
    json += ",\\"textures\\":[{\\"name\\":\\"colorMap\\",\\"semantic\\":\\"colorMap\\",\\"image\\":" + quote(stem + "_c") + "}";
    if (!cm.normalImage.empty())
        json += ",{\\"name\\":\\"normalMap\\",\\"semantic\\":\\"normalMap\\",\\"image\\":" + quote(stem + "_n") + "}";
    if (!cm.specImage.empty())
        json += ",{\\"name\\":\\"specularMap\\",\\"semantic\\":\\"specularMap\\",\\"image\\":" + quote(stem + "_s") + "}";
    return json + "]}";
}
''')

# 2. the decal loop names the decal-form material
rep('''        if (opt.library && opt.library->find(material)) material = materialFor(material);
''', '''        if (opt.library)
            if (const CustomMaterial* cm = opt.library->find(material)) {
                material = (cm->buildName.empty() ? cm->name : cm->buildName) + "_dcl";
                decalCustom[material] = cm;
            }
''')
rep('''    std::map<std::string, const CustomMaterial*> customUsed;
''', '''    std::map<std::string, const CustomMaterial*> customUsed;
    std::map<std::string, const CustomMaterial*> decalCustom;   // decal-form twins (<name>_dcl) of library materials
''')

# 3. files
rep('''    if (!skyValue.empty()) customFiles.push_back({root / "extracted" / "images" / (skyValue + ".dds"), skyCube});
''', '''    for (const auto& [name, cm] : decalCustom) {
        // The picture's alpha is the decal's shape, so it is always kept.
        const std::string stem = customImageStem(name);
        const std::pair<const std::string*, const char*> jobs[] = {{&cm->colorImage, "_c"}, {&cm->normalImage, "_n"}, {&cm->specImage, "_s"}};
        for (const auto& [source, suffix] : jobs) {
            if (source->empty()) continue;
            std::vector<uint8_t> dds;
            std::string imageErr;
            if (!encodeDdsFromImage(*source, std::string(suffix) == "_n", true, dds, &imageErr)) return fail(imageErr);
            customFiles.push_back({root / "extracted" / "images" / (stem + suffix + ".dds"), std::string(dds.begin(), dds.end())});
        }
        customFiles.push_back({root / "extracted" / "materials" / (name + ".json"), customDecalMaterialJson(*cm, name) + "\n"});
    }
    if (!skyValue.empty()) customFiles.push_back({root / "extracted" / "images" / (skyValue + ".dds"), skyCube});
''')
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
