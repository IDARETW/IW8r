import io
def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:90])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)
patch('src/core/game_look.h', [
 ('''// replay_vision_names.inc): distance fog that halves every halfPlane units past start,''',
  '''// replay_vision_names.inc): distance fog whose transmittance halves every `density` units past start (Replay's fog
// evaluates exp2(-(distance - fogStartDist) / fogDensity), x64-zt iw3_vision.cpp; halfPlane is kept for reference),'''),
 ('''    double start = 0, halfPlane = 5000,''', '''    double start = 0, density = 10000, halfPlane = 5000,'''),
])
patch('src/core/game_look.cpp', [
 ('''    num("RPNOKKL", f.start);                                       // fogStartDist''',
  '''    num("RPNOKKL", f.start);                                       // fogStartDist
    num("MSTOLQOTKN", f.density);                                  // fogDensity (the half distance)'''),
 ('''    f.on = haveHalf && scatter > 0 && f.halfPlane > 0 && f.maxOpacity > 0;''',
  '''    f.on = haveHalf && scatter > 0 && f.density > 0 && f.maxOpacity > 0;'''),
])
patch('tests/game_look_tests.cpp', [
 ('''"NPMPRRLNPO \\"0.3\\"\nMPRLMQNQO \\"0.7\\"\n";''', '''"NPMPRRLNPO \\"0.3\\"\nMPRLMQNQO \\"0.7\\"\nMSTOLQOTKN \\"10000\\"\n";'''),
 ('''CHECK(f.on && f.halfPlane == 2550 &&''', '''CHECK(f.on && f.halfPlane == 2550 && f.density == 10000 &&'''),
])
patch('src/qtapp/camera_view.cpp', [
 ('''{{float(f.start), float(f.halfPlane), float(f.maxOpacity), 1.0f},''', '''{{float(f.start), float(f.density), float(f.maxOpacity), 1.0f},'''),
])
print('ok')
