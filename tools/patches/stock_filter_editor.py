import io

# Custom shaders on stock maps (plan M6): a shader file's optional `float3 iw8rFilter(float3 colour)` is the look of
# surfaces that keep the game's own lighting (a stock map's world and props). The viewport runs it on the stock
# reference; File > Rebuild Stock Zone compiles it and hands it to the converter's editzone "lit_filter", which splices
# it into the stock lit pixel shaders (core dxbc_splice).


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


# ---- core custom_shader
H = 'E:/IW8-Radiant/src/core/custom_shader.h'
patch(H, '''// True when the code reads an edge input''', '''// A shader file may hold two functions (at least one):
//   float3 iw8rSurface(IW8RSurface s)   the whole lighting of the map's own surfaces (above)
//   float3 iw8rFilter(float3 colour)    a filter on the finished colour of surfaces that keep the game's own lighting:
//                                       a stock map's world and props. `colour` is the lit, fogged, pre-exposed linear
//                                       colour the game's pixel shader ends with; the result replaces it. The function
//                                       may use only its argument, constants and intrinsics (it is spliced into the
//                                       game's compiled shaders, core dxbc_splice).
bool customShaderHasSurface(const std::string& code);
bool customShaderHasFilter(const std::string& code);
// The pixel shader that holds the filter alone (entry point kIw8rFilterEntry, ps_5_0): it reads the colour from
// TEXCOORD0 and writes the filtered colour, alpha unchanged. The hosts compile it; the build splices it.
inline constexpr const char* kIw8rFilterEntry = "iw8rFilterMain";
std::string customShaderFilterProgramSource(const CustomShader& shader);
// The viewport's textured shader with the filter (IW8R_FILTER defined): for the stock reference.
std::string customShaderFilterViewportSource(const std::string& baseSource, const std::string& insertBefore, const CustomShader& shader);

// True when the code reads an edge input''')

C = 'E:/IW8-Radiant/src/core/custom_shader.cpp'
patch(C, '''    const std::string c = withoutComments(code);
    if (!std::regex_search(c, std::regex(R"(float3\\s+iw8rSurface\\s*\\(\\s*IW8RSurface\\s+\\w+\\s*\\))"))) {
        if (err) *err = "the shader must define float3 iw8rSurface(IW8RSurface s)";
        return false;
    }''', '''    const std::string c = withoutComments(code);
    if (!customShaderHasSurface(code) && !customShaderHasFilter(code)) {
        if (err) *err = "the shader must define float3 iw8rSurface(IW8RSurface s) or float3 iw8rFilter(float3 colour)";
        return false;
    }''')
patch(C, '''bool customShaderUsesEdges(const std::string& code) {''', '''bool customShaderHasSurface(const std::string& code) {
    static const std::regex re(R"(float3\\s+iw8rSurface\\s*\\(\\s*IW8RSurface\\s+\\w+\\s*\\))");
    return std::regex_search(withoutComments(code), re);
}

bool customShaderHasFilter(const std::string& code) {
    static const std::regex re(R"(float3\\s+iw8rFilter\\s*\\(\\s*float3\\s+\\w+\\s*\\))");
    return std::regex_search(withoutComments(code), re);
}

std::string customShaderFilterProgramSource(const CustomShader& shader) {
    return customShaderPreamble() + "#line 1 \\"" + shader.name + ".iw8shader.hlsl\\"\\n" + shader.code +
           "\\n#line 1 \\"filter\\"\\nfloat4 " + kIw8rFilterEntry +
           "(float4 colour : TEXCOORD0) : SV_Target { return float4(iw8rFilter(colour.rgb), colour.a); }\\n";
}

std::string customShaderFilterViewportSource(const std::string& baseSource, const std::string& insertBefore, const CustomShader& shader) {
    const size_t at = baseSource.find(insertBefore);
    if (at == std::string::npos) return {};
    std::string out = "#define IW8R_FILTER 1\\n";
    out += baseSource.substr(0, at);
    out += customShaderPreamble();
    out += "#line 1 \\"" + shader.name + ".iw8shader.hlsl\\"\\n";
    out += shader.code;
    out += "\\n#line 1 \\"viewport\\"\\n";
    out += baseSource.substr(at);
    return out;
}

bool customShaderUsesEdges(const std::string& code) {''')

# ---- core stock_zone_edit: the "lit_filter" key
SH = 'E:/IW8-Radiant/src/core/stock_zone_edit.h'
patch(SH, '''                          const std::string* removeWorld = nullptr);''', '''                          const std::string* removeWorld = nullptr, const std::string* litFilter = nullptr);
// The "lit_filter" value for stockEditJson: the compiled filter program (customShaderFilterProgramSource, core
// custom_shader) and the shared zones a lit shader's program may live in when the map's techset zone only names it.
std::string stockLitFilterJson(const std::string& programPath);''')
SC = 'E:/IW8-Radiant/src/core/stock_zone_edit.cpp'
patch(SC, '''                          const std::string* removeWorld) {''', '''                          const std::string* removeWorld, const std::string* litFilter) {''')
patch(SC, '''    if (removeWorld && !removeWorld->empty()) {
        sep();
        json += "\\"remove_world_surfaces\\":" + *removeWorld;
    }''', '''    if (removeWorld && !removeWorld->empty()) {
        sep();
        json += "\\"remove_world_surfaces\\":" + *removeWorld;
    }
    if (litFilter && !litFilter->empty()) {
        sep();
        json += "\\"lit_filter\\":" + *litFilter;
    }''')
s = io.open(SC, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
i = s.index('std::string stockEditJson(')
add = '''std::string stockLitFilterJson(const std::string& programPath) {
    std::string path;
    for (const char ch : programPath) {
        if (ch == '\\\\' || ch == '"') path += '\\\\';
        path += ch;
    }
    // The zones the game loads with every multiplayer map: a map's techset zone names their shaders without holding them.
    return "{\\"program\\":\\"" + path +
           "\\",\\"shared\\":[\\"techsets_common_mp\\",\\"techsets_common_core_mp\\",\\"techsets_common_base_mp\\",\\"techsets_common\\","
           "\\"techsets_common_stream_mp\\",\\"techsets_global_mp\\",\\"techsets_global_core_mp\\",\\"techsets_global\\","
           "\\"techsets_global_stream_mp\\",\\"techsets_common_br_mp\\"]}";
}

'''.replace('\n', nl)
s = s[:i] + add + s[i:]
io.open(SC, 'w', encoding='utf-8', newline='').write(s)

# ---- renderer: the filter variant for the stock reference
RH = 'E:/IW8-Radiant/src/viewport/render.h'
patch(RH, '''    static bool compileCheck(const std::string& source, std::string* err);
''', '''    static bool compileCheck(const std::string& source, std::string* err);
    // Compiles a pixel shader (ps_5_0) to its bytecode: false with the compiler's errors.
    static bool compileProgram(const std::string& source, const char* entry, std::vector<uint8_t>& program, std::string* err);
    // The stock reference (world and props) drawn with this textured pixel shader source instead of the editor's own
    // (customShaderFilterViewportSource: a map shader's filter on surfaces that keep the game's lighting). Empty source:
    // the editor's own again. False with the errors when it does not compile (the editor's own is kept).
    bool setReferenceFilterShader(const std::string& source, std::string* err);
    bool referenceFilterShader() const { return filterPs_ != nullptr; }
''')
patch(RH, '''    ID3D11PixelShader* texPsOverride_ = nullptr;''', '''    ID3D11PixelShader* filterPs_ = nullptr;  // setReferenceFilterShader
    ID3D11PixelShader* texPsOverride_ = nullptr;''')
RC = 'E:/IW8-Radiant/src/viewport/render.cpp'
patch(RC, '''const char* SceneRenderer::texShaderSource() { return kTexShader; }
''', '''const char* SceneRenderer::texShaderSource() { return kTexShader; }

bool SceneRenderer::compileProgram(const std::string& source, const char* entry, std::vector<uint8_t>& program, std::string* err) {
    program.clear();
    ID3DBlob* blob = nullptr;
    ID3DBlob* errb = nullptr;
    const HRESULT hr = D3DCompile(source.data(), source.size(), "iw8r_program", nullptr, nullptr, entry, "ps_5_0",
                                  D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errb);
    if (err) *err = errb ? std::string(static_cast<const char*>(errb->GetBufferPointer()), errb->GetBufferSize()) : std::string();
    safeRelease(errb);
    if (FAILED(hr) || !blob) {
        safeRelease(blob);
        if (err && err->empty()) *err = "D3DCompile failed";
        return false;
    }
    // Reflection and debug data are not needed where the program goes (it is spliced into another shader).
    ID3DBlob* stripped = nullptr;
    if (SUCCEEDED(D3DStripShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                 D3DCOMPILER_STRIP_REFLECTION_DATA | D3DCOMPILER_STRIP_DEBUG_INFO | D3DCOMPILER_STRIP_TEST_BLOBS, &stripped)) && stripped) {
        safeRelease(blob);
        blob = stripped;
    }
    const auto* p = static_cast<const uint8_t*>(blob->GetBufferPointer());
    program.assign(p, p + blob->GetBufferSize());
    safeRelease(blob);
    return true;
}

bool SceneRenderer::setReferenceFilterShader(const std::string& source, std::string* err) {
    if (source.empty()) {
        safeRelease(filterPs_);
        return true;
    }
    if (!dev_) return false;
    ID3DBlob* blob = nullptr;
    ID3DBlob* errb = nullptr;
    const HRESULT hr = D3DCompile(source.data(), source.size(), "iw8r_filter", nullptr, nullptr, "ps_tex", "ps_5_0", 0, 0, &blob, &errb);
    if (FAILED(hr)) {
        if (err) *err = errb ? std::string(static_cast<const char*>(errb->GetBufferPointer()), errb->GetBufferSize()) : "D3DCompile failed";
        safeRelease(errb);
        return false;
    }
    safeRelease(errb);
    ID3D11PixelShader* ps = nullptr;
    const HRESULT ch = dev_->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &ps);
    safeRelease(blob);
    if (FAILED(ch)) {
        if (err) *err = "CreatePixelShader failed";
        return false;
    }
    safeRelease(filterPs_);
    filterPs_ = ps;
    return true;
}
''')
patch(RC, '''    safeRelease(psTex_);''', '''    safeRelease(psTex_);
    safeRelease(filterPs_);''')
# Props and the reference world draw with the filter variant when one is set.
patch(RC, '''    ctx_->PSSetShader(texPsOverride_ ? texPsOverride_ : psTex_, nullptr, 0);''',
      '''    ctx_->PSSetShader(texPsOverride_ ? texPsOverride_ : (filterPs_ ? filterPs_ : psTex_), nullptr, 0);''')
patch(RC, '''    if (vbRefTex_ && refVisible_ && vsTex_ && psTex_ && layoutTex_) {  // textured stock reference''',
      '''    ID3D11PixelShader* const refPs = filterPs_ ? filterPs_ : psTex_;  // a map shader's filter on the stock reference
    if (vbRefTex_ && refVisible_ && vsTex_ && psTex_ && layoutTex_) {  // textured stock reference''')
# The filter in the textured shader: the pre-exposed colour through iw8rFilter, right before the tone map.
patch(RC, '''        if (gameAo.w < 0.5) c = gamePost(c);  // HDR frames: the post pass applies it (drawPost)''', '''#ifdef IW8R_FILTER
        {
            // A map shader's filter (core custom_shader): on the lit, fogged colour with the exposure applied, as the
            // game's lit pixel shaders end (their last step multiplies by the exposure, cb7[42].x).
            const float pre = max(game0.y, 1e-9);
            c = iw8rFilter(c * pre) / pre;
        }
#endif
        if (gameAo.w < 0.5) c = gamePost(c);  // HDR frames: the post pass applies it (drawPost)''')
print('ok')
