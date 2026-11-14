import io

# Plan 1.8, the open point "white balance": the game's white balance matrix (research 120, core white_balance) in the
# Game Lighting post pass, where the game applies it: after the veil and the vignette, before the exposure and the grade.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, n=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


# ---- core: the vision's post effects carry it
patch('src/core/game_look.h', '''    double volHeightBase = 0, volHeightHalf = 0;
};
GamePostFx gamePostFxFromVision(const std::string& visionText);''', '''    double volHeightBase = 0, volHeightHalf = 0;
    GameWhiteBalance whiteBalance;  // research 120 (core white_balance.h); the defaults change nothing
};
GamePostFx gamePostFxFromVision(const std::string& visionText);''')
s = io.open(ROOT + 'src/core/game_look.h', encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
assert '#include "white_balance.h"' not in s
i = s.index('#include <')
s = s[:i] + '#include "white_balance.h"' + nl + nl + s[i:]
io.open(ROOT + 'src/core/game_look.h', 'w', encoding='utf-8', newline='').write(s)
patch('src/core/game_look.cpp', '''    p.grain = grain * 0.25;  // r_filmGrainAtten''', '''    p.grain = grain * 0.25;  // r_filmGrainAtten
    p.whiteBalance = gameWhiteBalanceFromVision(text);''')

# ---- viewport
R = 'src/viewport/render.h'
patch(R, '''        float grain = 0;  // film grain strength (the vision's tonemapGrainStrength x 0.25; research 109 section 4.2)
    };''', '''        float grain = 0;  // film grain strength (the vision's tonemapGrainStrength x 0.25; research 109 section 4.2)
        // The map's white balance (research 120, core gameWhiteBalanceMatrix): out = M in, row-major, on the linear
        // colour before the exposure. whiteBalanceOn false = none.
        bool whiteBalanceOn = false;
        float whiteBalance[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    };''')
patch(R, '''    void setFilmGrainEnabled(bool on) { filmGrainEnabled_ = on; }''', '''    void setFilmGrainEnabled(bool on) { filmGrainEnabled_ = on; }
    void setWhiteBalanceEnabled(bool on) { whiteBalanceEnabled_ = on; }
    bool whiteBalanceDrawn() const { return whiteBalanceDrawn_; }  // the last frame's post pass applied a matrix''')
patch(R, '''    bool filmGrainEnabled_ = true;''', '''    bool filmGrainEnabled_ = true;
    bool whiteBalanceEnabled_ = true, whiteBalanceDrawn_ = false;''')
P = 'src/viewport/render.cpp'
patch(P, '''// veilDst = (dst w, dst h, 1 / w, 1 / h), veilW = (wIn, wOut); postFx0 = (veil, screen w, screen h, film grain); postVig0..2 = the
// vignette (core gameVignetteConstants).''', '''// veilDst = (dst w, dst h, 1 / w, 1 / h), veilW = (wIn, wOut); postFx0 = (veil, screen w, screen h, film grain); postVig0..2 = the
// vignette (core gameVignetteConstants); postWb0..2 = the white balance matrix's rows (research 120), postWb0.w > 0.5 = on.''')
patch(P, '''cbuffer Veil : register(b12) { float4 veilDst; float4 veilW; float4 postFx0; float4 postVig0; float4 postVig1; float4 postVig2; };''',
      '''cbuffer Veil : register(b12) {
    float4 veilDst; float4 veilW; float4 postFx0; float4 postVig0; float4 postVig1; float4 postVig2;
    float4 postWb0; float4 postWb1; float4 postWb2;
};''')
patch(P, '''        c *= 1.0 - (t + 0.6 * (t * t * (3.0 - 2.0 * t) - t)) * postVig0.z;
    }
    float3 shown = gamePost(c);''', '''        c *= 1.0 - (t + 0.6 * (t * t * (3.0 - 2.0 * t) - t)) * postVig0.z;
    }
    // The white balance (research 120): the map's 3x3 matrix on the linear colour, after the vignette and before the
    // exposure (cs_tonemap CB0[0..2]). Kept at 0 or above for the log shaper.
    if (postWb0.w > 0.5) c = max(float3(dot(postWb0.xyz, c), dot(postWb1.xyz, c), dot(postWb2.xyz, c)), 0.0);
    float3 shown = gamePost(c);''')
patch(P, '''        float cb[24] = {};
        cb[8] = veil ? game_.veil : 0.0f, cb[9] = float(w_), cb[10] = float(h_);
        cb[11] = postFxEnabled_ && filmGrainEnabled_ ? game_.grain : 0.0f;
        if (postFxEnabled_) std::memcpy(cb + 12, game_.vignette, sizeof(game_.vignette));''', '''        float cb[36] = {};
        cb[8] = veil ? game_.veil : 0.0f, cb[9] = float(w_), cb[10] = float(h_);
        cb[11] = postFxEnabled_ && filmGrainEnabled_ ? game_.grain : 0.0f;
        if (postFxEnabled_) std::memcpy(cb + 12, game_.vignette, sizeof(game_.vignette));
        whiteBalanceDrawn_ = whiteBalanceEnabled_ && game_.whiteBalanceOn;
        for (int r = 0; r < 3; ++r)
            for (int k = 0; k < 3; ++k) cb[24 + r * 4 + k] = whiteBalanceDrawn_ ? game_.whiteBalance[r * 3 + k] : (r == k ? 1.0f : 0.0f);
        cb[27] = whiteBalanceDrawn_ ? 1.0f : 0.0f;''')

# ---- editor
patch('src/qtapp/camera_view.h', '''    void setFilmGrainEnabled(bool on) { renderer_.setFilmGrainEnabled(on); update(); }''',
      '''    void setFilmGrainEnabled(bool on) { renderer_.setFilmGrainEnabled(on); update(); }
    void setWhiteBalanceEnabled(bool on) { renderer_.setWhiteBalanceEnabled(on); update(); }
    bool whiteBalanceDrawn() const { return renderer_.whiteBalanceDrawn(); }
    const GamePostFx& stockPostFx() const { return stockPostFx_; }''')
patch('src/qtapp/camera_view.cpp', '''            g.veil = float(stockPostFx_.veil);
            g.grain = float(stockPostFx_.grain);''', '''            g.veil = float(stockPostFx_.veil);
            g.grain = float(stockPostFx_.grain);
            if (gameWhiteBalanceActive(stockPostFx_.whiteBalance)) {  // research 120
                double wbm[9];
                gameWhiteBalanceMatrix(stockPostFx_.whiteBalance, wbm);
                g.whiteBalanceOn = true;
                for (int k = 0; k < 9; ++k) g.whiteBalance[k] = float(wbm[k]);
            }''')
patch('src/qtapp/main_window.cpp', '''        toggle("GameFilmGrain", "Film Grain", "game/filmGrain", [this](bool on) { camera_->setFilmGrainEnabled(on); });''',
      '''        toggle("GameFilmGrain", "Film Grain", "game/filmGrain", [this](bool on) { camera_->setFilmGrainEnabled(on); });
        toggle("GameWhiteBalance", "White Balance", "game/whiteBalance", [this](bool on) { camera_->setWhiteBalanceEnabled(on); });''')
patch('src/qtapp/qt_gui_test.cpp', '''                                                               {"GameFilmGrain", "game/filmGrain"},''',
      '''                                                               {"GameFilmGrain", "game/filmGrain"},
                                                               {"GameWhiteBalance", "game/whiteBalance"},''')
print('ok')
