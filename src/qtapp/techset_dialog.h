// IW8 Radiant (Qt shell) - technique set inspector (roadmap 12.1). Shows the material and technique set names,
// plain-English lines from the name, and the technique set body when its ACTS JSON export is readable.
#pragma once

#include <QString>
#include <QWidget>

class QDialog;

namespace iw8r::qt {

// Opens a non-modal dialog. techsetJsonPath may be empty or unreadable: the dialog then says the body has not been
// exported yet. A Copy button copies everything shown as plain text.
void showTechsetDialog(QWidget* parent, const QString& materialName, const QString& techsetName, const QString& techsetJsonPath);

// Builds the dialog without showing it (for tests). Objects: "techsetTree" (one top-level item per technique),
// "techsetNotExported" (shown when the body is missing). The caller owns the dialog and deletes it.
QDialog* createTechsetDialog(QWidget* parent, const QString& materialName, const QString& techsetName, const QString& techsetJsonPath);

}  // namespace iw8r::qt
