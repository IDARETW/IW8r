import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


R = 'src/viewport/render.cpp'
patch(R, '''bool SceneRenderer::uploadLayerParams() {''', '''// A/B switches for the research 107 terms: IW8R_NO_EMISSIVE, IW8R_NO_TRANSMISSION, IW8R_NO_WRAP clear their "on" floats
// in RefLayer::extra as it is uploaded.
static void maskExtraTerms(float* extra) {
    static const bool noE = std::getenv("IW8R_NO_EMISSIVE") != nullptr, noT = std::getenv("IW8R_NO_TRANSMISSION") != nullptr,
                      noW = std::getenv("IW8R_NO_WRAP") != nullptr;
    if (noE) extra[3] = 0;
    if (noT) extra[7] = 0;
    if (noW) extra[11] = 0;
}

bool SceneRenderer::uploadLayerParams() {''')
patch(R, '''                std::memcpy(&all[j * 64 + 16], refLayers_[k].extra, sizeof(refLayers_[0].extra));''',
      '''                std::memcpy(&all[j * 64 + 16], refLayers_[k].extra, sizeof(refLayers_[0].extra));
                maskExtraTerms(&all[j * 64 + 16]);''')
patch(R, '''                std::memcpy(&all[j * 64 + 16], gi.extra, sizeof(gi.extra));''', '''                std::memcpy(&all[j * 64 + 16], gi.extra, sizeof(gi.extra));
                maskExtraTerms(&all[j * 64 + 16]);''')
print('ok')
