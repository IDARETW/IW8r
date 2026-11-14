import io

# Custom surface shaders get generic edge inputs (core face_edges): faceNormal, edgePixels, edgeUnits, edgeNormal.
# The helper that fills them is compiled into both hosts from one text (kIw8rEdgeHelper).


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'E:/IW8-Radiant/src/core/custom_shader.h'
patch(H, '''    float  gloss;      // 0 rough .. 1 mirror (the material's gloss, 0.3 without a gloss map)
};
)";
''', '''    float  gloss;      // 0 rough .. 1 mirror (the material's gloss, 0.3 without a gloss map)
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
''')
patch(H, '''// What the build hands the converter''', '''// True when the code reads an edge input (edgePixels, edgeUnits or edgeNormal): the hosts then make the edge data of
// the brush faces (core face_edges); shaders that do not read them cost nothing.
bool customShaderUsesEdges(const std::string& code);

// The text both hosts put before a shader's code: the IW8RSurface struct, IW8R_EDGE_RANGE and iw8rEdgeFields.
std::string customShaderPreamble();

// What the build hands the converter''')
patch(H, '''std::string customShaderBuildSource(const CustomShader& shader);''',
      '''// `edges`: the map's brush faces carry edge data in their vertex colour (IW8R_EDGES is defined for the converter).
std::string customShaderBuildSource(const CustomShader& shader, bool edges = false);''')

C = 'E:/IW8-Radiant/src/core/custom_shader.cpp'
patch(C, '''#include "custom_shader.h"
''', '''#include "custom_shader.h"

#include "face_edges.h"
''')
patch(C, '''    out += baseSource.substr(0, at);
    out += kIw8rSurfaceStruct;
''', '''    out += baseSource.substr(0, at);
    out += customShaderPreamble();
''')
s = io.open(C, encoding='utf-8', newline='').read()
a = s.index('std::string customShaderBuildSource(const CustomShader& shader)')
b = s.index('}', a) + 1
nl = '\r\n' if '\r\n' in s else '\n'
new = '''bool customShaderUsesEdges(const std::string& code) {
    static const std::regex re(R"(\\b(edgePixels|edgeUnits|edgeNormal)\\b)");
    return std::regex_search(withoutComments(code), re);
}

std::string customShaderPreamble() {
    std::ostringstream o;
    o << kIw8rSurfaceStruct << "#define IW8R_EDGE_RANGE " << kEdgeRange << "\\n" << kIw8rEdgeHelper;
    return o.str();
}

std::string customShaderBuildSource(const CustomShader& shader, bool edges) {
    return std::string(edges ? "#define IW8R_EDGES 1\\n" : "") + customShaderPreamble() + "#line 1 \\"" + shader.name +
           ".iw8shader.hlsl\\"\\n" + shader.code;
}'''.replace('\n', nl)
old = s[a:b]
print(old)
s = s[:a] + new + s[b:]
io.open(C, 'w', encoding='utf-8', newline='').write(s)
print('ok')
