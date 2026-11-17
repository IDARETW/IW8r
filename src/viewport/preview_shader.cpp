#include "preview_shader.h"
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <list>
#include <map>
#include <regex>
#include <set>

namespace iw8r::app {
namespace {
using Microsoft::WRL::ComPtr;
struct CacheEntry {
    std::string key;
    CompiledPreviewStage stage;
    std::vector<ShaderDiagnostic> warnings;
    size_t bytes = 0;
};
struct Cache {
    std::mutex mutex;
    std::list<CacheEntry> entries;
    size_t bytes = 0, compiled = 0, hits = 0;
};
Cache &cache() {
    static Cache c;
    return c;
}
bool replace(std::string &s, const std::string &from, const std::string &to) {
    const auto p = s.find(from);
    if (p == std::string::npos)
        return false;
    s.replace(p, from.size(), to);
    return true;
}
// The host's entry points contain no raw strings; locate their body and last return without touching other functions.
bool injectReturn(std::string &s, const std::string &function, const std::string &insertion) {
    const auto start = s.find(function + "(");
    if (start == std::string::npos)
        return false;
    const auto open = s.find('{', start);
    if (open == std::string::npos)
        return false;
    size_t end = open + 1;
    int braces = 1;
    for (; end < s.size() && braces; ++end) {
        if (s[end] == '{')
            ++braces;
        else if (s[end] == '}')
            --braces;
    }
    const auto ret = s.rfind("return o;", end);
    if (ret == std::string::npos || ret < open)
        return false;
    s.insert(ret, insertion);
    return true;
}
std::string stageSource(const ShaderStageDocument *s, const ShaderCompileRequest &req, ShaderStage stage,
                        ShaderCompileResult &out) {
    if (!s)
        return defaultPreviewSource(stage);
    if (s->mode == "imported") {
        out.diagnostics.push_back({"Replace the imported " + std::string(shaderStageName(stage)) +
                                   " stage with a preview template before compiling. Original HLSL is unavailable."});
        return {};
    }
    if (s->mode == "graph") {
        auto r = compileShaderGraph(s->graph, stage, req.document.parameters, req.document.resources,
                                    req.graphPreviewStage == stage ? req.graphPreviewNode : std::string());
        out.diagnostics.insert(out.diagnostics.end(), r.diagnostics.begin(), r.diagnostics.end());
        return r.ok ? r.source : std::string();
    }
    if (s->source.find("#include") != std::string::npos) {
        out.diagnostics.push_back({"External #include is not supported in a portable techset document."});
        return {};
    }
    std::string source = s->source;
    const auto expected = stage == ShaderStage::Vertex ? "iw8rPreviewVertex" : "iw8rPreviewPixel";
    if (s->entry != expected)
        source +=
            "\n" +
            std::string(stage == ShaderStage::Vertex ? "void iw8rPreviewVertex(inout IW8RPreviewVertex v) { "
                                                     : "float4 iw8rPreviewPixel(IW8RPreviewSurface s) { return ") +
            s->entry + (stage == ShaderStage::Vertex ? "(v); }\n" : "(s); }\n");
    return "#line 1 \"" + std::string(shaderStageName(stage)) + ".hlsl\"\n" + source + "\n#line 1 \"preview_host\"\n";
}
std::vector<ShaderReflection> reflection(ID3DBlob *blob) {
    std::vector<ShaderReflection> bindings;
    ComPtr<ID3D11ShaderReflection> reflect;
    if (FAILED(D3DReflect(blob->GetBufferPointer(), blob->GetBufferSize(), __uuidof(ID3D11ShaderReflection),
                          reinterpret_cast<void **>(reflect.GetAddressOf()))))
        return bindings;
    D3D11_SHADER_DESC desc{};
    reflect->GetDesc(&desc);
    for (UINT i = 0; i < desc.BoundResources; ++i) {
        D3D11_SHADER_INPUT_BIND_DESC b{};
        reflect->GetResourceBindingDesc(i, &b);
        bindings.push_back({b.Name,
                            b.Type == D3D_SIT_CBUFFER   ? "cbuffer"
                            : b.Type == D3D_SIT_SAMPLER ? "sampler"
                                                        : "resource",
                            b.BindPoint, b.BindCount});
    }
    return bindings;
}
void diagnostics(const std::string &text, ShaderCompileResult &out, bool failed) {
    static const std::regex loc(R"(([^\s()]+)\((\d+)(?:,(\d+)(?:-\d+)?)?\)\s*:\s*(.*))");
    size_t begin = 0;
    while (begin < text.size()) {
        auto end = text.find('\n', begin);
        if (end == std::string::npos)
            end = text.size();
        const auto line = text.substr(begin, end - begin);
        begin = end + 1;
        if (line.empty())
            continue;
        ShaderDiagnostic d;
        d.message = line;
        d.warning = !failed || line.find("warning") != std::string::npos;
        std::smatch m;
        if (std::regex_search(line, m, loc)) {
            d.file = m[1];
            d.line = static_cast<unsigned>(std::stoul(m[2]));
            if (m[3].matched)
                d.column = static_cast<unsigned>(std::stoul(m[3]));
            d.message = m[4];
            if (d.file.rfind("node_", 0) == 0 && d.file.size() > 10)
                d.node = d.file.substr(5, d.file.size() - 10);
        }
        out.diagnostics.push_back(std::move(d));
    }
}
bool compile(const std::string &source, const std::string &adapter, const char *entry, const char *target,
             const std::set<std::string> &allowed, ShaderCompileResult &out) {
    const std::string key = source + '\n' + adapter + '\n' + entry + '\n' + target;
    auto &c = cache();
    {
        std::lock_guard lock(c.mutex);
        for (auto i = c.entries.begin(); i != c.entries.end(); ++i)
            if (i->key == key) {
                out.stages.push_back(i->stage);
                out.diagnostics.insert(out.diagnostics.end(), i->warnings.begin(), i->warnings.end());
                c.entries.splice(c.entries.begin(), c.entries, i);
                ++c.hits;
                return true;
            }
    }
    const auto firstDiagnostic = out.diagnostics.size();
    ComPtr<ID3DBlob> blob, errors;
    if (const char *trace = std::getenv("IW8R_PREVIEW_SHADER_TRACE")) {
        std::fprintf(stderr, "compile %s/%s (%zu bytes)\n", adapter.c_str(), entry, source.size());
        std::fflush(stderr);
        const auto file = std::string(trace) + "_" + adapter + "_" + entry + ".hlsl";
        if (FILE *f = std::fopen(file.c_str(), "wb")) {
            std::fwrite(source.data(), 1, source.size(), f);
            std::fclose(f);
        }
    }
    const HRESULT hr = D3DCompile(source.data(), source.size(), "techset", nullptr, nullptr, entry, target,
                                  D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL1, 0, &blob, &errors);
    {
        std::lock_guard lock(c.mutex);
        ++c.compiled;
    }
    if (errors)
        diagnostics(std::string(static_cast<const char *>(errors->GetBufferPointer()), errors->GetBufferSize()), out,
                    FAILED(hr));
    if (FAILED(hr) || !blob) {
        if (!errors)
            out.diagnostics.push_back({"D3DCompile failed for " + adapter + "/" + entry});
        return false;
    }
    CompiledPreviewStage stage;
    stage.adapter = adapter;
    stage.entry = entry;
    stage.target = target;
    stage.bindings = reflection(blob.Get());
    for (const auto &b : stage.bindings)
        if (!allowed.count(b.name)) {
            out.diagnostics.push_back({"Undeclared preview resource '" + b.name +
                                       "'. Add textures/parameters through the document bindings."});
            return false;
        }
    const auto *p = static_cast<const uint8_t *>(blob->GetBufferPointer());
    stage.bytecode.assign(p, p + blob->GetBufferSize());
    {
        std::lock_guard lock(c.mutex);
        CacheEntry value;
        value.key = key;
        value.stage = stage;
        value.warnings.assign(out.diagnostics.begin() + firstDiagnostic, out.diagnostics.end());
        value.bytes = key.size() + stage.bytecode.size();
        c.bytes += value.bytes;
        c.entries.push_front(std::move(value));
        while (c.entries.size() > 64 || c.bytes > 32 * 1024 * 1024) {
            c.bytes -= c.entries.back().bytes;
            c.entries.pop_back();
        }
    }
    out.stages.push_back(std::move(stage));
    return true;
}
std::set<std::string> allowedResources(const std::string &source) {
    std::set<std::string> names;
    const std::regex r(
        R"(\b(?:cbuffer|Texture\w*(?:<[^>]+>)?|Buffer<[^>]+>|StructuredBuffer<[^>]+>|SamplerState|SamplerComparisonState)\s+(\w+)\s*:\s*register)");
    for (std::sregex_iterator i(source.begin(), source.end(), r), end; i != end; ++i)
        names.insert((*i)[1]);
    return names;
}
const char *vertexApply = R"(
    const uint previewTag = uint(o.edge.x*255+0.5)|(uint(o.edge.y*255+0.5)<<8)|(uint(o.edge.z*255+0.5)<<16);
    if ((iw8rClock.y < 0.5 || previewTag == uint(iw8rClock.y)) && o.pos.w >= 0) {
    IW8RPreviewVertex pv; pv.position=o.w; pv.normal=o.n; pv.tangent=o.t; pv.uv=o.uv; pv.color=o.col;
    iw8rPreviewVertex(pv); o.w=pv.position; o.n=pv.normal; o.t=pv.tangent; o.uv=pv.uv; o.col=pv.color;
    o.pos=mul(float4(o.w,1),viewProj);
    }
)";
const char *surfaceAdapter = R"(
float iw8rPreviewSceneDepth(float2 pixel) { return iw8rSceneDepth(pixel); }
IW8RPreviewSurface iw8rMakeSurface(PSIn i) {
    IW8RPreviewSurface s=(IW8RPreviewSurface)0;
    float4 texel=tex0.Sample(samp0,i.uv);s.albedo=srgbToLinear3(texel.rgb);s.alpha=rawSpecAlpha()?1:texel.a;
    s.position=i.w;s.uv=i.uv;s.color=i.col;s.pixel=i.pos.xy;
    s.normal=dot(i.n,i.n)>1e-6?normalize(i.n):normalize(cross(ddx(i.w),ddy(i.w)));
    float3 tangent=ddx(i.w)*ddy(i.uv).y-ddy(i.w)*ddx(i.uv).y;
    tangent-=s.normal*dot(tangent,s.normal);
    if(dot(tangent,tangent)<1e-12)tangent=abs(s.normal.z)<0.9?cross(float3(0,0,1),s.normal):cross(float3(0,1,0),s.normal);
    s.tangent=normalize(tangent);s.view=normalize(eyeStart.xyz-i.w);s.sunDir=normalize(gameSun.xyz);s.sunColor=gameSunCol.rgb;
    s.ambient=gameSky.rgb;s.sunShadow=sunShadow(i.w);s.viewDepth=mul(float4(i.w,1),viewProj).w;s.roughness=0.6;
    return s;
}
float4 iw8rPreviewFinished(PSIn i,float4 colour) {
    IW8RPreviewSurface s=iw8rMakeSurface(i);s.litColor=colour.rgb;s.alpha=colour.a;
#ifdef IW8R_PREVIEW_MATERIAL
    iw8rPreviewMaterial(s);
#endif
    return iw8rPreviewPixel(s);
}
void ps_preview_depth(PSIn i) {
    IW8RPreviewSurface s=iw8rMakeSurface(i);
#ifdef IW8R_PREVIEW_MATERIAL
    iw8rPreviewMaterial(s);
#endif
    float4 result=iw8rPreviewPixel(s);clip(result.a-0.001);
}
)";

} // namespace

ShaderCompileResult compilePreviewShaders(const ShaderCompileRequest &req, const std::string &host,
                                          const std::string &particleHost) {
    ShaderCompileResult result;
    result.revision = req.revision;
    const auto start = std::chrono::steady_clock::now();
    std::string error;
    if (!validateTechsetDocument(req.document, error)) {
        result.diagnostics.push_back({error});
        return result;
    }
    const TechniqueDefinition *technique = nullptr;
    for (const auto &t : req.document.techniques)
        if (t.id == req.techniqueId)
            technique = &t;
    if (!technique) {
        result.diagnostics.push_back({"No technique selected."});
        return result;
    }
    if (const auto why = technique->previewLimitation(); !why.empty()) {
        result.diagnostics.push_back({why});
        return result;
    }
    result.generatedVertex = stageSource(technique->findStage(ShaderStage::Vertex), req, ShaderStage::Vertex, result);
    result.generatedPixel = stageSource(technique->findStage(ShaderStage::Pixel), req, ShaderStage::Pixel, result);
    if (result.generatedVertex.empty() || result.generatedPixel.empty())
        return result;
    const std::string preamble = previewShaderInterface(req.document.resources);
    // Graph helpers have distinct stage namespaces to allow two generated stages in a single host translation unit.
    std::string vertex = result.generatedVertex;
    for (const char *helper :
         {"iw8rHash", "iw8rNoise", "iw8rFbm", "iw8rVoronoi", "iw8rRotateUV", "iw8rNormalBlend", "iw8rLit"}) {
        size_t p = 0;
        while ((p = vertex.find(helper, p)) != std::string::npos) {
            vertex.insert(p + std::strlen(helper), "VS");
            p += std::strlen(helper) + 2;
        }
    }
    std::string geometry = host;
    // Keep Game Lighting in its original entry point. Calling that large entry from a wrapper makes
    // FXC's inliner overflow its stack. Material graphs run before lighting; HLSL colour runs after it.
    const std::string materialInput = R"(
        IW8RPreviewSurface previewSurface=iw8rMakeSurface(i);
        previewSurface.albedo=albedo;previewSurface.normal=N;previewSurface.alpha=outA;
#ifdef IW8R_PREVIEW_MATERIAL
        iw8rPreviewMaterial(previewSurface);
        albedo=previewSurface.albedo;N=normalize(previewSurface.normal);outA=previewSurface.alpha;
#endif
)";
    const std::string finish = R"(
        previewSurface.litColor=c;previewSurface.normal=N;previewSurface.ambient=indirect;previewSurface.sunShadow=visibility;
        float4 previewColour=iw8rPreviewPixel(previewSurface);c=previewColour.rgb;outA=previewColour.a;
)";
    if (!replace(geometry, "float4 ps_tex(PSIn i) : SV_TARGET {",
                 std::string(surfaceAdapter) + "float4 ps_preview(PSIn i) : SV_TARGET {") ||
        !replace(geometry, "float3 indirect = gameSky.rgb",
                 materialInput + "\n        float3 indirect = gameSky.rgb") ||
        !replace(geometry, "c = gameFog[0].w > 0.5 ? gameFogApply(c, i.w, false) : applyFog(c, i.w);",
                 finish + "\n        c = gameFog[0].w > 0.5 ? gameFogApply(c, i.w, false) : applyFog(c, i.w);") ||
        !replace(geometry, "const float gp = 1.0 - sqrt(1.0 - glossM);",
                 "#ifdef IW8R_PREVIEW_MATERIAL\n            "
                 "glossM=1-saturate(previewSurface.roughness);f0=lerp(0.04.xxx,albedo,saturate(previewSurface.metallic)"
                 ");diffAlbedo=albedo*(1-saturate(previewSurface.metallic));\n#endif\n            const float gp = 1.0 "
                 "- sqrt(1.0 - glossM);")) {
        result.diagnostics.push_back({"Game Lighting material adapter does not match the renderer."});
        return result;
    }
    if (result.generatedPixel.find("void iw8rPreviewMaterial(") != std::string::npos)
        replace(geometry, "const bool stockRef = batchFlags2.z > 0.5;", "const bool stockRef = true;");
    replace(geometry, "return float4(cu, outA);", "return iw8rPreviewFinished(i,float4(cu,outA));");
    replace(geometry, "return float4(applyGrade(applyFog(c, i.w)), outA);",
            "return iw8rPreviewFinished(i,float4(applyGrade(applyFog(c,i.w)),outA));");
    if (!injectReturn(geometry, "vs_tex", vertexApply) || !injectReturn(geometry, "vs_tex_inst", vertexApply)) {
        result.diagnostics.push_back({"Geometry preview adapter does not match the renderer's vertex interface."});
        return result;
    }
    geometry = preamble + vertex + result.generatedPixel + "\n" + geometry;
    const auto allowed = allowedResources(preamble + host);
    bool ok = true;
    ok = compile(geometry, "geometry", "vs_tex", "vs_5_0", allowed, result) && ok;
    ok = compile(geometry, "instanced", "vs_tex_inst", "vs_5_0", allowed, result) && ok;
    ok = compile(geometry, "geometry", "ps_preview", "ps_5_0", allowed, result) && ok;
    ok = compile(geometry, "depth", "ps_preview_depth", "ps_5_0", allowed, result) && ok;
    // Particle host is supplied by the renderer; its adapter uses the same authoring structs.
    if (!particleHost.empty()) {
        std::string particle = preamble + vertex + result.generatedPixel + "\n" + particleHost;
        const auto particleAllowed = allowedResources(preamble + particleHost);
        ok = compile(particle, "particle", "vs_preview_particle", "vs_5_0", particleAllowed, result) && ok;
        ok = compile(particle, "particle", "ps_preview_particle", "ps_5_0", particleAllowed, result) && ok;
    }
    result.ok = ok;
    if (!ok)
        result.stages.clear();
    result.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return result;
}
std::string disassemblePreviewShader(const std::vector<uint8_t> &bytes, std::vector<ShaderReflection> *bindings) {
    ComPtr<ID3DBlob> text;
    if (bytes.empty() ||
        FAILED(D3DDisassemble(bytes.data(), bytes.size(), D3D_DISASM_ENABLE_INSTRUCTION_NUMBERING, nullptr, &text)))
        return "Shader bytecode is unavailable or is not supported DXBC.";
    if (bindings) {
        ComPtr<ID3DBlob> blob;
        if (SUCCEEDED(D3DCreateBlob(bytes.size(), &blob))) {
            std::memcpy(blob->GetBufferPointer(), bytes.data(), bytes.size());
            *bindings = reflection(blob.Get());
        }
    }
    return std::string(static_cast<const char *>(text->GetBufferPointer()), text->GetBufferSize());
}
bool PreviewOverrideSet::matches(const PreviewTarget &t) const {
    if (!enabled)
        return false;
    if (!material.empty() && material != t.material)
        return false;
    if (all || materialWide)
        return true;
    for (const auto &x : targets) {
        if (x.family != t.family)
            continue;
        if (x.object && x.object != t.object)
            continue;
        if (x.primitive && x.primitive != t.primitive)
            continue;
        if (x.instance >= 0 && x.instance != t.instance)
            continue;
        if (!x.material.empty() && x.material != t.material)
            continue;
        return true;
    }
    return false;
}
PreviewShaderCacheStats previewShaderCacheStats() {
    auto &c = cache();
    std::lock_guard lock(c.mutex);
    return {c.entries.size(), c.bytes, c.compiled, c.hits};
}
} // namespace iw8r::app
