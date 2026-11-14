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
