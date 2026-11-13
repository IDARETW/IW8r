import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


R = 'src/viewport/render.cpp'
patch(R, '''    const auto upload = [&](GpuTexInstanced& g) {
        g.big = unsigned(keepBig.size() / 24);
        if (g.big && g.bigInst) {
            D3D11_MAPPED_SUBRESOURCE mb;
            if (SUCCEEDED(ctx_->Map(g.bigInst, 0, D3D11_MAP_WRITE_DISCARD, 0, &mb))) {
                std::memcpy(mb.pData, keepBig.data(), keepBig.size() * sizeof(float));
                ctx_->Unmap(g.bigInst, 0);
            } else {
                g.big = 0;
            }
        }
        g.visible = unsigned(keep.size() / 24);
        if (g.visible) {
            D3D11_MAPPED_SUBRESOURCE ms;
            if (SUCCEEDED(ctx_->Map(g.visInst, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
                std::memcpy(ms.pData, keep.data(), keep.size() * sizeof(float));
                ctx_->Unmap(g.visInst, 0);
            } else {
                g.visible = g.instances;  // fall back to every placement
            }
        }
    };''', '''    // The visible placements, those large on screen first: the depth pre-pass draws only that prefix (`big`). One
    // buffer instead of two of the full size (Verdansk's instance buffers were 877 MB with every prop drawn).
    const auto upload = [&](GpuTexInstanced& g) {
        g.big = unsigned(keepBig.size() / 24);
        g.visible = unsigned((keepBig.size() + keep.size()) / 24);
        if (g.visible) {
            D3D11_MAPPED_SUBRESOURCE ms;
            if (SUCCEEDED(ctx_->Map(g.visInst, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
                std::memcpy(ms.pData, keepBig.data(), keepBig.size() * sizeof(float));
                std::memcpy(static_cast<char*>(ms.pData) + keepBig.size() * sizeof(float), keep.data(), keep.size() * sizeof(float));
                ctx_->Unmap(g.visInst, 0);
            } else {
                g.visible = g.instances;  // fall back to every placement
                g.big = 0;
            }
        }
    };''')
patch(R, '''                const float* r = &g.rows[i * 12];
                keep.insert(keep.end(), r, r + 12);
                keep.insert(keep.end(), &g.light[i * 12], &g.light[i * 12] + 12);
                if (e.dist <= e.rad || e.rad >= e.dist * pixelAngle * bigPx)
                    keepBig.insert(keepBig.end(), r, r + 12), keepBig.insert(keepBig.end(), &g.light[i * 12], &g.light[i * 12] + 12);''', '''                const float* r = &g.rows[i * 12];
                std::vector<float>& to = e.dist <= e.rad || e.rad >= e.dist * pixelAngle * bigPx ? keepBig : keep;
                to.insert(to.end(), r, r + 12);
                to.insert(to.end(), &g.light[i * 12], &g.light[i * 12] + 12);''')
patch(R, '''            keep.insert(keep.end(), r, r + 12);
            keep.insert(keep.end(), &g.light[i * 12], &g.light[i * 12] + 12);
            {
                const float dx = c[0] - eye[0], dy = c[1] - eye[1], dz = c[2] - eye[2];
                const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (dist <= rad || rad >= dist * pixelAngle * bigPx)
                    keepBig.insert(keepBig.end(), r, r + 12), keepBig.insert(keepBig.end(), &g.light[i * 12], &g.light[i * 12] + 12);
            }''', '''            {
                const float dx = c[0] - eye[0], dy = c[1] - eye[1], dz = c[2] - eye[2];
                const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                std::vector<float>& to = dist <= rad || rad >= dist * pixelAngle * bigPx ? keepBig : keep;
                to.insert(to.end(), r, r + 12);
                to.insert(to.end(), &g.light[i * 12], &g.light[i * 12] + 12);
            }''')
patch(R, '''safeRelease(g.inst), safeRelease(g.visInst), safeRelease(g.bigInst);''', '''safeRelease(g.inst), safeRelease(g.visInst);''', 2)
patch(R, '''propInst += bufferBytes(g.inst) + bufferBytes(g.visInst) + bufferBytes(g.bigInst);''', '''propInst += bufferBytes(g.inst) + bufferBytes(g.visInst);''')
patch(R, '''                    dev_->CreateBuffer(&dd, nullptr, &g.bigInst);
''', '')
patch(R, '''        const bool prepassBig = texPsOverride_ && texPsOverride_ == psPrepass_ && culled && g.bigInst;''',
      '''        const bool prepassBig = texPsOverride_ && texPsOverride_ == psPrepass_ && culled;''')
patch(R, '''        ID3D11Buffer* bufs[2] = {g.vb, prepassBig ? g.bigInst : culled ? g.visInst : g.inst};''',
      '''        ID3D11Buffer* bufs[2] = {g.vb, culled ? g.visInst : g.inst};  // the pre-pass: the visible buffer's large prefix''')
RH = 'src/viewport/render.h'
s = io.open(RH, encoding='utf-8', newline='').read()
i = s.index('ID3D11Buffer* bigInst = nullptr;')
line_start = s.rfind('\n', 0, i) + 1
line_end = s.index('\n', i) + 1
print('removing:', repr(s[line_start:line_end]))
s = s[:line_start] + s[line_end:]
io.open(RH, 'w', encoding='utf-8', newline='').write(s)
print('ok')
