import io
p = 'tests/custom_materials_tests.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = 'TEST(compile_blended_custom_material_gets_b0_techset) {'
assert s.count(a) == 1
t = r'''// 5.8 (research 82): a decal volume that names a library material gets a twin in the converter's effect-decal form
// (<name>_dcl); the same material painted on a face stays a normal custom material.
TEST(compile_decal_volume_custom_material_is_decal_form) {
    const std::string sample = samplePath();
    CHECK(!sample.empty());
    if (sample.empty()) return;
    const std::string dir = freshDir("decalform");
    writeBytes(dir + "/library/custom/stain.png", pngBytes(8, 8, flatRgba(8, 8, 90, 60, 40, 128)));
    writeBytes(dir + "/library/custom/stain_n.png", pngBytes(8, 8, flatRgba(8, 8, 128, 128, 255, 255)));
    writeText(dir + "/library/custom/stain.iw8mat.json",
              "{\"name\":\"custom/stain\",\"color\":\"stain.png\",\"normal\":\"stain_n.png\",\"blend\":true}");
    MaterialLibrary library;
    std::string err;
    CHECK(library.load({dir + "/library"}, &err));
    std::string text;
    CHECK(readFile(sample, text));
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    text += "{" + eol + "\"classname\" \"iw8r_decal\"" + eol + "\"origin\" \"0 0 4\"" + eol + "\"angles\" \"90 0 0\"" + eol +
            "\"size\" \"8 64 64\"" + eol + "\"material\" \"custom/stain\"" + eol + "}" + eol;
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    CompileOptions opt;
    opt.mapId = "mp_iw8_sample";
    opt.library = &library;
    CompileReport rep;
    const std::string out = dir + "/out";
    CHECK(compileIw8Extraction(*pr.map, out, opt, rep, &err));
    if (!fs::exists(out + "/complete")) return;
    const std::string world = readFileOrEmpty(out + "/extracted/maps/mp/mp_iw8_sample.d3dbsp.replay-world.json");
    CHECK(contains(world, "\"decal_volumes\":[") && contains(world, "\"material\":\"custom/stain_dcl\""));
    const std::string mat = readFileOrEmpty(out + "/extracted/materials/custom/stain_dcl.json");
    const std::string stem = "iw8rm_custom_stain_dcl_" + customNameHash("custom/stain_dcl");
    CHECK(contains(mat, "\"_game\":\"iw3\",\"_type\":\"material\",\"_version\":1"));
    CHECK(contains(mat, "\"techniqueSet\":\"wc_l_sm_b0c0n0\""));
    CHECK(contains(mat, "\"textureAtlas\":{\"rows\":1,\"columns\":1},\"stateFlags\":52,\"cameraRegion\":\"decal\""));
    CHECK(contains(mat, "\"name\":\"colorTint\",\"literal\":[1,1,1,1]"));
    CHECK(contains(mat, "\"semantic\":\"colorMap\",\"image\":\"" + stem + "_c\""));
    CHECK(contains(mat, "\"semantic\":\"normalMap\",\"image\":\"" + stem + "_n\""));
    CHECK(!contains(mat, "specularMap") && !contains(mat, "surfaceTypeBits"));
    CHECK(fs::exists(out + "/extracted/images/" + stem + "_c.dds"));
    CHECK(fs::exists(out + "/extracted/images/" + stem + "_n.dds"));
    // Not painted on any face, so the normal form of the material is not written.
    CHECK(!fs::exists(out + "/extracted/materials/custom/stain.json"));
}

'''.replace('\n', nl)
s = s.replace(a, t + a)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
