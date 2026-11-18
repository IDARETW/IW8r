#pragma once
#include "render.h"
#include "preview_gpu.h"
#include "game_look.h"
#include <functional>

inline void runPreviewRenderCases(ID3D11Device *device, ID3D11DeviceContext *context,
                                  const std::function<void(bool, const char *)> &check) {
    using namespace iw8r;
    using namespace iw8r::app;
    SceneRenderer renderer;
    std::string error;
    check(renderer.init(device, context, &error), "render fixture initializes");
    renderer.resize(128, 128);
    const uint8_t white[4] = {255, 255, 255, 255};
    const int texture = renderer.createTexture(white, 1, 1);
    SceneRenderer::GameLook look;
    look.on = true;
    look.skyIsUnbaked = true;
    renderer.setGameLook(look);
    const auto lut = buildGameTonemapLut(16);
    renderer.setGameTonemapLut(16, lut.data());
    const float eye[3] = {0, 0, 2}, black[3] = {0, 0, 0};
    renderer.setFog(eye, 0, 0, black);
    ShaderCompileRequest req;
    req.document = newTechsetDocument();
    req.techniqueId = "technique_34";
    req.revision = 10;
    req.document.techniques[0].layout["previewState"]["cull"] = "none";
    req.document.techniques[0].findStage(ShaderStage::Pixel)->source =
        "float4 iw8rPreviewPixel(IW8RPreviewSurface s){return float4(1,0,0,1);}";
    const auto result =
        compilePreviewShaders(req, SceneRenderer::texShaderSource(), SceneRenderer::particlePreviewSource());
    auto program = std::make_shared<PreviewGpuProgram>();
    check(result.ok && program->create(device, result, req.document, req.techniqueId, "", error),
          "render fixture program compiles");
    if (!result.ok)
        return;
    const float vp[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}, clear[4] = {0, 0, 0, 1};
    FrameInput input;
    input.viewProj = vp;
    input.clear = clear;
    input.clean = true;
    input.capture = true;
    input.geometryChanged = true;
    const auto triangle = [](float x, uint32_t tag = 0xffffffffu) {
        std::vector<GpuTexVertex> v;
        for (const auto &p : std::vector<std::array<float, 2>>{{x - 0.35f, -0.65f}, {x + 0.35f, -0.65f}, {x, 0.65f}}) {
            GpuTexVertex vertex{p[0], p[1], 0.5f, 0.5f, 0.5f, 0xffffffffu};
            vertex.nz = 1;
            vertex.tx = 1;
            vertex.edge = tag;
            v.push_back(vertex);
        }
        return v;
    };
    const auto draw = [&] {
        renderer.drawFrame(input);
        std::vector<uint8_t> pixels;
        int w = 0, h = 0;
        renderer.readback(pixels, w, h);
        return pixels;
    };
    const auto reds = [](const std::vector<uint8_t> &p, bool right) {
        size_t n = 0;
        for (int y = 0; y < 128; ++y)
            for (int x = right ? 64 : 0; x < (right ? 128 : 64); ++x) {
                const size_t at = (size_t(y) * 128 + x) * 4;
                if (at + 2 < p.size() && p[at] > p[at + 1] + 40 && p[at] > p[at + 2] + 40)
                    ++n;
            }
        return n;
    };
    SceneBatch left, right;
    left.texture = right.texture = texture;
    left.material = right.material = "shared";
    left.previewObject = 7;
    right.previewObject = 8;
    left.v = triangle(-0.45f);
    right.v = triangle(0.45f);
    std::vector<SceneBatch *> batches = {&left, &right};
    input.batches = &batches;
    PreviewOverrideSet selected;
    selected.enabled = true;
    selected.targets = {{PreviewFamily::Geometry, 7, 0, "shared"}};
    renderer.setPreviewProgram(program, selected);
    auto image = draw();
    check(reds(image, false) > 400 && reds(image, true) < 10,
          "geometry selection isolates two objects sharing a material/texture");
    selected.materialWide = true;
    selected.material = "shared";
    renderer.setPreviewProgram(program, selected);
    image = draw();
    check(reds(image, false) > 400 && reds(image, true) > 400, "explicit material-wide override draws both objects");
    renderer.setPreviewProgram({}, {});
    image = draw();
    check(reds(image, false) < 10 && reds(image, true) < 10, "clearing restores original geometry shaders");
    // Parameter-only updates and complete GPU replacement must not retain a chain of old programs.
    auto parameterRequest = req;
    parameterRequest.document.parameters = {{"Tint", 0, {1, 0, 0, 1}}};
    parameterRequest.document.techniques[0].findStage(ShaderStage::Pixel)->source =
        "float4 iw8rPreviewPixel(IW8RPreviewSurface s){return iw8rParameters[0];}";
    auto parameterResult = compilePreviewShaders(parameterRequest, SceneRenderer::texShaderSource(),
                                                 SceneRenderer::particlePreviewSource());
    auto parameterProgram = std::make_shared<PreviewGpuProgram>();
    check(parameterResult.ok &&
              parameterProgram->create(device, parameterResult, parameterRequest.document, req.techniqueId, "", error),
          "parameter program installs");
    const auto compileCount = previewShaderCacheStats().compiled;
    renderer.setPreviewProgram(parameterProgram, selected);
    const auto parameterRed = draw();
    parameterProgram->parameters({{"Tint", 0, {0, 1, 0, 1}}});
    const auto parameterGreen = draw();
    check(parameterGreen != parameterRed && reds(parameterGreen, false) < 10 &&
              previewShaderCacheStats().compiled == compileCount,
          "parameter update visibly changes the frame without compilation");
    // A transparent authored pixel must leave both colour and the AO depth prepass empty.
    parameterProgram->parameters({{"Tint", 0, {1, 0, 0, 0}}});
    auto transparentRequest = parameterRequest;
    transparentRequest.document.techniques[0].findStage(ShaderStage::Pixel)->source =
        "float4 iw8rPreviewPixel(IW8RPreviewSurface s){clip(iw8rParameters[0].a-0.5);return iw8rParameters[0];}";
    auto transparentCode = compilePreviewShaders(transparentRequest, SceneRenderer::texShaderSource(),
                                                 SceneRenderer::particlePreviewSource());
    auto transparent = std::make_shared<PreviewGpuProgram>();
    check(transparentCode.ok &&
              transparent->create(device, transparentCode, transparentRequest.document, req.techniqueId, "", error),
          "coverage program installs");
    transparent->parameters({{"Tint", 0, {1, 0, 0, 0}}});
    renderer.setPreviewProgram(transparent, selected);
    check(reds(draw(), false) == 0 && reds(draw(), true) == 0, "authored cutout removes colour and depth coverage");
    for (unsigned revision = 0; revision < 12; ++revision) {
        auto replacement = std::make_shared<PreviewGpuProgram>();
        check(replacement->create(device, parameterResult, parameterRequest.document, req.techniqueId, "", error),
              "replacement GPU program creates");
        renderer.setPreviewProgram(replacement, selected);
        std::weak_ptr<PreviewGpuProgram> retired = replacement;
        replacement.reset();
        renderer.setPreviewProgram(program, selected);
        check(retired.expired(), "superseded GPU program retires after view releases it");
    }
    renderer.setPreviewProgram({}, {});
    batches.clear();
    input.batches = nullptr;
    auto world = triangle(-0.45f, referenceSurfaceTag(11));
    const auto other = triangle(0.45f, referenceSurfaceTag(12));
    world.insert(world.end(), other.begin(), other.end());
    check(renderer.setReferenceTextured({world}), "stock world fixture uploads");
    renderer.setReferenceTexture(0, texture);
    renderer.setReferenceVisible(true);
    renderer.setPreviewMaterialNames({"shared"}, {});
    selected = {};
    selected.enabled = true;
    selected.targets = {{PreviewFamily::StockWorld, 0, 0, "shared", -1, referenceSurfaceTag(11)}};
    renderer.setPreviewProgram(program, selected);
    image = draw();
    check(reds(image, false) > 400 && reds(image, true) < 10,
          "stock surface selection isolates surfaces within one material batch");
    selected.materialWide = true;
    selected.material = "shared";
    renderer.setPreviewProgram(program, selected);
    image = draw();
    check(reds(image, false) > 400 && reds(image, true) > 400, "stock world material override draws both surfaces");
    renderer.setReferenceTextured({});
    auto mesh = std::make_shared<PropMeshData>();
    for (const auto &v : triangle(0)) {
        PropVertex p{};
        p.x = v.x;
        p.y = v.y;
        p.z = v.z;
        p.u = v.u;
        p.v = v.v;
        p.rgba = 0xffffffff;
        p.normal = 0x007f0000;
        mesh->vertices.push_back(p);
    }
    mesh->indices = {0, 1, 2};
    InstancedTexMesh inst;
    inst.mesh = mesh;
    inst.rows = {1, 0, 0, -0.45f, 0, 1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0.45f, 0, 1, 0, 0, 0, 0, 1, 0};
    inst.previewIds = {41, 42};
    renderer.setReferenceTexInstances({inst});
    renderer.setReferenceInstTexture(0, texture);
    renderer.setPreviewMaterialNames({}, {"shared"});
    selected = {};
    selected.enabled = true;
    selected.targets = {{PreviewFamily::StockModel, 0, 0, "shared", 41}};
    renderer.setPreviewProgram(program, selected);
    image = draw();
    check(reds(image, false) > 400 && reds(image, true) < 10, "instanced model selection retains placement identity");
    renderer.setReferenceTexInstances({});
    ParticleBatch a, b;
    a.texture = b.texture = texture;
    a.previewObject = 1;
    b.previewObject = 2;
    a.previewMaterial = b.previewMaterial = "sprite";
    a.lit = b.lit = true;
    for (const auto &v : triangle(-0.45f))
        a.v.push_back({v.x, v.y, v.z, v.u, v.v, 1, 1, 1, 1});
    for (const auto &v : triangle(0.45f))
        b.v.push_back({v.x, v.y, v.z, v.u, v.v, 1, 1, 1, 1});
    input.clean = false;
    renderer.setParticles({a, b});
    selected = {};
    selected.enabled = true;
    selected.targets = {{PreviewFamily::Particle, 1, 0, "sprite"}};
    renderer.setPreviewProgram(program, selected);
    image = draw();
    check(reds(image, false) > 400 && reds(image, true) < 10, "effect sprite override isolates effect instances");
}
