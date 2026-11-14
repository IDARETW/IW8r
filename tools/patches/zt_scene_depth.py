import io
import sys

# Converter: the scene's depth for IW8 Radiant custom surface shaders (iw8rSceneDepth, IW8RSurface.pixel / viewDepth /
# eye). Replay's "Frontend FloatZ" is code image 0; the lit technique's one code image argument (79, the thermal mask
# the generated shader never reads) is re-pointed to it when the map's shader calls iw8rSceneDepth.
# Argument: the converter checkout (default the IW8 Radiant worktree).

ROOT = (sys.argv[1] if len(sys.argv) > 1 else 'C:/zsw-iw8r/scriptables').rstrip('/') + '/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


H = 'src/convert/iw3_iw8/resources/map_surface_realtime.hlsl'
patch(H, '''cbuffer ReplayView : register(b2) {
    float4 replayView[8];
};
''', '''cbuffer ReplayView : register(b2) {
    float4 replayView[8];
};
#ifdef IW8R_SCENE_DEPTH
// IW8 Radiant custom surface shaders: the scene's depth (iw8rSceneDepth, declared ahead of the shader's code). It is
// Replay's "Frontend FloatZ", code image 0: the view depth / 6 in a 16-bit float, negative on first-person viewmodel
// pixels, 65500 and up for the sky (IW8 Radiant research 104). The stock effect shaders read it the same way: the
// pixel scaled by replayView[7].x and shifted by the mip in replayView[7].z. The lit technique's code image argument
// (79: the thermal mask, which this shader never reads) is re-pointed to it (RepointCodeImage).
#ifdef STATIC_MODEL
Texture2D<float4> iw8rSceneZ : register(t3);
#else
Texture2D<float4> iw8rSceneZ : register(t4);
#endif
float iw8rSceneDepth(float2 pixel) {
    if (pixel.x < 0 || pixel.y < 0)
        return 0;
    const uint mip = asuint(replayView[7].z);
    // Past the right or bottom edge Load gives 0: "no depth", as the interface says.
    const float depth = abs(iw8rSceneZ.Load(int3(uint2(pixel * replayView[7].x) >> mip, mip)).x) * 6.0;
    return depth >= 392999.0 ? 1e9 : depth;
}
#endif
''')
patch(H, '''        iw8rSurfaceIn.faceNormal = normalize(input.normal);
#if defined(IW8R_EDGES) && !defined(STATIC_MODEL)''', '''        iw8rSurfaceIn.faceNormal = normalize(input.normal);
#ifdef IW8R_SURFACE_SCREEN
        iw8rSurfaceIn.pixel = input.position.xy;
        iw8rSurfaceIn.viewDepth = input.position.w;
        iw8rSurfaceIn.eye = replayView[4].xyz;
#endif
#if defined(IW8R_EDGES) && !defined(STATIC_MODEL)''')

C = 'src/convert/iw3_iw8/iw3_render_assets.cpp'
patch(C, '''std::string g_surfaceShaderSource;
''', '''std::string g_surfaceShaderSource;
// True when that shader reads the scene's depth (iw8rSceneDepth): the lit techniques then bind Replay's scene depth
// (code image 0) where the thermal mask (code image 79) was.
bool g_surfaceSceneDepth = false;
''')
patch(C, '''    if (const auto custom = world.find("iw8r_surface_shader"); custom != world.end() && custom->is_string() &&
                                                              !custom->get<std::string>().empty())
        g_surfaceShaderSource = "#define IW8R_CUSTOM 1\\n" + custom->get<std::string>() + "\\n#line 1 \\"iw3_map_surface.hlsl\\"\\n";
''', '''    g_surfaceSceneDepth = false;
    if (const auto custom = world.find("iw8r_surface_shader"); custom != world.end() && custom->is_string() &&
                                                              !custom->get<std::string>().empty())
    {
        const std::string code = custom->get<std::string>();
        // An editor that hands over the screen inputs (pixel, viewDepth, eye) has them in its struct; a shader that
        // calls iw8rSceneDepth gets the function (defined in the surface shader, after the view constants) and the
        // depth's precision.
        std::string head = "#define IW8R_CUSTOM 1\\n";
        if (code.find("viewDepth;") != std::string::npos && code.find(" eye;") != std::string::npos)
            head += "#define IW8R_SURFACE_SCREEN 1\\n";
        g_surfaceSceneDepth = code.find("iw8rSceneDepth(") != std::string::npos;
        if (g_surfaceSceneDepth)
            head += "#define IW8R_SCENE_DEPTH 1\\nfloat iw8rSceneDepth(float2 pixel);\\n"
                    "static const float iw8rSceneDepthStep = 1.0 / 1024.0;\\n";
        g_surfaceShaderSource = head + code + "\\n#line 1 \\"iw3_map_surface.hlsl\\"\\n";
    }
''')
patch(C, '''void KeepReferencedShaders(Json &techset)
{''', '''// Re-points a technique's code image argument (type 1) from one code image to another, the register kept.
void RepointCodeImage(Json &technique, const std::uint16_t from, const std::uint16_t to)
{
    auto args = Unhex(technique.at("args").get<std::string>());
    unsigned found = 0;
    for (std::size_t at = 0; at + 6 <= args.size(); at += 6)
        if (args[at] == 1 && (args[at + 4] | (args[at + 5] << 8)) == from)
        {
            args[at + 4] = static_cast<std::uint8_t>(to & 0xff);
            args[at + 5] = static_cast<std::uint8_t>(to >> 8);
            ++found;
        }
    if (found != 1)
        throw std::runtime_error("the lit technique has no single code image " + std::to_string(from) +
                                 " argument to give the scene depth");
    technique["args"] = Hex(args);
}

void KeepReferencedShaders(Json &techset)
{''')
patch(C, '''    if (uvdTexture)
        AppendTextureArgument(lit, "05100c003900"); // t12, slot 0x39: the noise map, in MaterialTextureID_flowMap
    PatchStateIdentity(lit, pixel);
''', '''    if (uvdTexture)
        AppendTextureArgument(lit, "05100c003900"); // t12, slot 0x39: the noise map, in MaterialTextureID_flowMap
    if (g_surfaceSceneDepth)
        RepointCodeImage(lit, 79, 0); // t4 (models t3): Replay's scene depth for iw8rSceneDepth
    PatchStateIdentity(lit, pixel);
''')

R = 'src/games/iw8/common/replay_render.cpp'
patch(R, '''bool hasCodeTextureArgument(const std::vector<uint8_t> &args, const uint8_t stageMask,''', '''// A code image argument (type 1) of the pixel stage at a register, for one code image.
bool hasPixelCodeImageArgument(const std::vector<uint8_t> &args, const uint8_t registerIndex,
                               const uint16_t codeImage)
{
    for (size_t offset = 0; offset + 6 <= args.size(); offset += 6)
        if (args[offset] == 1 && (args[offset + 1] & 0x10) && args[offset + 2] == registerIndex &&
            args[offset + 4] == (codeImage & 0xff) && args[offset + 5] == (codeImage >> 8))
            return true;
    return false;
}

bool hasCodeTextureArgument(const std::vector<uint8_t> &args, const uint8_t stageMask,''')
patch(R, '''            const auto materialSlot = generatedTextureSlot(resourceName.c_str());
            if (!materialSlot || binding.Type != D3D_SIT_TEXTURE)
                continue;''', '''            if (resourceName == "iw8rSceneZ")
            {
                // IW8 Radiant custom surface shaders read the scene depth: code image 0 at the shader's register.
                if (binding.Type != D3D_SIT_TEXTURE || binding.BindPoint > UINT8_MAX ||
                    !hasPixelCodeImageArgument(technique.args, static_cast<uint8_t>(binding.BindPoint), 0))
                {
                    reflection->Release();
                    throw std::runtime_error("Generated scene depth is not bound to code image 0: " +
                                             technique.name);
                }
                continue;
            }
            const auto materialSlot = generatedTextureSlot(resourceName.c_str());
            if (!materialSlot || binding.Type != D3D_SIT_TEXTURE)
                continue;''')
print('ok')
