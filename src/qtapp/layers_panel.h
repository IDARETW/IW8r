// IW8 Radiant Suite (Qt shell) - Layers dock: Radiant layers (header declarations + per-primitive
// `layer "NAME"` lines), per-layer visibility, select contents, move the selection to a layer, new layer.
#pragma once

#include <vector>
#include <QWidget>
#include <set>
#include <string>

#include "editor_context.h"

class QTableWidget;
class QLabel;

namespace iw8r::qt {

class LayersPanel : public QWidget {
    Q_OBJECT
public:
    explicit LayersPanel(EditorContext* ctx, QWidget* parent = nullptr);
    // Exposed for --gui-test.
    QStringList layerNames() const;
    void setLayerVisible(const QString& layer, bool visible);
    size_t selectLayer(const QString& layer);
    bool moveSelectionToLayer(const QString& layer);  // declares the layer when needed; one undo step
    bool newLayer(const QString& name);
    // The dock's rows: "(no layer)" first, then every declared or used layer.
    struct Row {
        QString name;
        size_t count = 0;  // primitives in the layer
        bool visible = true;
    };
    std::vector<Row> rows() const;

public slots:
    void refresh();

private:
    void applyVisibility();
    EditorContext* ctx_;
    QTableWidget* table_;
    QLabel* info_;
    std::set<std::string> hiddenLayers_;
    bool filling_ = false;
};

}  // namespace iw8r::qt
