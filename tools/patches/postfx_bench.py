import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


RH = 'src/viewport/render.h'
R = 'src/viewport/render.cpp'
CH = 'src/qtapp/camera_view.h'
M = 'src/qtapp/main.cpp'

patch(RH, '''    void setPostFxEnabled(bool on) { postFxEnabled_ = on; }''', '''    void setPostFxEnabled(bool on) { postFxEnabled_ = on; }
    bool veilDrawn() const { return veilDrawn_; }  // the last post pass mixed the veil (bench)
    float gameVeil() const { return game_.veil; }''')
patch(RH, '''    bool postFxEnabled_ = true;''', '''    bool postFxEnabled_ = true;
    bool veilDrawn_ = false;''')
patch(R, '''    const bool veil = drawVeil();''', '''    const bool veil = drawVeil();
    veilDrawn_ = veil;''')
patch(CH, '''    void setPostFxEnabled(bool on) { renderer_.setPostFxEnabled(on); update(); }''', '''    void setPostFxEnabled(bool on) { renderer_.setPostFxEnabled(on); update(); }
    // Bench: the stock vision's veil and vignette intensity, the renderer's veil and whether it was drawn.
    QString postFxReport() const {
        return QString("veil %1 vignette %2 renderer_veil %3 drawn %4")
            .arg(stockPostFx_.veil).arg(stockPostFx_.vignetteIntensity).arg(renderer_.gameVeil()).arg(renderer_.veilDrawn() ? 1 : 0);
    }''')
patch(M, '''                report << QString("stock_lights %1 in the map, %2 drawn, %3 shadow maps").arg(cam->stockLocalLightCount()).arg(cam->stockLocalLightsDrawn())
                              .arg(cam->stockLightShadowsDrawn());''', '''                report << QString("stock_lights %1 in the map, %2 drawn, %3 shadow maps").arg(cam->stockLocalLightCount()).arg(cam->stockLocalLightsDrawn())
                              .arg(cam->stockLightShadowsDrawn());
                report << "post_fx " + cam->postFxReport();''')
print('ok')
