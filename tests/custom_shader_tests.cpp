// Custom surface shaders for a map's materials (core custom_shader, plan M6).
#include "custom_shader.h"
#include "test_harness.h"

using namespace iw8r;

TEST(custom_shader_code_checks) {
    std::string err;
    CHECK(checkCustomShaderCode("float3 iw8rSurface(IW8RSurface s) { return s.albedo; }", &err));
    CHECK(checkCustomShaderCode("// Texture2D in a comment is fine\nfloat3 iw8rSurface( IW8RSurface surf ) { return 0; }", &err));
    CHECK(!checkCustomShaderCode("float3 shade(IW8RSurface s) { return s.albedo; }", &err) && err.find("iw8rSurface") != std::string::npos);
    CHECK(!checkCustomShaderCode("Texture2D t;\nfloat3 iw8rSurface(IW8RSurface s) { return 0; }", &err) && err.find("resources") != std::string::npos);
    CHECK(!checkCustomShaderCode("cbuffer C { float a; };\nfloat3 iw8rSurface(IW8RSurface s) { return 0; }", &err));
    CHECK(!checkCustomShaderCode("float x : register(c0);\nfloat3 iw8rSurface(IW8RSurface s) { return 0; }", &err));
    CHECK(!checkCustomShaderCode("#include \"x.hlsl\"\nfloat3 iw8rSurface(IW8RSurface s) { return 0; }", &err));
}

TEST(custom_shader_viewport_splice) {
    CustomShader s;
    s.name = "toon";
    s.code = "float3 iw8rSurface(IW8RSurface s) { return s.albedo; }";
    const std::string base = "cbuffer A : register(b0) { float4 x; };\nfloat4 ps_tex(PSIn i) : SV_TARGET {\n#ifdef IW8R_CUSTOM\n#endif\n}\n";
    const std::string out = customShaderViewportSource(base, "float4 ps_tex(PSIn i) : SV_TARGET {", s);
    CHECK(out.rfind("#define IW8R_CUSTOM 1\n", 0) == 0);
    const size_t st = out.find("struct IW8RSurface"), fn = out.find("float3 iw8rSurface"), entry = out.find("float4 ps_tex(");
    CHECK(st != std::string::npos && fn != std::string::npos && entry != std::string::npos && st < fn && fn < entry);
    CHECK(out.find("cbuffer A") < st);  // the host's declarations stay first
    CHECK(customShaderViewportSource(base, "no such entry", s).empty());
}

TEST(custom_shader_edge_inputs) {
    // A shader that reads an edge input asks for the brush faces' edge data; one that does not costs nothing.
    CHECK(customShaderUsesEdges("float3 iw8rSurface(IW8RSurface s) { return s.albedo * saturate(s.edgePixels); }"));
    CHECK(customShaderUsesEdges("float3 iw8rSurface(IW8RSurface s) { return s.edgeNormal; }"));
    CHECK(!customShaderUsesEdges("// uses s.edgePixels only in a comment\nfloat3 iw8rSurface(IW8RSurface s) { return s.albedo; }"));
    CHECK(!customShaderUsesEdges("float3 iw8rSurface(IW8RSurface s) { return s.faceNormal; }"));
    // Both hosts get the struct, the range and the helper that fills the edge inputs before the shader's code.
    const std::string pre = customShaderPreamble();
    CHECK(pre.find("struct IW8RSurface") < pre.find("#define IW8R_EDGE_RANGE 12"));
    CHECK(pre.find("#define IW8R_EDGE_RANGE 12") < pre.find("void iw8rEdgeFields("));
    for (const char* field : {"faceNormal", "edgePixels", "edgeUnits", "edgeNormal"}) CHECK(pre.find(field) != std::string::npos);
    CustomShader s;
    s.name = "lines";
    s.code = "float3 iw8rSurface(IW8RSurface s) { return s.albedo; }";
    const std::string plain = customShaderBuildSource(s), edges = customShaderBuildSource(s, true);
    CHECK(plain.find("#define IW8R_EDGES") == std::string::npos && edges.rfind("#define IW8R_EDGES 1\n", 0) == 0);
    CHECK(edges.find("void iw8rEdgeFields(") < edges.find("float3 iw8rSurface(IW8RSurface s)"));
}

TEST(custom_shader_filter_function) {
    // A shader file may hold a surface function, a filter, or both; one of them at least.
    const std::string surface = "float3 iw8rSurface(IW8RSurface s) { return s.albedo; }";
    const std::string filter = "float3 iw8rFilter(float3 colour) { return colour * 0.5; }";
    std::string err;
    CHECK(customShaderHasSurface(surface) && !customShaderHasFilter(surface));
    CHECK(customShaderHasFilter(filter) && !customShaderHasSurface(filter));
    CHECK(checkCustomShaderCode(filter, &err) && checkCustomShaderCode(surface + "\n" + filter, &err));
    CHECK(!checkCustomShaderCode("float3 other(float3 c) { return c; }", &err) && err.find("iw8rFilter") != std::string::npos);
    CHECK(!customShaderHasFilter("// float3 iw8rFilter(float3 c) in a comment\n" + surface));
    CustomShader s;
    s.name = "look";
    s.code = filter;
    // The program the build compiles: the filter alone behind an entry point that reads TEXCOORD0.
    const std::string program = customShaderFilterProgramSource(s);
    CHECK(program.find("float3 iw8rFilter(float3 colour)") < program.find(std::string("float4 ") + kIw8rFilterEntry));
    CHECK(program.find("TEXCOORD0") != std::string::npos && program.find("iw8rFilter(colour.rgb)") != std::string::npos);
    // The viewport's variant: IW8R_FILTER defined, the code before the textured shader's entry point.
    const std::string base = "cbuffer A { float4 a; };\nfloat4 ps_tex(PSIn i) : SV_TARGET { return 0; }\n";
    const std::string view = customShaderFilterViewportSource(base, "float4 ps_tex(PSIn i) : SV_TARGET {", s);
    CHECK(view.rfind("#define IW8R_FILTER 1\n", 0) == 0);
    CHECK(view.find("float3 iw8rFilter") < view.find("float4 ps_tex("));
    CHECK(customShaderFilterViewportSource("no entry point", "float4 ps_tex(PSIn i) : SV_TARGET {", s).empty());
}
