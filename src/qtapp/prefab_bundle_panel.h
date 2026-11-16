// IW8 Radiant - the prefab bundle part of the Entity dock (docs/PREFAB_BUNDLES.md). When the selected entity is
// a placement of a bundle, it shows the bundle's title and description, says when the bundle builds its fallback
// contents and why, and gives one field per parameter of the manifest (label, default, range). A field writes the
// placement's key with prefabParameterKey (one undo step). It hides itself for everything else.
#pragma once

#include <QString>
#include <QWidget>

#include <functional>
#include <optional>
#include <vector>

#include "editor_context.h"
#include "prefab_bundle.h"

class QFormLayout;
class QLabel;

namespace iw8r::qt {

class PrefabBundlePanel : public QWidget {
public:
    struct Hooks {
        // The bundle a misc_prefab entity places (false: not a bundle placement).
        std::function<bool(Id entity, PrefabBundle& out)> bundleOf;
        // Writes (value) or removes (no value) a parameter's key on the placement; one undo step.
        std::function<bool(Id entity, const PrefabParameter& p, const std::optional<std::string>& value)> setParameter;
        std::function<void()> editPrefab, openFolder, explode, makeUnique;
    };
    PrefabBundlePanel(EditorContext* ctx, Hooks hooks, QWidget* parent = nullptr);

    void refresh();  // shows the selected placement's bundle, or hides the panel

    // What is shown (also for the GUI test).
    bool active() const { return entity_ != kNoId; }  // a bundle placement is shown
    Id entity() const { return entity_; }
    QString titleText() const;
    QString descriptionText() const;
    QString badgeText() const;         // "" when the bundle builds its main contents with nothing missing
    QString badgeReason() const;
    QString placementIdText() const;
    int parameterCount() const { return int(rows_.size()); }
    QString parameterLabel(int i) const;
    QString parameterValue(int i) const;   // what the field shows
    QString parameterHint(int i) const;    // "100 to 3000, default 700"
    QWidget* parameterField(int i) const;  // QDoubleSpinBox (number), QComboBox (choice) or QLineEdit (text, material)
    // Types `value` into the field of parameter i and commits it, as the mapper does. False when nothing changed.
    bool setParameter(int i, const QString& value);
    bool resetParameter(int i);  // back to the bundle's default: the key is removed from the placement

private:
    struct Row {
        PrefabParameter param;
        QWidget* field = nullptr;
        QLabel* hint = nullptr;
    };
    void rebuild(const PrefabBundle& bundle);
    void fill(const Entity& e);
    bool commit(int i, const QString& value);
    static QString shownValue(const PrefabParameter& p, const Entity& e);

    EditorContext* ctx_;
    Hooks hooks_;
    Id entity_ = kNoId;
    QString shownKey_;  // bundle folder and manifest text the fields were built for
    QLabel* title_ = nullptr;
    QLabel* sub_ = nullptr;
    QLabel* description_ = nullptr;
    QLabel* badge_ = nullptr;
    QFormLayout* form_ = nullptr;
    QWidget* formHost_ = nullptr;
    std::vector<Row> rows_;
    QString badgeText_, badgeReason_;
    bool filling_ = false;
};

}  // namespace iw8r::qt
