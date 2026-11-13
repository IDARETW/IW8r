import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


R = 'src/viewport/render.cpp'
patch(R, '''cbuffer Game : register(b4) { float4 game0; float4 gameSun; float4 gameSunCol; float4 gameSky; float4 gameFog[6]; float4 gameSkyScale; float4 gameTerrainCut; float4 gameAo; };''',
      '''cbuffer Game : register(b4) { float4 game0; float4 gameSun; float4 gameSunCol; float4 gameSky; float4 gameFog[6]; float4 gameSkyScale; float4 gameTerrainCut; float4 gameAo;
                              float4 fogK[8]; };  // fogK = the game's cb7[131..138] (core game_fog, research 111)''')
# The fog: the game's analytic fog (research 111, PO:39-214), a 1:1 port of core gameFogEvaluate.
s = io.open(R, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = s.index('// The stock map\'s atmosphere (GameLook::fog): distance fog with a height falloff')
b = s.index('Texture2D gameLmA : register(t6);')
new = '''// The stock map's analytic fog, the game's own (research 111: cs_post_opaque_processes PO:39-214 with the exe's
// constants cb7[131..138] in fogK, filled by core gameFogConstants; a 1:1 port of core gameFogEvaluate). The fog
// spline image is zero on the stock maps (their visions name no spline) and is left out. gameFog[0].w = on.
// `sky`: the sky's band of the fog. Owner 2026-11-13: the earlier fitted fog was too bright on mp_rust (sRGB albedos
// used as linear, and a desaturation the game does not do).
float3 gameFogApply(float3 c, float3 w, bool sky) {
    if (gameFog[0].w < 0.5) return c;
    float3 v = w - eyeStart.xyz;
    float dist = length(v);
    float3 dir = v / max(dist, 1e-3);
    float dz = dir.z * dist, skyF = 0.0, path = dist + fogK[1].w;
    if (sky) {
        dz = dir.z * fogK[0].x;
        skyF = saturate(dir.z * fogK[0].y + fogK[0].z);
        path = fogK[0].x;
    }
    float cosSun = dot(dir, fogK[1].xyz);
    float3 T, L;
    if (fogK[6].z < -0.5) {
        float sunF = saturate(cosSun * fogK[6].x + fogK[6].y);
        float tf = max(exp2(-(path * fogK[2].x)), fogK[3].w);
        float ts = max(exp2(-(path * fogK[2].y)), fogK[3].w);
        float t = saturate(sunF * (ts - tf) + tf);
        if (sky) t = 1.0 - fogK[0].w * skyF;
        T = t.xxx;
        L = (sunF * (fogK[3].rgb - fogK[4].rgb) + fogK[4].rgb) * (1.0 - t);
    } else {
        float x = saturate(path * fogK[6].w);
        float kz = dz * fogK[2].w;
        float h = abs(kz) > 1e-6 ? (1.0 - exp2(kz * -1.442695)) / kz * fogK[4].w : fogK[4].w;
        x *= h + fogK[5].w;
        T = exp2(-(x * fogK[2].xyz));
        if (sky) T = T * skyF + (1.0 - skyF);
        float phaseR = (cosSun * cosSun + 1.0) * 0.059683;
        float g = fogK[3].w;
        float m = 1.0 - g * cosSun;
        float phaseM = (1.0 - g * g) * 0.079577 / (m * m);
        L = (fogK[3].rgb * phaseM + fogK[4].rgb * phaseR) * (1.0 - T);
    }
    return c * T + L;
}
'''
s = s[:a] + new.replace('\n', nl) + s[b:]
io.open(R, 'w', encoding='utf-8', newline='').write(s)
patch(R, '''            bd.ByteWidth = 208;  // game0, sun toward, sun colour, unbaked light, fog[6], sky scale, terrain cut, ao''',
      '''            bd.ByteWidth = 336;  // game0, sun toward, sun colour, unbaked light, fog[6], sky scale, terrain cut, ao, fogK[8]''')
patch(R, '''    float g[52] = {on ? 1.0f : 0.0f, game_.lutScale,''', '''    float g[84] = {on ? 1.0f : 0.0f, game_.lutScale,''')
patch(R, '''    g[51] = hdrFrame_ ? 1.0f : 0.0f;  // the lit scene goes to the HDR target: no post chain in the shaders''',
      '''    g[51] = hdrFrame_ ? 1.0f : 0.0f;  // the lit scene goes to the HDR target: no post chain in the shaders
    std::memcpy(&g[52], game_.fogK, sizeof(game_.fogK));''')
RH = 'src/viewport/render.h'
patch(RH, '''        float fog[6][4] = {};''', '''        float fog[6][4] = {};  // only fog[0][3] (on) is read now; the rest of the fit is kept for the GUI's reports
        float fogK[8][4] = {};  // the game's fog constants cb7[131..138] (core gameFogConstants, research 111)''')

C = 'src/qtapp/camera_view.cpp'
patch(C, '''            std::memcpy(g.fog, fog, sizeof(fog));
        }''', '''            std::memcpy(g.fog, fog, sizeof(fog));
            // The game's own constants (research 111); 135.w follows the camera's height.
            const GameFogConstants k = gameFogConstants(stockFogRefdef_, float(ctx_->cam.pos.z));
            std::memcpy(g.fogK, k.c, sizeof(g.fogK));
        }''')
CH = 'src/qtapp/camera_view.h'
patch(CH, '''        stockFog_ = gameFogFromVision(visionText);''', '''        stockFog_ = gameFogFromVision(visionText);
        stockFogRefdef_ = gameFogRefdef(gameFogVisionFromText(visionText));''')
patch(CH, '''    GameFog stockFog_;''', '''    GameFog stockFog_;
    GameFogRefdef stockFogRefdef_{};  // the game's fog from the same vision (core game_fog, research 111)''')
s = io.open(CH, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
i = s.index('#include "')
s = s[:i] + '#include "game_fog.h"' + nl + s[i:]
io.open(CH, 'w', encoding='utf-8', newline='').write(s)
print('ok')
