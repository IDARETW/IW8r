// IW8 Radiant Suite - Tools > Effect Editor (roadmap 8.2): the map's own sprite effects (<map dir>/fx/<name>.iw8fx.json,
// core fx_author.h), element by element, with an animated side-view preview. The build turns each placed effect into a
// Replay vfx through the converter's IW3 effect route (research 67).
#pragma once

#include <QDialog>
#include <QString>

#include <functional>

#include "fx_author.h"

class QListWidget;
class QLineEdit;
class QCheckBox;
class QSpinBox;
class QDoubleSpinBox;
class QPushButton;
class QLabel;
class QTimer;
class QTableWidget;
class QComboBox;

namespace iw8r::qt {

class FxPreviewWidget;

class FxEditorDialog : public QDialog {
    Q_OBJECT
public:
    // place(name): put an fx_origin with this effect in the map (the main window's placement point).
    FxEditorDialog(const QString& fxFolder, std::function<void(const QString&)> place, QWidget* parent = nullptr);

    QString fxFolder() const { return folder_; }
    QStringList effectNames() const;            // names found under the folder (iw8r/campfire)
    bool openEffect(const QString& name);       // false when it cannot be read
    bool newEffect(const QString& name);        // a starter effect (one smoke element), saved at once
    bool save();                                // writes the open effect
    const AuthoredFx& effect() const { return fx_; }
    AuthoredFx& effectForEdit() { return fx_; }  // tests: change, then save()
    int previewParticles() const;               // particles drawn in the last preview frame
    QString fileFor(const QString& name) const;

private:
    void refreshList();
    void showElement(int index);
    void readForm();
    void fillElements();

    QString folder_;
    std::function<void(const QString&)> place_;
    AuthoredFx fx_;
    QString current_;
    int element_ = -1;
    bool loading_ = false;
    QListWidget* effects_ = nullptr;
    QListWidget* elements_ = nullptr;
    QLineEdit* elemName_ = nullptr;
    QComboBox* kind_ = nullptr;  // Sprite / Light / Model
    QLineEdit* model_ = nullptr;  // kind Model: a custom model name
    QLineEdit* image_ = nullptr;
    QCheckBox* additive_ = nullptr;
    QCheckBox* looping_ = nullptr;
    QSpinBox* interval_ = nullptr;
    QSpinBox* count_ = nullptr;
    QSpinBox* life_ = nullptr;
    QSpinBox* lifeVar_ = nullptr;
    QDoubleSpinBox* radius_ = nullptr;
    QDoubleSpinBox* velMin_[3] = {};
    QDoubleSpinBox* velMax_[3] = {};
    QDoubleSpinBox* gravity_ = nullptr;
    QDoubleSpinBox* sizeStart_ = nullptr;
    QDoubleSpinBox* sizeEnd_ = nullptr;
    QDoubleSpinBox* colStart_[4] = {};
    QDoubleSpinBox* colEnd_[4] = {};
    QDoubleSpinBox* spin_ = nullptr;
    QTableWidget* keys_ = nullptr;  // curve keys: t, size, r, g, b, a (empty: start -> end)
    void readKeys();

public:
    // Tests: the open element's curve keys as the table shows them.
    int keyRows() const;
    void addKeyRow(double t, double size, double r, double g, double b, double a);

private:
    QLabel* status_ = nullptr;
    FxPreviewWidget* preview_ = nullptr;
};

}  // namespace iw8r::qt
