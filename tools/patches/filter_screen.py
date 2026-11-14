import io

# The stock-surface filter that reads the screen: float3 iw8rFilterScreen(float3 colour, float2 pixel, float viewDepth,
# float pixelSize), optional beside iw8rFilter. With it a map shader can draw on a stock map's world and props what
# needs the scene depth: the toon shader's ink lines. Core custom_shader (the program the build splices), the toon
# shader (its ink code now takes plain screen inputs, shared by both functions), the viewport's filter branch.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


H = 'src/core/custom_shader.h'
patch(H, '''bool customShaderHasSurface(const std::string& code);
bool customShaderHasFilter(const std::string& code);''', '''//   float3 iw8rFilterScreen(float3 colour, float2 pixel, float viewDepth, float pixelSize)
//                                       optional, beside iw8rFilter: the same filter with the screen. `pixel` and
//                                       `viewDepth` as IW8RSurface's, `pixelSize` a pixel's size per unit of depth
//                                       (the build has no view constants in a stock shader: it takes a 90 degree
//                                       view over the depth image's width). It may call iw8rSceneDepth (Load only)
//                                       and use screen derivatives; no other texture, no constant tables. The build
//                                       uses it where a stock shader has the screen position and a slot for the
//                                       scene depth, iw8rFilter elsewhere.
bool customShaderHasSurface(const std::string& code);
bool customShaderHasFilter(const std::string& code);
bool customShaderHasScreenFilter(const std::string& code);''')
patch(H, '''std::string customShaderFilterProgramSource(const CustomShader& shader);''', '''std::string customShaderFilterProgramSource(const CustomShader& shader);
// The same for iw8rFilterScreen: input 1 is SV_Position and the texture t0 the game's scene depth (the build splices
// it with core dxbc_splice's screen mode). "" when the shader has no iw8rFilterScreen.
std::string customShaderScreenFilterProgramSource(const CustomShader& shader);''')
C = 'src/core/custom_shader.cpp'
patch(C, '''std::string customShaderFilterProgramSource(const CustomShader& shader) {''', '''bool customShaderHasScreenFilter(const std::string& code) {
    static const std::regex re(R"(float3\\s+iw8rFilterScreen\\s*\\(\\s*float3\\s+\\w+\\s*,\\s*float2\\s+\\w+\\s*,\\s*float\\s+\\w+\\s*,\\s*float\\s+\\w+\\s*\\))");
    return std::regex_search(withoutComments(code), re);
}

std::string customShaderScreenFilterProgramSource(const CustomShader& shader) {
    if (!customShaderHasScreenFilter(shader.code)) return {};
    // The game's scene depth as the converter's surface shader reads it (research 104: depth / 6 in a 16-bit float,
    // negative on viewmodel pixels, 65500 and up for the sky). A stock lit pass draws at the depth image's own size.
    return std::string("Texture2D<float4> iw8rSceneZ : register(t0);\\n"
                       "static const float iw8rSceneDepthStep = 1.0 / 1024.0;\\n"
                       "float iw8rSceneDepth(float2 pixel) {\\n"
                       "    if (pixel.x < 0 || pixel.y < 0) return 0;\\n"
                       "    const float depth = abs(iw8rSceneZ.Load(int3(int2(pixel), 0)).x) * 6.0;\\n"
                       "    return depth >= 392999.0 ? 1e9 : depth;\\n"
                       "}\\n") +
           customShaderPreamble() + "#line 1 \\"" + shader.name + ".iw8shader.hlsl\\"\\n" + shader.code + "\\n#line 1 \\"filter\\"\\nfloat4 " +
           kIw8rFilterEntry +
           "(float4 colour : TEXCOORD0, float4 position : SV_Position) : SV_Target {\\n"
           "    uint w, h;\\n"
           "    iw8rSceneZ.GetDimensions(w, h);\\n"
           "    return float4(iw8rFilterScreen(colour.rgb, position.xy, position.w, 2.0 / float(max(w, 1u))), colour.a);\\n"
           "}\\n";
}

std::string customShaderFilterProgramSource(const CustomShader& shader) {''')

# ---- the toon shader
P = 'data/shaders/toon.iw8shader.hlsl'
s = io.open(ROOT + P, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
s = s.replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (s.count(a), a[:70])
    s = s.replace(a, b)


rep('''float toonInkScene(IW8RSurface s, float width, out bool usable)
{
    const float invMine = 1.0 / max(s.viewDepth, 1e-3);
    const float2 slope = float2(ddx(invMine), ddy(invMine));
    const float3 ray = (s.position - s.eye) * invMine;  // from the eye, per unit of depth: the same step every pixel
    const float pixelSize = max(length(ddx(ray)), length(ddy(ray)));  // a pixel's size, per unit of depth

    const float here = iw8rSceneDepth(s.pixel);''', '''//
// `pixel`, `viewDepth`: this pixel on screen and its depth along the view; `pixelSize`: a pixel's size per unit of
// depth. Nothing else of the surface is needed, so a stock map's surfaces can be inked the same way (iw8rFilterScreen).
float toonInkScene(float2 pixel, float viewDepth, float pixelSize, float width, out bool usable)
{
    const float invMine = 1.0 / max(viewDepth, 1e-3);
    const float2 slope = float2(ddx(invMine), ddy(invMine));

    const float here = iw8rSceneDepth(pixel);''')
rep('''            const float depth = iw8rSceneDepth(s.pixel + o);''', '''            const float depth = iw8rSceneDepth(pixel + o);''')
rep('''    const float edges = toonInkEdges(s);
    bool usable;
    const float scene = toonInkScene(s, width, usable);
    return usable ? scene * strength : edges;
}
''', '''    const float edges = toonInkEdges(s);
    bool usable;
    const float scene = toonInkScene(s.pixel, s.viewDepth, unitsPerPixel / max(s.viewDepth, 1e-3), width, usable);
    return usable ? scene * strength : edges;
}
''')
rep('''// Two functions: iw8rSurface lights a map's own surfaces (all of the above). iw8rFilter, at the end, is the toon look
// for surfaces that keep the game's own lighting: a stock map's world and props (File > Rebuild Stock Zone builds it
// into the map's lit shaders). It sees only the finished colour, so it gives the vivid colour and the light in steps;
// the ink lines need the edge inputs, which stock geometry does not have.
''', '''// Three functions: iw8rSurface lights a map's own surfaces (all of the above). iw8rFilter and iw8rFilterScreen, at the
// end, are the toon look for surfaces that keep the game's own lighting: a stock map's world and props (File >
// Rebuild Stock Zone builds them into the map's lit shaders). They see only the finished colour, so they give the
// vivid colour and the light in steps; iw8rFilterScreen also has the screen, and draws the same ink lines from the
// scene's depth (the build uses it in every stock shader that can take it, iw8rFilter in the rest).
''')
s = s.rstrip('\n') + '''

// The same with the screen: the ink lines over the filtered colour.
float3 iw8rFilterScreen(float3 colour, float2 pixel, float viewDepth, float pixelSize)
{
    const float3 c = iw8rFilter(colour);
    const float wanted = kLineMaxUnits / max(pixelSize * viewDepth, 1e-6);
    const float width = clamp(wanted, kLineMinPixels, kLinePixels);
    const float strength = lerp(kLineFarFade, 1.0, saturate((wanted - kLineMinPixels * 0.5) / kLineMinPixels));
    bool usable;
    const float ink = toonInkScene(pixel, viewDepth, pixelSize, width, usable) * strength;
    return usable ? lerp(c, c * kInk * kInkTint, ink) : c;
}
'''
io.open(ROOT + P, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))
print('ok')
