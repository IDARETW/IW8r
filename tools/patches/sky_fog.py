import io
def patch(p, pairs):
    s=io.open(p,encoding='utf-8',newline='').read(); nl='\r\n' if '\r\n' in s else '\n'
    for a,b in pairs:
        a=a.replace('\n',nl); b=b.replace('\n',nl); assert s.count(a)==1,(p,a[:80]); s=s.replace(a,b)
    io.open(p,'w',encoding='utf-8',newline='').write(s)
patch('src/core/game_look.h', [
 ('''    double mieAnisotropy = 0.4, mieBlend = 0;''', '''    double mieAnisotropy = 0.4, mieBlend = 0;
    // The sky's fog: brightness (fogSkyIntensity scales the inscattered light seen on the sky; Hideout 10, Speedball
    // 0.5) and the band of zenith angles it covers (fogSkyMinAngle .. fogSkyMaxAngle, degrees from straight up: none at
    // the min, full at the max; Hideout 59 .. 100.8 keeps its haze to the horizon).
    double skyMinAngle = -90, skyMaxAngle = 90;'''),
])
patch('src/core/game_look.cpp', [
 ('''    num("NORQKLTPTR", f.skyIntensity);                             // fogSkyIntensity''', '''    num("NORQKLTPTR", f.skyIntensity);                             // fogSkyIntensity
    num("LOROLOTNMP", f.skyMinAngle);                              // fogSkyMinAngle
    num("SPQNRNKQR", f.skyMaxAngle);                               // fogSkyMaxAngle
    if (f.skyMaxAngle <= f.skyMinAngle) f.skyMaxAngle = f.skyMinAngle + 1;'''),
])
patch('src/qtapp/camera_view.cpp', [
 ('''                                     {float(f.maxDistance), 0, 0, 0}};''', '''                                     {float(f.maxDistance), float(f.skyMinAngle), float(f.skyMaxAngle), 0}};'''),
])
patch('src/viewport/render.cpp', [
 ('''    float amount = min(1.0 - exp2(-dd / gameFog[0].y * max(hf, 0.0)), gameFog[0].z);
    if (sky) amount *= gameFog[4].w;
    float g = gameFog[2].w, mu = dot(dir, gameSun.xyz);
    float hg = (1.0 - g * g) / pow(max(1.0 + g * g - 2.0 * g * mu, 1e-4), 1.5);
    float3 inscatter = gameFog[1].rgb * lerp(gameFog[2].rgb, gameFog[3].rgb * hg, gameFog[1].w);''',
  '''    float amount = min(1.0 - exp2(-dd / gameFog[0].y * max(hf, 0.0)), gameFog[0].z);
    float g = gameFog[2].w, mu = dot(dir, gameSun.xyz);
    float hg = (1.0 - g * g) / pow(max(1.0 + g * g - 2.0 * g * mu, 1e-4), 1.5);
    float3 inscatter = gameFog[1].rgb * lerp(gameFog[2].rgb, gameFog[3].rgb * hg, gameFog[1].w);
    if (sky) {
        // The sky's haze: brighter or dimmer by fogSkyIntensity, over the band of zenith angles fogSkyMin/MaxAngle.
        float zenith = degrees(acos(clamp(dir.z, -1.0, 1.0)));
        amount *= saturate((zenith - gameFog[5].y) / max(gameFog[5].z - gameFog[5].y, 1e-3));
        inscatter *= gameFog[4].w;
    }
    amount = saturate(amount);'''),
])
print('ok')
