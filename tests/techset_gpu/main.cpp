#include "preview_shader.h"
#include "preview_gpu.h"
#include "render.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>
#include "render_cases.h"

using namespace iw8r;
using namespace iw8r::app;
using Microsoft::WRL::ComPtr;
int main() {
    int failed = 0, checks = 0;
    const auto check = [&](bool ok, const char *name) {
        ++checks;
        if (!ok) {
            ++failed;
            std::printf("FAIL: %s\n", name);
        }
    };
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL level;
    check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device,
                                      &level, &context)),
          "WARP device");
    if (!device)
        return 1;
    ShaderCompileRequest req;
    req.document = newTechsetDocument();
    req.techniqueId = "technique_34";
    req.revision = 1;
    auto result = compilePreviewShaders(req, SceneRenderer::texShaderSource(), SceneRenderer::particlePreviewSource());
    for (const auto &d : result.diagnostics)
        std::printf("%s: %s\n", d.warning ? "warning" : "error", d.message.c_str());
    check(result.ok, "default vertex/pixel geometry, instanced, depth, particle adapters compile");
    std::string error;
    PreviewGpuProgram program;
    check(result.ok && program.create(device.Get(), result, req.document, req.techniqueId, "", error),
          "complete GPU revision installs");
    if (!error.empty())
        std::printf("%s\n", error.c_str());
    const auto stats = previewShaderCacheStats();
    auto repeat = compilePreviewShaders(req, SceneRenderer::texShaderSource(), SceneRenderer::particlePreviewSource());
    check(repeat.ok && previewShaderCacheStats().compiled == stats.compiled, "unchanged source uses bounded cache");
    req.document.techniques[0].findStage(ShaderStage::Pixel)->source =
        "float4 iw8rPreviewPixel(IW8RPreviewSurface s) { return broken_symbol; }";
    req.revision = 2;
    auto bad = compilePreviewShaders(req, SceneRenderer::texShaderSource(), SceneRenderer::particlePreviewSource());
    check(!bad.ok && bad.stages.empty(), "failed revision publishes no partial program");
    check(program.revision() == 1, "last successful GPU revision retained");
    check(!bad.diagnostics.empty() && bad.diagnostics[0].line > 0, "HLSL diagnostics map to source lines");
    PreviewOverrideSet targets;
    targets.enabled = true;
    targets.targets = {{PreviewFamily::Geometry, 7, 9, "shared"}};
    check(targets.matches({PreviewFamily::Geometry, 7, 9, "shared"}), "selected slot matches");
    check(!targets.matches({PreviewFamily::Geometry, 8, 9, "shared"}),
          "shared material on a different object is isolated");
    for (const auto &spec : shaderNodeLibrary()) {
        if (spec.kind == "surface" || spec.kind == "vertex_output" || spec.kind == "comment" || spec.kind == "input" ||
            spec.kind == "output" || spec.kind == "subgraph")
            continue;
        auto graph = newShaderGraph(ShaderStage::Pixel);
        ShaderNode n{"node_under_test", spec.kind};
        if (spec.kind == "parameter")
            n.properties = {{"parameter", "P"}, {"type", "float4"}};
        if (spec.kind == "texture" || spec.kind == "texture_cube" || spec.kind == "triplanar")
            n.properties["resource"] = spec.kind == "texture_cube" ? "C" : "T";
        graph.nodes.push_back(n);
        std::vector<ResourceBinding> resources = {{"T", "texture2d", "", 0}, {"C", "texturecube", "", 1}};
        auto generated = compileShaderGraph(graph, ShaderStage::Pixel, {{"P", 0, {1, 1, 1, 1}}}, resources, n.id);
        check(generated.ok, spec.kind.c_str());
        if (!generated.ok)
            continue;
        const auto source =
            previewShaderInterface(resources) + "\nfloat iw8rPreviewSceneDepth(float2 pixel) { return 1; }\n" +
            generated.source +
            "\nfloat4 main(float4 p:SV_Position):SV_Target { IW8RPreviewSurface "
            "s=(IW8RPreviewSurface)0;s.pixel=p.xy;s.normal=float3(0,0,1);s.view=s.normal;return iw8rPreviewPixel(s); }";
        ComPtr<ID3DBlob> blob, errors;
        const auto hr = D3DCompile(source.data(), source.size(), "graph", nullptr, nullptr, "main", "ps_5_0",
                                   D3DCOMPILE_ENABLE_STRICTNESS, 0, &blob, &errors);
        check(SUCCEEDED(hr), spec.kind.c_str());
        if (FAILED(hr) && errors)
            std::printf("%s\n", static_cast<const char *>(errors->GetBufferPointer()));
    }
    check(previewShaderCacheStats().entries <= 64 && previewShaderCacheStats().bytes <= 32 * 1024 * 1024,
          "preview cache bounded");
    runPreviewRenderCases(device.Get(), context.Get(), check);
    std::printf("%d checks, %d failures\n", checks, failed);
    return failed ? 1 : 0;
}
