// IW8 Radiant Suite (Qt shell) - Vision editor (roadmap 5.6): every Replay vision field with an optional override value.
#pragma once

#include <QString>

class QWidget;
class QDialog;

namespace iw8r::qt {

// Shows the vision editor. Returns true when OK was pressed; `overrides` is then the "name=value;name=value" text of the
// non-empty values, in table order. Cancel leaves `overrides` unchanged.
bool showVisionEditorDialog(QWidget* parent, const QString& title, QString& overrides);

// Builds the dialog without running it (for tests). The table is "visionTable", the OK button is "visionOk" and is
// disabled while any Value is invalid. The caller owns the dialog and deletes it.
QDialog* createVisionEditorDialog(QWidget* parent, const QString& title, const QString& overrides);

// The "name=value;name=value" text OK would give for the dialog's current values. Empty when a value is invalid
// (OK is refused then) or when the dialog is not a vision editor.
QString visionEditorResult(QDialog* dialog);

}  // namespace iw8r::qt
