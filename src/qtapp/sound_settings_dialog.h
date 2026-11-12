// IW8 Radiant Suite - Sound Settings dialog (roadmap 12.8): edits the <stem>.iw8snd.json sidecar of a custom sound.
#pragma once

#include <QString>

class QWidget;
class QDialog;

namespace iw8r::qt {

// Opens the dialog for the wav at `wavPath`. Returns true when the sidecar was written or removed.
bool showSoundSettingsDialog(QWidget* parent, const QString& wavPath);

// Builds the dialog without running it (for tests). Save writes or removes the sidecar, then accepts. The caller
// owns the dialog and deletes it. After an accepted Save, dynamic property "iw8rSidecarChanged" is true when the
// sidecar was written or removed.
QDialog* createSoundSettingsDialog(QWidget* parent, const QString& wavPath);

}  // namespace iw8r::qt
