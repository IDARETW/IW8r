import io

# Research 136: what a model-layered technique set's name says about each layer L >= 1: its kind (standard, overlay,
# code), whether it samples the second UV set, and whether the layers are masked by slot 126 (hexB starting with 3).
# Core parser + test; ImportedMesh::uv1 from the GLB's TEXCOORD_1.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


H = 'src/core/stock_layered_material.h'
patch(H, '''bool decodeStockLayeredMaterial(const std::vector<uint8_t>& psData, int layerCount, StockLayeredMaterial& out,''',
      '''// Research 136 section 2: a model-layered technique set's layers from its name (`<family>/lit_3_lit_..._<hexB>_0_<N>_...`;
// layer L >= 1 holds the fields 18 + 8 (L - 1) onward: a, b, hex, colour mode, normal mode, c, 0, 0).
enum class ModelLayerKind { Standard, Overlay, Code };
struct ModelLayerTokens {
    int layers = 0;                       // N
    bool mask126 = false;                 // hexB starts with 3: slot 126 (R, G, B = layers 1, 2, 3) weights the layers
    std::vector<ModelLayerKind> kind;     // index L (0: the base, Standard)
    std::vector<bool> uv1;                // the layer samples the second UV set (hex bit 0x80000; a `2` family only)
};
// Empty (layers 0) for a name that is not a lit technique set with layer fields.
ModelLayerTokens parseModelLayerTokens(const std::string& techsetName);

bool decodeStockLayeredMaterial(const std::vector<uint8_t>& psData, int layerCount, StockLayeredMaterial& out,''')

C = 'src/core/stock_layered_material.cpp'
s = io.open(ROOT + C, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
add = '''
ModelLayerTokens parseModelLayerTokens(const std::string& name) {
    ModelLayerTokens t;
    const size_t slash = name.find('/');
    if (slash == std::string::npos) return t;
    const std::string family = name.substr(0, slash);
    std::vector<std::string> f;
    for (size_t i = slash + 1, j; i <= name.size(); i = j + 1) {
        j = name.find('_', i);
        if (j == std::string::npos) j = name.size();
        f.push_back(name.substr(i, j - i));
    }
    if (f.size() < 9 || f[0] != "lit" || f[2] != "lit") return t;
    const int n = std::atoi(f[8].c_str());
    if (n < 1 || n > 8) return t;
    t.layers = n;
    t.mask126 = !f[6].empty() && f[6][0] == '3';
    // A `2` in the family (tm2o, tm2co, m2o, m2co): the model has a second UV set.
    const bool secondUvFamily = family.find('2') != std::string::npos;
    t.kind.assign(size_t(n), ModelLayerKind::Standard);
    t.uv1.assign(size_t(n), false);
    for (int L = 1; L < n; ++L) {
        const size_t at = 18 + 8 * size_t(L - 1);
        if (at + 2 >= f.size()) break;
        const unsigned long long hex = std::strtoull(f[at + 2].c_str(), nullptr, 16);
        if (f[at + 1] == "1") t.kind[size_t(L)] = ModelLayerKind::Code;
        else if ((hex >> 24) == 4) t.kind[size_t(L)] = ModelLayerKind::Overlay;
        t.uv1[size_t(L)] = secondUvFamily && (hex & 0x80000ull) != 0;
    }
    return t;
}
'''
# append before the closing namespace
idx = s.rstrip().rfind('}')
s = s[:idx] + add.replace('\n', nl) + nl + s[idx:]
if '#include <cstdlib>' not in s:
    s = s.replace('#include <cstdint>', '#include <cstdint>' + nl + '#include <cstdlib>', 1)
io.open(ROOT + C, 'w', encoding='utf-8', newline='').write(s)

T = 'tests/stock_layered_material_tests.cpp'
s = io.open(ROOT + T, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
test = '''
// Research 136 section 2: the layer kinds and UV sets from technique set names (M2 of research 107, overlays, a UV1
// layer of a `2` family, the slot-126 mask by hexB, code layers).
TEST(model_layer_tokens_from_techset_names) {
    // M2: tmo, 2 layers, layer 1 standard (3810015f, colour mode 2), UV0, no 126 mask (hexB 1...).
    const ModelLayerTokens m2 = parseModelLayerTokens("tmo/lit_3_lit_rpl_ta1_800800_1002000000000010_0_2_1_0_0_13810015b_0_0_1_0_0_0_0_3810015f_2_1_2_0_0");
    CHECK(m2.layers == 2 && !m2.mask126 && m2.kind.size() == 2 && m2.kind[1] == ModelLayerKind::Standard && !m2.uv1[1]);
    // An overlay layer (410005b) and a standard UV1 layer (3818015f) on a tm2co set masked by slot 126 (hexB 3...).
    const ModelLayerTokens o = parseModelLayerTokens(
        "tm2co/lit_3_lit_rpl_ta1_800800_3002000000000010_0_3_1_0_0_13810015b_0_0_1_0_0_0_0_410005b_0_0_1_0_0_0_0_3818015f_0_0_2_0_0");
    CHECK(o.layers == 3 && o.mask126);
    if (o.kind.size() == 3) {
        CHECK(o.kind[1] == ModelLayerKind::Overlay && !o.uv1[1]);
        CHECK(o.kind[2] == ModelLayerKind::Standard && o.uv1[2]);
    }
    // The same UV1 bit in a family without a second UV set: UV0. A code layer (b = 1).
    const ModelLayerTokens c = parseModelLayerTokens("tmo/lit_3_lit_rpl_ta1_800800_1002000000000010_0_3_1_0_0_13810015b_0_0_1_0_0_0_0_3818015f_0_0_2_0_0_0_1_180153_0_0_1_0_0");
    CHECK(c.layers == 3 && !c.uv1[1] && c.kind[2] == ModelLayerKind::Code);
    // Not a lit technique set.
    CHECK(parseModelLayerTokens("tw/dist_10_distortion_rpl").layers == 0);
    CHECK(parseModelLayerTokens("no_slash").layers == 0);
}
'''
s = s.rstrip() + nl + test.replace('\n', nl)
io.open(ROOT + T, 'w', encoding='utf-8', newline='').write(s)

MI = 'src/core/model_import.h'
patch(MI, '''    std::vector<std::array<float, 2>> lmapUv;''', '''    std::vector<std::array<float, 2>> lmapUv;
    // The second UV set (glTF TEXCOORD_1; stock `2` families' SECONDARY_TEXCOORDS, research 136), as stored. Empty
    // when the primitive has none.
    std::vector<std::array<double, 2>> uv1;''')
G = 'src/core/glb_mesh.cpp'
patch(G, '''            if (const json::Value* sv = attrs->get("_SELFVIS")) {''', '''            if (const json::Value* tc1 = attrs->get("TEXCOORD_1")) {
                size_t n1 = 0;
                if (!readAccessor(int(tc1->num), 2, v, n1)) return false;
                if (n1 != n) return fail("TEXCOORD_1 count differs from POSITION");
                m.uv1.resize(n);
                for (size_t i = 0; i < n; ++i) m.uv1[i] = {v[i * 2], v[i * 2 + 1]};
            }
            if (const json::Value* sv = attrs->get("_SELFVIS")) {''')
print('ok')
