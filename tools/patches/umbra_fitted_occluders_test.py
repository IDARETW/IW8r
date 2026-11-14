import io

# Test for the fitted occluder boxes: a compile with an opaque library material on a box brush, an eight-sided prism
# and a thin three-sided sliver writes two occluder boxes (the box as it is, the prism's inner box) and says so.

P = 'E:/IW8-Radiant/tests/custom_materials_tests.cpp'
s = io.open(P, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
if 'compile_occlusion_bake_fits_boxes_into_slanted_brushes' not in s:
    inc = '#include "compile_iw8.h"'
    assert s.count(inc) == 1
    s = s.replace(inc, '#include "brush_ops.h"' + nl + inc)
    test = '''
// The occlusion bake's occluders (worldspawn iw8r_umbra_bake 1): an opaque box brush as it is, an opaque many-sided
// brush by the box found inside it (core occluder_box.h), nothing for a brush too thin to hold one.
TEST(compile_occlusion_bake_fits_boxes_into_slanted_brushes) {
    const std::string dir = freshDir("occluders");
    writeBytes(dir + "/library/custom/wall.png", pngBytes(8, 8, flatRgba(8, 8, 120, 120, 120, 255)));
    writeText(dir + "/library/custom/wall.iw8mat.json", "{\\"name\\":\\"custom/wall\\",\\"color\\":\\"wall.png\\",\\"surface\\":\\"concrete\\"}");
    MaterialLibrary library;
    std::string err;
    CHECK(library.load({dir + "/library"}, &err));
    const std::string suffix = " custom/wall 128 128 0 0 0 0";
    const std::string text = "iwmap 4\\n{\\n\\"classname\\" \\"worldspawn\\"\\n\\"iw8r_umbra_bake\\" \\"1\\"\\n" +
                             makeBrushPrimitive(makeBox({{0, 0, 0}, {128, 256, 192}}, suffix), "\\n")->text() +
                             makeBrushPrimitive(makePrism({{512, 0, 0}, {768, 256, 192}}, 8, 2, suffix), "\\n")->text() +
                             makeBrushPrimitive(makePrism({{1024, 0, 0}, {1028, 256, 192}}, 3, 2, suffix), "\\n")->text() + "}\\n";
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    CompileOptions opt;
    opt.mapId = "mp_occ";
    opt.library = &library;
    CompileReport rep;
    const std::string out = dir + "/out";
    CHECK(compileIw8Extraction(*pr.map, out, opt, rep, &err));
    const std::string world = readFileOrEmpty(out + "/extracted/maps/mp/mp_occ.d3dbsp.replay-world.json");
    const size_t at = world.find("\\"umbra_occluders\\":[");
    CHECK(at != std::string::npos);
    if (at == std::string::npos) return;
    // The numbers of the list, six per box.
    std::vector<double> v;
    size_t i = at + std::string("\\"umbra_occluders\\":[").size();
    int depth = 1;
    std::string num;
    for (; i < world.size() && depth > 0; ++i) {
        const char c = world[i];
        if (c == '[') ++depth;
        else if (c == ']') --depth;
        if ((c >= '0' && c <= '9') || c == '-' || c == '.' || c == 'e' || c == '+') num += c;
        else if (!num.empty()) v.push_back(std::atof(num.c_str())), num.clear();
    }
    CHECK(v.size() == 12);
    if (v.size() != 12) return;
    CHECK(v[0] == 0 && v[1] == 0 && v[2] == 0 && v[3] == 128 && v[4] == 256 && v[5] == 192);
    // The prism's box: inside the prism's bounds, most of its height, and more than half its width both ways.
    CHECK(v[6] >= 512 && v[9] <= 768 && v[7] >= 0 && v[10] <= 256 && v[8] >= 0 && v[11] <= 192);
    CHECK(v[9] - v[6] > 128 && v[10] - v[7] > 128 && v[11] - v[8] > 180);
    bool said = false;
    for (const std::string& w : rep.warnings)
        said = said || (contains(w, "occlusion bake: 2 occluder box(es)") && contains(w, "(1 of them fitted") && contains(w, "; 1 opaque brush(es) are too thin"));
    CHECK(said);
}
'''.replace('\n', nl)
    s = s.rstrip() + nl + test
    io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('ok')
