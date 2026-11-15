import io

# Research 132 G7, Warzone: every Warzone tile has its own light grid atlas (widths 64 / 32 / 16 / 8 on Verdansk,
# depths up to 16). packStockLgvAtlases stacks several into one for the GPU: the width of the widest, the depths one
# after another, a narrower atlas at the corner of its slices with texture 1's six tiles re-laid at the stack's width,
# and each volume's packing turned to the stack, so sampleStockLgv on the stack equals sampling the source.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


H = 'src/core/stock_lightgrid.h'
patch(H, '''// E(n) of research 105 §1.4 (band weights 1, 4/9, 1/16), clamped >= 0, before bakedLightScale. n: unit world normal.''',
      '''// Several atlases (the main zone's and Warzone tiles') stacked into one for the GPU: the width of the widest, the
// depths one after another (in order, while the sum stays within maxDepth), a narrower atlas at the corner of its
// slices with texture 1's six tiles re-laid at the stack's width. Every volume keeps its transform; its packing is
// turned to the stack, so sampleStockLgv(stack, firstBox[i] + v, c) equals sampleStockLgv(*in[i], v, c).
// firstBox[i] = UINT32_MAX for a null or malformed atlas and for one past maxDepth. False when none fits.
struct StockLgvStack {
    StockLgvAtlas atlas;
    std::vector<uint32_t> firstBox;  // per input
};
bool packStockLgvAtlases(const std::vector<const StockLgvAtlas*>& in, StockLgvStack& out, int maxDepth = 2048);

// E(n) of research 105 §1.4 (band weights 1, 4/9, 1/16), clamped >= 0, before bakedLightScale. n: unit world normal.''')

C = 'src/core/stock_lightgrid.cpp'
patch(C, '''void evalStockLgv(const StockLgvSh& s, const double n[3], double outRgb[3]) {''', '''bool packStockLgvAtlases(const std::vector<const StockLgvAtlas*>& in, StockLgvStack& out, int maxDepth) {
    out = StockLgvStack{};
    out.firstBox.assign(in.size(), 0xFFFFFFFFu);
    const auto valid = [](const StockLgvAtlas* a) {
        if (!a || a->width <= 0 || a->depth <= 0 || a->pack.empty() || a->xform.size() != a->pack.size()) return false;
        const size_t w = size_t(a->width), d = size_t(a->depth);
        return a->tex0.size() == w * w * d && a->tex1.size() == 6 * w * w * d;
    };
    size_t W = 0, Dt = 0;
    std::vector<size_t> zAt(in.size(), 0);
    std::vector<char> take(in.size(), 0);
    for (size_t i = 0; i < in.size(); ++i) {
        if (!valid(in[i]) || Dt + size_t(in[i]->depth) > size_t(std::max(1, maxDepth))) continue;
        take[i] = 1, zAt[i] = Dt;
        Dt += size_t(in[i]->depth);
        W = std::max(W, size_t(in[i]->width));
    }
    if (Dt == 0) return false;
    StockLgvAtlas& o = out.atlas;
    o.width = int(W), o.depth = int(Dt);
    o.tex0.assign(W * W * Dt, 0u);
    o.tex1.assign(6 * W * W * Dt, 0u);
    for (size_t i = 0; i < in.size(); ++i) {
        if (!take[i]) continue;
        const StockLgvAtlas& a = *in[i];
        const size_t w = size_t(a.width), d = size_t(a.depth), Z = zAt[i];
        for (size_t z = 0; z < d; ++z)
            for (size_t y = 0; y < w; ++y)
                for (size_t x = 0; x < w; ++x) {
                    o.tex0[x + W * (y + W * (Z + z))] = a.tex0[x + w * (y + w * z)];
                    for (size_t t = 0; t < 6; ++t)
                        o.tex1[(x + W * (t & 1)) + 2 * W * (y + W * (t >> 1)) + 2 * W * 3 * W * (Z + z)] =
                            a.tex1[(x + w * (t & 1)) + 2 * w * (y + w * (t >> 1)) + 2 * w * 3 * w * z];
                }
        out.firstBox[i] = uint32_t(o.pack.size());
        const float sx = float(double(w) / double(W)), sz = float(double(d) / double(Dt));
        for (size_t v = 0; v < a.pack.size(); ++v) {
            StockLgvAtlas::Pack p = a.pack[v];
            for (int k = 0; k < 2; ++k) p.uvScale[k] *= sx, p.uvBias[k] *= sx;
            p.uvScale[2] *= sz;
            p.uvBias[2] = float((double(a.pack[v].uvBias[2]) * double(d) + double(Z)) / double(Dt));
            o.pack.push_back(p);
            o.xform.push_back(a.xform[v]);
        }
    }
    return true;
}

void evalStockLgv(const StockLgvSh& s, const double n[3], double outRgb[3]) {''')

T = 'tests/stock_lightgrid_tests.cpp'
s = io.open(ROOT + T, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
test = '''
// Warzone tiles (research 132 G7): atlases of different widths stacked into one sample exactly as their sources.
TEST(stock_lgv_pack_atlases_samples_like_the_sources) {
    StockLgvAtlas a;
    std::string err;
    CHECK(parseStockLgvBlock(syntheticBlock(), a, &err));  // W 4, D 2, two volumes
    // A W 2, D 1 atlas: one 2 x 2 x 1 box, every texel distinct.
    StockLgvAtlas b;
    b.width = 2, b.depth = 1;
    for (uint32_t k = 0; k < 4; ++k) b.tex0.push_back(encRgb(1.0 + k, 2.0 + 0.5 * k, 3.0 - 0.5 * k));
    for (uint32_t k = 0; k < 24; ++k) b.tex1.push_back(0x01010101u * (10u * k + 3u) ^ (k << 8));
    StockLgvAtlas::Pack pk;
    pk.uvScale[0] = pk.uvScale[1] = 0.5f, pk.uvScale[2] = 0.0f;
    pk.uvBias[0] = pk.uvBias[1] = 0.25f, pk.uvBias[2] = 0.5f;
    b.pack.push_back(pk);
    b.xform.push_back(StockLgvAtlas::Xform{});
    StockLgvStack st;
    CHECK(packStockLgvAtlases({&a, nullptr, &b}, st));
    CHECK(st.atlas.width == 4 && st.atlas.depth == 3 && st.atlas.pack.size() == 3 && st.atlas.xform.size() == 3);
    CHECK(st.firstBox.size() == 3 && st.firstBox[0] == 0 && st.firstBox[1] == 0xFFFFFFFFu && st.firstBox[2] == 2);
    const double coords[][3] = {{0, 0, 0}, {1, 1, 1}, {0.25, 0.6, 0.3}, {0.5, 0.5, 0.5}, {0.9, 0.1, 0.75}};
    const auto same = [&](const StockLgvAtlas& src, size_t v, size_t packed) {
        for (const auto& c : coords) {
            const StockLgvSh x = sampleStockLgv(src, v, c), y = sampleStockLgv(st.atlas, packed, c);
            for (int ch = 0; ch < 3; ++ch) {
                CHECK(near(x.dc[ch], y.dc[ch], 1e-4));
                for (int j = 0; j < 8; ++j) CHECK(near(x.sh[j][ch], y.sh[j][ch], 1e-3));
            }
        }
    };
    same(a, 0, 0);
    same(a, 1, 1);
    same(b, 0, 2);
    // Past the depth budget: left out.
    StockLgvStack small;
    CHECK(packStockLgvAtlases({&a, &b}, small, 2));
    CHECK(small.atlas.depth == 2 && small.firstBox[1] == 0xFFFFFFFFu);
    StockLgvStack none;
    CHECK(!packStockLgvAtlases({nullptr}, none));
}
'''
s = s.rstrip() + nl + test.replace('\n', nl)
io.open(ROOT + T, 'w', encoding='utf-8', newline='').write(s)
print('ok')
