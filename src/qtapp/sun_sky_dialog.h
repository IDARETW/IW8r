// IW8 Radiant Suite - Sun & Sky dialog: worldspawn `sundirection` (pitch yaw roll, degrees; the compiler's sun light and
// the lit preview read it), `suncolor` (0..1 RGB) and `skyname` (a stock sky the compiler builds when its cubemap is
// available, docs/research/33). One undo step.
#pragma once

#include <QDialog>

#include "geometry.h"

class QComboBox;
class QDoubleSpinBox;
class QPushButton;

namespace iw8r::qt {

class EditorContext;

struct SunSky {
    double pitch = -50, yaw = 45;  // compiler defaults (compile_iw8.cpp)
    Vec3 color{1, 1, 1};
    QString sky;                   // "" = no sky key
};

SunSky currentSunSky(const EditorContext& ctx);
// Writes the three keys as one undo step ("Sun & Sky"); returns false when nothing changed.
bool applySunSky(EditorContext& ctx, const SunSky& s);

class SunSkyDialog : public QDialog {
    Q_OBJECT
public:
    SunSkyDialog(const SunSky& initial, const QStringList& skies, QWidget* parent = nullptr);
    SunSky value() const;

private:
    QDoubleSpinBox *pitch_ = nullptr, *yaw_ = nullptr;
    QPushButton* colorButton_ = nullptr;
    Vec3 color_;
    QComboBox* sky_ = nullptr;
    void updateColorButton();
};

// Opens the dialog (skies: stock sky names for the list; free text is allowed) and applies it on OK.
void editSunSky(EditorContext& ctx, const QStringList& skies, QWidget* parent);

}  // namespace iw8r::qt
