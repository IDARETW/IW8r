// IW8 Radiant Suite - custom surface shaders for a map's own materials (owner 2026-11-13: "make custom shaders for
// maps"; plan M6).
//
// A shader is a file `<name>.iw8shader.hlsl` beside the map's materials. It defines one function,
//
//     float3 iw8rSurface(IW8RSurface s)
//
// returning the lit colour (linear, scene light units) of a pixel from the inputs below. The same function is
// compiled twice: into the viewport's Game Lighting shader (customShaderViewportSource) and, for the build, into a
// pixel shader with the game's interface (research 114). A custom material uses it with "shader": "<file>" in its
// .iw8mat.json. The function may use the HLSL intrinsics and its own helper functions and constants; it must not
// declare resources (textures, buffers, samplers), which differ between the two hosts.
#pragma once

#include <string>
#include <vector>

namespace iw8r {

// The inputs every host fills (declared before the user's code).
inline constexpr const char* kIw8rSurfaceStruct = R"(struct IW8RSurface {
    float3 albedo;     // the colour image times the material tint, linear
    float  alpha;      // coverage (1 for opaque surfaces)
    float3 normal;     // world space, unit, with the normal map applied
    float3 view;       // world space, unit, from the surface toward the eye
    float3 position;   // world space (inches)
    float2 uv;         // the colour image's coordinates
    float3 sunDir;     // world space, unit, toward the sun
    float3 sunColor;   // the sun's radiance (game light units)
    float  sunShadow;  // 0 in the sun's shadow .. 1 lit
    float3 ambient;    // baked indirect irradiance at the surface (lightmap, light grid or probes), with occlusion
    float  gloss;      // 0 rough .. 1 mirror (the material's gloss, 0.3 without a gloss map)
    float3 faceNormal; // world space, unit: the surface's own normal, without the normal map
    float  edgePixels; // distance to the nearest edge of the brush face, in screen pixels (1e6: none near, or no data)
    float  edgeUnits;  // the same distance in world units (1e6: none near)
    float3 edgeNormal; // the normal of the face across that edge (faceNormal when none)
};
)";

// The edge inputs (core face_edges) are filled by this function in both hosts: `edge` is a vertex's packed edge data
// as unorm (x distance / range, y fold / pi, z direction / 2 pi x 255 / 256), dpx / dpy the screen derivatives of the
// position. IW8R_EDGE_RANGE is kEdgeRange.
inline constexpr const char* kIw8rEdgeHelper = R"(void iw8rEdgeFields(inout IW8RSurface s, float3 edge, float3 dpx, float3 dpy)
{
    s.edgePixels = 1e6;
    s.edgeUnits = 1e6;
    s.edgeNormal = s.faceNormal;
    if (edge.x > 0.998) return;
    const float3 n = s.faceNormal;
    float3 a = abs(n.z) < 0.999 ? cross(float3(0, 0, 1), n) : float3(1, 0, 0) - n * n.x;
    a = normalize(a);
    const float3 b = cross(n, a);
    const float angle = edge.z * (255.0 / 256.0) * 6.28318530718;
    const float3 away = cos(angle) * a + sin(angle) * b;  // in the face, leading away from the edge
    s.edgeUnits = edge.x * IW8R_EDGE_RANGE;
    const float xx = dot(dpx, dpx), xy = dot(dpx, dpy), yy = dot(dpy, dpy);
    const float det = xx * yy - xy * xy;
    if (det > 1e-24) {
        // One world unit along `away` in screen pixels: the solution of away = px.x dpx + px.y dpy in the face.
        const float ax = dot(away, dpx), ay = dot(away, dpy);
        s.edgePixels = s.edgeUnits * length(float2(yy * ax - xy * ay, xx * ay - xy * ax)) / det;
    }
    const float fold = edge.y * 3.14159265359;
    s.edgeNormal = cos(fold) * n - sin(fold) * away;
}
)";

struct CustomShader {
    std::string name;  // file name without .iw8shader.hlsl
    std::string path;  // absolute path
    std::string code;  // the user's HLSL (the iw8rSurface function and its helpers)
};

// Reads and checks a shader file: it must define iw8rSurface(IW8RSurface ...) and declare no resources or registers.
bool readCustomShader(const std::string& path, CustomShader& out, std::string* err = nullptr);
// Checks shader code (as readCustomShader does, without a file).
bool checkCustomShaderCode(const std::string& code, std::string* err = nullptr);

// The viewport's textured shader with the custom function: the struct and the user's code before `insertBefore`
// (the textured pixel shader's entry point) and IW8R_CUSTOM defined, so the shader calls iw8rSurface for the lit
// colour. Empty when `insertBefore` is not in the base source.
std::string customShaderViewportSource(const std::string& baseSource, const std::string& insertBefore, const CustomShader& shader);

// A map's surface shader by name (worldspawn "iw8r_surface_shader"): "<name>" or "<name>.iw8shader.hlsl" (or a path
// ending in .hlsl) looked for in each folder in order (the map's shaders folder, then the editor's data/shaders).
// The first file found is read; false with *err when none is found or it does not check.
bool findCustomShader(const std::string& name, const std::vector<std::string>& folders, CustomShader& out, std::string* err = nullptr);

// True when the code reads an edge input (edgePixels, edgeUnits or edgeNormal): the hosts then make the edge data of
// the brush faces (core face_edges); shaders that do not read them cost nothing.
bool customShaderUsesEdges(const std::string& code);

// The text both hosts put before a shader's code: the IW8RSurface struct, IW8R_EDGE_RANGE and iw8rEdgeFields.
std::string customShaderPreamble();

// What the build hands the converter (world JSON "iw8r_surface_shader"): the IW8RSurface struct and the shader's code.
// `edges`: the map's brush faces carry edge data in their vertex colour (IW8R_EDGES is defined for the converter).
std::string customShaderBuildSource(const CustomShader& shader, bool edges = false);

}  // namespace iw8r
