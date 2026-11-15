// IW8 Radiant Suite (Qt shell) - dockable panels: entity inspector, undo history, IW8 spawns, hotkeys.
#pragma once

#include <array>
#include <QKeySequence>
#include <QMap>
#include <QWidget>

#include "editor_context.h"
#include "entity_defs.h"

class QComboBox;
class QDoubleSpinBox;
class QCompleter;
class QGroupBox;
class QLabel;
class QPushButton;
class QStringListModel;
class QLineEdit;
class QListWidget;
class QTableWidget;
class QToolButton;
class QVBoxLayout;
class QAction;

namespace iw8r::qt {

class EntityInspector : public QWidget {
    Q_OBJECT
public:
    explicit EntityInspector(EditorContext* ctx, QWidget* parent = nullptr);
    QTableWidget* table() const { return table_; }
    int rowForKey(const QString& key) const;  // -1 when absent
    QString classBoxText() const;
    int classChoices() const;
    QStringList suggestedKeys() const;
    QString classInfoText() const;   // HTML: class description and "built into Replay: yes/no"
    QStringList missingKeys() const;  // required keys the warning line lists
    // Writes _color as 0..1 floats on the inspected entity (one undo step).
    void setLightColor(double r, double g, double b);
    // Removes the keys of the selected rows (the Delete key; classname stays), one undo step. False when none went.
    bool removeSelectedKeys();
    // Why the table and the Add row refuse `value` for `key` on the shown entity: "" when it fits the key's documented
    // type (or the key has no definition). Refused values are not written; the status bar gives the reason.
    QString refusalFor(const QString& key, const QString& value) const;

public slots:
    void refresh();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void commitCell(int row, int column);
    void showMissing(const std::vector<const KeyDef*>& missing, Id eid);
    void refreshLightGroup(const Entity* e);
    EditorContext* ctx_;
    QLabel* header_;
    QComboBox* classBox_;          // IW8 classes seen in stock maps (data/iw8_entity_classes.json)
    QLabel* classInfo_;            // class description from data/iw8_entity_defs.json
    QWidget* missingBox_;          // missing required keys, one row each with an Add button
    QVBoxLayout* missingLay_;
    QStringList missing_;
    QGroupBox* lightGroup_;        // light and light_spot only
    QPushButton* lightColor_;
    QDoubleSpinBox* lightRadius_;
    QDoubleSpinBox* lightFovOuter_;
    QDoubleSpinBox* lightFovInner_;
    QStringListModel* keyModel_;   // documented keys for the class, then stock keys, offered while typing a new key
    QTableWidget* table_;
    QLineEdit* newKey_;
    QLineEdit* newValue_;
    QLabel* surfaces_;
    Id shownEntity_ = kNoId;  // the entity the rows belong to: edits in the table and the Add row go to it
    bool filling_ = false;
};

// Radiant surface inspector: texture fields of the selected brushes (applies to every face of the selection).
class SurfacePanel : public QWidget {
    Q_OBJECT
public:
    explicit SurfacePanel(EditorContext* ctx, QWidget* parent = nullptr);
public slots:
    void refresh();
public:
    // The given texture fields on the selected faces, or on every face of the selected brushes (one undo step).
    void apply(const Primitive::TexFields& f, const QString& what);
    // What the panel shows: the first selected face (or the first face of the first selected brush).
    struct Current {
        bool any = false;    // there is a brush or a face to edit
        bool faces = false;  // faces are selected (else whole brushes)
        size_t count = 0;    // selected faces, or selected brushes
        QString material;
        bool hasTex = false;
        std::array<double, 5> tex{};  // width, height, offset x, offset y, rotation
        double lightmapX = 16384, lightmapY = 16384;
    };
    Current current() const;
private:
    void nudge(int field, double delta, bool multiply);
public:
    // Fit / natural / justify on the selected faces, or every face of the selected brushes (one undo step).
    enum class Op { Fit, Natural, JustifyLeft, JustifyRight, JustifyTop, JustifyBottom, JustifyCenter };
    bool applyOp(Op op);
    // 2.4 / 2.21: lightmap scale (L1, L2) of the selected faces, or of every face of the selected brushes; the other
    // lightmap tokens stay as written. One undo step; false when nothing changed.
    bool applyLightmapScale(double sx, double sy);
private:
    EditorContext* ctx_;
    QLabel* info_;
    QDoubleSpinBox* fields_[5] = {};
    QDoubleSpinBox* lmFields_[2] = {};  // lightmap scale x / y
    QToolButton* nudge_[5][2] = {};     // the -/+ (or /2, x2) buttons beside each field, enabled with it
    QToolButton* ops_[7] = {};          // Fit, Natural, Left, Right, Top, Bottom, Center: enabled with a face or brush selected
    bool filling_ = false;
};

class HistoryPanel : public QWidget {
    Q_OBJECT
public:
    explicit HistoryPanel(EditorContext* ctx, QWidget* parent = nullptr);
public slots:
    void refresh();
private:
    EditorContext* ctx_;
    QListWidget* list_;
    bool filling_ = false;
    std::vector<std::string> shownNames_;  // the step names and undo position the list shows
    size_t shownPos_ = size_t(-1);
};

class SpawnsPanel : public QWidget {
    Q_OBJECT
public:
    explicit SpawnsPanel(EditorContext* ctx, QWidget* parent = nullptr);
public slots:
    void refresh();
private:
    EditorContext* ctx_;
    QLabel* info_;
    QTableWidget* table_;
    quint64 shown_ = 0;     // signature of the spawn records the table shows (0 = not filled yet)
    bool filling_ = false;  // the table is being rebuilt: current-cell changes do not move the camera
};

// Rebindable shortcuts. Bindings persist in IW8Radiant.ini next to the executable.
class HotkeysPanel : public QWidget {
    Q_OBJECT
public:
    HotkeysPanel(const QMap<QString, QAction*>& actions, QWidget* parent = nullptr);
private:
    QTableWidget* table_;
};

}  // namespace iw8r::qt
