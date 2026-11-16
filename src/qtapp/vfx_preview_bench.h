// IW8 Radiant (Qt shell) - pictures of the side bar's effect preview (VfxPreview), before and after its sprite
// look (owner 2026-11-15: "the preview should be square with the vfx fit in the middle centered", "a lot of vfx shows
// as faint orbs").
//
//   IW8R_FEATURE_SHOTS_ONLY=fx_panel IW8Radiant.exe --feature-shots --out <dir>
//   IW8R_VFX_PANEL_EFFECTS=<name>,<name>,...   (optional: the effects to picture; default a smoke, a fire, sparks, a
//                                              heat haze and a few the Effects panel lists)
//
// Each effect is exported with ACTS first when it has no export (as the panel does), its materials are fetched through
// the material cache, then pictured at 300 and 420 px wide: <leaf>_old_<w>.png (the soft discs of before, as the panel
// showed them), <leaf>_new_<w>.png (the sprites, at the busiest moment of a one-shot / after a looping one filled up)
// and <leaf>_strip.png (four moments of its run at 300 px). Also the Effects panel itself with spark_death selected
// (effects_panel.png). Writes <dir>/vfx-panel.txt (counts, materials, render times).
#pragma once

#include <QString>

namespace iw8r::qt {

class MainWindow;

int runVfxPanelShots(MainWindow& w, const QString& outDir);

}  // namespace iw8r::qt
