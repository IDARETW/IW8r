#include "test_harness.h"
#include "techset_document.h"
#include "shader_graph.h"
#include <fstream>
#include <filesystem>
#include <sstream>

using namespace iw8r;

TEST(techset_document_exact_roundtrip) {
    auto d = newTechsetDocument();
    d.layout["flags"] = UINT64_MAX;
    d.layout["unknown"] = {{"hash", uint64_t(17011406470303970170ull)}};
    d.techniques[0].layout["pipelineStateMap"] = std::array<uint8_t, 25>{};
    d.techniques[0].layout["rootParameterMap"] = std::array<uint8_t, 16>{};
    d.techniques[0].layout["args"] = {{"values", {{{"type", 9}, {"shader", 16}, {"dest", 0}, {"index", 72}}}}};
    d.extra["futureData"] = {{"value", UINT64_MAX}};
    std::string text, error;
    CHECK(writeTechsetDocument(d, text, error));
    TechsetDocument loaded;
    CHECK(readTechsetDocument(text, loaded, error));
    CHECK(loaded.layout == d.layout);
    CHECK(loaded.techniques[0].layout == d.techniques[0].layout);
    CHECK(loaded.extra.at("futureData") == d.extra.at("futureData"));
    std::string again;
    CHECK(writeTechsetDocument(loaded, again, error));
    CHECK(text == again);
}

TEST(techset_document_import_complete_layout) {
    const TechsetJson body = {
        {"name", {{"string", "test_lit"}}},
        {"type", {{"value", 34}, {"name", "TECHNIQUE_LIT_FORWARDPLUS_BITMASK"}}},
        {"pipelineStates", {{"values", {{{"hash", uint64_t(17011406470303970170ull)}, {"pso", nullptr}}}}}},
        {"customBufferFlags", {UINT64_MAX, 0, 0, 0}},
        {"vertexShader", {{"type", "ComputeShader"}, {"name", ",vs_hash"}}},
        {"pixelShader", {{"type", "ComputeShader"}, {"name", ",ps_hash"}}},
        {"hullShader", {{"type", "ComputeShader"}, {"name", ",hs_hash"}}},
        {"domainShader", {{"type", "ComputeShader"}, {"name", ",ds_hash"}}},
        {"defaultStateBits", {{"otherBits", {{"value", UINT64_MAX}}}}}};
    TechsetJson source = {{"asset",
                           {{"type", "MaterialTechniqueSet"},
                            {"fields",
                             {{"name", {{"string", "test/complete"}}},
                              {"flags", UINT64_MAX},
                              {"maskedTechniques", {{"values", {{{"values", {body}}}}}}}}}}}};
    TechsetDocument d;
    std::string error, text;
    CHECK(importTechsetDocument(source.dump(), d, error));
    CHECK(d.provenance == source);
    CHECK(d.techniques.size() == 1);
    CHECK(d.techniques[0].layout == body);
    CHECK(d.techniques[0].findStage(ShaderStage::Vertex)->reference == ",vs_hash");
    CHECK(d.techniques[0].findStage(ShaderStage::Pixel)->reference == ",ps_hash");
    CHECK(d.techniques[0].stages.size() == 4);
    CHECK(!d.techniques[0].previewLimitation().empty());
    CHECK(writeTechsetDocument(d, text, error));
    TechsetDocument read;
    CHECK(readTechsetDocument(text, read, error));
    CHECK(read.techniques[0].layout == body);
    CHECK(read.provenance == source);
    source["asset"]["type"] = "Material";
    CHECK(!importTechsetDocument(source.dump(), d, error));
}

TEST(techset_document_invalid_read_is_transactional) {
    auto d = newTechsetDocument();
    d.name = "keep_me";
    std::string error, text = "keep_text";
    CHECK(!readTechsetDocument("{bad", d, error));
    CHECK(d.name == "keep_me");
    auto invalid = d;
    invalid.techniques.push_back(invalid.techniques.front());
    CHECK(!writeTechsetDocument(invalid, text, error));
    CHECK(text == "keep_text");
    invalid = d;
    invalid.parameters = {{"p", 64}};
    CHECK(!validateTechsetDocument(invalid, error));
    invalid = d;
    invalid.resources = {{"r", "texture2d", "", 8}};
    CHECK(!validateTechsetDocument(invalid, error));
    auto high = d.techniques[0];
    high.slot = 255;
    high.id = "last";
    d.techniques.push_back(high);
    rebuildTechsetMask(d);
    CHECK(d.layout["techniqueMask"][3].get<uint64_t>() == (uint64_t(1) << 63));
}

TEST(shader_graph_deterministic_and_typed) {
    auto g = newShaderGraph(ShaderStage::Pixel);
    g.nodes.push_back({"color_1", "constant", "Tint", 0, 0, {{"value", {1.0, 0.25, 0.1}}}});
    g.links.push_back({"color_1", "out", "output_1", "color"});
    const auto a = compileShaderGraph(g, ShaderStage::Pixel, {}, {}),
               b = compileShaderGraph(g, ShaderStage::Pixel, {}, {});
    CHECK(a.ok);
    CHECK(a.source == b.source);
    CHECK(a.source.find("node_color_1.hlsl") != std::string::npos);
    g.nodes[1].properties["value"] = {1.0, 0.25};
    CHECK(!compileShaderGraph(g, ShaderStage::Pixel, {}, {}).ok);
    g.nodes[1].properties["value"] = 0.5;
    CHECK(compileShaderGraph(g, ShaderStage::Pixel, {}, {}).ok); // scalar splat is explicit in generated HLSL
    ShaderGraph copy;
    std::string error;
    CHECK(readShaderGraph(shaderGraphJson(g), copy, error));
    CHECK(shaderGraphJson(copy) == shaderGraphJson(g));
}

TEST(shader_graph_cycles_and_stage_validation) {
    auto g = newShaderGraph(ShaderStage::Pixel);
    g.nodes.push_back({"a", "reroute"});
    g.nodes.push_back({"b", "reroute"});
    g.links = {{"a", "out", "b", "value"}, {"b", "out", "a", "value"}};
    CHECK(!compileShaderGraph(g, ShaderStage::Pixel, {}, {}).ok); // even disconnected cycles
    auto vertex = newShaderGraph(ShaderStage::Vertex);
    vertex.nodes.push_back({"v", "view"});
    vertex.links = {{"v", "out", "output_1", "offset"}};
    CHECK(!compileShaderGraph(vertex, ShaderStage::Vertex, {}, {}).ok);
    g = newShaderGraph(ShaderStage::Pixel);
    g.subgraphs["recursive"].nodes.push_back({"self", "subgraph", "", 0, 0, {{"name", "recursive"}}});
    CHECK(!compileShaderGraph(g, ShaderStage::Pixel, {}, {}).ok);
}

TEST(shader_graph_subgraphs_parameters_custom_and_preview) {
    auto g = newShaderGraph(ShaderStage::Pixel);
    auto &sub = g.subgraphs["scale"];
    sub.nodes = {{"in_1", "input", "", 0, 0, {{"name", "tint"}, {"type", "float3"}}}, {"out_1", "output"}};
    sub.links = {{"in_1", "out", "out_1", "value"}};
    g.nodes.push_back({"p", "parameter", "", 0, 0, {{"parameter", "Tint"}, {"type", "float3"}}});
    g.nodes.push_back({"fn", "subgraph", "", 0, 0, {{"name", "scale"}}});
    g.links = {{"p", "out", "fn", "tint"}, {"fn", "out", "output_1", "color"}};
    std::vector<ShaderParameter> params = {{"Tint", 2, {1, 0, 0, 1}}};
    auto r = compileShaderGraph(g, ShaderStage::Pixel, params, {});
    CHECK(r.ok);
    CHECK(r.source.find("iw8rParameters[2].xyz") != std::string::npos);
    CHECK(!compileShaderGraph(g, ShaderStage::Pixel, {}, {}).ok);
    CHECK(compileShaderGraph(g, ShaderStage::Pixel, params, {}, "fn").ok);
    g.nodes.push_back({"custom_1",
                       "custom",
                       "",
                       0,
                       0,
                       {{"expression", "$tint * 2.0"}, {"type", "float3"}, {"inputs", {{"tint", "float3"}}}}});
    g.links = {{"p", "out", "custom_1", "tint"}, {"custom_1", "out", "output_1", "color"}};
    CHECK(compileShaderGraph(g, ShaderStage::Pixel, params, {}).ok);
    g.nodes.back().properties["expression"] = "return 0;";
    CHECK(!compileShaderGraph(g, ShaderStage::Pixel, params, {}).ok);
}

TEST(shader_graph_node_library_coverage) {
    CHECK(shaderNodeLibrary().size() >= 50);
    for (const auto &spec : shaderNodeLibrary()) {
        if (spec.kind == "surface" || spec.kind == "vertex_output" || spec.kind == "comment" || spec.kind == "input" ||
            spec.kind == "output" || spec.kind == "subgraph")
            continue;
        auto g = newShaderGraph(ShaderStage::Pixel);
        ShaderNode n{"test_node", spec.kind};
        if (spec.kind == "parameter")
            n.properties = {{"parameter", "P"}, {"type", "float4"}};
        if (spec.kind == "texture" || spec.kind == "texture_cube" || spec.kind == "triplanar")
            n.properties["resource"] = spec.kind == "texture_cube" ? "C" : "T";
        g.nodes.push_back(n);
        auto r = compileShaderGraph(g, ShaderStage::Pixel, {{"P", 0, {1, 1, 1, 1}}},
                                    {{"T", "texture2d", "", 0}, {"C", "texturecube", "", 1}}, "test_node");
        if (!r.ok)
            std::printf("node %s: %s\n", spec.kind.c_str(), r.diagnostics.front().message.c_str());
        CHECK(r.ok);
    }
}
