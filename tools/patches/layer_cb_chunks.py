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
 ('''    ID3D11Buffer* cbLayerAll_ = nullptr;
    size_t cbLayerAllBatches_ = 0;''', '''    std::vector<ID3D11Buffer*> cbLayerAll_;  // 64 KB each: 256 batches (a 1.3 MB buffer lost the device on Hideout)
    size_t cbLayerAllBatches_ = 0;'''),
])
patch('src/viewport/render.cpp', [
 ('''    safeRelease(cbLayerAll_);
    if (ctx1_) ctx1_->Release(), ctx1_ = nullptr;''', '''    for (auto*& b : cbLayerAll_) safeRelease(b);
    cbLayerAll_.clear();
    if (ctx1_) ctx1_->Release(), ctx1_ = nullptr;'''),
 ('''            if (ctx1_ && refLayers_.size() <= 4096 && (layerParamsDirty_ || cbLayerAllBatches_ != refLayers_.size())) {
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
            const bool offsetCb = ctx1_ && cbLayerAll_ && cbLayerAllBatches_ == refLayers_.size();''',
  '''            constexpr size_t kPerCb = 256;  // batches per 64 KB buffer (256 bytes each)
            if (ctx1_ && (layerParamsDirty_ || cbLayerAllBatches_ != refLayers_.size())) {
                layerParamsDirty_ = false;
                const size_t chunks = (refLayers_.size() + kPerCb - 1) / kPerCb;
                if (cbLayerAllBatches_ != refLayers_.size()) {
                    for (auto*& b : cbLayerAll_) safeRelease(b);
                    cbLayerAll_.assign(chunks, nullptr);
                    cbLayerAllBatches_ = refLayers_.size();
                    D3D11_BUFFER_DESC bd{};
                    bd.ByteWidth = UINT(kPerCb * 256);
                    bd.Usage = D3D11_USAGE_DEFAULT;
                    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
                    for (auto*& b : cbLayerAll_)
                        if (FAILED(dev_->CreateBuffer(&bd, nullptr, &b))) cbLayerAllBatches_ = 0;
                }
                std::vector<float> all(kPerCb * 64, 0.0f);  // 256 bytes (16 constants) per batch
                for (size_t c = 0; c < cbLayerAll_.size() && cbLayerAllBatches_; ++c) {
                    std::fill(all.begin(), all.end(), 0.0f);
                    for (size_t j = 0; j < kPerCb && c * kPerCb + j < refLayers_.size(); ++j)
                        std::memcpy(&all[j * 64], refLayers_[c * kPerCb + j].params, sizeof(refLayers_[0].params));
                    if (cbLayerAll_[c]) ctx_->UpdateSubresource(cbLayerAll_[c], 0, nullptr, all.data(), 0, 0);
                }
            }
            const bool offsetCb = ctx1_ && !cbLayerAll_.empty() && cbLayerAllBatches_ == refLayers_.size();'''),
 ('''                if (offsetCb) {
                    const UINT first = UINT(k * 16), num = 16;
                    ctx1_->PSSetConstantBuffers1(7, 1, &cbLayerAll_, &first, &num);
                } else {''', '''                if (offsetCb && cbLayerAll_[k / kPerCb]) {
                    const UINT first = UINT((k % kPerCb) * 16), num = 16;
                    ctx1_->PSSetConstantBuffers1(7, 1, &cbLayerAll_[k / kPerCb], &first, &num);
                } else {'''),
])
print('ok')
