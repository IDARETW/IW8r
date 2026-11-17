#pragma once
#include "techset_document.h"

namespace iw8r {
struct ShaderPort { std::string name, type = "float", fallback = "0"; };
struct ShaderNodeSpec {
    std::string kind, title, category;
    std::vector<ShaderPort> inputs;
    std::string output = "float";
    bool pixelOnly = false;
};
struct ShaderDiagnostic {
    std::string message, node, file;
    unsigned line = 0, column = 0;
    bool warning = false;
};
struct ShaderGraphResult {
    bool ok = false;
    std::string source;
    std::vector<ShaderDiagnostic> diagnostics;
    std::map<std::string, std::string> nodeFiles;
};
const std::vector<ShaderNodeSpec>& shaderNodeLibrary();
const ShaderNodeSpec* shaderNodeSpec(const std::string& kind);
ShaderGraph newShaderGraph(ShaderStage stage);
ShaderGraphResult compileShaderGraph(const ShaderGraph& graph, ShaderStage stage,
                                    const std::vector<ShaderParameter>& parameters,
                                    const std::vector<ResourceBinding>& resources,
                                    const std::string& previewNode = {});
// Declarations shared by the geometry and particle hosts; no game-facing registers.
std::string previewShaderInterface(const std::vector<ResourceBinding>& resources);
} // namespace iw8r
