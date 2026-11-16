// IW8 Radiant (Qt shell) - side bar thumbnail timings (owner 2026-11-15: "all thumbnails should be cached once so
// they dont need to load in the editor at runtime").
//
//   IW8R_FEATURE_SHOTS_ONLY=thumbs IW8Radiant.exe --feature-shots --out <dir>
//
// Opens the Materials, Models, Images and Prefabs panels one after another (after the catalogues are read) and, for
// the first open and for jumps to five places in each list, counts the visible tiles still waiting for their picture
// (a placeholder on screen) and the time until none waits; then scrolls a page per frame for two seconds and counts
// the frames that showed a waiting tile. Writes <dir>/thumb-bench.txt (and prints it).
#pragma once

#include <QString>

namespace iw8r::qt {

class MainWindow;

int runThumbBench(MainWindow& w, const QString& outDir);

}  // namespace iw8r::qt
