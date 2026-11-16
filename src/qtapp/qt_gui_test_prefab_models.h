// IW8 Radiant (Qt shell) - GUI checks for the camera view's prefab contents (2026-11-12): models inside placed
// prefabs drawn as their meshes at the composed placement (nested prefabs too, models that arrive later), the inner
// entity markers left out of captures, scriptables drawn with their definition's model, and the stock reference's
// editor-only marker meshes left out of probe captures. Called from the GUI test runner (qt_gui_test.cpp).
#pragma once

#include <QString>

#include <functional>

namespace iw8r::qt {

class MainWindow;

// Files are written under <outDir>/prefab_models; the custom model written for the window checks is removed again.
void runPrefabModelChecks(std::function<void(const QString&)> section, std::function<void(bool, const QString&)> check, MainWindow& w,
                          const QString& outDir);

}  // namespace iw8r::qt
