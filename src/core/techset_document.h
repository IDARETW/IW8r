// Editor authoring data. No game/build assignments or GPU pointers are created here.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace iw8r {

using TechsetJson = nlohmann::ordered_json;
enum class ShaderStage { Vertex, Pixel, Hull, Domain };
const char* shaderStageName(ShaderStage stage);
bool parseShaderStage(const std::string& name, ShaderStage& stage);

struct ResourceBinding {
    std::string name, kind = "texture2d", path;
    unsigned slot = 0;
    bool srgb = true;
    std::string filter = "linear", address = "wrap";
    TechsetJson extra = TechsetJson::object();
};
struct ShaderParameter {
    std::string name;
    unsigned slot = 0;
    std::array<float, 4> value{0, 0, 0, 0};
    TechsetJson extra = TechsetJson::object();
};
struct ShaderNode {
    std::string id, kind, label;
    double x = 0, y = 0;
    TechsetJson properties = TechsetJson::object();
    TechsetJson extra = TechsetJson::object();
};
struct ShaderLink {
    std::string from, output = "out", to, input;
    TechsetJson extra = TechsetJson::object();
};
struct ShaderGraph {
    std::vector<ShaderNode> nodes;
    std::vector<ShaderLink> links;
    // Each definition has input nodes and an output node; recursive calls are rejected.
    std::map<std::string, ShaderGraph> subgraphs;
    TechsetJson extra = TechsetJson::object();
};
struct ShaderStageDocument {
    ShaderStage stage = ShaderStage::Pixel;
    std::string mode = "hlsl", source, entry = "iw8rPreviewPixel";
    std::string reference, bytecodePath;
    ShaderGraph graph;
    TechsetJson extra = TechsetJson::object();
};
struct TechniqueDefinition {
    std::string id, name;
    uint32_t slot = 34;
    // Complete Replay MaterialTechnique body, retaining exact integer values and unknown fields.
    // The layout owns flags, pipelineStateInputLayoutCount, pipelineStateMap[25], pipelineStates,
    // customBufferFlags[4], rootParameterMap[16], all four stage references, argument counts/sizes,
    // colorRtCount, stateBits, renderTargetFormats, customSamplerFlags, precompiledIndex,
    // stageConfig, defaultStateBits and args. Address fields are provenance, never dereferenced.
    TechsetJson layout = TechsetJson::object();
    std::vector<ShaderStageDocument> stages;
    TechsetJson extra = TechsetJson::object();
    ShaderStageDocument* findStage(ShaderStage stage);
    const ShaderStageDocument* findStage(ShaderStage stage) const;
    std::string previewLimitation() const;
};
struct TechsetDocument {
    static constexpr int kVersion = 1;
    std::string name = "new_techset", profile = "replay-1.20", sourceZone;
    // Complete source export and MaterialTechniqueSet body (flags, geometry type, 256-bit mask).
    TechsetJson provenance = TechsetJson::object(), layout = TechsetJson::object();
    std::vector<TechniqueDefinition> techniques;
    std::vector<ResourceBinding> resources;
    std::vector<ShaderParameter> parameters;
    TechsetJson extra = TechsetJson::object();
};

bool readTechsetDocument(const std::string& text, TechsetDocument& out, std::string& error);
bool writeTechsetDocument(const TechsetDocument& document, std::string& text, std::string& error);
bool importTechsetDocument(const std::string& text, TechsetDocument& out, std::string& error);
bool validateTechsetDocument(const TechsetDocument& document, std::string& error);
void rebuildTechsetMask(TechsetDocument& document);
TechsetDocument newTechsetDocument();
std::string defaultPreviewSource(ShaderStage stage);
TechsetJson shaderGraphJson(const ShaderGraph& graph);
bool readShaderGraph(const TechsetJson& json, ShaderGraph& graph, std::string& error);

} // namespace iw8r
