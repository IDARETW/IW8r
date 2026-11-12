// GUI checks for the Image Adjust and Asset Usage dialogs. Called from the GUI test runner (qt_gui_test.cpp).
#pragma once

#include <QString>

#include <functional>

#include "map_text.h"

namespace iw8r::qt {

// Runs the dialog checks. section starts a named group, check records one result (ok, message).
// outDir receives the PNGs the image checks write. Never opens a native file dialog.
void runAssetDialogChecksA(std::function<void(const QString&)> section, std::function<void(bool, const QString&)> check,
                           const iw8r::MapFile& map, const QString& outDir);

}  // namespace iw8r::qt
