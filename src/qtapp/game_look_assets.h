// IW8 Radiant Suite - game data the viewport's Game Lighting reads from the owner's install (never shipped).
//
// The stock world shader's environment BRDF table (`ggx_env_brdf_lut`, 64 x 64 BC5 in the always-loaded zone
// code_post_gfx; research 95 "Shipped LUTs": a recomputed table does not match it) is exported once through ACTS and kept
// in AppLocalData/game_look/. Blocking: call it off the UI thread.
#pragma once

#include <QByteArray>
#include <QString>

#include <functional>
#include <vector>

namespace iw8r::qt {

struct EnvBrdfLut {
    int width = 0, height = 0;
    std::vector<uint8_t> rg;  // width * height * 2: A, B (rows = gloss, columns = N.V)
    QString error;
};

// toolPath: the editor's tool paths ("acts", "replay", "oodle"; MainWindow::toolPath). Reads the cached copy, or runs
// ACTS once (a few seconds) and caches it.
EnvBrdfLut loadEnvBrdfLut(const std::function<QString(const QString&)>& toolPath);

// The stock world shader's specular occlusion table (`gtso_lut`, 16 x 16 x 16 BC4 3D in code_post_gfx; research 95).
// The shader samples it (linear, clamped) at (max(sqrt(1 - ao), sqrt(1 - cavity)), a2, saturate(N_geom . R)).
struct GtsoLut {
    int width = 0, height = 0, depth = 0;
    std::vector<uint8_t> r;  // width * height * depth, x fastest (occlusion), then y (a2), then z (N.R)
    QString error;
};
GtsoLut loadGtsoLut(const std::function<QString(const QString&)>& toolPath);

// A BC4 DDS volume (DX10 header BC4_UNORM, or FourCC ATI1 / BC4U), mip 0 decoded to one byte per texel. Fails on
// anything else. Exposed for the tests.
bool decodeBc4VolumeDds(const QByteArray& dds, GtsoLut& out);

// A stock map's IES strip (`*ieslookup`: DX10 header R8_UNORM, width x 1): level 0, one byte per texel. Empty on
// anything else.
std::vector<uint8_t> decodeR8StripDds(const QByteArray& dds);

}  // namespace iw8r::qt
