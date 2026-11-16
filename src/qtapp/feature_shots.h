// IW8 Radiant (Qt shell) - screenshots for the feature list (docs/features/FEATURES.md).
//
//   IW8Radiant.exe --feature-shots --out <dir>
//
// Runs the editor on the real window system (so text is drawn with real fonts; the GUI test runs on Qt's offscreen
// platform, which has none and draws squares), with the window kept off every monitor and never activated, and with
// volatile settings (nothing of the owner's layout or preferences is changed). It drives the editor through its
// menu commands, waits until each view has settled, and saves one PNG per feature into <dir>, plus
// <dir>/feature-shots.txt (one line per picture: file name, what it shows, ok or the reason it was skipped).
// The game is never started and the game folder is only read.
// IW8R_FEATURE_SHOTS_ONLY=stock takes only the stock map pictures (mp_br_tut), to check them without the whole run.
#pragma once

#include <QString>

namespace iw8r::qt {

class MainWindow;

// Returns 0 when every planned picture was written.
int runFeatureShots(MainWindow& w, const QString& outDir);

}  // namespace iw8r::qt
