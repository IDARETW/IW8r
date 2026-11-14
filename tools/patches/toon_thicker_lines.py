import io

# Thicker toon lines (owner, 2026-11-14). The line width is the toon shader's own constant; what limited it was the
# reach of the edge inputs every custom shader gets (core face_edges kEdgeRange): 4 units from an edge, so a line
# could not stay wide on distant walls. The reach is now 12 units; the toon shader draws 5 px lines (was 3) that keep
# their width about three times as far.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, n=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('src/core/face_edges.h', 'inline constexpr double kEdgeRange = 4.0;       // world units the edge distance is kept over',
      'inline constexpr double kEdgeRange = 12.0;      // world units the edge distance is kept over (4 until 2026-11-14)')
patch('tests/custom_shader_tests.cpp', '#define IW8R_EDGE_RANGE 4', '#define IW8R_EDGE_RANGE 12', 2)
for f in ('data/shaders/toon.iw8shader.hlsl', 'out/agent/mp_cinderrail_toon/shaders/toon.iw8shader.hlsl'):
    patch(f, '''static const float kLinePixels = 3.0;     // line width on screen
static const float kLineMaxUnits = 3.4;   // ... but never wider than this on the surface (far lines thin out;
                                          // the edge inputs reach 4 units from an edge)''',
          '''static const float kLinePixels = 5.0;     // line width on screen
static const float kLineMaxUnits = 10.0;  // ... but never wider than this on the surface (far lines thin out;
                                          // the edge inputs reach 12 units from an edge)''')
print('ok')
