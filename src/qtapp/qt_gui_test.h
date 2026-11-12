// IW8 Radiant Suite (Qt shell) - scripted end-to-end GUI test (--gui-test).
// Runs on Qt's offscreen platform: events go straight to the widgets, so no OS cursor, keyboard or
// visible window is involved and a mapper can keep working in another instance.
#pragma once

#include <QString>

namespace iw8r::qt {

class MainWindow;

// Returns the process exit code (0 = all checks passed) and writes <outDir>/gui-test-report.txt.
int runQtGuiTest(MainWindow& w, const QString& samplePath, const QString& outDir);

}  // namespace iw8r::qt
