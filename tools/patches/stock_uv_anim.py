import io

# Research 133 section 1: UV animation. Constant 63 (u/s, v/s, rad/s) of a stock material turns its UV about the
# texture centre and scrolls it by the game's clock; the technique set's hexC bit 0x800000000 marks the materials that
# bind it. The material cache keeps constant 63 (constRules 3: entries of such technique sets resolve again once), the
# material facts carry it to the batch constants (RefLayer::extra 13..15), the pixel shader animates the UV before any
# other UV use, and the view redraws while an animated material is drawn.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


# --- techset trait
patch('src/core/stock_decal.h', '''    bool transmission = false;   // hexB bit 0 (last digit odd): texture slot 68 x constant 18 lets light through (research 107)''',
      '''    bool transmission = false;   // hexB bit 0 (last digit odd): texture slot 68 x constant 18 lets light through (research 107)
    bool uvAnim = false;         // hexC & 0x800000000: the technique binds constant 63, UV animation (research 133 section 1)''')
patch('src/core/stock_decal.cpp', '''    t.layers = std::atoi(tok[8].c_str());
    t.valid = true;''', '''    t.layers = std::atoi(tok[8].c_str());
    uint64_t c = 0;
    t.uvAnim = tok.size() > 12 && parseHex64(tok[12], c) && (c & 0x800000000ull) != 0;
    t.valid = true;''')
patch('tests/stock_decal_tests.cpp', '''    t = parseStockTechsetName("tw/lit_3_lit_bad_tca_802002_1002000000000010_0_1_1_0_0_13810015b_0_0_3_0_0");
    CHECK(t.valid && t.blended && !t.polygonOffset);''', '''    t = parseStockTechsetName("tw/lit_3_lit_bad_tca_802002_1002000000000010_0_1_1_0_0_13810015b_0_0_3_0_0");
    CHECK(t.valid && t.blended && !t.polygonOffset && !t.uvAnim);
    // UV animation (research 133 section 1): hexC bit 0x800000000 (the fan blade, the Shipment water).
    t = parseStockTechsetName("mo/lit_3_lit_bad_tca_802_1002000000000010_0_1_1_0_0_938100158_0_0_3_0_0");
    CHECK(t.valid && t.uvAnim);
    t = parseStockTechsetName("twc/lit_3_lit_rpl_ta1_801010_1002000000010012_0_1_1_0_0_193810015b_0_0_3_0_0");
    CHECK(t.valid && t.uvAnim && t.ssr);''')

# --- material cache: keep constant 63
M = 'src/qtapp/material_cache.cpp'
patch(M, '''namespace iw8r::qt {
namespace {''', '''namespace iw8r::qt {
namespace {

// Research 133 section 1: a technique set whose hex field carries bit 0x800000000 binds constant 63 (UV animation).
// Read from any long hex token, so the unlit effect sets (unlit_6_effect_...) count too.
bool techsetAnimatesUv(const QString& techset) {
    const QStringList tok = techset.section('/', -1).split('_');
    for (const QString& t : tok) {
        if (t.size() < 9 || t.size() > 12) continue;
        bool ok = false;
        const qulonglong v = t.toULongLong(&ok, 16);
        if (ok && (v & 0x800000000ull)) return true;
    }
    return false;
}''')
patch(M, '''        if (!e.failed && o.value("constRules").toInt() < 2) continue;  // 2: non-finite words in memory byte order''',
      '''        if (!e.failed && o.value("constRules").toInt() < 2) continue;  // 2: non-finite words in memory byte order
        // Resolved before constant 63 was kept (research 133, UV animation): those technique sets resolve again once.
        if (!e.failed && o.value("constRules").toInt() < 3 && techsetAnimatesUv(e.techset)) continue;''')
patch(M, '''            o.insert("constRules", 2);  // 1: the lit buffer, canonical rows, constants 18 / 36 / 103 / 104, slot 68; 2: bits order''',
      '''            o.insert("constRules", 3);  // 1: the lit buffer, canonical rows, constants 18 / 36 / 103 / 104, slot 68; 2: bits order; 3: + 63''')
patch(M, '''        if (finite && (index == 18 || index == 36 || index == 103 || index == 104)) work.consts[index] = f;''',
      '''        if (finite && (index == 18 || index == 36 || index == 63 || index == 103 || index == 104)) work.consts[index] = f;''')

# --- facts
patch('src/qtapp/camera_view.h', '''        // Research 132 G1: a blended model material that is no decal (glass and other see-through surfaces).
        bool translucent = false;''', '''        // Research 132 G1: a blended model material that is no decal (glass and other see-through surfaces).
        bool translucent = false;
        // Research 133 section 1: constant 63, UV animation (u / s, v / s, radians / s about the texture centre).
        std::array<float, 4> uvAnim{0, 0, 0, 0};''')
patch('src/qtapp/main_window.cpp', '''        if (const auto* c103 = cst(103)) f.wrap = {(*c103)[0], (*c103)[1], (*c103)[2], 1.0f};''',
      '''        if (const auto* c103 = cst(103)) f.wrap = {(*c103)[0], (*c103)[1], (*c103)[2], 1.0f};
        if (const auto* c63 = cst(63)) f.uvAnim = *c63;  // UV animation (research 133 section 1)''')

# --- batch constants
C = 'src/qtapp/camera_view.cpp'
patch(C, '''    if (!f || (f->emissiveScale[3] <= 0 && f->transTint[3] <= 0 && f->wrap[3] <= 0 && !f->unlit)) return true;''',
      '''    const bool uvAnim = f && (f->uvAnim[0] != 0 || f->uvAnim[1] != 0 || f->uvAnim[2] != 0);
    if (!f || (f->emissiveScale[3] <= 0 && f->transTint[3] <= 0 && f->wrap[3] <= 0 && !f->unlit && !uvAnim)) return true;
    if (uvAnim) uvAnimSeen_ = true;''')
patch(C, '''                   f->wrap[0], f->wrap[1], f->wrap[2], f->wrap[3] > 0 ? 1.0f : 0.0f, f->unlit ? 1.0f : 0.0f, 0, 0, 0};''',
      '''                   f->wrap[0], f->wrap[1], f->wrap[2], f->wrap[3] > 0 ? 1.0f : 0.0f, f->unlit ? 1.0f : 0.0f,
                   f->uvAnim[0], f->uvAnim[1], f->uvAnim[2]};''')
patch(C, '''        in.geometryChanged = rebuild;
        renderer_.drawFrame(in);''', '''        in.geometryChanged = rebuild;
        // The game's clock for animated materials (research 133 section 1); a capture shows time 0.
        if (!maskClock_.isValid()) maskClock_.start();
        renderer_.setGameTime(in.capture ? 0.0 : maskClock_.elapsed() / 1000.0);
        renderer_.drawFrame(in);
        // While a material with UV animation is drawn, the view keeps redrawing (30 a second, the editor's window active).
        if (uvAnimSeen_ && !in.capture && !uvAnimTick_ && window() && window()->isActiveWindow()) {
            uvAnimTick_ = true;
            QTimer::singleShot(33, this, [this] {
                uvAnimTick_ = false;
                update();
            });
        }''')
patch('src/qtapp/camera_view.h', '''    QElapsedTimer maskClock_;''', '''    QElapsedTimer maskClock_;
    // A stock material with UV animation was set up for drawing (research 133): the view redraws while it is active.
    bool uvAnimSeen_ = false, uvAnimTick_ = false;''')

# --- renderer: the clock and the shader
patch('src/viewport/render.h', '''    void setGameAoStrength(float s) { gameAoStrength_ = s; }''', '''    void setGameAoStrength(float s) { gameAoStrength_ = s; }
    // Seconds on the game's clock for animated materials (research 133 section 1: UV animation by constant 63).
    void setGameTime(double seconds) { gameTime_ = seconds; }''')
patch('src/viewport/render.h', '''    float gameAoStrength_ = 1.0f;''', '''    float gameAoStrength_ = 1.0f;
    double gameTime_ = 0.0;''')
R = 'src/viewport/render.cpp'
patch(R, '''                              float4 fogK[8]; };''', '''                              float4 fogK[8]; float4 gameTime; };  // gameTime.x: seconds (fmod 43200, research 133)''')
patch(R, '''            bd.ByteWidth = 336;  // game0, sun toward, sun colour, unbaked light, fog[6], sky scale, terrain cut, ao, fogK[8]''',
      '''            bd.ByteWidth = 352;  // game0, sun toward, sun colour, unbaked light, fog[6], sky scale, terrain cut, ao, fogK[8], time''')
patch(R, '''    float g[84] = {on ? 1.0f : 0.0f,''', '''    float g[88] = {on ? 1.0f : 0.0f,''')
patch(R, '''    std::memcpy(&g[52], game_.fogK, sizeof(game_.fogK));
    ctx_->UpdateSubresource(cbGame_, 0, nullptr, g, 0, 0);''', '''    std::memcpy(&g[52], game_.fogK, sizeof(game_.fogK));
    g[84] = float(std::fmod(gameTime_, 43200.0));  // the game's clock wraps (research 133 section 0)
    ctx_->UpdateSubresource(cbGame_, 0, nullptr, g, 0, 0);''')
patch(R, '''float4 ps_tex(PSIn i) : SV_TARGET {
    const float3 propLgv = propLit(i) ? i.t : float3(0, 0, 0);''', '''float4 ps_tex(PSIn i) : SV_TARGET {
    // UV animation (research 133 section 1, constant 63 in matX.yzw): turn about the texture centre, then scroll; every
    // texture of the material reads the animated UV. Stock reference batches only (the plain layer buffer is zero).
    if (batchFlags2.z > 0.5 && any(matX.yzw != 0)) {
        float sa, ca;
        sincos(gameTime.x * matX.w, sa, ca);
        const float2 d = i.uv - 0.5;
        i.uv = float2(ca * d.x - sa * d.y, sa * d.x + ca * d.y) + 0.5 + gameTime.x * matX.yz;
    }
    const float3 propLgv = propLit(i) ? i.t : float3(0, 0, 0);''')
print('ok')
