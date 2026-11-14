import io

# SceneRenderer::compileCheck: the Game Lighting pixel shader compiled without a device (Tools > Shader Editor > Check).


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('E:/IW8-Radiant/src/viewport/render.h', '''    static const char* texShaderSource();  // the base the custom shaders are made from
''', '''    static const char* texShaderSource();  // the base the custom shaders are made from
    // Compiles a textured pixel shader source (ps_tex, ps_5_0) without creating it: false with the compiler's errors.
    static bool compileCheck(const std::string& source, std::string* err);
''')
patch('E:/IW8-Radiant/src/viewport/render.cpp', '''const char* SceneRenderer::texShaderSource() { return kTexShader; }
''', '''const char* SceneRenderer::texShaderSource() { return kTexShader; }

bool SceneRenderer::compileCheck(const std::string& source, std::string* err) {
    ID3DBlob* blob = nullptr;
    ID3DBlob* errb = nullptr;
    const HRESULT hr = D3DCompile(source.data(), source.size(), "iw8r_custom", nullptr, nullptr, "ps_tex", "ps_5_0", 0, 0, &blob, &errb);
    if (err) *err = errb ? std::string(static_cast<const char*>(errb->GetBufferPointer()), errb->GetBufferSize()) : std::string();
    safeRelease(errb);
    safeRelease(blob);
    if (FAILED(hr) && err && err->empty()) *err = "D3DCompile failed";
    return SUCCEEDED(hr);
}
''')
print('ok')
