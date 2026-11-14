import io

# x64-zt converter (C:\zsw-iw8r\scriptables): a map's surface shader from the editor (world JSON
# "iw8r_surface_shader": the IW8RSurface struct + the map's iw8rSurface function), compiled into the map's own
# surface pixel shaders (IW8 Radiant plan M6, research 114).


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


ROOT = 'C:/zsw-iw8r/scriptables/src/convert/iw3_iw8/'
H = ROOT + 'resources/map_surface_realtime.hlsl'
patch(H, '''    float3 specular = 0;
    float3 color = texel.rgb * light;''', '''    float3 specular = 0;
    float3 color = texel.rgb * light;
#ifdef IW8R_CUSTOM
    // The map's surface shader (IW8 Radiant "iw8r_surface_shader"): its lit colour for every lit surface, from the same
    // inputs the editor's viewport gives it (IW8RSurface). Sky, layers and unlit materials keep theirs.
    if (!unlit && !sky && !layer) {
        IW8RSurface iw8rSurfaceIn;
        iw8rSurfaceIn.albedo = texel.rgb;
        iw8rSurfaceIn.alpha = 1;
        iw8rSurfaceIn.normal = shadingNormal;
        iw8rSurfaceIn.view = normalize(-input.relativePosition);
        iw8rSurfaceIn.position = input.relativePosition + replayView[4].xyz;
        iw8rSurfaceIn.uv = input.uv.xy;
        iw8rSurfaceIn.sunDir = direction;
        iw8rSurfaceIn.sunColor = nativeSun ? sun : 0;
        iw8rSurfaceIn.sunShadow = visibility;
        iw8rSurfaceIn.ambient = indirect;
        iw8rSurfaceIn.gloss = gloss;
        color = iw8rSurface(iw8rSurfaceIn);
    }
#endif''')

C = ROOT + 'iw3_render_assets.cpp'
patch(C, '''std::vector<std::uint8_t> CompileUncached(const std::string &source, const char *target, const char *name);
''', '''std::vector<std::uint8_t> CompileUncached(const std::string &source, const char *target, const char *name);

// IW8 Radiant's map surface shader (world JSON "iw8r_surface_shader": the IW8RSurface struct and its iw8rSurface
// function), prepended to the surface pixel shaders with IW8R_CUSTOM; empty for maps without one.
std::string g_surfaceShaderSource;
''')
patch(C, '''        g_unlitMaskDefines + g_uvAnimationDefines + g_transmissionDefines + g_uvClampDefines + g_extinctionDefines;''',
      '''        g_unlitMaskDefines + g_uvAnimationDefines + g_transmissionDefines + g_uvClampDefines + g_extinctionDefines +
        g_surfaceShaderSource;''')
patch(C, '''    g_iw7SpecularDefines = world.value("iw7_specular", false) ? "#define MAP_IW7_SPECULAR 1\\n" : "";''',
      '''    g_iw7SpecularDefines = world.value("iw7_specular", false) ? "#define MAP_IW7_SPECULAR 1\\n" : "";
    g_surfaceShaderSource.clear();
    if (const auto custom = world.find("iw8r_surface_shader"); custom != world.end() && custom->is_string() &&
                                                              !custom->get<std::string>().empty())
        g_surfaceShaderSource = "#define IW8R_CUSTOM 1\\n" + custom->get<std::string>() + "\\n#line 1 \\"iw3_map_surface.hlsl\\"\\n";''')
print('ok')
