#include "preview_gpu.h"
#include "texture_decode.h"
#include "custom_materials.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstring>
#include <filesystem>

namespace iw8r::app {
namespace {
using Microsoft::WRL::ComPtr;
struct Constants {
    float parameters[64][4]{};
    float clock[4]{};
    float viewport[4]{};
};
template <class T, size_t N> void release(T *(&a)[N]) {
    for (auto *&p : a)
        if (p) {
            p->Release();
            p = nullptr;
        }
}
} // namespace
struct PreviewGpuProgram::Impl {
    ComPtr<ID3D11VertexShader> vertex, instanced, particleVertex;
    ComPtr<ID3D11PixelShader> pixel, depth, particlePixel;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11DepthStencilState> depthState;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11ShaderResourceView> textures[8];
    ComPtr<ID3D11SamplerState> samplers[8];
    Constants values;
    uint64_t revision = 0;
    bool deforms = false;
};
struct PreviewGpuScope::Impl {
    ID3D11DeviceContext *ctx;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11Buffer> vsConstant, psConstant;
    ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11DepthStencilState> depth;
    ComPtr<ID3D11RasterizerState> raster;
    ID3D11ShaderResourceView *vsTextures[8]{}, *psTextures[8]{};
    ID3D11SamplerState *vsSamplers[8]{}, *psSamplers[8]{};
    float factor[4]{};
    UINT mask = 0, stencil = 0;
    explicit Impl(ID3D11DeviceContext *c) : ctx(c) {
        c->VSGetShader(&vertex, nullptr, nullptr);
        c->PSGetShader(&pixel, nullptr, nullptr);
        c->VSGetConstantBuffers(13, 1, &vsConstant);
        c->PSGetConstantBuffers(13, 1, &psConstant);
        c->VSGetShaderResources(96, 8, vsTextures);
        c->PSGetShaderResources(96, 8, psTextures);
        c->VSGetSamplers(8, 8, vsSamplers);
        c->PSGetSamplers(8, 8, psSamplers);
        c->OMGetBlendState(&blend, factor, &mask);
        c->OMGetDepthStencilState(&depth, &stencil);
        c->RSGetState(&raster);
    }
    ~Impl() {
        ctx->VSSetShader(vertex.Get(), nullptr, 0);
        ctx->PSSetShader(pixel.Get(), nullptr, 0);
        ctx->VSSetConstantBuffers(13, 1, vsConstant.GetAddressOf());
        ctx->PSSetConstantBuffers(13, 1, psConstant.GetAddressOf());
        ctx->VSSetShaderResources(96, 8, vsTextures);
        ctx->PSSetShaderResources(96, 8, psTextures);
        ctx->VSSetSamplers(8, 8, vsSamplers);
        ctx->PSSetSamplers(8, 8, psSamplers);
        ctx->OMSetBlendState(blend.Get(), factor, mask);
        ctx->OMSetDepthStencilState(depth.Get(), stencil);
        ctx->RSSetState(raster.Get());
        release(vsTextures);
        release(psTextures);
        release(vsSamplers);
        release(psSamplers);
    }
};
PreviewGpuScope::PreviewGpuScope(ID3D11DeviceContext *ctx) : d_(std::make_unique<Impl>(ctx)) {}
PreviewGpuScope::~PreviewGpuScope() = default;
PreviewGpuProgram::PreviewGpuProgram() : d_(std::make_unique<Impl>()) {}
PreviewGpuProgram::~PreviewGpuProgram() = default;
uint64_t PreviewGpuProgram::revision() const { return d_->revision; }
bool PreviewGpuProgram::deformsVertices() const { return d_->deforms; }
void PreviewGpuProgram::parameters(const std::vector<ShaderParameter> &parameters) {
    std::memset(d_->values.parameters, 0, sizeof(d_->values.parameters));
    for (const auto &p : parameters)
        if (p.slot < 64)
            std::memcpy(d_->values.parameters[p.slot], p.value.data(), 16);
}
bool PreviewGpuProgram::create(ID3D11Device *dev, const ShaderCompileResult &code, const TechsetDocument &doc,
                               const std::string &technique, const std::string &directory, std::string &error) {
    if (!dev || !code.ok) {
        error = "No device or compiled revision.";
        return false;
    }
    auto next = std::make_unique<Impl>();
    for (const auto &s : code.stages) {
        HRESULT hr = E_FAIL;
        if (s.target == "vs_5_0") {
            auto *dst = s.adapter == "particle"    ? std::addressof(next->particleVertex)
                        : s.adapter == "instanced" ? std::addressof(next->instanced)
                                                   : std::addressof(next->vertex);
            hr = dev->CreateVertexShader(s.bytecode.data(), s.bytecode.size(), nullptr, dst->GetAddressOf());
        } else {
            auto *dst = s.adapter == "particle" ? std::addressof(next->particlePixel)
                        : s.adapter == "depth"  ? std::addressof(next->depth)
                                                : std::addressof(next->pixel);
            hr = dev->CreatePixelShader(s.bytecode.data(), s.bytecode.size(), nullptr, dst->GetAddressOf());
        }
        if (FAILED(hr)) {
            error = "The device rejected " + s.adapter + "/" + s.entry;
            return false;
        }
    }
    if (!next->vertex || !next->pixel || !next->depth || !next->instanced) {
        error = "The compiled revision is incomplete.";
        return false;
    }
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof(Constants);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(dev->CreateBuffer(&bd, nullptr, &next->constants))) {
        error = "Cannot create preview parameters.";
        return false;
    }
    TechsetJson state = TechsetJson::object();
    for (const auto &t : doc.techniques)
        if (t.id == technique)
            state = t.layout.value("previewState", TechsetJson::object());
    try {
        D3D11_BLEND_DESC blend{};
        auto &rt = blend.RenderTarget[0];
        rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        const auto mode = state.value("blend", "opaque");
        rt.BlendEnable = mode != "opaque";
        rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
        rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        if (mode == "additive")
            rt.DestBlend = D3D11_BLEND_ONE;
        else if (mode == "multiply") {
            rt.SrcBlend = D3D11_BLEND_DEST_COLOR;
            rt.DestBlend = D3D11_BLEND_ZERO;
        } else if (mode != "opaque" && mode != "alpha")
            throw std::runtime_error("unsupported blend mode");
        rt.BlendOp = rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D11_BLEND_ONE;
        rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        if (FAILED(dev->CreateBlendState(&blend, &next->blend)))
            throw std::runtime_error("invalid blend state");
        D3D11_DEPTH_STENCIL_DESC ds{};
        ds.DepthEnable = state.value("depthTest", true);
        ds.DepthWriteMask =
            state.value("depthWrite", mode == "opaque") ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
        const auto compare = state.value("depthFunction", "less_equal");
        const std::map<std::string, D3D11_COMPARISON_FUNC> compares = {
            {"never", D3D11_COMPARISON_NEVER},
            {"less", D3D11_COMPARISON_LESS},
            {"equal", D3D11_COMPARISON_EQUAL},
            {"less_equal", D3D11_COMPARISON_LESS_EQUAL},
            {"greater", D3D11_COMPARISON_GREATER},
            {"not_equal", D3D11_COMPARISON_NOT_EQUAL},
            {"greater_equal", D3D11_COMPARISON_GREATER_EQUAL},
            {"always", D3D11_COMPARISON_ALWAYS}};
        if (!compares.count(compare))
            throw std::runtime_error("invalid depth comparison");
        ds.DepthFunc = compares.at(compare);
        ds.StencilEnable = state.value("stencilEnable", false);
        ds.StencilReadMask = state.value("stencilReadMask", 255);
        ds.StencilWriteMask = state.value("stencilWriteMask", 255);
        ds.FrontFace.StencilFunc = ds.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
        ds.FrontFace.StencilFailOp = ds.FrontFace.StencilDepthFailOp = ds.FrontFace.StencilPassOp =
            D3D11_STENCIL_OP_KEEP;
        ds.BackFace = ds.FrontFace;
        if (FAILED(dev->CreateDepthStencilState(&ds, &next->depthState)))
            throw std::runtime_error("invalid depth/stencil state");
        D3D11_RASTERIZER_DESC rs{};
        rs.FillMode = state.value("wireframe", false) ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
        const auto cull = state.value("cull", "back");
        if (cull != "back" && cull != "front" && cull != "none")
            throw std::runtime_error("invalid culling mode");
        rs.CullMode = cull == "none" ? D3D11_CULL_NONE : cull == "front" ? D3D11_CULL_FRONT : D3D11_CULL_BACK;
        rs.DepthClipEnable = true;
        rs.FrontCounterClockwise = state.value("frontCounterClockwise", false);
        rs.DepthBias = state.value("depthBias", 0);
        rs.SlopeScaledDepthBias = state.value("slopeDepthBias", 0.0f);
        if (FAILED(dev->CreateRasterizerState(&rs, &next->raster)))
            throw std::runtime_error("invalid rasterizer state");
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }
    for (const auto &r : doc.resources) {
        if (r.path.empty()) {
            error = "Texture '" + r.name + "' has no file.";
            return false;
        }
        auto path = std::filesystem::u8path(r.path);
        if (path.is_relative())
            path = std::filesystem::u8path(directory) / path;
        const auto u8 = path.u8string();
        const std::string file(reinterpret_cast<const char *>(u8.data()), u8.size());
        Rgba8Image image;
        std::string why;
        if (path.extension() == ".dds") {
            if (!loadDdsPreview(file, 0, image, nullptr, nullptr, &why)) {
                error = r.name + ": " + why;
                return false;
            }
        } else if (!decodeImageFile(file, image.width, image.height, image.pixels, &why)) {
            error = r.name + ": " + why;
            return false;
        }
        const bool cube = r.kind == "texturecube";
        int w = image.width, h = image.height;
        std::vector<uint8_t> faces;
        if (cube) {
            if (w == 6 * h)
                w = h;
            else if (h == 6 * w)
                h = w;
            else {
                error = "Cube texture '" + r.name + "' needs a six-face strip (+X -X +Y -Y +Z -Z).";
                return false;
            }
            faces.resize(size_t(w) * h * 4 * 6);
            for (int face = 0; face < 6; ++face)
                for (int y = 0; y < h; ++y) {
                    const int sx = image.width == 6 * w ? face * w : 0, sy = image.height == 6 * h ? face * h + y : y;
                    std::memcpy(faces.data() + (size_t(face) * w * h + size_t(y) * w) * 4,
                                image.pixels.data() + (size_t(sy) * image.width + sx) * 4, size_t(w) * 4);
                }
        }
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w;
        td.Height = h;
        td.MipLevels = 1;
        td.ArraySize = cube ? 6 : 1;
        td.Format = r.srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        td.MiscFlags = cube ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;
        D3D11_SUBRESOURCE_DATA data[6]{};
        for (UINT i = 0; i < td.ArraySize; ++i) {
            data[i].pSysMem = cube ? faces.data() + size_t(i) * w * h * 4 : image.pixels.data();
            data[i].SysMemPitch = w * 4;
        }
        ComPtr<ID3D11Texture2D> texture;
        if (FAILED(dev->CreateTexture2D(&td, data, &texture)) ||
            FAILED(dev->CreateShaderResourceView(texture.Get(), nullptr, &next->textures[r.slot]))) {
            error = "Cannot upload texture " + r.name;
            return false;
        }
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = r.filter == "point" ? D3D11_FILTER_MIN_MAG_MIP_POINT : D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = r.address == "clamp"    ? D3D11_TEXTURE_ADDRESS_CLAMP
                                                                 : r.address == "mirror" ? D3D11_TEXTURE_ADDRESS_MIRROR
                                                                                         : D3D11_TEXTURE_ADDRESS_WRAP;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(dev->CreateSamplerState(&sampler, &next->samplers[r.slot]))) {
            error = "Cannot create sampler " + r.name;
            return false;
        }
    }
    for (const auto &t : doc.techniques)
        if (t.id == technique)
            if (const auto *stage = t.findStage(ShaderStage::Vertex))
                next->deforms = stage->mode == "graph" ? !stage->graph.links.empty()
                                                       : stage->source != defaultPreviewSource(ShaderStage::Vertex);
    next->revision = code.revision;
    d_ = std::move(next);
    parameters(doc.parameters);
    error.clear();
    return true;
}
std::unique_ptr<PreviewGpuScope> PreviewGpuProgram::bind(ID3D11DeviceContext *ctx, PreviewFamily family, bool depth,
                                                         float time, int width, int height, uint32_t surfaceTag) {
    auto *vs = family == PreviewFamily::Particle     ? d_->particleVertex.Get()
               : family == PreviewFamily::StockModel ? d_->instanced.Get()
                                                     : d_->vertex.Get();
    auto *ps = family == PreviewFamily::Particle ? d_->particlePixel.Get() : depth ? d_->depth.Get() : d_->pixel.Get();
    if (!vs || !ps)
        return {};
    auto saved = std::unique_ptr<PreviewGpuScope>(new PreviewGpuScope(ctx));
    d_->values.clock[0] = time;
    d_->values.clock[1] = float(surfaceTag);
    d_->values.viewport[0] = float(width);
    d_->values.viewport[1] = float(height);
    ctx->UpdateSubresource(d_->constants.Get(), 0, nullptr, &d_->values, 0, 0);
    ctx->VSSetConstantBuffers(13, 1, d_->constants.GetAddressOf());
    ctx->PSSetConstantBuffers(13, 1, d_->constants.GetAddressOf());
    ID3D11ShaderResourceView *textures[8]{};
    ID3D11SamplerState *samplers[8]{};
    for (size_t i = 0; i < 8; ++i) {
        textures[i] = d_->textures[i].Get();
        samplers[i] = d_->samplers[i].Get();
    }
    ctx->VSSetShaderResources(96, 8, textures);
    ctx->PSSetShaderResources(96, 8, textures);
    ctx->VSSetSamplers(8, 8, samplers);
    ctx->PSSetSamplers(8, 8, samplers);
    ctx->VSSetShader(vs, nullptr, 0);
    ctx->PSSetShader(ps, nullptr, 0);
    // Depth/shadow targets retain their write mask and bias. Colour draws use the edited state.
    if (!depth) {
        ctx->OMSetBlendState(d_->blend.Get(), nullptr, 0xffffffff);
        ctx->OMSetDepthStencilState(d_->depthState.Get(), 0);
        ctx->RSSetState(d_->raster.Get());
    }
    return saved;
}
} // namespace iw8r::app
