// IW8 Radiant (Qt shell) - GUI checks for the side bar rework (owner 2026-11-15): model tiles and their size
// control, the compact model preview, stored thumbnails decoded off the GUI thread, the Effects and Sounds previews,
// the FLAC decoder, and the Tools panel.
#pragma once

#include <functional>

#include <QString>

namespace iw8r::qt {

class MainWindow;

void runSidebarChecks(std::function<void(const QString&)> section, std::function<void(bool, const QString&)> check, MainWindow& w,
                      const QString& outDir);

}  // namespace iw8r::qt
