import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


R = 'src/viewport/render.cpp'
patch(R, '''            const auto bind = [&] {
                // Batches that share a texture (the plain one while images load) do not bind it again.
                if (bound != textures_[size_t(id)]) ctx_->PSSetShaderResources(0, 1, &textures_[size_t(id)]), bound = textures_[size_t(id)];
                bindNormal(k);''', '''            const auto bind = [&] {
                // Batches that share a texture (the plain one while images load) do not bind it again.
                if (bound != textures_[size_t(id)]) ctx_->PSSetShaderResources(0, 1, &textures_[size_t(id)]), bound = textures_[size_t(id)];
                bindNormal(k);
                // Research 107: the batch's emissive (t3) and transmission (t19) images, when its material has them.
                if (k < refLayers_.size() && (refLayers_[k].emissive >= 0 || refLayers_[k].transmission >= 0)) {
                    const RefLayer& L = refLayers_[k];
                    ID3D11ShaderResourceView* e = L.emissive >= 0 && size_t(L.emissive) < textures_.size() ? textures_[size_t(L.emissive)] : nullptr;
                    ID3D11ShaderResourceView* t = L.transmission >= 0 && size_t(L.transmission) < textures_.size() ? textures_[size_t(L.transmission)] : nullptr;
                    ctx_->PSSetShaderResources(3, 1, &e);
                    ctx_->PSSetShaderResources(19, 1, &t);
                }''')
print('ok')
