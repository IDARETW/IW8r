import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'src/core/custom_materials.h'
patch(H, '''    int sortKey = 0;
    int polygonOffset = 0;
    bool hasTint() const { return tint[0] != 1 || tint[1] != 1 || tint[2] != 1; }
};''', '''    int sortKey = 0;
    int polygonOffset = 0;
    // "shader": a custom surface shader file (core custom_shader, *.iw8shader.hlsl, relative to the definition):
    // the Game Lighting viewport and the build light the material with it. Null = the game's own lighting.
    std::shared_ptr<const CustomShader> shader;
    bool hasTint() const { return tint[0] != 1 || tint[1] != 1 || tint[2] != 1; }
};''')
s = io.open(H, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
if '#include "custom_shader.h"' not in s:
    i = s.index('#include')
    s = s[:i] + '#include <memory>' + nl + '#include "custom_shader.h"' + nl + s[i:]
    io.open(H, 'w', encoding='utf-8', newline='').write(s)

C = 'src/core/custom_materials.cpp'
patch(C, '''    if (const json::Value* v = doc->get("unlit")) {
        if (v->type != json::Value::Type::Bool) return fail("'unlit' must be true or false");
        m.unlit = v->b;
    }''', '''    if (const json::Value* v = doc->get("unlit")) {
        if (v->type != json::Value::Type::Bool) return fail("'unlit' must be true or false");
        m.unlit = v->b;
    }
    if (const json::Value* v = doc->get("shader")) {
        if (!v->string() || v->string()->empty()) return fail("'shader' must be a file name (*.iw8shader.hlsl)");
        fs::path p = fs::path(*v->string());
        if (!p.is_absolute()) p = base / p;
        auto shader = std::make_shared<CustomShader>();
        std::string serr;
        if (!readCustomShader(p.lexically_normal().string(), *shader, &serr)) return fail("shader: " + serr);
        m.shader = std::move(shader);
    }''')

CH = 'src/qtapp/camera_view.h'
patch(CH, '''    std::string gpuMemoryReport() const { return renderer_.gpuMemoryReport(); }''', '''    std::string gpuMemoryReport() const { return renderer_.gpuMemoryReport(); }
    // A map material's custom surface shader (core custom_shader), or null: compiled into the Game Lighting viewport.
    using ShaderLookup = std::function<std::shared_ptr<const CustomShader>(const std::string& material)>;
    void setCustomShaderLookup(ShaderLookup fn) { shaderLookup_ = std::move(fn); }
    int customShaderFor(const std::string& material);
    // The last compile error of a custom shader ("" = none), for the status bar and the Check Map report.
    const std::string& customShaderError() const { return customShaderError_; }''')
patch(CH, '''    GameFog stockFog_;''', '''    GameFog stockFog_;
    ShaderLookup shaderLookup_;
    std::map<std::string, int> customShaderIds_;  // path + code -> createCustomShader id (-1: failed to compile)
    std::string customShaderError_;''')
s = io.open(CH, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
if '#include "custom_shader.h"' not in s:
    i = s.index('#include "')
    s = s[:i] + '#include "custom_shader.h"' + nl + s[i:]
if '#include <map>' not in s:
    i = s.index('#include <')
    s = s[:i] + '#include <map>' + nl + s[i:]
io.open(CH, 'w', encoding='utf-8', newline='').write(s)

C2 = 'src/qtapp/camera_view.cpp'
s = io.open(C2, encoding='utf-8', newline='').read()
i = s.index('        ctx_->scene.emissiveFor = ')
j = s.index(';', s.index('\n', i + 1) if s.count('\n', i, i + 400) else i)  # end of that statement
nl = '\r\n' if '\r\n' in s else '\n'
# insert after the emissiveFor statement (find the statement end: the first ";\n" after i)
end = s.index(';' + nl, i) + 1 + len(nl)
ins = ('        ctx_->scene.shaderFor = shaderLookup_ ? std::function<int(const std::string&)>([this](const std::string& m) { return customShaderFor(m); })' + nl +
       '                                              : std::function<int(const std::string&)>();' + nl)
s = s[:end] + ins + s[end:]
patch_marker = 'int CameraView::emissiveFor(const std::string& material) {'
k = s.index(patch_marker)
fn = '''int CameraView::customShaderFor(const std::string& material) {
    if (!shaderLookup_ || !rendererOk_) return -1;
    const std::shared_ptr<const CustomShader> shader = shaderLookup_(material);
    if (!shader) return -1;
    const std::string key = shader->path + '\\n' + shader->code;
    if (const auto it = customShaderIds_.find(key); it != customShaderIds_.end()) return it->second;
    std::string err;
    const std::string src = customShaderViewportSource(app::SceneRenderer::texShaderSource(), "float4 ps_tex(PSIn i) : SV_TARGET {", *shader);
    const int id = src.empty() ? -1 : renderer_.createCustomShader(src, &err);
    customShaderIds_[key] = id;
    if (id < 0) {
        customShaderError_ = shader->name + ": " + (err.empty() ? std::string("cannot be built into the viewport shader") : err);
        if (ctx_) emit ctx_->statusMessage(QString::fromStdString("Custom shader " + customShaderError_));
    }
    return id;
}

'''.replace('\n', nl)
s = s[:k] + fn + s[k:]
io.open(C2, 'w', encoding='utf-8', newline='').write(s)
print('ok')
