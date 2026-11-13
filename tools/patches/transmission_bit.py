import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/core/stock_decal.h', '''    bool wrapDiffuse = false;    // hexB & 0x0008000000000000: one extra psData row, RGB wrap (stockWrapDiffuse)''',
      '''    bool wrapDiffuse = false;    // hexB & 0x0008000000000000: one extra psData row, RGB wrap (stockWrapDiffuse)
    bool transmission = false;   // hexB bit 0 (last digit odd): texture slot 68 x constant 18 lets light through (research 107)''')
patch('src/core/stock_decal.cpp', '''    t.wrapDiffuse = (b & 0x0008000000000000ull) != 0;''', '''    t.wrapDiffuse = (b & 0x0008000000000000ull) != 0;
    t.transmission = (b & 0x1) != 0;''')
patch('tests/stock_decal_tests.cpp', '''    t = parseStockTechsetName("tw/lit_3_lit_bad_ta1_815002_1002000000000018_0_1_1_0_0_113810015b_0_0_1_0_0");
    CHECK(t.valid && !t.ssr && t.displacement);''', '''    t = parseStockTechsetName("tw/lit_3_lit_bad_ta1_815002_1002000000000018_0_1_1_0_0_113810015b_0_0_1_0_0");
    CHECK(t.valid && !t.ssr && t.displacement && !t.transmission);
    // Transmission: hexB bit 0 (Speedball's grass, research 107 section 1.1); the "...3" glass set has it too.
    t = parseStockTechsetName("tmco/lit_3_lit_alt_ta1_848810_1012000201000051_0_1_1_0_0_13810015b_0_0_1_0_0");
    CHECK(t.valid && t.transmission && t.blendToken == "alt" && !t.ssr);
    t = parseStockTechsetName("tw/lit_3_lit_bad_ta1_815002_1002000000000013_0_1_1_0_0_113810015b_0_0_1_0_0");
    CHECK(t.transmission && t.ssr);''')
patch('src/qtapp/main_window.cpp', '''        if (!e->transmissionDds.isEmpty()) {''', '''        // Transmission: the techset's bit 0 on opaque and alpha-tested sets only (research 107 section 1.1: the blended glass
        // sets also bind slot 68, for a use that was not decoded).
        const StockTechsetTraits traits = parseStockTechsetName(e->techset.toStdString());
        if (!e->transmissionDds.isEmpty() && traits.transmission && (traits.blendToken == "rpl" || traits.blendToken == "alt")) {''')
print('ok')
