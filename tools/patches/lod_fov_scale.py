import io

# Plan 1.13, the open point: the game's field-of-view factor on LOD distances. Read in the IW8 game-test decompile
# (INFERRED for Replay: R_SetLodParms in gfx_d3d/r_scene.cpp:14600-14660, R_GetStaticModelCollectionLODAndDists in
# r_add_staticmodel.cpp:3908-3936):
#   invFovScale    = tanHalfFovY x 2.1186731      (1 at the game's own view: 80 degrees wide at 16:9; x2 in split screen)
#   LOD level      = by  distance x invFovScale   against the model's LOD distances
#   past the last  = by  distance x min(invFovScale, 1) / the placement's lodScale   against the cull distance
# The viewport compared the plain distance. A wide editor view (say 110 degrees) therefore kept more detail than the
# game would at that view, and a narrow one less. The placement's own lodScale is not in the editor's data (1 is used).

P = 'E:/IW8-Radiant/src/viewport/render.cpp'
s = io.open(P, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'


def rep(a, b):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (s.count(a), a[:80])
    s = s.replace(a, b)


rep('''    static const bool noLodOut = std::getenv("IW8R_NO_FOLIAGE_LOD_OUT") != nullptr;''',
    '''    static const bool noLodOut = std::getenv("IW8R_NO_FOLIAGE_LOD_OUT") != nullptr;
    // The game's field-of-view factor on LOD distances (GT R_SetLodParms): tanHalfFovY x 2.1186731, which is 1 at the
    // game's own view. The view-projection's second column holds the up axis x (1 / tanHalfFovY). IW8R_NO_LOD_FOV: off
    // (a bench switch).
    static const bool noLodFov = std::getenv("IW8R_NO_LOD_FOV") != nullptr;
    const float ysProj = std::sqrt(viewProj[1] * viewProj[1] + viewProj[5] * viewProj[5] + viewProj[9] * viewProj[9]);
    const float lodFov = noLodFov || !(ysProj > 1e-6f) ? 1.0f : std::clamp(2.1186731f / ysProj, 0.05f, 8.0f);
    const float lodFovCapped = std::min(lodFov, 1.0f);  // the cull past the last level never comes nearer than its distance''')
rep('''            for (size_t i = 0; i < n; ++i) {
                const PlaceEval& e = ev[i];
                if (e.out || e.dist < g.lodNear || e.dist >= farD) continue;''', '''            for (size_t i = 0; i < n; ++i) {
                const PlaceEval& e = ev[i];
                // The level by distance x the view factor; the end of the last level by the capped factor.
                const float dSel = e.dist * lodFov, dEnd = lastFoliage ? e.dist * lodFovCapped : dSel;
                if (e.out || dSel < g.lodNear || dEnd >= farD) continue;''')
rep('''                if (!noLodOut && g.lodOut > 0 && dist > g.lodOut) out = true;''',
    '''                if (!noLodOut && g.lodOut > 0 && dist * lodFovCapped > g.lodOut) out = true;''')
io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('ok')
