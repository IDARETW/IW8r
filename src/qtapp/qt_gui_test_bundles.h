// IW8 Radiant Suite (Qt shell) - GUI checks for prefab bundles (docs/PREFAB_BUNDLES.md): the Prefabs dock, placing
// and copying placements, the parameter fields in the Entity dock, Save Selection As Prefab, explode / edit / make
// unique, the bundles' asset folders while editing, and the build. Called from the GUI test runner (qt_gui_test.cpp).
#pragma once

#include <QString>

#include <functional>

namespace iw8r::qt {

class MainWindow;

// Everything is written under <outDir>/bundles. The prefab folder setting is changed for the checks and put back.
void runPrefabBundleChecks(std::function<void(const QString&)> section, std::function<void(bool, const QString&)> check, MainWindow& w,
                           const QString& outDir);

}  // namespace iw8r::qt
