// Shader compilation runs without a device/context. GPU creation and drawing are separately synchronized.
#pragma once
#include "shader_graph.h"
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace iw8r::app {
class PreviewGpuProgram;
struct ShaderCompileRequest {
    uint64_t revision = 0;
    TechsetDocument document;
    std::string techniqueId;
    ShaderStage graphPreviewStage = ShaderStage::Pixel;
    std::string graphPreviewNode;
};
struct ShaderReflection {
    std::string name, kind;
    unsigned slot = 0, count = 0;
};
struct CompiledPreviewStage {
    std::string adapter, entry, target;
    std::vector<uint8_t> bytecode;
    std::vector<ShaderReflection> bindings;
};
struct ShaderCompileResult {
    uint64_t revision = 0;
    bool ok = false;
    std::vector<ShaderDiagnostic> diagnostics;
    std::vector<CompiledPreviewStage> stages;
    std::string generatedVertex, generatedPixel;
    double milliseconds = 0;
};
ShaderCompileResult compilePreviewShaders(const ShaderCompileRequest &request, const std::string &geometryHost,
                                          const std::string &particleHost);
std::string disassemblePreviewShader(const std::vector<uint8_t> &bytes,
                                     std::vector<ShaderReflection> *bindings = nullptr);
struct PreviewShaderCacheStats {
    size_t entries = 0, bytes = 0, compiled = 0, hits = 0;
};
PreviewShaderCacheStats previewShaderCacheStats();

enum class PreviewFamily { Geometry, StockWorld, StockModel, Particle };
struct PreviewTarget {
    PreviewFamily family = PreviewFamily::Geometry;
    uint64_t object = 0, primitive = 0;
    std::string material;
    int64_t instance = -1;
    uint32_t surfaceTag = 0; // stock surface index + 1, as packed in the reference vertex
};
struct PreviewOverrideSet {
    bool enabled = false, all = false, materialWide = false;
    std::string material;
    std::vector<PreviewTarget> targets;
    bool matches(const PreviewTarget &target) const;
};
} // namespace iw8r::app
