import io

# Second half of smodel_lod_scale.py: SceneRenderer::cullTexInstances uses the game's rule (core smodel_lod.h), and the
# editor hands over every stock prop's last LOD distance and each placement's scale.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, n=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


P = 'src/viewport/render.cpp'
patch(P, '''#include "render.h"
#include "stock_probes.h"
''', '''#include "render.h"
#include "smodel_lod.h"
#include "stock_probes.h"
''')
patch(P, '''    static const bool noLodFov = std::getenv("IW8R_NO_LOD_FOV") != nullptr;
    const float ysProj = std::sqrt(viewProj[1] * viewProj[1] + viewProj[5] * viewProj[5] + viewProj[9] * viewProj[9]);
    const float lodFov = noLodFov || !(ysProj > 1e-6f) ? 1.0f : std::clamp(2.1186731f / ysProj, 0.05f, 8.0f);
    const float lodFovCapped = std::min(lodFov, 1.0f);  // the cull past the last level never comes nearer than its distance''',
      '''    static const bool noLodFov = std::getenv("IW8R_NO_LOD_FOV") != nullptr;
    const float ysProj = std::sqrt(viewProj[1] * viewProj[1] + viewProj[5] * viewProj[5] + viewProj[9] * viewProj[9]);
    const float lodFov = noLodFov || !(ysProj > 1e-6f) ? 1.0f : iw8r::lodFovFactor(1.0f / ysProj);
    // IW8R_LOD_CENTRE_DIST: the distances of before 2026-11-14 (to the model's centre, every placement's scale 1; a
    // bench switch). The game measures to the nearest point of the placement's box.
    static const bool lodCentreDist = std::getenv("IW8R_LOD_CENTRE_DIST") != nullptr;''')
patch(P, '''    struct PlaceEval {
        float dist, rad;
        bool out;
    };''', '''    struct PlaceEval {
        float dist, rad;
        float lod;  // the game's LOD distance: to the nearest point of the placement's box
        bool out;
    };''')
patch(P, '''        const size_t n = lead.rows.size() / 12;
        ev.resize(n);
        for (size_t i = 0; i < n; ++i) {
            const float* r = &lead.rows[i * 12];''', '''        // The model's box: every level's together.
        float gLo[3] = {1e30f, 1e30f, 1e30f}, gHi[3] = {-1e30f, -1e30f, -1e30f};
        for (size_t mi : grp) {
            const GpuTexInstanced& m = refTexInst_[mi];
            for (int c = 0; c < 3; ++c)
                gLo[c] = std::min(gLo[c], m.boxMid[c] - m.boxHalf[c]), gHi[c] = std::max(gHi[c], m.boxMid[c] + m.boxHalf[c]);
        }
        const float gMid[3] = {0.5f * (gLo[0] + gHi[0]), 0.5f * (gLo[1] + gHi[1]), 0.5f * (gLo[2] + gHi[2])};
        const float gHalf[3] = {0.5f * (gHi[0] - gLo[0]), 0.5f * (gHi[1] - gLo[1]), 0.5f * (gHi[2] - gLo[2])};
        const bool scaled = !lodCentreDist && lead.cullScale.size() * 12 == lead.rows.size();
        const size_t n = lead.rows.size() / 12;
        ev.resize(n);
        for (size_t i = 0; i < n; ++i) {
            const float* r = &lead.rows[i * 12];''')
patch(P, '''            e.dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (!e.out) e.out = e.dist > e.rad && e.rad < e.dist * pixelAngle * kPx;
            if (e.out) ++instCulledLast_;
        }''', '''            e.dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (!e.out) e.out = e.dist > e.rad && e.rad < e.dist * pixelAngle * kPx;
            e.lod = lodCentreDist ? e.dist : iw8r::placementBoxDistance(r, gMid, gHalf, eye);
            // Past the model's last LOD distance x the placement's scale the game does not draw it (research 119).
            if (!e.out && !noLodOut) e.out = iw8r::placementLodCulled(e.lod, lodFov, scaled ? lead.cullScale[i] : 1.0f, lead.lodOut);
            if (e.out) ++instCulledLast_;
        }''')
patch(P, '''            const bool lastFoliage = g.lodOut > 0 && g.lodFar >= g.lodOut;  // IW8R_NO_FOLIAGE_LOD_OUT: the last level has no end
            const float farD = g.lodFar > 0 && !(noLodOut && lastFoliage) ? g.lodFar : 1e30f;
            for (size_t i = 0; i < n; ++i) {
                const PlaceEval& e = ev[i];
                // The level by distance x the view factor; the end of the last level by the capped factor.
                const float dSel = e.dist * lodFov, dEnd = lastFoliage ? e.dist * lodFovCapped : dSel;
                if (e.out || dSel < g.lodNear || dEnd >= farD) continue;''', '''            // The last level has no end of its own: the placement is left out by the rule above (e.out).
            const bool last = g.lodOut > 0 && g.lodFar >= g.lodOut;
            const float farD = g.lodFar > 0 && !last ? g.lodFar : 1e30f;
            for (size_t i = 0; i < n; ++i) {
                const PlaceEval& e = ev[i];
                const float dSel = e.lod * lodFov;  // the level by distance x the view factor
                if (e.out || dSel < g.lodNear || dSel >= farD) continue;''')
patch(P, '''                out = dist > rad && rad < dist * pixelAngle * kPx;  // radius under k pixels (4: Verdansk 37 -> 15.5 ms)
                // Stock foliage past its last LOD distance: the game does not draw it (research 100 section 3.3).
                if (!noLodOut && g.lodOut > 0 && dist * lodFovCapped > g.lodOut) out = true;''',
      '''                out = dist > rad && rad < dist * pixelAngle * kPx;  // radius under k pixels (4: Verdansk 37 -> 15.5 ms)
                // A stock prop past its last LOD distance x the placement's scale: the game does not draw it (research
                // 100 section 3.3, 119).
                if (!out && !noLodOut && g.lodOut > 0) {
                    const float ld = lodCentreDist ? dist : iw8r::placementBoxDistance(r, g.boxMid, g.boxHalf, eye);
                    const float cs = !lodCentreDist && g.cullScale.size() == n ? g.cullScale[i] : 1.0f;
                    out = iw8r::placementLodCulled(ld, lodFov, cs, g.lodOut);
                }''')
patch(P, '''                    for (int c = 0; c < 3; ++c) g.centre[c] = 0.5f * (lo[c] + hi[c]), r2 += 0.25f * (hi[c] - lo[c]) * (hi[c] - lo[c]);
                    g.radius = std::sqrt(r2);''', '''                    for (int c = 0; c < 3; ++c) g.centre[c] = 0.5f * (lo[c] + hi[c]), r2 += 0.25f * (hi[c] - lo[c]) * (hi[c] - lo[c]);
                    g.radius = std::sqrt(r2);
                    for (int c = 0; c < 3; ++c) g.boxMid[c] = g.centre[c], g.boxHalf[c] = 0.5f * (hi[c] - lo[c]);''')

M = 'src/qtapp/main_window.cpp'
patch(M, '''                // Foliage (grass, weeds, plants, trees) stops drawing past its last LOD distance in game. Other props
                // keep drawing: the map can override their LOD-out per placement, which the editor does not read.
                {
                    const QString lower = qs(in.model).toLower();
                    if (lower.contains("foliage") || lower.contains("grass") || lower.contains("weed"))
                        if (const auto detail = modelCacheStock_->loadDetail(qs(in.model)); detail && !detail->lods.empty())
                            fresh->lodOut = std::max(0.0, detail->lods.back().distance);
                }''', '''                // A placed model stops drawing past its last LOD distance x the placement's own scale in game (research
                // 119; the scale goes with each placement below).
                if (const auto detail = modelCacheStock_->loadDetail(qs(in.model)); detail && !detail->lods.empty())
                    fresh->lodOut = std::max(0.0, detail->lods.back().distance);''')
patch(M, '''            texInstanced[size_t(ti)].ids.push_back((in.index & 0x80000000u) ? 0xFFFFFFFFu : in.collection);''',
      '''            texInstanced[size_t(ti)].ids.push_back((in.index & 0x80000000u) ? 0xFFFFFFFFu : in.collection);
            // The map's scripted objects (index bit 31) are not static models: never left out by distance here.
            texInstanced[size_t(ti)].cullScale.push_back((in.index & 0x80000000u) ? 0.0f : in.lodCullScale);''')
print('ok')
