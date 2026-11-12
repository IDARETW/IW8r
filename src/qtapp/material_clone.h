// Editable copy of a cached stock material (roadmap 12.5): writes the stock images as PNG files and a custom material
// definition (<name>.iw8mat.json) that the editor and the build load like any other custom material.
#pragma once

#include "material_cache.h"

#include <QString>
#include <QStringList>

namespace iw8r::qt {

struct MaterialCloneResult {
    QString definitionFile;  // absolute path of the written <safe name>.iw8mat.json
    QString name;            // the new material name (newName)
    QStringList warnings;    // non-fatal problems (normal map skipped, mask unreadable, ...)
};

// Writes <destDir>/<safe name>_c.png (+ _n.png, _s.png when the stock material has a normal image) and
// <destDir>/<safe name>.iw8mat.json for an editable custom material named `newName` made from the cached stock
// material `entry`. Full-size images (no downscale). Returns false and sets *err when newName is not a valid custom
// material name, the colour image cannot be read, a file cannot be written, or the definition already exists (an
// existing definition is never overwritten).
bool cloneStockMaterial(const MaterialCache::Entry& entry, const QString& destDir, const QString& newName,
                        MaterialCloneResult* out, QString* err);

// A valid custom material name suggested for a stock material: "my/<last path part>" made lower case, with characters
// other than [a-z0-9_/] replaced by '_', made unique against `taken` (compared case-insensitively) by appending _2, _3...
QString suggestCloneName(const QString& stockName, const QStringList& taken);

}  // namespace iw8r::qt
