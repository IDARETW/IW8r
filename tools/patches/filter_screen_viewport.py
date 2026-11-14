import io
# Viewport: a map shader's iw8rFilterScreen on the stock reference (the same ink lines the build splices into a stock
# map's shaders): the filter variant of the textured shader calls it with the pixel, the view depth and the pixel
# size, and the scene depth is kept for it as for a surface shader.
ROOT = 'E:/IW8-Radiant/'
def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))
# (core custom_shader.cpp: customShaderFilterViewportSource defines IW8R_FILTER_SCREEN; edited by hand.)
R = 'src/viewport/render.cpp'
patch(R, '''            const float pre = max(game0.y, 1e-9);
            c = iw8rFilter(c * pre) / pre;''', '''            const float pre = max(game0.y, 1e-9);
#ifdef IW8R_FILTER_SCREEN
            // With the screen (iw8rFilterScreen: the pixel, its view depth, a pixel's size per unit of depth).
            const float filterDepth = mul(float4(i.w, 1.0), viewProj).w;
            const float3 filterRay = (i.w - eyeStart.xyz) / max(filterDepth, 1e-3);
            c = iw8rFilterScreen(c * pre, i.pos.xy, filterDepth, max(length(ddx(filterRay)), length(ddy(filterRay)))) / pre;
#else
            c = iw8rFilter(c * pre) / pre;
#endif''')
patch(R, '''    safeRelease(filterPs_);
    filterPs_ = ps;
    return true;''', '''    safeRelease(filterPs_);
    filterPs_ = ps;
    filterScreen_ = source.find("#define IW8R_FILTER_SCREEN") != std::string::npos;
    return true;''')
patch(R, '''    if (source.empty()) {
        safeRelease(filterPs_);
        return true;''', '''    if (source.empty()) {
        safeRelease(filterPs_);
        filterScreen_ = false;
        return true;''')
patch(R, '''    if (customPs_.empty() || !sceneZRtv_ || !sceneZPs_''', '''    if (!sceneDepthWanted() || !sceneZRtv_ || !sceneZPs_''')
patch(R, '''        ID3D11ShaderResourceView* sceneZ = hdrFrame_ && sceneZValid_ && !customPs_.empty() ? sceneZSrv_ : nullptr;''',
      '''        ID3D11ShaderResourceView* sceneZ = hdrFrame_ && sceneZValid_ && sceneDepthWanted() ? sceneZSrv_ : nullptr;''')
H = 'src/viewport/render.h'
patch(H, '''    bool sceneDepthStale() const { return sceneZStale_ && !customPs_.empty(); }''', '''    bool sceneDepthStale() const { return sceneZStale_ && sceneDepthWanted(); }
    // A shader that may read the scene depth is in use: a map's surface shader, or a filter with iw8rFilterScreen.
    bool sceneDepthWanted() const { return !customPs_.empty() || (filterPs_ && filterScreen_); }''')
patch(H, '''    ID3D11PixelShader* filterPs_ = nullptr;  // setReferenceFilterShader''', '''    ID3D11PixelShader* filterPs_ = nullptr;  // setReferenceFilterShader
    bool filterScreen_ = false;              // ... and it has iw8rFilterScreen (reads the scene depth)''')
print('ok')
