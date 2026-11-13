import io
p = 'src/viewport/render.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'


def rep(a, b):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, a[:80]
    s = s.replace(a, b)


rep('''            int lastFlags = -1;''', '''            int lastFlags = -1;
            ID3D11BlendState* lastBlend = nullptr;
            ID3D11ShaderResourceView *lastCol = nullptr, *lastRev = nullptr;
            static const bool noPropDecals = perfOff("propdecals");  // benchmark switch'''),
rep('''                const RefLayer& L = refLayers_[k];
                if (!L.ready || refTexIds_[k] < 0 || size_t(refTexIds_[k]) >= textures_.size()) continue;
                const bool volume''', '''                const RefLayer& L = refLayers_[k];
                if (!L.ready || refTexIds_[k] < 0 || size_t(refTexIds_[k]) >= textures_.size()) continue;
                if (noPropDecals && k >= refTexExtraFrom_) continue;
                const bool volume'''),
rep('''                {
                    const float bfz[4] = {0, 0, 0, 0};
                    ctx_->OMSetBlendState(L.params[3] > 0.5f && blendMul_ ? blendMul_ : blend_, bfz, 0xffffffff);
                }
                ctx_->PSSetShaderResources(0, 1, &textures_[size_t(refTexIds_[k])]);
                ctx_->PSSetShaderResources(12, 1, &textures_[size_t(rv)]);''', '''                {
                    ID3D11BlendState* want = L.params[3] > 0.5f && blendMul_ ? blendMul_ : blend_;
                    if (want != lastBlend) {
                        const float bfz[4] = {0, 0, 0, 0};
                        ctx_->OMSetBlendState(want, bfz, 0xffffffff);
                        lastBlend = want;
                    }
                }
                if (textures_[size_t(refTexIds_[k])] != lastCol) lastCol = textures_[size_t(refTexIds_[k])], ctx_->PSSetShaderResources(0, 1, &lastCol);
                if (textures_[size_t(rv)] != lastRev) lastRev = textures_[size_t(rv)], ctx_->PSSetShaderResources(12, 1, &lastRev);''')
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
