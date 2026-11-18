#pragma once
#include "preview_shader.h"
#include <memory>
struct ID3D11Device;
struct ID3D11DeviceContext;

namespace iw8r::app {
class PreviewGpuScope;
class PreviewGpuProgram {
  public:
    PreviewGpuProgram();
    ~PreviewGpuProgram();
    bool create(ID3D11Device *device, const ShaderCompileResult &code, const TechsetDocument &document,
                const std::string &technique, const std::string &directory, std::string &error);
    void parameters(const std::vector<ShaderParameter> &parameters);
    // The caller serializes use of the immediate context. The returned scope restores every touched binding.
    std::unique_ptr<PreviewGpuScope> bind(ID3D11DeviceContext *context, PreviewFamily family, bool depth, float time,
                                          int width, int height, uint32_t surfaceTag = 0);
    uint64_t revision() const;
    bool deformsVertices() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};
class PreviewGpuScope {
  public:
    ~PreviewGpuScope();

  private:
    friend class PreviewGpuProgram;
    explicit PreviewGpuScope(ID3D11DeviceContext *context);
    struct Impl;
    std::unique_ptr<Impl> d_;
};
} // namespace iw8r::app
