import io
p = 'tests/compile_iw8_tests.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = '// 12.7: "iw8r_mat:<model material>" replaces one slot of a stock model; the build makes a copy named from the change.'
assert s.count(a) == 1
new = r'''// 5.8 (research 80 section 6): iw8r_decal point entities are written as world.json decal_volumes; a bad one is left
// out with a warning and never stops the build; the class does not reach the entity list.
TEST(compile_iw8_decal_volumes) {
    const std::string sample = samplePath();
    std::string text;
    CHECK(!sample.empty() && readFile(sample, text));
    if (text.empty()) return;
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    const auto decal = [&](const std::string& keys) { return "{" + eol + "\"classname\" \"iw8r_decal\"" + eol + keys + "}" + eol; };
    const auto kv = [&](const std::string& k, const std::string& v) { return "\"" + k + "\" \"" + v + "\"" + eol; };
    const std::string base = text;
    text += decal(kv("origin", "32 -16 4") + kv("angles", "90 0 0") + kv("size", "8 64 32") + kv("material", "i/vfx_decal_surface_scorch_1") +
                  kv("opacity", "0.5") + kv("feather", "0.25") + kv("draw_order", "2") + kv("uv_scale", "2 4") + kv("uv_shift", "0.5 0"));
    text += decal(kv("origin", "0 0 4") + kv("size", "8 64 64"));                                         // no material
    text += decal(kv("origin", "0 0 4") + kv("material", "i/x") + kv("size", "0 64 64"));                 // no depth
    text += decal(kv("origin", "64 0 4") + kv("material", "i/y") + kv("opacity", "7") + kv("draw_order", "9"));  // bad ranges: defaults
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const std::string out = scratch("decals");
    CompileOptions opt;
    opt.mapId = kSampleId;
    CompileReport rep;
    std::string err;
    CHECK(compileIw8Extraction(*pr.map, out, opt, rep, &err));
    const fs::path mp = fs::path(out) / "extracted" / "maps" / "mp";
    const std::string world = readAll((mp / (std::string(kSampleId) + ".d3dbsp.replay-world.json")).string());
    const size_t at = world.find("\"decal_volumes\":[");
    CHECK(at != std::string::npos);
    if (at == std::string::npos) return;
    const std::string decals = world.substr(at, world.find("]}", at) == std::string::npos ? std::string::npos : world.find(",\"dpvs\"", at) - at);
    size_t entries = 0;
    for (size_t p2 = 0; (p2 = decals.find("\"center\":", p2)) != std::string::npos; ++p2) ++entries;
    CHECK(entries == 2);  // the two valid ones
    CHECK(decals.find("\"material\":\"i/vfx_decal_surface_scorch_1\"") != std::string::npos);
    CHECK(decals.find("\"half_size\":[4,32,16]") != std::string::npos);
    CHECK(decals.find("\"opacity\":0.5") != std::string::npos && decals.find("\"feather\":0.25") != std::string::npos &&
          decals.find("\"draw_order\":2") != std::string::npos);
    CHECK(decals.find("\"uv_matrix\":[2,-0,0,4]") != std::string::npos || decals.find("\"uv_matrix\":[2,0,0,4]") != std::string::npos);
    CHECK(decals.find("\"uv_offset\":[0.5,0]") != std::string::npos);
    // The entry with bad ranges keeps its place with the defaults.
    CHECK(decals.find("\"material\":\"i/y\",\"opacity\":1,\"feather\":0,\"draw_order\":0") != std::string::npos);
    bool noMaterial = false, noDepth = false, range = false, written = false;
    for (const auto& w : rep.warnings) {
        noMaterial |= w.find("iw8r_decal") != std::string::npos && w.find("no material; left out") != std::string::npos;
        noDepth |= w.find("size must be three numbers above 0") != std::string::npos;
        range |= w.find("opacity '7' out of range") != std::string::npos;
        written |= w.find("2 decal volume(s) written") != std::string::npos;
    }
    CHECK(noMaterial && noDepth && range && written);
    // The class is not an entity of the built map.
    const std::string ents = readAll((mp / (std::string(kSampleId) + ".d3dbsp.ents")).string());
    CHECK(ents.find("iw8r_decal") == std::string::npos);
    // A map without decals has no decal_volumes key at all (the built world is as before).
    Id next2 = 1;
    ParseResult plain = parseMap(base, next2);
    CHECK(plain.map.has_value());
    if (!plain.map) return;
    const std::string out2 = scratch("decals_none");
    CompileReport rep2;
    CHECK(compileIw8Extraction(*plain.map, out2, opt, rep2, &err));
    const std::string world2 = readAll((fs::path(out2) / "extracted" / "maps" / "mp" / (std::string(kSampleId) + ".d3dbsp.replay-world.json")).string());
    CHECK(!world2.empty() && world2.find("decal_volumes") == std::string::npos);
}

'''
s = s.replace(a, new.replace('\n', nl) + a)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
