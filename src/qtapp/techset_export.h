// IW8 Radiant (Qt shell) - technique set export on demand (roadmap 12.1). A stock technique set is exported from
// zone techsets_common_mp with ACTS and kept as <AppLocalData>/techset_cache/<safe name>.asset.json.
#pragma once

#include <QString>

#include <functional>

class QObject;

namespace iw8r::qt {

// Where an exported technique set is kept. A leading comma is stripped. Empty when the name is empty.
QString techsetCachePath(const QString& techsetName);

// Exports the technique set `techsetName` (a leading comma is stripped) from zone techsets_common_mp, caches the JSON
// at techsetCachePath(), and calls `done(path, error)` on the GUI thread. path is "" on failure and error a short
// reason. A cached file calls `done` at once. `context` bounds the callback: it is not called once context is gone.
// `tools` takes the keys "acts", "replay", "oodle" (the same function the caches use).
void requestTechsetExport(const std::function<QString(const QString&)>& tools, const QString& techsetName, QObject* context,
                          std::function<void(const QString& path, const QString& error)> done);

}  // namespace iw8r::qt
