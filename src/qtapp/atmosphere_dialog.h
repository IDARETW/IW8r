// IW8 Radiant Suite - Atmosphere dialog (doc 31): exponential fog and film grade, stored as worldspawn iw8r_fog /
// iw8r_film (see src/core/atmosphere.h) and written by the compiler as the setExpFog gsc and the r_film* vision file.
#pragma once

#include <QDialog>

#include "atmosphere.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QDoubleSpinBox;
class QGroupBox;

namespace iw8r::qt {

class EditorContext;

class AtmosphereDialog : public QDialog {
    Q_OBJECT
public:
    AtmosphereDialog(const Atmosphere& initial, QWidget* parent = nullptr);
    Atmosphere value() const;

private:
    // The game's own fog and volumetric air (iw8r_haze): the preset ("" = none) and its amount.
    QComboBox* haze_ = nullptr;
    QDoubleSpinBox* hazeAmount_ = nullptr;
    QLabel* hazeAbout_ = nullptr;
    QGroupBox* fogBox_ = nullptr;
    QDoubleSpinBox *fogStart_ = nullptr, *fogHalf_ = nullptr, *fogRgb_[3] = {};
    QGroupBox* filmBox_ = nullptr;
    QDoubleSpinBox *contrast_ = nullptr, *brightness_ = nullptr, *desat_ = nullptr, *light_[3] = {}, *dark_[3] = {};
    QCheckBox* invert_ = nullptr;
    QLineEdit* lut_ = nullptr;
    QLineEdit* vision_ = nullptr;
};

// Writes the worldspawn keys as one undo step ("Atmosphere"); absent values remove the keys. Returns false when
// nothing changed or there is no worldspawn.
bool applyAtmosphere(EditorContext& ctx, const Atmosphere& a);
// Reads the current worldspawn atmosphere (empty when there is no worldspawn).
Atmosphere currentAtmosphere(const EditorContext& ctx);
// Opens the dialog and applies it on OK.
void editAtmosphere(EditorContext& ctx, QWidget* parent);

}  // namespace iw8r::qt
