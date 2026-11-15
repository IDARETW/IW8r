import io

# Every compile of a built-in shader (and of a map's custom surface shader / reference filter) goes through
# compileShaderCached (src/viewport/shader_cache.h); SceneRenderer::prewarmShaders() compiles the built-in ones on
# worker threads at start-up.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if s.count(b) >= count and s.count(a) == 0:
        return  # applied already
    assert s.count(a) == count, (p, s.count(a), a[:90])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


R = 'src/viewport/render.cpp'
patch(R, '#include "stock_probes.h"\n', '#include "stock_probes.h"\n#include "shader_cache.h"\n')
patch(R, 'D3DCompile(kShader, std::strlen(kShader), "iw8r_scene", nullptr, nullptr, entry, target, 0, 0, &out, &errb)',
      'compileShaderCached(kShader, std::strlen(kShader), "iw8r_scene", entry, target, 0, &out, &errb)')
patch(R, 'D3DCompile(kTexShader, std::strlen(kTexShader), "iw8r_tex", nullptr, nullptr, entry, target, 0, 0, &out, &errb)',
      'compileShaderCached(kTexShader, std::strlen(kTexShader), "iw8r_tex", entry, target, 0, &out, &errb)')
patch(R, 'D3DCompile(kParticleShader, std::strlen(kParticleShader), "iw8r_part", nullptr, nullptr, "vs_part", "vs_5_0", 0, 0, &vsb, &errb)',
      'compileShaderCached(kParticleShader, std::strlen(kParticleShader), "iw8r_part", "vs_part", "vs_5_0", 0, &vsb, &errb)')
patch(R, 'D3DCompile(kParticleShader, std::strlen(kParticleShader), "iw8r_part", nullptr, nullptr, "ps_part", "ps_5_0", 0, 0, &psb, &errb)',
      'compileShaderCached(kParticleShader, std::strlen(kParticleShader), "iw8r_part", "ps_part", "ps_5_0", 0, &psb, &errb)')
patch(R, 'D3DCompile(kAoShader, std::strlen(kAoShader), "iw8r_ao", nullptr, nullptr, entry, target, 0, 0, &out, &errb)',
      'compileShaderCached(kAoShader, std::strlen(kAoShader), "iw8r_ao", entry, target, 0, &out, &errb)')
patch(R, 'D3DCompile(kMeterShader, std::strlen(kMeterShader), "iw8r_meter", nullptr, nullptr, "vs_full", "vs_5_0", 0, 0, &vsb, &errb)',
      'compileShaderCached(kMeterShader, std::strlen(kMeterShader), "iw8r_meter", "vs_full", "vs_5_0", 0, &vsb, &errb)')
patch(R, 'D3DCompile(kMeterShader, std::strlen(kMeterShader), "iw8r_meter", nullptr, nullptr, "ps_meter", "ps_5_0", 0, 0, &psb, &errb)',
      'compileShaderCached(kMeterShader, std::strlen(kMeterShader), "iw8r_meter", "ps_meter", "ps_5_0", 0, &psb, &errb)')
patch(R, 'D3DCompile(kMeterShader, std::strlen(kMeterShader), "iw8r_meter", nullptr, nullptr, "ps_meter_hdr", "ps_5_0", 0, 0, &hb, nullptr)',
      'compileShaderCached(kMeterShader, std::strlen(kMeterShader), "iw8r_meter", "ps_meter_hdr", "ps_5_0", 0, &hb, nullptr)')
# A map's custom surface shaders and the reference filter: the same cache (the key is the whole source).
patch(R, '''    const HRESULT hr = D3DCompile(source.data(), source.size(), "iw8r_custom", nullptr, nullptr, "ps_tex", "ps_5_0", 0, 0, &blob, &errb);
    if (FAILED(hr)) {
        if (err) *err = errb ? std::string(static_cast<const char*>(errb->GetBufferPointer()), errb->GetBufferSize()) : "D3DCompile failed";
        safeRelease(errb);
        return -1;
    }''', '''    const HRESULT hr = compileShaderCached(source.data(), source.size(), "iw8r_custom", "ps_tex", "ps_5_0", 0, &blob, &errb);
    if (FAILED(hr)) {
        if (err) *err = errb ? std::string(static_cast<const char*>(errb->GetBufferPointer()), errb->GetBufferSize()) : "D3DCompile failed";
        safeRelease(errb);
        return -1;
    }''')
patch(R, 'D3DCompile(source.data(), source.size(), "iw8r_filter", nullptr, nullptr, "ps_tex", "ps_5_0", 0, 0, &blob, &errb)',
      'compileShaderCached(source.data(), source.size(), "iw8r_filter", "ps_tex", "ps_5_0", 0, &blob, &errb)')

# The prewarm list: every built-in shader, the big textured ones first (the longest compiles).
patch(R, '''bool SceneRenderer::init(ID3D11Device* dev, ID3D11DeviceContext* ctx, std::string* err) {''',
      '''void SceneRenderer::prewarmShaders() {
    runShaderPrewarm({
        {kTexShader, "iw8r_tex", "ps_tex", "ps_5_0"},
        {kTexShader, "iw8r_tex", "ps_prepass", "ps_5_0"},
        {kTexShader, "iw8r_tex", "ps_shadow_clip", "ps_5_0"},
        {kTexShader, "iw8r_tex", "vs_tex", "vs_5_0"},
        {kTexShader, "iw8r_tex", "vs_tex_inst", "vs_5_0"},
        {kTexShader, "iw8r_tex", "ps_post", "ps_5_0"},
        {kTexShader, "iw8r_tex", "vs_post", "vs_5_0"},
        {kTexShader, "iw8r_tex", "ps_veil_down", "ps_5_0"},
        {kTexShader, "iw8r_tex", "ps_veil_karis", "ps_5_0"},
        {kTexShader, "iw8r_tex", "ps_veil_up", "ps_5_0"},
        {kShader, "iw8r_scene", "vs_main", "vs_5_0"},
        {kShader, "iw8r_scene", "ps_main", "ps_5_0"},
        {kShader, "iw8r_scene", "vs_inst", "vs_5_0"},
        {kAoShader, "iw8r_ao", "vs_full", "vs_5_0"},
        {kAoShader, "iw8r_ao", "ps_ao", "ps_5_0"},
        {kAoShader, "iw8r_ao", "ps_ao_blur", "ps_5_0"},
        {kAoShader, "iw8r_ao", "ps_prevz", "ps_5_0"},
        {kAoShader, "iw8r_ao", "ps_scenez", "ps_5_0"},
        {kAoShader, "iw8r_ao", "ps_prevcolor", "ps_5_0"},
        {kAoShader, "iw8r_ao", "ps_aoz", "ps_5_0"},
        {kAoShader, "iw8r_ao", "ps_aozdown", "ps_5_0"},
        {kAoShader, "iw8r_ao", "ps_ao_ssao", "ps_5_0"},
        {kParticleShader, "iw8r_part", "vs_part", "vs_5_0"},
        {kParticleShader, "iw8r_part", "ps_part", "ps_5_0"},
        {kMeterShader, "iw8r_meter", "vs_full", "vs_5_0"},
        {kMeterShader, "iw8r_meter", "ps_meter", "ps_5_0"},
        {kMeterShader, "iw8r_meter", "ps_meter_hdr", "ps_5_0"},
    });
}

bool SceneRenderer::init(ID3D11Device* dev, ID3D11DeviceContext* ctx, std::string* err) {''')
print('ok')
