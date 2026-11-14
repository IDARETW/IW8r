import io

# Docs and tests for the custom shader edge inputs (core face_edges) and the toon shader as a real shader.


def patch(p, a, b):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:60])
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


T = 'E:/IW8-Radiant/tests/custom_shader_tests.cpp'
s = io.open(T, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
s = s.rstrip('\r\n') + nl + '''
TEST(custom_shader_edge_inputs) {
    // A shader that reads an edge input asks for the brush faces' edge data; one that does not costs nothing.
    CHECK(customShaderUsesEdges("float3 iw8rSurface(IW8RSurface s) { return s.albedo * saturate(s.edgePixels); }"));
    CHECK(customShaderUsesEdges("float3 iw8rSurface(IW8RSurface s) { return s.edgeNormal; }"));
    CHECK(!customShaderUsesEdges("// uses s.edgePixels only in a comment\\nfloat3 iw8rSurface(IW8RSurface s) { return s.albedo; }"));
    CHECK(!customShaderUsesEdges("float3 iw8rSurface(IW8RSurface s) { return s.faceNormal; }"));
    // Both hosts get the struct, the range and the helper that fills the edge inputs before the shader's code.
    const std::string pre = customShaderPreamble();
    CHECK(pre.find("struct IW8RSurface") < pre.find("#define IW8R_EDGE_RANGE 4"));
    CHECK(pre.find("#define IW8R_EDGE_RANGE 4") < pre.find("void iw8rEdgeFields("));
    for (const char* field : {"faceNormal", "edgePixels", "edgeUnits", "edgeNormal"}) CHECK(pre.find(field) != std::string::npos);
    CustomShader s;
    s.name = "lines";
    s.code = "float3 iw8rSurface(IW8RSurface s) { return s.albedo; }";
    const std::string plain = customShaderBuildSource(s), edges = customShaderBuildSource(s, true);
    CHECK(plain.find("#define IW8R_EDGES") == std::string::npos && edges.rfind("#define IW8R_EDGES 1\\n", 0) == 0);
    CHECK(edges.find("void iw8rEdgeFields(") < edges.find("float3 iw8rSurface(IW8RSurface s)"));
}
'''.replace('\n', nl)
io.open(T, 'w', encoding='utf-8', newline='').write(s)

C = 'E:/IW8-Radiant/CHANGELOG.md'
s = io.open(C, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = s.index('- **Toon ink outlines.**')
b = s.index("- **The sky's sun lines up with the map's sun.**")
new = '''- **Edge inputs for custom shaders.** A shader can now ask how far a pixel is from the nearest edge of its brush
  face, in screen pixels and in units, and which way the face across that edge points. Nothing about a look is built
  in: the inputs are there for any shader (outlines, wear, highlights). Seams between flush brushes are no edge. Maps
  whose shaders do not read them are unchanged.
- **The toon shader is a real shader now.** `toon.iw8shader.hlsl` holds the whole look, editable in the Shader Editor:
  more vivid, painted colour; the sun in flat tones with crisp shadow borders; shade as a cool tone instead of
  blackness; and ink lines 3 pixels wide at any distance, with clean corners, drawn by the shader itself. The ink strip
  brushes and Tools > Toon Ink Outlines are gone.
- **Example: Cinder Rail (Toon).** A copy of Cinder Rail with the toon shader and its sun matched to its sky. It
  builds and passes validation. Not seen in game since these changes.
'''.replace('\n', nl)
s = s[:a] + new + s[b:]
io.open(C, 'w', encoding='utf-8', newline='').write(s)

patch('E:/IW8-Radiant/docs/USER_GUIDE.md', '''- **Tools > Toon Ink Outlines** adds black ink lines along the hard edges of brushes, for cel-shaded maps. The lines
  have no collision, and running it again replaces them.
- The example map Cinder Rail (Toon) uses both. Custom shaders are built and checked offline but not yet seen in game.''',
      '''- **Edge inputs.** `s.edgePixels` is the distance to the nearest edge of the brush face in screen pixels,
  `s.edgeUnits` the same in units (up to 4), `s.edgeNormal` the normal of the face across that edge, and
  `s.faceNormal` the surface's own normal without the normal map. With them a shader can draw lines of a constant
  width on screen. Seams between flush brushes are not edges. Models have no edge data (`edgePixels` is 1e6).
- The whole toon look is in `toon.iw8shader.hlsl`: its constants at the top set the line width (`kLinePixels`), the
  colour strength, the tones and the shade level. Make a copy with New to change them for your map.
- The example map Cinder Rail (Toon) uses it. Custom shaders are built and checked offline but not yet seen in game.''')
patch('E:/IW8-Radiant/docs/TEST_CHECKLIST.md', '''- [ ] Black ink lines run along building corners, roof edges and window frames, and you can walk and shoot through
      them as if they were not there.''', '''- [ ] Dark ink lines run along building corners, roof edges and window frames. They are about 3 pixels wide whether
      you stand next to a wall or far from it, and meet cleanly at corners (they are drawn by the shader, not geometry).
- [ ] Colours are stronger than on the normal Cinder Rail, and shade is a cool darker tone, not black.''')
patch('E:/IW8-Radiant/docs/PROGRESS.md', '''## Latest (2026-11-13)

''', '''## Latest (2026-11-13)

- Owner (2026-11-13 late): the toon look must be a real custom shader, lines of constant screen width, nothing toon
  built into the editor. Done: the ink strip brushes (core toon_outline, Tools > Toon Ink Outlines, --ink-outlines,
  iw8r_ink) are removed. Core face_edges (generic): per brush face, convex pieces with the exact distance to the
  nearest edge (EdgeVertex edge / fold / dir, packEdge 4 bytes), seams between flush brushes of a set excluded, soft
  folds kept with their fold, T-junction free per brush, ear-clipped, slivers avoided (the converter throws on
  degenerate triangles). IW8RSurface gained faceNormal, edgePixels, edgeUnits, edgeNormal; kIw8rEdgeHelper
  (iw8rEdgeFields) is compiled into both hosts; customShaderUsesEdges decides whether edge data is made. Viewport:
  GpuTexVertex::edge (60 bytes, TEXCOORD6), SceneCache::updateFaceEdges (PrimGeom::polyEdges, keyed by the brush and
  its touching neighbours' revs), camera_geometry draws the edge meshes. Build: world brush faces as edge meshes, the
  data in the vertex colour, "#define IW8R_EDGES" in the shader text; x64-zt 7bc751ba (hook reads input.color as edge
  data on world surfaces, shader outside flow control). data/shaders/toon.iw8shader.hlsl v3 holds the whole look
  (vibrance, painted steps, tones on faceNormal, shade level, 3 px lines; silhouette full width, crease half per
  face). Cinder Rail (Toon): 141,946 world triangles (22,580 plain), validation clean. Test renders used a scratch
  copy of the material cache (deleted after).
''')
N = 'E:/IW8-Radiant/out/agent/mp_cinderrail_toon/NOTES.md'
s = io.open(N, encoding='utf-8').read()
s += '''
2026-11-13 late: the ink strip brushes and materials/iw8r_ink are removed. The outlines are drawn by the toon shader
from the brush faces' edge inputs (constant 3 px width); the whole look is in shaders/toon.iw8shader.hlsl.
'''
io.open(N, 'w', encoding='utf-8').write(s)
print('ok')
