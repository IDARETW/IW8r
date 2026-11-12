// IW8 Radiant Suite (Qt shell) - GUI checks for the sound settings, vision editor and technique set dialogs.
#pragma once

#include <functional>

#include <QString>

namespace iw8r::qt {

// Opens and drives the three dialogs, reporting through `section` and `check`. Files go under `outDir`.
// Called from the GUI test runner only.
void runAssetDialogChecksB(std::function<void(const QString&)> section, std::function<void(bool, const QString&)> check, const QString& outDir);

}  // namespace iw8r::qt
