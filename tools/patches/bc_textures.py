import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if b and s.count(b) >= 1:
        return  # already applied
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


TH = 'src/viewport/texture_decode.h'
TC = 'src/viewport/texture_decode.cpp'
H = 'src/viewport/render.h'
R = 'src/viewport/render.cpp'
C = 'src/qtapp/camera_view.cpp'

# ---------------------------------------------------------------- loader
patch(TH, '''struct Rgba8Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;  // RGBA8, row-major, top-down; size == width * height * 4
};''', '''struct Rgba8Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;  // RGBA8, row-major, top-down; size == width * height * 4
};

// A block-compressed colour image kept compressed for the GPU (BC1 / BC2 / BC3 / BC7): about 4 to 8 times smaller than
// RGBA8 and nothing to decode. dxgiFormat is the non-sRGB UNORM format (the shaders convert to linear themselves).
struct DdsBlockLevels {
    int dxgiFormat = 0;
    int width = 0, height = 0;          // of levels[0]
    std::vector<std::string> levels;    // levels[0] at the size picked, then each smaller level stored after it
    size_t bytes = 0;
};''')
patch(TH, '''bool loadDdsPreview(const std::string& path, int maxSide, Rgba8Image& out, int* fullWidth = nullptr,''',
      '''// The BC1 / BC2 / BC3 / BC7 image at `path` for direct upload: the level loadDdsPreview would pick for maxSide and
// every smaller level stored after it. false for any other format, a volume, or a top level under 4 texels (decode
// those with loadDdsPreview).
bool loadDdsBlocks(const std::string& path, int maxSide, DdsBlockLevels& out, int* fullWidth = nullptr, int* fullHeight = nullptr,
                   std::string* err = nullptr);

bool loadDdsPreview(const std::string& path, int maxSide, Rgba8Image& out, int* fullWidth = nullptr,''')
patch(TC, '''Rgba8Image downscale(const Rgba8Image& in, int maxSide) {''', '''bool loadDdsBlocks(const std::string& path, int maxSide, DdsBlockLevels& out, int* fullWidth, int* fullHeight, std::string* err) {
    out = DdsBlockLevels{};
    const std::filesystem::path file(std::u8string(reinterpret_cast<const char8_t*>(path.data()), path.size()));
    std::ifstream in(file, std::ios::binary);
    if (!in) return failWith(err, "cannot open " + path);
    in.seekg(0, std::ios::end);
    const std::streamoff fileSize = in.tellg();
    if (fileSize < 0) return failWith(err, "cannot read " + path);
    std::string header(static_cast<size_t>(std::min<std::streamoff>(fileSize, 148)), '\\0');
    in.seekg(0);
    in.read(header.data(), static_cast<std::streamsize>(header.size()));
    if (!in) return failWith(err, "cannot read " + path);
    DdsLayout l;
    if (!parseHeader(header, l, err)) return false;
    int dxgi = 0;
    switch (l.codec) {
        case Codec::Bc1: dxgi = 71; break;  // DXGI_FORMAT_BC1_UNORM
        case Codec::Bc2: dxgi = 74; break;  // BC2_UNORM
        case Codec::Bc3: dxgi = 77; break;  // BC3_UNORM
        case Codec::Bc7: dxgi = 98; break;  // BC7_UNORM
        default: return failWith(err, "not a BC1/2/3/7 colour image");
    }
    if (l.volume) return failWith(err, "volume image");
    if (fullWidth) *fullWidth = static_cast<int>(l.width);
    if (fullHeight) *fullHeight = static_cast<int>(l.height);
    const size_t size = static_cast<size_t>(fileSize);
    const size_t available = size > l.dataOffset ? size - l.dataOffset : 0;
    const LevelPick pick = pickLevel(l, maxSide, available);
    if (pick.width < 4 || pick.height < 4 || pick.width % 4 || pick.height % 4) return failWith(err, "top level not a whole number of blocks");
    // The level index of the pick: walk the chain to find where it starts.
    size_t w = l.width, h = l.height, offset = 0;
    uint32_t level = 0;
    while (level < l.mipCount && offset < pick.offset) {
        offset += levelBytes(l.codec, w, h);
        w = std::max<size_t>(1, w / 2), h = std::max<size_t>(1, h / 2);
        ++level;
    }
    if (offset != pick.offset) return failWith(err, "mip chain does not line up");
    out.dxgiFormat = dxgi;
    out.width = static_cast<int>(w), out.height = static_cast<int>(h);
    in.seekg(static_cast<std::streamoff>(l.dataOffset + offset));
    for (; level < l.mipCount; ++level) {
        const size_t bytes = levelBytes(l.codec, w, h);
        if (offset > available || bytes > available - offset) break;  // the file ends here: keep the levels read
        std::string data(bytes, '\\0');
        in.read(data.data(), static_cast<std::streamsize>(bytes));
        if (!in) break;
        out.levels.push_back(std::move(data));
        out.bytes += bytes;
        offset += bytes;
        if (w == 1 && h == 1) break;
        w = std::max<size_t>(1, w / 2), h = std::max<size_t>(1, h / 2);
    }
    if (out.levels.empty()) return failWith(err, "no level could be read");
    return true;
}

Rgba8Image downscale(const Rgba8Image& in, int maxSide) {''')

# ---------------------------------------------------------------- renderer: compressed textures, raw-alpha flag
patch(H, '''    void setReferenceTexture(size_t batch, int textureId) {
        if (batch < refTexIds_.size()) refTexIds_[batch] = textureId;
    }''', '''    void setReferenceTexture(size_t batch, int textureId) {
        if (batch < refTexIds_.size()) refTexIds_[batch] = textureId, layerParamsDirty_ = true;
    }
    // A block-compressed colour texture uploaded as it is stored (texture_decode loadDdsBlocks). Its alpha is the stock
    // specular code, never a cut-out: batches drawing it are opaque (the shaders get "raw spec", no alpha test).
    int createTextureBlocks(int dxgiFormat, int width, int height, const std::vector<std::string>& levels);
    bool textureRawAlpha(int id) const { return id >= 0 && size_t(id) < texRawAlpha_.size() && texRawAlpha_[size_t(id)]; }''')
patch(H, '''    std::vector<ID3D11ShaderResourceView*> textures_;''', '''    std::vector<ID3D11ShaderResourceView*> textures_;
    std::vector<uint8_t> texRawAlpha_;  // per texture: 1 = alpha is the specular code (createTextureBlocks)''')
patch(H, '''    void setReferenceInstTexture(size_t mesh, int textureId) {
        if (mesh < refTexInst_.size()) refTexInst_[mesh].texture = textureId;
    }''', '''    void setReferenceInstTexture(size_t mesh, int textureId) {
        if (mesh < refTexInst_.size()) refTexInst_[mesh].texture = textureId, instParamsDirty_ = true;
    }''')
patch(R, '''int SceneRenderer::createTexture(const uint8_t* rgba, int width, int height) {''', '''int SceneRenderer::createTextureBlocks(int dxgiFormat, int width, int height, const std::vector<std::string>& levels) {
    if (!dev_ || levels.empty() || width < 4 || height < 4) return -1;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = UINT(width);
    td.Height = UINT(height);
    td.MipLevels = UINT(levels.size());
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT(dxgiFormat);
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const UINT blockBytes = dxgiFormat == DXGI_FORMAT_BC1_UNORM ? 8 : 16;
    std::vector<D3D11_SUBRESOURCE_DATA> init(levels.size());
    UINT w = UINT(width), h = UINT(height);
    size_t bytes = 0;
    for (size_t k = 0; k < levels.size(); ++k) {
        init[k].pSysMem = levels[k].data();
        init[k].SysMemPitch = std::max(1u, (w + 3) / 4) * blockBytes;
        init[k].SysMemSlicePitch = UINT(levels[k].size());
        bytes += levels[k].size();
        w = std::max(1u, w / 2), h = std::max(1u, h / 2);
    }
    ID3D11Texture2D* tex = nullptr;
    if (FAILED(dev_->CreateTexture2D(&td, init.data(), &tex))) return -1;
    ID3D11ShaderResourceView* srv = nullptr;
    const HRESULT hr = dev_->CreateShaderResourceView(tex, nullptr, &srv);
    tex->Release();
    if (FAILED(hr)) return -1;
    textures_.push_back(srv);
    texRawAlpha_.resize(textures_.size(), 0);
    texRawAlpha_.back() = 1;
    textureBytes_ += bytes;
    return int(textures_.size()) - 1;
}

int SceneRenderer::createTexture(const uint8_t* rgba, int width, int height) {''')
patch(R, '''    ctx_->GenerateMips(srv);
    textures_.push_back(srv);
    textureBytes_ += size_t(width) * size_t(height) * 4 * 4 / 3;  // + the mip chain''', '''    ctx_->GenerateMips(srv);
    textures_.push_back(srv);
    texRawAlpha_.resize(textures_.size(), 0);
    textureBytes_ += size_t(width) * size_t(height) * 4 * 4 / 3;  // + the mip chain''')
# the uploads mark raw-alpha batches (params[1] = 2)
patch(R, '''            for (size_t j = 0; j < kPerCb && c * kPerCb + j < refLayers_.size(); ++j)
                std::memcpy(&all[j * 64], refLayers_[c * kPerCb + j].params, sizeof(refLayers_[0].params));
            if (cbLayerAll_[c]) ctx_->UpdateSubresource(cbLayerAll_[c], 0, nullptr, all.data(), 0, 0);''', '''            for (size_t j = 0; j < kPerCb && c * kPerCb + j < refLayers_.size(); ++j) {
                const size_t k = c * kPerCb + j;
                std::memcpy(&all[j * 64], refLayers_[k].params, sizeof(refLayers_[0].params));
                // A base batch drawing a raw-alpha texture: opaque, its alpha the specular code.
                if (refLayers_[k].layer == 0 && !refLayers_[k].decal && k < refTexIds_.size() && textureRawAlpha(refTexIds_[k]))
                    all[j * 64 + 1] = 2.0f;
            }
            if (cbLayerAll_[c]) ctx_->UpdateSubresource(cbLayerAll_[c], 0, nullptr, all.data(), 0, 0);''')
patch(R, '''                for (size_t j = 0; j < kInstPerCb && c * kInstPerCb + j < refTexInst_.size(); ++j)
                    std::memcpy(&all[j * 64], refTexInst_[c * kInstPerCb + j].params, sizeof(refTexInst_[0].params));''',
      '''                for (size_t j = 0; j < kInstPerCb && c * kInstPerCb + j < refTexInst_.size(); ++j) {
                    const GpuTexInstanced& gi = refTexInst_[c * kInstPerCb + j];
                    std::memcpy(&all[j * 64], gi.params, sizeof(gi.params));
                    if (textureRawAlpha(gi.texture)) all[j * 64 + 1] = 2.0f;  // opaque: alpha is the specular code
                }''')

# ---------------------------------------------------------------- shaders: raw spec = no alpha test
patch(R, '''void ps_shadow_clip(PSIn i) { clip(tex0.Sample(samp0, i.uv).a - 0.3); }
// The depth pre-pass: the shading pass's own cut-out threshold and the coarse terrain cut, so the depths match.
void ps_prepass(PSIn i) {
    clip(tex0.Sample(samp0, i.uv).a - (game0.x > 0.5 && batchFlags2.z > 0.5 ? 0.5 : 0.3));
    if (gameTerrainCut.w > 0.5 && batchFlags2.z > 0.5 && i.col.a > 0.99 && i.col.a < 0.998 && distance(i.w.xy, gameTerrainCut.xy) < gameTerrainCut.z) discard;
}''', '')
patch(R, '''cbuffer Layer : register(b7) { float4 layerP; float4 layerTint; float4 layerUvT; float4 layerUvO; };''',
      '''cbuffer Layer : register(b7) { float4 layerP; float4 layerTint; float4 layerUvT; float4 layerUvO; };
// Opaque with the stock specular code in alpha (block-compressed stock images): no alpha test. Editor batches say so
// with blend mode -2, stock batches and props with layerP.y = 2.
bool rawSpecAlpha() { return batchFlags.y < -1.5 || (layerP.x < 0.5 && layerP.y > 1.5); }
void ps_shadow_clip(PSIn i) {
    if (!rawSpecAlpha()) clip(tex0.Sample(samp0, i.uv).a - 0.3);
}
// The depth pre-pass: the shading pass's own cut-out threshold and the coarse terrain cut, so the depths match.
void ps_prepass(PSIn i) {
    if (!rawSpecAlpha()) clip(tex0.Sample(samp0, i.uv).a - (game0.x > 0.5 && batchFlags2.z > 0.5 ? 0.5 : 0.3));
    if (gameTerrainCut.w > 0.5 && batchFlags2.z > 0.5 && i.col.a > 0.99 && i.col.a < 0.998 && distance(i.w.xy, gameTerrainCut.xy) < gameTerrainCut.z) discard;
}''')
patch(R, '''    if (batchFlags.y < 0.5) {
        const float cutAt = game0.x > 0.5 && batchFlags2.z > 0.5 ? 0.5 : 0.3;''', '''    if (batchFlags.y < 0.5 && !rawSpecAlpha()) {
        const float cutAt = game0.x > 0.5 && batchFlags2.z > 0.5 ? 0.5 : 0.3;''')
patch(R, '''    } else {
        outA = t.a > 0.99 ? 0.6 : t.a;
    }
    if (layerB >= 0.0) outA = layerB;''', '''    } else if (batchFlags.y > 0.5) {
        outA = t.a > 0.99 ? 0.6 : t.a;
    }
    if (layerB >= 0.0) outA = layerB;''')
patch(R, '''                if (layerP.y > 0.5 && t.a < 0.999) {
                    const float sCode = saturate((t.a * 255.0 - 192.0) / 62.0) * layerTint.x + layerTint.y;''', '''                // The specular code: raw in a block-compressed image (layerP.y = 2), 192..254 in an RGBA one (1).
                const float aCode = layerP.y > 1.5 ? t.a : layerP.y > 0.5 && t.a < 0.999 ? saturate((t.a * 255.0 - 192.0) / 62.0) : -1.0;
                if (aCode >= 0.0) {
                    const float sCode = aCode * layerTint.x + layerTint.y;''')

# ---------------------------------------------------------------- draw paths: editor batches say raw with blend -2
patch(R, '''                const float flags[8] = {hasNormal ? 1.0f : 0.0f, float(b.blend), b.sway ? 1.0f : 0.0f, b.baseZ,''',
      '''                const float flags[8] = {hasNormal ? 1.0f : 0.0f, b.blend == 0 && textureRawAlpha(b.texture) ? -2.0f : float(b.blend), b.sway ? 1.0f : 0.0f, b.baseZ,''')
patch(R, '''                const float flags[8] = {0, float(b.blend), b.sway ? 1.0f : 0.0f, b.baseZ, 0, 0, 0, 0};''',
      '''                const float flags[8] = {0, b.blend == 0 && textureRawAlpha(b.texture) ? -2.0f : float(b.blend), b.sway ? 1.0f : 0.0f, b.baseZ, 0, 0, 0, 0};''')
# shadow pass, editor batches: per batch flags (raw = no clip), b1 and a zero b7 on the pixel shader
patch(R, '''            for (const BatchDraw& d : drawList_) {
                const SceneBatch& b = *d.b;
                if (b.blend != 0 || b.v.front().rgba >> 24 < 0x80) continue;
                if (psShadowClip_ && b.texture >= 0 && size_t(b.texture) < textures_.size())
                    ctx_->PSSetShaderResources(0, 1, &textures_[size_t(b.texture)]);''', '''            if (cbBatch_) ctx_->PSSetConstantBuffers(1, 1, &cbBatch_);
            if (cbLayer_) ctx_->PSSetConstantBuffers(7, 1, &cbLayer_);
            bool lastRaw = false;
            for (const BatchDraw& d : drawList_) {
                const SceneBatch& b = *d.b;
                if (b.blend != 0 || b.v.front().rgba >> 24 < 0x80) continue;
                if (psShadowClip_ && b.texture >= 0 && size_t(b.texture) < textures_.size())
                    ctx_->PSSetShaderResources(0, 1, &textures_[size_t(b.texture)]);
                if (const bool raw = textureRawAlpha(b.texture); raw != lastRaw && cbBatch_) {
                    const float rf[8] = {0, raw ? -2.0f : 0.0f, 0, 0, 0, 0, 0, 0};
                    ctx_->UpdateSubresource(cbBatch_, 0, nullptr, rf, 0, 0);
                    lastRaw = raw;
                }''')
# pre-pass, stock world: each base batch's constants (raw spec) in b7
patch(R, '''            ctx_->IASetVertexBuffers(0, 1, &vbRefTex_, &tstride, &offset);
            for (size_t k = 0; k < refTexIds_.size(); ++k) {
                if (k < refLayers_.size() && (refLayers_[k].layer > 0 || refLayers_[k].decal)) continue;  // layers, decals: later
                const int id = refTexIds_[k] >= 0 ? refTexIds_[k] : whiteTex_;
                if (id < 0 || size_t(id) >= textures_.size()) continue;
                ctx_->PSSetShaderResources(0, 1, &textures_[size_t(id)]);
                if (refCulling_ && k < refTexChunks_.size() && !refTexChunks_[k].empty())''', '''            ctx_->IASetVertexBuffers(0, 1, &vbRefTex_, &tstride, &offset);
            const bool preCb = uploadLayerParams();
            for (size_t k = 0; k < refTexIds_.size(); ++k) {
                if (k < refLayers_.size() && (refLayers_[k].layer > 0 || refLayers_[k].decal)) continue;  // layers, decals: later
                const int id = refTexIds_[k] >= 0 ? refTexIds_[k] : whiteTex_;
                if (id < 0 || size_t(id) >= textures_.size()) continue;
                ctx_->PSSetShaderResources(0, 1, &textures_[size_t(id)]);
                if (preCb && cbLayerAll_[k / 256]) {
                    const UINT first = UINT((k % 256) * 16), num = 16;
                    ctx1_->PSSetConstantBuffers1(7, 1, &cbLayerAll_[k / 256], &first, &num);
                }
                if (refCulling_ && k < refTexChunks_.size() && !refTexChunks_[k].empty())''')
# the pre-pass's editor batches: a zero b7
patch(R, '''        ctx_->PSSetShader(psPrepass_, nullptr, 0);
        ctx_->PSSetSamplers(0, 1, &sampler_);
        if (cbBatch_) ctx_->VSSetConstantBuffers(1, 1, &cbBatch_), ctx_->PSSetConstantBuffers(1, 1, &cbBatch_);''', '''        ctx_->PSSetShader(psPrepass_, nullptr, 0);
        ctx_->PSSetSamplers(0, 1, &sampler_);
        if (cbBatch_) ctx_->VSSetConstantBuffers(1, 1, &cbBatch_), ctx_->PSSetConstantBuffers(1, 1, &cbBatch_);
        if (cbLayer_) ctx_->PSSetConstantBuffers(7, 1, &cbLayer_);''')
print('ok')
