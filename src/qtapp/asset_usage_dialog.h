// IW8 Radiant - Asset Usage dialog (roadmap 12.9): what the map uses (materials, models, prefabs, effects,
// sounds) and which of the mapper's own asset files nothing uses. See src/core/asset_usage.h.
#pragma once

#include <QStringList>

#include "map_text.h"

class QWidget;
class QDialog;

namespace iw8r::qt {

// Shows the dialog modally. The roots are the folders searched for the mapper's own files.
void showAssetUsageDialog(QWidget* parent, const iw8r::MapFile& map, const QStringList& materialRoots,
                          const QStringList& modelRoots, const QStringList& soundRoots, const QStringList& fxRoots);

// Builds the same dialog without exec(), for tests. The caller owns it: close and delete it.
QDialog* createAssetUsageDialog(QWidget* parent, const iw8r::MapFile& map, const QStringList& materialRoots,
                                const QStringList& modelRoots, const QStringList& soundRoots,
                                const QStringList& fxRoots);

}  // namespace iw8r::qt
