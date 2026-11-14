import io
import sys

# Converter: the game's fog and volumetric air on see-through surfaces (IW8 Radiant research 127 part B). The game's
# fog pass fogs opaque pixels only; its own blended and additive shaders fog themselves from cb7[131..137] and read the
# volumetric extinction / scatter images. The generated surface shader does the same under MAP_GAME_FOG, which the
# converter defines when the map's vision turns the game's fog or the volumetric air on, or the map has volumetric
# boxes.
# Argument: the converter checkout (default the IW8 Radiant worktree).

ROOT = (sys.argv[1] if len(sys.argv) > 1 else 'C:/zsw-iw8r/scriptables').rstrip('/') + '/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


H = 'src/convert/iw3_iw8/resources/map_surface_realtime.hlsl'
patch(H, '''#if defined(MAP_VOLUMETRIC_EXTINCTION) && MAP_LIGHTING_ROWS < 189''',
      '''#if (defined(MAP_VOLUMETRIC_EXTINCTION) || defined(MAP_GAME_FOG)) && MAP_LIGHTING_ROWS < 189''')
patch(H, '''#if defined(MAP_VOLUMETRIC_EXTINCTION) && !defined(MAP_COVERAGE)
// Replay's volumetric extinction (code texture 26: t111) before a pixel''',
      '''#if defined(MAP_GAME_FOG) && !defined(MAP_COVERAGE)
// Replay's analytic fog for a see-through pixel, as its own blended world shaders compute it (PS 077107f8 lines
// 1612-1758; IW8 Radiant research 111 sections 4-5 and 127 part B): no sky branch, no Warzone circle, no fog spline.
// T is the transmittance, L the in-scatter in this shader's light units. With the game's fog off (the plain branch at
// opacity 0) it gives T = 1, L = 0.
void GameFogTranslucent(float3 relativePosition, out float3 T, out float3 L) {
    float len = length(relativePosition);
    float3 dir = relativePosition / max(len, 1e-6);
    float path = len + replayLighting[132].w;
    float cosS = dot(dir, replayLighting[132].xyz);
    if (replayLighting[137].z < -0.5) {
        float sunF = saturate(cosS * replayLighting[137].x + replayLighting[137].y);
        float2 t = max(exp2(-path * replayLighting[133].xy), replayLighting[134].w);
        float tt = saturate(lerp(t.x, t.y, sunF));
        T = tt;
        L = lerp(replayLighting[135].xyz, replayLighting[134].xyz, sunF) * (1 - tt);
    } else {
        float x = saturate(path * replayLighting[137].w);
        float k = relativePosition.z * replayLighting[133].w;
        float H = abs(k) > 1e-6 ? (1 - exp2(-k * 1.442695)) / k * replayLighting[135].w : replayLighting[135].w;
        H += replayLighting[136].w;
        T = exp2(-x * H * replayLighting[133].xyz);
        float g = replayLighting[134].w;
        float m = 1 - g * cosS;
        float pR = (1 + cosS * cosS) * 0.059683;
        float pM = (1 - g * g) * 0.079577 / (m * m);
        L = (replayLighting[134].xyz * pM + replayLighting[135].xyz * pR) * (1 - T);
    }
    L /= MAP_LIGHT_UNIT_SCALE;
}
#endif
#if (defined(MAP_VOLUMETRIC_EXTINCTION) || defined(MAP_GAME_FOG)) && !defined(MAP_COVERAGE)
// Replay's volumetric extinction (code texture 26: t111) before a pixel''')
patch(H, '''#if defined(MAP_VOLUMETRIC_EXTINCTION) && !defined(MAP_COVERAGE) && MAP_DEBUG_MODE == 0
    // IW7's blended and additive surfaces that read its volumetric extinction (row 2.w): Replay's volumetric pass covers''',
      '''#if defined(MAP_GAME_FOG) && !defined(MAP_COVERAGE) && MAP_DEBUG_MODE == 0
    // The game's own fog and volumetric air (IW8 Radiant iw8r_haze / volumetric boxes): Replay's fog pass and its
    // volumetric composite cover opaque pixels only, so every see-through surface (blended materials, additive and
    // multiply layers) fogs itself and takes the air before it, as Replay's own blended shaders do.
    float3 gameFogT = 1, gameFogL = 0;
    float gameAirT = 1;
    float3 gameAirS = 0;
    if ((kind == 2 || (flags & 768) != 0) && !skyPlaced)
    {
        GameFogTranslucent(input.relativePosition, gameFogT, gameFogL);
        float gameSlice = VolumetricSlice(input.position);
        gameAirT = VolumetricExtinction(screenUV, gameSlice);
        if (kind == 2)
            gameAirS = VolumetricScatter(screenUV, gameSlice) * replayLighting[42].y / MAP_LIGHT_UNIT_SCALE;
    }
#elif defined(MAP_VOLUMETRIC_EXTINCTION) && !defined(MAP_COVERAGE) && MAP_DEBUG_MODE == 0
    // IW7's blended and additive surfaces that read its volumetric extinction (row 2.w): Replay's volumetric pass covers''')
patch(H, '''    if ((flags & 512) != 0)
        return float4(lerp(1, texel.rgb, input.color.a * fogFactor), 1);''',
      '''#if defined(MAP_GAME_FOG) && !defined(MAP_COVERAGE) && MAP_DEBUG_MODE == 0
    // (Under the game's fog a multiply layer fades to neutral by the fog and the air before it.)
    if ((flags & 512) != 0)
        return float4(lerp(1, texel.rgb, input.color.a * fogFactor * dot(gameFogT, 1.0 / 3) * gameAirT), 1);
#endif
    if ((flags & 512) != 0)
        return float4(lerp(1, texel.rgb, input.color.a * fogFactor), 1);''')
patch(H, '''#if defined(MAP_VOLUMETRIC_EXTINCTION) && !defined(MAP_COVERAGE) && MAP_DEBUG_MODE == 0
    color *= (flags & 256) != 0 ? volumetricTransmittance : 1;
#endif''', '''#if defined(MAP_GAME_FOG) && !defined(MAP_COVERAGE) && MAP_DEBUG_MODE == 0
    // (An additive layer only dims: no in-scatter of its own.)
    if ((flags & 256) != 0)
        color *= gameFogT * gameAirT;
#elif defined(MAP_VOLUMETRIC_EXTINCTION) && !defined(MAP_COVERAGE) && MAP_DEBUG_MODE == 0
    color *= (flags & 256) != 0 ? volumetricTransmittance : 1;
#endif''')
patch(H, '''        color = color * alpha + reflection * fogFactor;
#if defined(MAP_VOLUMETRIC_EXTINCTION) && !defined(MAP_COVERAGE) && MAP_DEBUG_MODE == 0
    color = color * volumetricTransmittance + volumetricInScatter * alpha;
#endif''', '''        color = color * alpha + reflection * fogFactor;
#if defined(MAP_GAME_FOG) && !defined(MAP_COVERAGE) && MAP_DEBUG_MODE == 0
    // out = (C T + a L) Tv + a Sv (PS 077107f8 lines 1891-1997); an opaque surface has T = Tv = 1 and L = Sv = 0.
    color = color * (gameFogT * gameAirT) + alpha * (gameFogL * gameAirT + gameAirS);
#elif defined(MAP_VOLUMETRIC_EXTINCTION) && !defined(MAP_COVERAGE) && MAP_DEBUG_MODE == 0
    color = color * volumetricTransmittance + volumetricInScatter * alpha;
#endif''')

F = 'src/convert/iw3_iw8/iw3_fastfile.cpp'
patch(F, '''    bool gameFog = false;
''', '''    bool gameFog = false;
    // ... or the volumetric air (volumetricAirDensity above 0), or the map has volumetric boxes: see-through surfaces
    // then fog themselves and take the air before them in the surface shader (MAP_GAME_FOG).
    bool gameAir = world.contains("volumetrics") && world.at("volumetrics").is_array() && !world.at("volumetrics").empty();
''')
patch(F, '''                if (gameFog)
                {
                    zt::info("iw3: the map's vision turns the game's atmospheric fog on: no fog is baked into the surface shader");''',
      '''                for (const char *key : {"volumetricAirDensity", "NSKNLMKQSR"})
                    if (const auto value = visionOverrides.find(key); value != visionOverrides.end())
                        gameAir = gameAir || (value->is_number()   ? value->get<float>() > 0.0f
                                              : value->is_string() ? std::atof(value->get<std::string>().c_str()) > 0.0
                                                                   : false);
                if (gameFog)
                {
                    zt::info("iw3: the map's vision turns the game's atmospheric fog on: no fog is baked into the surface shader");''')
patch(F, '''                            FogShaderDefines(gameFog ? std::optional<ExpFog>{} : expFog, options.style,
                                             ReadMapSun(exportRoot, sourceMap), lightingMatch.lightUnitScale,
                                             gameFog ? std::optional<NativeFog>{} : nativeFog),''',
      '''                            (gameFog || gameAir ? std::string("#define MAP_GAME_FOG 1\\n") : std::string()) +
                                FogShaderDefines(gameFog ? std::optional<ExpFog>{} : expFog, options.style,
                                                 ReadMapSun(exportRoot, sourceMap), lightingMatch.lightUnitScale,
                                                 gameFog ? std::optional<NativeFog>{} : nativeFog),''')
print('ok')
