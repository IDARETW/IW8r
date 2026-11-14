import io

# Plan 1.8, film grain (research 109 section 4.2, VERIFIED: the filmgrain pixel shader): multiplicative white noise on
# the display colour, damped toward white, strength = the vision's tonemapGrainStrength x 0.25 (already read into
# GamePostFx::grain, not drawn so far). After gamePost in ps_post, with a fixed time value (the game animates the
# noise; a still picture does not). View > Game Lighting Options > Film Grain, on by default as in the game.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, n=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('src/viewport/render.h', '''        float vignette[12] = {};
    };''', '''        float vignette[12] = {};
        float grain = 0;  // film grain strength (the vision's tonemapGrainStrength x 0.25; research 109 section 4.2)
    };''')
patch('src/viewport/render.h', '''    void setPostFxEnabled(bool on) { postFxEnabled_ = on; }''', '''    void setPostFxEnabled(bool on) { postFxEnabled_ = on; }
    void setFilmGrainEnabled(bool on) { filmGrainEnabled_ = on; }''')
patch('src/viewport/render.h', '''    bool postFxEnabled_ = true;''', '''    bool postFxEnabled_ = true;
    bool filmGrainEnabled_ = true;''')
R = 'src/viewport/render.cpp'
patch(R, '''// veilDst = (dst w, dst h, 1 / w, 1 / h), veilW = (wIn, wOut); postFx0 = (veil, screen w, screen h); postVig0..2 = the''',
      '''// veilDst = (dst w, dst h, 1 / w, 1 / h), veilW = (wIn, wOut); postFx0 = (veil, screen w, screen h, film grain); postVig0..2 = the''')
patch(R, '''        c *= 1.0 - (t + 0.6 * (t * t * (3.0 - 2.0 * t) - t)) * postVig0.z;
    }
    return float4(gamePost(c), 1.0);''', '''        c *= 1.0 - (t + 0.6 * (t * t * (3.0 - 2.0 * t) - t)) * postVig0.z;
    }
    float3 shown = gamePost(c);
    if (postFx0.w > 0.0) {
        // Film grain (research 109 section 4.2: the game's filmgrain shader): white noise times the display colour,
        // none on black, fading toward white. The game moves the noise with time; here it stands still.
        const float T = 0.7310586;
        const float ga = frac(suv.y * T) * 1024.0 - 512.0, gb = frac(suv.x * T) * 1024.0 - 512.0;
        const float pp = gb * frac(gb * 0.554550) + ga, qq = ga * frac(ga * 0.308517) + gb;
        const float noise = frac(pp * qq) * 2.0 - 1.0;
        const float med = max(min(max(shown.r, shown.g), shown.b), min(shown.r, shown.g));
        const float k = med + 0.1 * saturate(1.0 - med);
        shown += shown * noise * max(1.0 - k * k, 0.0) * postFx0.w;
    }
    return float4(shown, 1.0);''')
patch(R, '''        cb[8] = veil ? game_.veil : 0.0f, cb[9] = float(w_), cb[10] = float(h_);''',
      '''        cb[8] = veil ? game_.veil : 0.0f, cb[9] = float(w_), cb[10] = float(h_);
        cb[11] = postFxEnabled_ && filmGrainEnabled_ ? game_.grain : 0.0f;''')
patch('src/qtapp/camera_view.h', '''    void setPostFxEnabled(bool on) { renderer_.setPostFxEnabled(on); update(); }''',
      '''    void setPostFxEnabled(bool on) { renderer_.setPostFxEnabled(on); update(); }
    void setFilmGrainEnabled(bool on) { renderer_.setFilmGrainEnabled(on); update(); }''')
patch('src/qtapp/camera_view.cpp', '''            g.veil = float(stockPostFx_.veil);''', '''            g.veil = float(stockPostFx_.veil);
            g.grain = float(stockPostFx_.grain);''')
patch('src/qtapp/main_window.cpp', '''        toggle("GamePostFx", "Bloom and Vignette", "game/postFx", [this](bool on) { camera_->setPostFxEnabled(on); });''',
      '''        toggle("GamePostFx", "Bloom and Vignette", "game/postFx", [this](bool on) { camera_->setPostFxEnabled(on); });
        toggle("GameFilmGrain", "Film Grain", "game/filmGrain", [this](bool on) { camera_->setFilmGrainEnabled(on); });''')
patch('src/qtapp/qt_gui_test.cpp', '''                                                               {"GamePostFx", "game/postFx"},''',
      '''                                                               {"GamePostFx", "game/postFx"},
                                                               {"GameFilmGrain", "game/filmGrain"},''')
print('ok')
