// On-demand technique-set and shader-payload import through the shared ACTS queue.
// Cache identity includes the Replay profile, source zones, asset name and source-file fingerprints.
#pragma once

#include <QString>
#include <QStringList>

#include <functional>

class QObject;

namespace iw8r::qt {

// Where an exported technique set is kept. A leading comma is stripped. Empty when the name is empty.
QString techsetCachePath(const QString& techsetName);

// Tries owningZones and their techset companions, followed by shared dependencies. Every returned body is checked
// against techsetName. A sibling .programs.json indexes available DXBC payloads. done runs on the GUI thread;
// context bounds its lifetime. Missing shader payloads leave their stage inspectable but without disassembly.
// `tools` takes the keys "acts", "replay", "oodle" (the same function the caches use).
void requestTechsetExport(const std::function<QString(const QString&)>& tools, const QString& techsetName, QObject* context,
                          std::function<void(const QString& path, const QString& error)> done,
                          const QStringList& owningZones = {});

}  // namespace iw8r::qt
