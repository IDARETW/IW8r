import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


RH = 'src/viewport/render.h'
patch(RH, '''struct SceneBatch : GeoStream<GpuTexVertex> {
    int key = 0;
    int texture = -1, normalTexture = -1, emissiveTexture = -1;''', '''struct SceneBatch : GeoStream<GpuTexVertex> {
    int key = 0;
    int texture = -1, normalTexture = -1, emissiveTexture = -1;
    int shader = -1;  // SceneRenderer::createCustomShader id of the material's custom shader (Game Lighting), -1 none''')
patch(RH, '''    size_t textureBytes() const { return textureBytes_; }''', '''    size_t textureBytes() const { return textureBytes_; }
    // A map's custom surface shader (core custom_shader): the textured pixel shader with the shader's iw8rSurface for
    // the Game Lighting colour. Returns its id (>= 0) or -1 with *err (the compiler's message).
    int createCustomShader(const std::string& source, std::string* err);''')
patch(RH, '''    ID3D11PixelShader* psTex_ = nullptr;''', '''    ID3D11PixelShader* psTex_ = nullptr;
    std::vector<ID3D11PixelShader*> customPs_;  // createCustomShader''')

R = 'src/viewport/render.cpp'
# The hook in ps_tex: own brushes in Game Lighting
patch(R, '''        } else {
            float3 light = indirect * aoV + visibility * saturate(dot(N, gameSun.xyz)) * gameSunCol.rgb;
            c = albedo * light;
        }''', '''        } else {
#ifdef IW8R_CUSTOM
            // A custom surface shader (core custom_shader): the map's own lighting function.
            IW8RSurface s;
            s.albedo = albedo, s.alpha = outA, s.normal = N, s.view = normalize(eyeStart.xyz - i.w), s.position = i.w;
            s.uv = i.uv, s.sunDir = gameSun.xyz, s.sunColor = gameSunCol.rgb, s.sunShadow = visibility;
            s.ambient = indirect * aoV, s.gloss = 0.3;
            c = iw8rSurface(s);
#else
            float3 light = indirect * aoV + visibility * saturate(dot(N, gameSun.xyz)) * gameSunCol.rgb;
            c = albedo * light;
#endif
        }''')
patch(R, '''int SceneRenderer::createTextureBlocks(int dxgiFormat, int width, int height, const std::vector<std::string>& levels) {''',
      '''int SceneRenderer::createCustomShader(const std::string& source, std::string* err) {
    if (!dev_) return -1;
    ID3DBlob* blob = nullptr;
    ID3DBlob* errb = nullptr;
    const HRESULT hr = D3DCompile(source.data(), source.size(), "iw8r_custom", nullptr, nullptr, "ps_tex", "ps_5_0", 0, 0, &blob, &errb);
    if (FAILED(hr)) {
        if (err) *err = errb ? std::string(static_cast<const char*>(errb->GetBufferPointer()), errb->GetBufferSize()) : "D3DCompile failed";
        safeRelease(errb);
        return -1;
    }
    safeRelease(errb);
    ID3D11PixelShader* ps = nullptr;
    const HRESULT ch = dev_->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &ps);
    safeRelease(blob);
    if (FAILED(ch)) {
        if (err) *err = "CreatePixelShader failed";
        return -1;
    }
    customPs_.push_back(ps);
    return int(customPs_.size()) - 1;
}

const char* SceneRenderer::texShaderSource() { return kTexShader; }

int SceneRenderer::createTextureBlocks(int dxgiFormat, int width, int height, const std::vector<std::string>& levels) {''')
patch(RH, '''    int createCustomShader(const std::string& source, std::string* err);''', '''    int createCustomShader(const std::string& source, std::string* err);
    static const char* texShaderSource();  // the base the custom shaders are made from''')
patch(R, '''            ctx_->PSSetShaderResources(0, 1, &textures_[size_t(b.texture)]);
            const bool hasNormal = b.normalTexture >= 0 && size_t(b.normalTexture) < textures_.size();''', '''            ctx_->PSSetShaderResources(0, 1, &textures_[size_t(b.texture)]);
            // The material's custom shader in Game Lighting (createCustomShader), else the editor's own.
            {
                ID3D11PixelShader* want = game_.on && b.shader >= 0 && size_t(b.shader) < customPs_.size() && customPs_[size_t(b.shader)]
                                              ? customPs_[size_t(b.shader)] : psTex_;
                if (want != boundTexPs) ctx_->PSSetShader(want, nullptr, 0), boundTexPs = want;
            }
            const bool hasNormal = b.normalTexture >= 0 && size_t(b.normalTexture) < textures_.size();''')
patch(R, '''        for (uint32_t k : drawOrder_) {
            const SceneBatch& b = *drawList_[k].b;
            StreamGpu& g = *drawList_[k].g;
            if (b.texture < 0 || size_t(b.texture) >= textures_.size()) continue;''', '''        ID3D11PixelShader* boundTexPs = psTex_;
        for (uint32_t k : drawOrder_) {
            const SceneBatch& b = *drawList_[k].b;
            StreamGpu& g = *drawList_[k].g;
            if (b.texture < 0 || size_t(b.texture) >= textures_.size()) continue;''')
patch(R, '''        ctx_->OMSetDepthStencilState(dsWrite_, 0);
        ctx_->OMSetBlendState(blend_, bf, 0xffffffff);
        ID3D11ShaderResourceView* noSrv[2] = {nullptr, nullptr};
        ctx_->PSSetShaderResources(0, 2, noSrv);''', '''        if (boundTexPs != psTex_) ctx_->PSSetShader(psTex_, nullptr, 0);
        ctx_->OMSetDepthStencilState(dsWrite_, 0);
        ctx_->OMSetBlendState(blend_, bf, 0xffffffff);
        ID3D11ShaderResourceView* noSrv[2] = {nullptr, nullptr};
        ctx_->PSSetShaderResources(0, 2, noSrv);''')
patch(R, '''    safeRelease(cbVol_);''', '''    safeRelease(cbVol_);
    for (auto*& ps : customPs_) safeRelease(ps);
    customPs_.clear();''')

# scene + geometry
SH = 'src/viewport/scene.h'
patch(SH, '''    std::function<int(const std::string& material)> emissiveFor;''', '''    std::function<int(const std::string& material)> emissiveFor;
    // The material's custom surface shader (SceneRenderer::createCustomShader id) or -1 (core custom_shader).
    std::function<int(const std::string& material)> shaderFor;''')
CG = 'src/viewport/camera_geometry.cpp'
patch(CG, '''    if (scene->emissiveFor && !scene->captureMode && t.material) b->emissiveTexture = scene->emissiveFor(*t.material);
    created = true;''', '''    if (scene->emissiveFor && !scene->captureMode && t.material) b->emissiveTexture = scene->emissiveFor(*t.material);
    if (scene->shaderFor && t.material) b->shader = scene->shaderFor(*t.material);
    created = true;''')
patch(CG, '''                    nb->emissiveTexture = scene.emissiveFor && !scene.captureMode && t.material ? scene.emissiveFor(*t.material) : -1;''',
      '''                    nb->emissiveTexture = scene.emissiveFor && !scene.captureMode && t.material ? scene.emissiveFor(*t.material) : -1;
                    nb->shader = scene.shaderFor && t.material ? scene.shaderFor(*t.material) : -1;''')
print('ok')
