import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:100])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/viewport/render.h', [
 ('''    ID3D11Buffer* cbLayer_ = nullptr;''', '''    ID3D11Buffer* cbLayer_ = nullptr;
    // Every layer / decal batch's constants in one buffer, 256 bytes per batch, bound per draw with a D3D 11.1 offset
    // (Hideout: 4,300 draws a frame each copying its constants cost 40 ms). Rebuilt when a batch's constants change.
    struct ID3D11DeviceContext1* ctx1_ = nullptr;
    ID3D11Buffer* cbLayerAll_ = nullptr;
    size_t cbLayerAllBatches_ = 0;
    bool layerParamsDirty_ = true;'''),
])
patch('src/viewport/render.cpp', [
 # mark dirty whenever constants change
 ('''    L.ready = true;
    L.reveal = revealTexture;
    std::memcpy(L.params, params, sizeof(L.params));
    refDecalDirty_ = true;
}''', '''    L.ready = true;
    L.reveal = revealTexture;
    std::memcpy(L.params, params, sizeof(L.params));
    refDecalDirty_ = true;
    layerParamsDirty_ = true;
}'''),
 ('''    RefLayer& L = refLayers_[batch];
    L.ready = true;
    L.reveal = revealTexture;
    std::memcpy(L.params, params, sizeof(L.params));
}''', '''    RefLayer& L = refLayers_[batch];
    L.ready = true;
    L.reveal = revealTexture;
    std::memcpy(L.params, params, sizeof(L.params));
    layerParamsDirty_ = true;
}'''),
 ('''    safeRelease(cbLayer_);''', '''    safeRelease(cbLayer_);
    safeRelease(cbLayerAll_);
    if (ctx1_) ctx1_->Release(), ctx1_ = nullptr;'''),
 # before the loop: the all-batches buffer
 ('''            ID3D11Buffer* boundRefVb = vbRefTex_;''', '''            ID3D11Buffer* boundRefVb = vbRefTex_;
            if (!ctx1_ && ctx_) ctx_->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&ctx1_));
            if (ctx1_ && (layerParamsDirty_ || cbLayerAllBatches_ != refLayers_.size())) {
                layerParamsDirty_ = false;
                std::vector<float> all(refLayers_.size() * 64, 0.0f);  // 256 bytes (16 constants) per batch
                for (size_t k = 0; k < refLayers_.size(); ++k) std::memcpy(&all[k * 64], refLayers_[k].params, sizeof(refLayers_[k].params));
                if (cbLayerAllBatches_ != refLayers_.size()) {
                    safeRelease(cbLayerAll_);
                    cbLayerAllBatches_ = 0;
                    if (!all.empty()) {
                        D3D11_BUFFER_DESC bd{};
                        bd.ByteWidth = UINT(all.size() * sizeof(float));
                        bd.Usage = D3D11_USAGE_DEFAULT;
                        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
                        if (SUCCEEDED(dev_->CreateBuffer(&bd, nullptr, &cbLayerAll_))) cbLayerAllBatches_ = refLayers_.size();
                    }
                }
                if (cbLayerAll_) ctx_->UpdateSubresource(cbLayerAll_, 0, nullptr, all.data(), 0, 0);
            }
            const bool offsetCb = ctx1_ && cbLayerAll_ && cbLayerAllBatches_ == refLayers_.size();
            int lastFlags = -1;'''),
 ('''                if (cbBatch_) {
                    const bool volume = L.params[0] > 1.5f;  // decal volumes carry their box UV in the tangent: no normal map
                    const float flags[8] = {refNormals && !volume ? 1.0f : 0.0f, 1.0f, 0, 0, 0, 0, 1.0f, stockLmSrv_[0] ? 1.0f : 0.0f};
                    ctx_->UpdateSubresource(cbBatch_, 0, nullptr, flags, 0, 0);
                }
                const int rv = L.reveal >= 0 && size_t(L.reveal) < textures_.size() ? L.reveal : whiteFullTex_;
                if (rv < 0 || size_t(rv) >= textures_.size()) continue;
                ctx_->UpdateSubresource(cbLayer_, 0, nullptr, L.params, 0, 0);''', '''                const bool volume = L.params[0] > 1.5f;  // decal volumes carry their box UV in the tangent: no normal map
                if (cbBatch_ && int(volume) != lastFlags) {
                    lastFlags = int(volume);
                    const float flags[8] = {refNormals && !volume ? 1.0f : 0.0f, 1.0f, 0, 0, 0, 0, 1.0f, stockLmSrv_[0] ? 1.0f : 0.0f};
                    ctx_->UpdateSubresource(cbBatch_, 0, nullptr, flags, 0, 0);
                }
                const int rv = L.reveal >= 0 && size_t(L.reveal) < textures_.size() ? L.reveal : whiteFullTex_;
                if (rv < 0 || size_t(rv) >= textures_.size()) continue;
                if (offsetCb) {
                    const UINT first = UINT(k * 16), num = 16;
                    ctx1_->PSSetConstantBuffers1(7, 1, &cbLayerAll_, &first, &num);
                } else {
                    ctx_->UpdateSubresource(cbLayer_, 0, nullptr, L.params, 0, 0);
                }'''),
 ('''            const float zero[16] = {};
            ctx_->UpdateSubresource(cbLayer_, 0, nullptr, zero, 0, 0);''', '''            const float zero[16] = {};
            ctx_->UpdateSubresource(cbLayer_, 0, nullptr, zero, 0, 0);
            ctx_->PSSetConstantBuffers(7, 1, &cbLayer_);  // back to the plain (zero) layer buffer'''),
])
s = io.open('src/viewport/render.cpp', encoding='utf-8', newline='').read()
if '#include <d3d11_1.h>' not in s:
    s = s.replace('#include <d3d11.h>', '#include <d3d11_1.h>', 1)
    assert '#include <d3d11_1.h>' in s
io.open('src/viewport/render.cpp', 'w', encoding='utf-8', newline='').write(s)
print('ok')
