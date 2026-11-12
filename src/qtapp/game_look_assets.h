// IW8 Radiant Suite - game data the viewport's Game Lighting reads from the owner's install (never shipped).
//
// The stock world shader's environment BRDF table (`ggx_env_brdf_lut`, 64 x 64 BC5 in the always-loaded zone
// code_post_gfx; research 95 "Shipped LUTs": a recomputed table does not match it) is exported once through ACTS and kept
// in AppLocalData/game_look/. Blocking: call it off the UI thread.
#pragma once

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

}  // namespace iw8r::qt
