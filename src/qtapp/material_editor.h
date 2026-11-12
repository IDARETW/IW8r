// IW8 Radiant Suite (Qt shell) - custom material editor (roadmap 4.17): every field a custom material (doc 21,
// *.iw8mat.json) builds with: colour, normal and specular (gloss in alpha) images, surface type, alpha test or blend,
// two-sided, and the texture size in world units; a live preview on a lit sphere and a wall. Images outside the
// library folder are copied next to the definition so the map stays portable.
#pragma once

#include <QDialog>
#include <QImage>

#include "custom_materials.h"

class QCheckBox;
class QSpinBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;

namespace iw8r::qt {

class MaterialEditorDialog : public QDialog {
    Q_OBJECT
public:
    // libraryRoot: folder the definition is written to (the map's materials folder). existing: edit that material.
    MaterialEditorDialog(const QString& libraryRoot, const CustomMaterial* existing, QWidget* parent = nullptr);
    // Fields (also used by the GUI test).
    void setName(const QString& n);
    void setImage(int slot, const QString& path);  // 0 colour, 1 normal, 2 specular
    void setFlags(bool alphaTest, bool blend, bool twoSided);
    void setSurface(const QString& name);
    void setScale(double w, double h);
    // 12.2 shader state (tests and presets): blendMode 0 none / 1 additive / 2 multiply, polygonOffset 0..2.
    void setShaderState(int blendMode, bool unlit, double emission, double tintR, double tintG, double tintB, int sortKey, int polygonOffset);
    QString jsonText(QString* err = nullptr) const;  // the definition (images as they will be written)
    QString save(QString* err = nullptr);           // writes <root>/<name>.iw8mat.json (+ copied images); returns the path
    QImage preview() const { return previewImage_; }
    // 12.12: the live shader preview (the material on a sphere / plane / cube under a movable sun), as last drawn.
    QImage livePreview() const;
    QString savedPath() const { return saved_; }  // the last definition save() wrote
    // Lit sphere + wall preview of a colour / normal (packed slope or plain tangent-space) / specular set.
    static QImage renderPreview(const QImage& color, const QImage& normal, const QImage& spec, bool alphaTest, bool blend, int size);

private:
    void refreshPreview();
    QString root_;
    QLineEdit* name_;
    QLineEdit* images_[3];
    QComboBox* surface_;
    QCheckBox* alphaTest_;
    QCheckBox* blend_;
    QCheckBox* twoSided_;
    // Shader state (12.2): blend mode, unlit / emission, tint, decal sort order and depth offset.
    QComboBox* blendMode_ = nullptr;
    QCheckBox* unlit_ = nullptr;
    QDoubleSpinBox* emission_ = nullptr;
    QDoubleSpinBox* tint_[3] = {nullptr, nullptr, nullptr};
    QSpinBox* sortKey_ = nullptr;
    QComboBox* polygonOffset_ = nullptr;
    QDoubleSpinBox* width_;
    QDoubleSpinBox* height_;
    QLabel* previewLabel_;
    class MaterialPreviewWidget* live_ = nullptr;  // 12.12 live shader preview
    QImage previewImage_;
    QString saved_;
};

}  // namespace iw8r::qt
