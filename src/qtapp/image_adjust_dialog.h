// Image adjustment dialog: colour grading, resize, optional normal map from height, Save As PNG.
#pragma once

#include <QString>

#include "image_adjust.h"

class QWidget;
class QDialog;

namespace iw8r::qt {

// Opens the dialog for imagePath (asks for a file when it is empty). Returns true when a PNG was saved;
// *savedPath then receives the written path.
bool showImageAdjustDialog(QWidget* parent, const QString& imagePath, QString* savedPath = nullptr);

// Builds the dialog without exec(), for tests. Returns nullptr when imagePath is empty or unreadable.
// The caller owns the dialog: close and delete it.
QDialog* createImageAdjustDialog(QWidget* parent, const QString& imagePath);

// Test seam: runs the same adjust, resize, normal map and PNG write as the dialog's Save As, with no dialog.
// resizeW / resizeH <= 0 keep the source size. Writes the PNG to savePath exactly as given.
bool runImageAdjustHeadless(const QString& imagePath, const iw8r::ImageAdjust& adjust, int resizeW, int resizeH,
                            bool normalFromHeight, double normalStrength, const QString& savePath, QString* err);

}  // namespace iw8r::qt
