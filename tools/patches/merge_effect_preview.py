import io

# Resolves the conflicts of merging refs/heads/effect-preview (200c008, 7b46358) into main after the threading work:
# the distortion map is read by the static readIndex (root, not root_), the particle shaders go through the shader
# cache (ps_dist added to the prewarm list), the changelog keeps both entries.

ROOT = 'E:/IW8-Radiant/'


def rw(p, fn):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    s = fn(s.replace('\r\n', '\n'))
    assert '<<<<<<<' not in s and '>>>>>>>' not in s, p
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))


def sub(s, a, b):
    assert s.count(a) == 1, (s.count(a), a[:80])
    return s.replace(a, b)


rw('src/qtapp/material_cache.cpp', lambda s: sub(s, '''<<<<<<< HEAD
        e.transmissionDds = transmission.isEmpty() ? QString() : QDir(root).filePath(transmission);
=======
        e.transmissionDds = transmission.isEmpty() ? QString() : QDir(root_).filePath(transmission);
        const QString distortion = o.value("distortion").toString();
        e.distortionDds = distortion.isEmpty() ? QString() : QDir(root_).filePath(distortion);
>>>>>>> refs/heads/effect-preview
''', '''        e.transmissionDds = transmission.isEmpty() ? QString() : QDir(root).filePath(transmission);
        const QString distortion = o.value("distortion").toString();
        e.distortionDds = distortion.isEmpty() ? QString() : QDir(root).filePath(distortion);
'''))


def render(s):
    s = sub(s, '''<<<<<<< HEAD
    ID3DBlob *vsb = nullptr, *psb = nullptr, *errb = nullptr;
    const bool ok = SUCCEEDED(compileShaderCached(kParticleShader, std::strlen(kParticleShader), "iw8r_part", "vs_part", "vs_5_0", 0, &vsb, &errb)) &&
                    SUCCEEDED(compileShaderCached(kParticleShader, std::strlen(kParticleShader), "iw8r_part", "ps_part", "ps_5_0", 0, &psb, &errb));
    if (errb) OutputDebugStringA(static_cast<const char*>(errb->GetBufferPointer()));
    safeRelease(errb);
=======
    ID3DBlob *vsb = nullptr, *psb = nullptr, *pdb = nullptr, *errb = nullptr;
    const auto compile = [&](const char* entry, const char* target, ID3DBlob*& out) {
        const bool good = SUCCEEDED(D3DCompile(kParticleShader, std::strlen(kParticleShader), "iw8r_part", nullptr, nullptr, entry, target, 0, 0, &out, &errb));
        if (errb) OutputDebugStringA(static_cast<const char*>(errb->GetBufferPointer()));
        safeRelease(errb);
        return good;
    };
    const bool ok = compile("vs_part", "vs_5_0", vsb) && compile("ps_part", "ps_5_0", psb) && compile("ps_dist", "ps_5_0", pdb);
>>>>>>> refs/heads/effect-preview
''', '''    ID3DBlob *vsb = nullptr, *psb = nullptr, *pdb = nullptr, *errb = nullptr;
    const auto compile = [&](const char* entry, const char* target, ID3DBlob*& out) {
        const bool good = SUCCEEDED(compileShaderCached(kParticleShader, std::strlen(kParticleShader), "iw8r_part", entry, target, 0, &out, &errb));
        if (errb) OutputDebugStringA(static_cast<const char*>(errb->GetBufferPointer()));
        safeRelease(errb);
        return good;
    };
    const bool ok = compile("vs_part", "vs_5_0", vsb) && compile("ps_part", "ps_5_0", psb) && compile("ps_dist", "ps_5_0", pdb);
''')
    s = sub(s, '''        {kParticleShader, "iw8r_part", "ps_part", "ps_5_0"},
''', '''        {kParticleShader, "iw8r_part", "ps_part", "ps_5_0"},
        {kParticleShader, "iw8r_part", "ps_dist", "ps_5_0"},
''')
    return s


rw('src/viewport/render.cpp', render)


def changelog(s):
    i = s.index('<<<<<<< HEAD\n')
    j = s.index('=======\n', i)
    k = s.index('>>>>>>> refs/heads/effect-preview\n', j)
    ours = s[i + len('<<<<<<< HEAD\n'):j]
    theirs = s[j + len('=======\n'):k]
    return s[:i] + ours + theirs + s[k + len('>>>>>>> refs/heads/effect-preview\n'):]


rw('CHANGELOG.md', changelog)
print('ok')
